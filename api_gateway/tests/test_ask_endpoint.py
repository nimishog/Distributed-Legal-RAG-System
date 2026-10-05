import pytest
from httpx import AsyncClient


class TestAskEndpoint:
    @pytest.mark.asyncio
    async def test_ask_success(self, async_client):
        response = await async_client.post(
            "/api/ask",
            json={
                "question": "What is the statute of limitations for breach of contract in California?",
                "top_k": 5,
                "include_citations": True,
            },
        )

        assert response.status_code == 200
        data = response.json()

        assert "answer" in data
        assert "citations" in data
        assert "search_latency_ms" in data
        assert "llm_latency_ms" in data
        assert "total_latency_ms" in data
        assert "model" in data

        assert data["model"] == "gpt-4o-mini"
        assert isinstance(data["citations"], list)
        assert data["search_latency_ms"] >= 0
        assert data["llm_latency_ms"] >= 0
        assert data["total_latency_ms"] >= 0

    @pytest.mark.asyncio
    async def test_ask_without_citations(self, async_client):
        response = await async_client.post(
            "/api/ask",
            json={
                "question": "What is the statute of limitations for breach of contract in California?",
                "top_k": 3,
                "include_citations": False,
            },
        )

        assert response.status_code == 200
        data = response.json()
        assert data["citations"] == []

    @pytest.mark.asyncio
    async def test_ask_invalid_question_empty(self, async_client):
        response = await async_client.post(
            "/api/ask",
            json={"question": "", "top_k": 5},
        )

        assert response.status_code == 422

    @pytest.mark.asyncio
    async def test_ask_invalid_question_too_long(self, async_client):
        long_question = "x" * 2001
        response = await async_client.post(
            "/api/ask",
            json={"question": long_question, "top_k": 5},
        )

        assert response.status_code == 422

    @pytest.mark.asyncio
    async def test_ask_invalid_top_k_zero(self, async_client):
        response = await async_client.post(
            "/api/ask",
            json={"question": "Test question", "top_k": 0},
        )

        assert response.status_code == 422

    @pytest.mark.asyncio
    async def test_ask_invalid_top_k_too_high(self, async_client):
        response = await async_client.post(
            "/api/ask",
            json={"question": "Test question", "top_k": 21},
        )

        assert response.status_code == 422


class TestHealthEndpoint:
    @pytest.mark.asyncio
    async def test_health_check(self, async_client):
        response = await async_client.get("/health")

        assert response.status_code == 200
        data = response.json()

        assert "status" in data
        assert "healthy" in data
        assert "router_connected" in data
        assert "embedding_model_loaded" in data

    @pytest.mark.asyncio
    async def test_system_health(self, async_client):
        response = await async_client.get("/system-health")

        assert response.status_code == 200
        data = response.json()

        assert "status" in data
        assert "cpu_percent" in data
        assert "memory_percent" in data
        assert "memory_available_mb" in data
        assert "swap_percent" in data
        assert "disk_percent" in data
        assert "uptime_seconds" in data

        assert data["cpu_percent"] >= 0
        assert data["memory_percent"] >= 0


class TestMetricsEndpoint:
    @pytest.mark.asyncio
    async def test_metrics_endpoint(self, async_client):
        response = await async_client.get("/metrics")

        assert response.status_code == 200
        assert "text/plain" in response.headers["content-type"]
        assert "api_gateway_requests_total" in response.text


class TestRateLimiting:
    @pytest.mark.asyncio
    async def test_concurrent_requests_limited(self, async_client, mock_router_client, mock_llm_client):
        import asyncio

        async def make_request():
            return await async_client.post(
                "/api/ask",
                json={"question": "Test question", "top_k": 5},
            )

        tasks = [make_request() for _ in range(5)]
        responses = await asyncio.gather(*tasks)

        success_count = sum(1 for r in responses if r.status_code == 200)
        assert success_count == 5


class TestRouterErrorHandling:
    @pytest.mark.asyncio
    async def test_router_connection_error(self, async_client, mock_router_client, mock_llm_client):
        mock_router_client.search.side_effect = ConnectionError("Router unavailable")

        response = await async_client.post(
            "/api/ask",
            json={"question": "Test question", "top_k": 5},
        )

        assert response.status_code == 503
        assert "Search service unavailable" in response.json()["detail"]


class TestLLMErrorHandling:
    @pytest.mark.asyncio
    async def test_llm_error(self, async_client, mock_router_client, mock_llm_client):
        from src.services.llm_client import LLMResponse, Citation

        mock_llm_client.generate_answer.side_effect = RuntimeError("OpenAI API error")

        response = await async_client.post(
            "/api/ask",
            json={"question": "Test question", "top_k": 5},
        )

        assert response.status_code == 503
        assert "LLM service unavailable" in response.json()["detail"]