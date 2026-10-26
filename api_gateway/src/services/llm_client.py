import time
import logging
from typing import List, Optional
from dataclasses import dataclass

from openai import AsyncOpenAI, APIConnectionError, APITimeoutError, InternalServerError
from ..config import get_settings
from ..schemas.models import Citation, SearchResult
from ..utils.retry import async_retry_with_backoff, get_circuit_breaker

logger = logging.getLogger(__name__)


@dataclass
class LLMResponse:
    answer: str
    citations: List[Citation]
    prompt_tokens: int
    completion_tokens: int
    latency_ms: float


RAG_PROMPT_TEMPLATE = """You are a legal research assistant. Answer ONLY using the provided context.
If the context does not contain enough information to answer the question, respond with:
"Insufficient context to answer this question."

Context:
{context}

Question: {question}

Answer with citations [1], [2]..."""


_llm_circuit_breaker = get_circuit_breaker("llm", "generate")


class LLMClient:
    def __init__(self):
        self.settings = get_settings()
        self.client: Optional[AsyncOpenAI] = None
        self._embedding_model = None
        self.llm_mode = (self.settings.llm_mode or "openai").lower()
        self.llm_ready = False

    async def initialize(self):
        # Limit PyTorch CPU threads to avoid oversubscription.
        try:
            import os
            import torch
            torch.set_num_threads(int(os.getenv("TORCH_NUM_THREADS", "8")))
        except Exception as e:
            logger.debug(f"Could not set torch threads: {e}")

        self.llm_mode = (self.settings.llm_mode or "openai").lower()

        if self.llm_mode == "mock":
            self.client = None
            self.llm_ready = True
            logger.info("LLM client initialized in MOCK mode")
        else:
            key = self.settings.openai_api_key or ""
            placeholder = (not key) or key.startswith("sk-your-") or "your-openai" in key
            if placeholder:
                logger.warning("OPENAI_API_KEY missing or placeholder; LLM client will not work")
                self.client = None
                self.llm_ready = False
            else:
                self.client = AsyncOpenAI(
                    api_key=key,
                    timeout=30.0,
                    max_retries=1,
                )
                self.llm_ready = True

        # Eagerly load the embedding model so /health reports ready and the
        # first query does not pay the model-load latency. The Docker image
        # preloads the weights into the HF cache, so this is a cache hit.
        self._load_embedding_model()
        logger.info("LLM client initialized")

    def _load_embedding_model(self):
        if self._embedding_model is None:
            try:
                import os
                import torch
                torch.set_num_threads(int(os.getenv("TORCH_NUM_THREADS", "8")))
            except Exception:
                pass
            from sentence_transformers import SentenceTransformer
            logger.info(f"Loading embedding model: {self.settings.embedding_model}")
            self._embedding_model = SentenceTransformer(
                self.settings.embedding_model,
                device=self.settings.embedding_device,
            )
        # Embedding model loaded => health can report ready (independent of LLM mode).
        if self._embedding_model is not None:
            if self.llm_mode == "mock":
                self.llm_ready = True
        return self._embedding_model

    def embed_query(self, text: str) -> List[float]:
        if not text or not text.strip():
            raise ValueError("Cannot embed an empty question")
        model = self._load_embedding_model()
        embedding = model.encode(text, convert_to_numpy=True, normalize_embeddings=True)
        return embedding.tolist()

    def _build_context(self, results: List[SearchResult]) -> str:
        if not results:
            return "No relevant documents found."

        context_parts = []
        for i, result in enumerate(results, 1):
            source = result.metadata.get("source", "unknown")
            page = result.metadata.get("page", "N/A")
            section = result.metadata.get("section", "")
            meta_parts = [f"Source: {source}"]
            if page != "N/A":
                meta_parts.append(f"Page: {page}")
            if section:
                meta_parts.append(f"Section: {section}")
            meta_str = " | ".join(meta_parts)

            context_parts.append(
                f"[{i}] ({meta_str}, relevance: {result.score:.3f})\n{result.text}"
            )
        return "\n\n".join(context_parts)

    def _extract_citations(self, results: List[SearchResult], answer: str) -> List[Citation]:
        citations = []
        for i, result in enumerate(results):
            if f"[{i + 1}]" in answer or f"[{i+1}]" in answer:
                citations.append(
                    Citation(
                        index=i + 1,
                        source_id=result.id,
                        text=result.text[:500] + ("..." if len(result.text) > 500 else ""),
                        score=result.score,
                        metadata=result.metadata,
                    )
                )
        return citations

    @async_retry_with_backoff(
        max_retries=3,
        base_delay=1.0,
        max_delay=60.0,
        exponential_base=2.0,
        jitter=True,
        retryable_exceptions=(
            APIConnectionError,
            APITimeoutError,
            InternalServerError,
            ConnectionError,
            TimeoutError,
        ),
        circuit_breaker=_llm_circuit_breaker,
        component="llm",
        operation="generate",
    )
    async def generate_answer(
        self,
        question: str,
        search_results: List[SearchResult],
    ) -> LLMResponse:
        if self.llm_mode == "mock":
            # Deterministic answer for load testing; no OpenAI call.
            if search_results:
                answer = "Mock answer based on retrieved legal context [1]."
                citations = self._extract_citations(search_results, answer)
                if not citations:
                    citations = [
                        Citation(
                            index=1,
                            source_id=search_results[0].id,
                            text=search_results[0].text[:500],
                            score=search_results[0].score,
                            metadata=search_results[0].metadata,
                        )
                    ]
            else:
                answer = "Insufficient context to answer this question."
                citations = []
            return LLMResponse(
                answer=answer,
                citations=citations,
                prompt_tokens=0,
                completion_tokens=0,
                latency_ms=0.0,
            )

        if not self.client:
            raise RuntimeError("OpenAI client not initialized. Check OPENAI_API_KEY.")

        context = self._build_context(search_results)
        prompt = RAG_PROMPT_TEMPLATE.format(context=context, question=question)

        start_time = time.perf_counter()

        try:
            response = await self.client.chat.completions.create(
                model=self.settings.openai_model,
                messages=[
                    {"role": "system", "content": "You are a legal research assistant."},
                    {"role": "user", "content": prompt},
                ],
                max_tokens=self.settings.openai_max_tokens,
                temperature=self.settings.openai_temperature,
            )
        except Exception as e:
            logger.error(f"OpenAI API error: {e}")
            raise

        latency_ms = (time.perf_counter() - start_time) * 1000

        answer = response.choices[0].message.content or ""
        prompt_tokens = response.usage.prompt_tokens if response.usage else 0
        completion_tokens = response.usage.completion_tokens if response.usage else 0

        citations = self._extract_citations(search_results, answer)

        return LLMResponse(
            answer=answer,
            citations=citations,
            prompt_tokens=prompt_tokens,
            completion_tokens=completion_tokens,
            latency_ms=latency_ms,
        )

    async def close(self):
        if self.client:
            await self.client.close()