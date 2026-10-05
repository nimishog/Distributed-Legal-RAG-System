import logging
import time
import psutil
from contextlib import asynccontextmanager
from fastapi import FastAPI, Response
from fastapi.middleware.cors import CORSMiddleware
from prometheus_client import Counter, Histogram, generate_latest, CONTENT_TYPE_LATEST

from .config import get_settings
from .routers.ask import router as ask_router
from .schemas.models import HealthResponse, SystemHealthResponse
from .services.db_client import RouterClient
from .services.llm_client import LLMClient

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger(__name__)

_router_client: RouterClient = None
_llm_client: LLMClient = None
_start_time = time.time()


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


@asynccontextmanager
async def lifespan(app: FastAPI):
    global _router_client, _llm_client
    settings = get_settings()

    logger.info("Starting API Gateway...")

    _router_client = get_router_client()
    await _router_client.initialize()

    _llm_client = get_llm_client()
    await _llm_client.initialize()

    router_healthy = await _router_client.health_check()
    embedding_loaded = _llm_client._embedding_model is not None

    logger.info(
        f"Startup complete: router_healthy={router_healthy}, "
        f"embedding_loaded={embedding_loaded}"
    )

    yield

    logger.info("Shutting down API Gateway...")
    if _router_client:
        await _router_client.close()
    if _llm_client:
        await _llm_client.close()
    logger.info("Shutdown complete")


app = FastAPI(
    title="Legal RAG API Gateway",
    description="Distributed Legal RAG System - API Gateway with OpenAI GPT-4o-mini",
    version="1.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(ask_router)


@app.get("/health", response_model=HealthResponse)
async def health_check():
    settings = get_settings()
    router_client = get_router_client()
    llm_client = get_llm_client()

    router_healthy = await router_client.health_check()
    embedding_loaded = llm_client._embedding_model is not None

    healthy = router_healthy and embedding_loaded

    return HealthResponse(
        status="healthy" if healthy else "degraded",
        healthy=healthy,
        router_connected=router_healthy,
        embedding_model_loaded=embedding_loaded,
    )


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