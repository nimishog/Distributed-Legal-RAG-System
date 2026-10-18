from fastapi import Request, HTTPException, Depends
from fastapi.security import APIKeyHeader
from starlette.middleware.base import BaseHTTPMiddleware
from starlette.responses import JSONResponse
import logging

from ..config import get_settings

logger = logging.getLogger(__name__)

API_KEY_HEADER = APIKeyHeader(name="X-API-Key", auto_error=False)


class APIKeyAuthMiddleware(BaseHTTPMiddleware):
    def __init__(self, app, excluded_paths: list[str] = None):
        super().__init__(app)
        self.excluded_paths = excluded_paths or ["/health", "/system-health", "/metrics", "/docs", "/openapi.json", "/redoc", "/"]
        self.settings = get_settings()

    async def dispatch(self, request: Request, call_next):
        # Let CORS preflight through untouched; CORSMiddleware handles OPTIONS
        # and must not be blocked by the API-key check.
        if request.method == "OPTIONS" or request.url.path in self.excluded_paths:
            return await call_next(request)

        if self.settings.api_key:
            api_key = request.headers.get("X-API-Key")
            if not api_key:
                logger.warning(f"Missing API key for {request.url.path}")
                return JSONResponse(
                    status_code=401,
                    content={"detail": "Missing API key. Provide X-API-Key header."}
                )
            if api_key != self.settings.api_key:
                logger.warning(f"Invalid API key for {request.url.path}")
                return JSONResponse(
                    status_code=403,
                    content={"detail": "Invalid API key"}
                )

        return await call_next(request)


async def verify_api_key(api_key: str = Depends(API_KEY_HEADER)) -> str:
    settings = get_settings()
    if settings.api_key:
        if not api_key:
            raise HTTPException(status_code=401, detail="Missing API key")
        if api_key != settings.api_key:
            raise HTTPException(status_code=403, detail="Invalid API key")
    return api_key or ""