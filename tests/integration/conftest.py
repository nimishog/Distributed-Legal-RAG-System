"""Configuration for integration tests."""

import os
import pytest
import asyncio


def pytest_configure(config):
    """Configure pytest for integration tests."""
    config.addinivalue_line(
        "markers", "integration: mark test as integration test"
    )
    config.addinivalue_line(
        "markers", "slow: mark test as slow running"
    )


@pytest.fixture(scope="session")
def event_loop():
    """Create an event loop for the test session."""
    loop = asyncio.get_event_loop_policy().new_event_loop()
    yield loop
    loop.close()


# Test environment variables
@pytest.fixture(scope="session")
def router_host():
    return os.getenv("ROUTER_HOST", "localhost")


@pytest.fixture(scope="session")
def router_port():
    return int(os.getenv("ROUTER_PORT", "8080"))


@pytest.fixture(scope="session")
def db_host():
    return os.getenv("DB_HOST", "localhost")


@pytest.fixture(scope="session")
def db_ports():
    return [int(p) for p in os.getenv("DB_PORTS", "8081,8082,8083").split(",")]