import time
import logging
from typing import List, Optional
from dataclasses import dataclass

from openai import AsyncOpenAI, RateLimitError, APIConnectionError, APITimeoutError, InternalServerError
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


_llm_circuit_breaker = get_circuit_breaker("api_gateway", "llm")


class LLMClient:
    def __init__(self):
        self.settings = get_settings()
        self.client: Optional[AsyncOpenAI] = None
        self._embedding_model = None

    async def initialize(self):
        if not self.settings.openai_api_key:
            logger.warning("OPENAI_API_KEY not set, LLM client will not work")
            return
        self.client = AsyncOpenAI(api_key=self.settings.openai_api_key)
        logger.info("LLM client initialized")

    def _load_embedding_model(self):
        if self._embedding_model is None:
            from sentence_transformers import SentenceTransformer
            logger.info(f"Loading embedding model: {self.settings.embedding_model}")
            self._embedding_model = SentenceTransformer(
                self.settings.embedding_model,
                device=self.settings.embedding_device,
            )
        return self._embedding_model

    def embed_query(self, text: str) -> List[float]:
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
            RateLimitError,
            APIConnectionError,
            APITimeoutError,
            InternalServerError,
            ConnectionError,
            TimeoutError,
        ),
        circuit_breaker=_llm_circuit_breaker,
        component="api_gateway",
        operation="llm_generate",
    )
    async def generate_answer(
        self,
        question: str,
        search_results: List[SearchResult],
    ) -> LLMResponse:
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