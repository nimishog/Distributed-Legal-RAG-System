import logging
import time
import psutil
from contextlib import asynccontextmanager
from fastapi import FastAPI, Depends, Response
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from prometheus_client import Counter, Histogram, generate_latest, CONTENT_TYPE_LATEST

from .config import get_settings
from .dependencies import get_llm_client, get_router_client
from .routers.ask import router as ask_router
from .schemas.models import HealthResponse, SystemHealthResponse
from .services.db_client import RouterClient
from .services.llm_client import LLMClient
from .utils.auth import APIKeyAuthMiddleware
from .utils.tracing import init_tracing

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger(__name__)

_start_time = time.time()


@asynccontextmanager
async def lifespan(app: FastAPI):
    logger.info("Starting API Gateway...")

    router_client = get_router_client()
    await router_client.initialize()

    llm_client = get_llm_client()
    await llm_client.initialize()

    router_healthy = await router_client.health_check()
    embedding_loaded = llm_client._embedding_model is not None

    settings = get_settings()
    logger.info(
        f"Startup complete: router_healthy={router_healthy}, "
        f"embedding_loaded={embedding_loaded}, "
        f"workers=1 router_pool={settings.router_pool_size} "
        f"semaphore={settings.rate_limit_concurrent} "
        f"llm_mode={settings.llm_mode}"
    )

    yield

    logger.info("Shutting down API Gateway...")
    await router_client.close()
    await llm_client.close()
    logger.info("Shutdown complete")


app = FastAPI(
    title="Legal RAG API Gateway",
    description="Distributed Legal RAG System - API Gateway with OpenAI GPT-4o-mini",
    version="1.0.0",
    lifespan=lifespan,
)

# Instrumentation must happen before the ASGI app starts (adds middleware)
init_tracing(app)

settings = get_settings()

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_origin_regex=settings.cors_origin_regex or None,
    allow_credentials=settings.cors_allow_credentials,
    allow_methods=["GET", "POST", "OPTIONS"],
    allow_headers=["Content-Type", "Authorization", "X-API-Key", "X-Request-ID"],
    expose_headers=["X-Request-ID"],
)

app.add_middleware(APIKeyAuthMiddleware)

app.include_router(ask_router)


@app.get("/health", response_model=HealthResponse)
async def health_check(
    router_client: RouterClient = Depends(get_router_client),
    llm_client: LLMClient = Depends(get_llm_client),
):
    router_healthy = await router_client.health_check()
    embedding_loaded = llm_client._embedding_model is not None
    llm_ready = getattr(llm_client, "llm_ready", True)

    healthy = router_healthy and embedding_loaded and llm_ready

    body = HealthResponse(
        status="healthy" if healthy else "degraded",
        healthy=healthy,
        router_connected=router_healthy,
        embedding_model_loaded=embedding_loaded,
    )
    # Return 503 when degraded so container healthchecks (and nginx) gate
    # traffic instead of reporting a false healthy status.
    if not healthy:
        return JSONResponse(status_code=503, content=body.model_dump(mode="json"))
    return body


@app.get("/system-health", response_model=SystemHealthResponse)
async def system_health():
    cpu_percent = psutil.cpu_percent(interval=0.1)
    memory = psutil.virtual_memory()
    swap = psutil.swap_memory()
    disk = psutil.disk_usage("/")

    return SystemHealthResponse(
        status="ok",
        cpu_percent=cpu_percent,
        memory_percent=memory.percent,
        memory_available_mb=memory.available / (1024 * 1024),
        swap_percent=swap.percent,
        disk_percent=disk.percent,
        uptime_seconds=time.time() - _start_time,
    )


@app.get("/metrics")
async def metrics():
    return Response(content=generate_latest(), media_type=CONTENT_TYPE_LATEST)


@app.get("/")
async def root():
    return {
        "service": "Legal RAG API Gateway",
        "version": "1.0.0",
        "docs": "/docs",
        "health": "/health",
        "system_health": "/system-health",
        "metrics": "/metrics",
    }