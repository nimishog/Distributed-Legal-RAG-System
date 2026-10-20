import time
import logging
import asyncio
from fastapi import APIRouter, HTTPException, Request, Depends
from prometheus_client import Histogram, Counter, Gauge
from openai import OpenAIError

from ..config import get_settings
from ..dependencies import get_llm_client, get_router_client, get_semaphore
from ..schemas.models import AskRequest, AskResponse
from ..services.db_client import RouterClient
from ..services.llm_client import LLMClient
from ..utils.retry import CircuitBreakerOpenError, CIRCUIT_BREAKER_STATE, CIRCUIT_BREAKER_FAILURES
from ..utils.auth import verify_api_key
from ..utils.tracing import start_span, add_span_attributes

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api", tags=["ask"], dependencies=[Depends(verify_api_key)])

# RED Metrics (Rate, Errors, Duration)
REQUEST_RATE = Counter(
    "api_gateway_http_requests_total",
    "Total HTTP requests",
    ["method", "endpoint", "status"],
)

REQUEST_DURATION = Histogram(
    "api_gateway_http_request_duration_seconds",
    "HTTP request duration in seconds",
    ["method", "endpoint"],
    buckets=[0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0],
)

REQUEST_ERRORS = Counter(
    "api_gateway_http_request_errors_total",
    "Total HTTP request errors",
    ["method", "endpoint", "error_type"],
)

# Business metrics
LLM_LATENCY = Histogram(
    "api_gateway_llm_latency_seconds",
    "LLM generation latency in seconds",
    buckets=[0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 30.0],
)

PROMPT_TOKENS = Counter(
    "api_gateway_llm_prompt_tokens_total",
    "Total prompt tokens",
)

COMPLETION_TOKENS = Counter(
    "api_gateway_llm_completion_tokens_total",
    "Total completion tokens",
)

# Dependency metrics
DEPENDENCY_LATENCY = Histogram(
    "api_gateway_dependency_latency_seconds",
    "Downstream dependency latency",
    ["dependency", "operation"],
    buckets=[0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0],
)

DEPENDENCY_ERRORS = Counter(
    "api_gateway_dependency_errors_total",
    "Downstream dependency errors",
    ["dependency", "operation", "error_type"],
)

@router.post("/ask", response_model=AskResponse)
async def ask_question(
    request: AskRequest,
    router_client: RouterClient = Depends(get_router_client),
    llm_client: LLMClient = Depends(get_llm_client),
    semaphore: asyncio.Semaphore = Depends(get_semaphore),
):
    start_time = time.perf_counter()
    settings = get_settings()

    if not request.question.strip():
        REQUEST_RATE.labels(method="POST", endpoint="/ask", status="400").inc()
        raise HTTPException(status_code=400, detail="Question cannot be empty")

    async with semaphore:
        try:
            with start_span("embed_query") as span:
                add_span_attributes({"question.length": len(request.question)})
                embed_start = time.perf_counter()
                query_embedding = await asyncio.to_thread(
                    llm_client.embed_query, request.question
                )
                embed_latency = (time.perf_counter() - embed_start) * 1000
                if span:
                    span.set_attribute("embed.latency_ms", embed_latency)

            with start_span("vector_search") as span:
                search_start = time.perf_counter()
                search_results = await router_client.search(
                    embedding=query_embedding,
                    top_k=request.top_k,
                )
                search_latency = (time.perf_counter() - search_start) * 1000
                if span:
                    span.set_attribute("search.latency_ms", search_latency)
                    span.set_attribute("search.results_count", len(search_results))

            with start_span("llm_generate") as span:
                llm_response = await llm_client.generate_answer(
                    question=request.question,
                    search_results=search_results,
                )
                if span:
                    span.set_attribute("llm.latency_ms", llm_response.latency_ms)
                    span.set_attribute("llm.prompt_tokens", llm_response.prompt_tokens)
                    span.set_attribute("llm.completion_tokens", llm_response.completion_tokens)

            total_latency = (time.perf_counter() - start_time) * 1000

            REQUEST_RATE.labels(method="POST", endpoint="/ask", status="200").inc()
            REQUEST_DURATION.labels(method="POST", endpoint="/ask").observe(total_latency / 1000)
            LLM_LATENCY.observe(llm_response.latency_ms / 1000)
            PROMPT_TOKENS.inc(llm_response.prompt_tokens)
            COMPLETION_TOKENS.inc(llm_response.completion_tokens)
            DEPENDENCY_LATENCY.labels(dependency="router", operation="search").observe(search_latency / 1000)
            DEPENDENCY_LATENCY.labels(dependency="llm", operation="generate").observe(llm_response.latency_ms / 1000)

            return AskResponse(
                answer=llm_response.answer,
                citations=llm_response.citations if request.include_citations else [],
                search_latency_ms=search_latency,
                llm_latency_ms=llm_response.latency_ms,
                total_latency_ms=total_latency,
                model=settings.openai_model,
            )

        except CircuitBreakerOpenError as e:
            REQUEST_RATE.labels(method="POST", endpoint="/ask", status="503").inc()
            REQUEST_ERRORS.labels(method="POST", endpoint="/ask", error_type="circuit_open").inc()
            CIRCUIT_BREAKER_STATE.labels(
                component=e.component, operation=e.operation
            ).set(1)
            logger.error(f"Circuit breaker open for {e.component}.{e.operation}")
            if "router" in e.operation:
                DEPENDENCY_ERRORS.labels(dependency="router", operation="search", error_type="circuit_open").inc()
                raise HTTPException(status_code=503, detail="Search service temporarily unavailable (circuit open)")
            else:
                DEPENDENCY_ERRORS.labels(dependency="llm", operation="generate", error_type="circuit_open").inc()
                raise HTTPException(status_code=503, detail="LLM service temporarily unavailable (circuit open)")

        except ConnectionError as e:
            REQUEST_RATE.labels(method="POST", endpoint="/ask", status="503").inc()
            REQUEST_ERRORS.labels(method="POST", endpoint="/ask", error_type="connection_error").inc()
            DEPENDENCY_ERRORS.labels(dependency="router", operation="search", error_type="connection_error").inc()
            logger.error(f"Router connection error: {e}")
            raise HTTPException(status_code=503, detail="Search service unavailable")

        except OpenAIError as e:
            REQUEST_RATE.labels(method="POST", endpoint="/ask", status="503").inc()
            REQUEST_ERRORS.labels(method="POST", endpoint="/ask", error_type="llm_error").inc()
            DEPENDENCY_ERRORS.labels(dependency="llm", operation="generate", error_type="openai_error").inc()
            logger.error(f"OpenAI API error: {e}")
            raise HTTPException(status_code=503, detail="LLM service unavailable")

        except RuntimeError as e:
            REQUEST_RATE.labels(method="POST", endpoint="/ask", status="503").inc()
            REQUEST_ERRORS.labels(method="POST", endpoint="/ask", error_type="llm_error").inc()
            DEPENDENCY_ERRORS.labels(dependency="llm", operation="generate", error_type="runtime_error").inc()
            logger.error(f"LLM error: {e}")
            raise HTTPException(status_code=503, detail="LLM service unavailable")

        except Exception as e:
            REQUEST_RATE.labels(method="POST", endpoint="/ask", status="500").inc()
            REQUEST_ERRORS.labels(method="POST", endpoint="/ask", error_type="internal_error").inc()
            logger.error(f"Internal error: {e}")
            raise HTTPException(status_code=500, detail="Internal server error")