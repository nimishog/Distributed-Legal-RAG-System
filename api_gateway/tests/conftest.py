import asyncio
import pytest
from unittest.mock import AsyncMock, MagicMock, patch
from fastapi.testclient import TestClient
from httpx import AsyncClient

from src.main import app, get_router_client, get_llm_client, get_semaphore
from src.services.db_client import RouterClient, SearchResult
from src.services.llm_client import LLMClient, LLMResponse
from src.config import Settings
from src.schemas.models import Citation


@pytest.fixture(scope="session")
def event_loop():
    loop = asyncio.get_event_loop_policy().new_event_loop()
    yield loop
    loop.close()


@pytest.fixture
def mock_settings():
    return Settings(
        router_host="localhost",
        router_port=8080,
        router_pool_size=2,
        openai_api_key="test-key",
        openai_model="gpt-4o-mini",
        openai_max_tokens=500,
        openai_temperature=0.1,
        api_host="0.0.0.0",
        api_port=8000,
        log_level="DEBUG",
        embedding_model="sentence-transformers/all-mpnet-base-v2",
        embedding_device="cpu",
        rate_limit_concurrent=2,
    )


@pytest.fixture
def mock_router_client():
    client = AsyncMock(spec=RouterClient)
    client.health_check = AsyncMock(return_value=True)
    client.search = AsyncMock(
        return_value=[
            SearchResult(
                id="test-id-1",
                text="Test legal document text about contract law",
                score=0.95,
                metadata={"source": "legalbench", "page": 1, "section": "contracts"},
            ),
            SearchResult(
                id="test-id-2",
                text="Another legal document about tort law",
                score=0.87,
                metadata={"source": "legalbench", "page": 2, "section": "torts"},
            ),
        ]
    )
    client.close = AsyncMock()
    return client


@pytest.fixture
def mock_llm_client():
    client = AsyncMock(spec=LLMClient)
    client._embedding_model = MagicMock()
    client.embed_query = MagicMock(return_value=[0.1] * 768)
    client.generate_answer = AsyncMock(
        return_value=LLMResponse(
            answer="Based on the context, the statute of limitations for breach of contract in California is 4 years [1].",
            citations=[
                Citation(
                    index=1,
                    source_id="test-id-1",
                    text="Test legal document text about contract law",
                    score=0.95,
                    metadata={"source": "legalbench", "page": 1, "section": "contracts"},
                )
            ],
            prompt_tokens=150,
            completion_tokens=50,
            latency_ms=1200.0,
        )
    )
    client.close = AsyncMock()
    return client


@pytest.fixture
def test_app(mock_router_client, mock_llm_client):
    async def override_router_client():
        return mock_router_client

    async def override_llm_client():
        return mock_llm_client

    def override_semaphore():
        return asyncio.Semaphore(2)

    app.dependency_overrides[get_router_client] = override_router_client
    app.dependency_overrides[get_llm_client] = override_llm_client
    app.dependency_overrides[get_semaphore] = override_semaphore

    yield app

    app.dependency_overrides.clear()


@pytest.fixture
def client(test_app):
    return TestClient(test_app)


@pytest.fixture
async def async_client(test_app):
    async with AsyncClient(app=test_app, base_url="http://test") as ac:
        yield ac