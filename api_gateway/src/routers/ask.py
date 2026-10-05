import time
import logging
import asyncio
from fastapi import APIRouter, HTTPException, Request, Depends
from prometheus_client import Histogram, Counter

from ..config import get_settings
from ..schemas.models import AskRequest, AskResponse
from ..services.db_client import RouterClient
from ..services.llm_client import LLMClient

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api", tags=["ask"])

REQUEST_COUNT = Counter(
    "api_gateway_requests_total",
    "Total API requests",
    ["endpoint", "status"],
)

REQUEST_LATENCY = Histogram(
    "api_gateway_latency_seconds",
    "API request latency in seconds",
    ["endpoint"],
)

LLM_LATENCY = Histogram(
    "api_gateway_llm_latency_seconds",
    "LLM generation latency in seconds",
)

PROMPT_TOKENS = Counter(
    "api_gateway_llm_prompt_tokens_total",
    "Total prompt tokens",
)

COMPLETION_TOKENS = Counter(
    "api_gateway_llm_completion_tokens_total",
    "Total completion tokens",
)

_router_client: RouterClient = None
_llm_client: LLMClient = None
_semaphore: asyncio.Semaphore = None


def get_router_client() -> RouterClient:
    global _router_client
    if _router_client is None:
        settings = get_settings()
        _router_client = RouterClient(
            host=settings.router_host,
            port=settings.router_port,
            pool_size=settings.router_pool_size,
        )
    return _router_client


def get_llm_client() -> LLMClient:
    global _llm_client
    if _llm_client is None:
        _llm_client = LLMClient()
    return _llm_client


def get_semaphore() -> asyncio.Semaphore:
    global _semaphore
    if _semaphore is None:
        settings = get_settings()
        _semaphore = asyncio.Semaphore(settings.rate_limit_concurrent)
    return _semaphore


@router.post("/ask", response_model=AskResponse)
async def ask_question(
    request: AskRequest,
    router_client: RouterClient = Depends(get_router_client),
    llm_client: LLMClient = Depends(get_llm_client),
    semaphore: asyncio.Semaphore = Depends(get_semaphore),
):
    start_time = time.perf_counter()
    settings = get_settings()

    async with semaphore:
        try:
            embed_start = time.perf_counter()
            query_embedding = llm_client.embed_query(request.question)
            embed_latency = (time.perf_counter() - embed_start) * 1000

            search_start = time.perf_counter()
            search_results = await router_client.search(
                embedding=query_embedding,
                top_k=request.top_k,
            )
            search_latency = (time.perf_counter() - search_start) * 1000

            llm_response = await llm_client.generate_answer(
                question=request.question,
                search_results=search_results,
            )

            total_latency = (time.perf_counter() - start_time) * 1000

            REQUEST_COUNT.labels(endpoint="/ask", status="success").inc()
            REQUEST_LATENCY.labels(endpoint="/ask").observe(total_latency / 1000)
            LLM_LATENCY.observe(llm_response.latency_ms / 1000)
            PROMPT_TOKENS.inc(llm_response.prompt_tokens)
            COMPLETION_TOKENS.inc(llm_response.completion_tokens)

            return AskResponse(
                answer=llm_response.answer,
                citations=llm_response.citations if request.include_citations else [],
                search_latency_ms=search_latency,
                llm_latency_ms=llm_response.latency_ms,
                total_latency_ms=total_latency,
                model=settings.openai_model,
            )

        except ConnectionError as e:
            REQUEST_COUNT.labels(endpoint="/ask", status="router_error").inc()
            logger.error(f"Router connection error: {e}")
            raise HTTPException(status_code=503, detail="Search service unavailable")
        except RuntimeError as e:
            REQUEST_COUNT.labels(endpoint="/ask", status="llm_error").inc()
            logger.error(f"LLM error: {e}")
            raise HTTPException(status_code=503, detail="LLM service unavailable")
        except Exception as e:
            REQUEST_COUNT.labels(endpoint="/ask", status="internal_error").inc()
            logger.error(f"Internal error: {e}")
            raise HTTPException(status_code=500, detail="Internal server error")