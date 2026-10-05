from pydantic_settings import BaseSettings
from pydantic import Field
from functools import lru_cache


class Settings(BaseSettings):
    router_host: str = Field(default="cpp_router", alias="ROUTER_HOST")
    router_port: int = Field(default=8080, alias="ROUTER_PORT")
    router_pool_size: int = Field(default=2, alias="ROUTER_POOL_SIZE")

    openai_api_key: str = Field(default="", alias="OPENAI_API_KEY")
    openai_model: str = Field(default="gpt-4o-mini", alias="OPENAI_MODEL")
    openai_max_tokens: int = Field(default=500, alias="OPENAI_MAX_TOKENS")
    openai_temperature: float = Field(default=0.1, alias="OPENAI_TEMPERATURE")

    api_host: str = Field(default="0.0.0.0", alias="API_HOST")
    api_port: int = Field(default=8000, alias="API_PORT")
    log_level: str = Field(default="INFO", alias="LOG_LEVEL")

    embedding_model: str = Field(
        default="sentence-transformers/all-mpnet-base-v2",
        alias="EMBEDDING_MODEL"
    )
    embedding_device: str = Field(default="cpu", alias="EMBEDDING_DEVICE")

    rate_limit_concurrent: int = Field(default=2, alias="RATE_LIMIT_CONCURRENT")

    class Config:
        env_file = ".env"
        env_file_encoding = "utf-8"
        case_sensitive = False
        extra = "ignore"


@lru_cache
def get_settings() -> Settings:
    return Settings()