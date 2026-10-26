from pydantic_settings import BaseSettings
from pydantic import Field
from functools import lru_cache
from pathlib import Path


def read_secret(secret_name: str) -> str:
    """Read a secret from Docker secrets directory."""
    secret_path = Path(f"/run/secrets/{secret_name}")
    if secret_path.exists():
        return secret_path.read_text().strip()
    return ""


class Settings(BaseSettings):
    router_host: str = Field(default="cpp_router", alias="ROUTER_HOST")
    router_port: int = Field(default=8080, alias="ROUTER_PORT")
    router_pool_size: int = Field(default=50, alias="ROUTER_POOL_SIZE")

    openai_api_key: str = Field(default="", alias="OPENAI_API_KEY")
    openai_model: str = Field(default="gpt-4o-mini", alias="OPENAI_MODEL")
    openai_max_tokens: int = Field(default=500, alias="OPENAI_MAX_TOKENS")
    openai_temperature: float = Field(default=0.1, alias="OPENAI_TEMPERATURE")

    llm_mode: str = Field(default="openai", alias="LLM_MODE")

    api_host: str = Field(default="0.0.0.0", alias="API_HOST")
    api_port: int = Field(default=8000, alias="API_PORT")
    log_level: str = Field(default="INFO", alias="LOG_LEVEL")

    embedding_model: str = Field(
        default="sentence-transformers/all-MiniLM-L6-v2",
        alias="EMBEDDING_MODEL"
    )
    embedding_device: str = Field(default="cpu", alias="EMBEDDING_DEVICE")

    rate_limit_concurrent: int = Field(default=100, alias="RATE_LIMIT_CONCURRENT")

    cors_origins: list[str] = Field(
        default_factory=lambda: ["http://localhost:5173", "https://nimish-legal-rag.duckdns.org"],
        alias="CORS_ORIGINS"
    )
    cors_allow_credentials: bool = Field(default=False, alias="CORS_ALLOW_CREDENTIALS")
    cors_origin_regex: str = Field(default="", alias="CORS_ORIGIN_REGEX")

    api_key: str = Field(default="", alias="API_KEY")

    class Config:
        env_file = ".env"
        env_file_encoding = "utf-8"
        case_sensitive = False
        extra = "ignore"


@lru_cache
def get_settings() -> Settings:
    settings = Settings()
    # Override with Docker secrets if available
    if not settings.openai_api_key:
        settings.openai_api_key = read_secret("openai_api_key")
    if not settings.api_key:
        settings.api_key = read_secret("api_key")
    return settings