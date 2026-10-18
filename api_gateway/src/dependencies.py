import asyncio
from typing import Optional

from .config import get_settings
from .services.db_client import RouterClient
from .services.llm_client import LLMClient

_router_client: Optional[RouterClient] = None
_llm_client: Optional[LLMClient] = None
_semaphore: Optional[asyncio.Semaphore] = None


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
