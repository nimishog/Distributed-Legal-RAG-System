import time
import random
import logging
from typing import Callable, Any, Tuple, Type, Optional
from functools import wraps
from dataclasses import dataclass, field
from enum import Enum
from threading import Lock

from prometheus_client import Counter, Histogram, Gauge

logger = logging.getLogger(__name__)


RETRY_ATTEMPTS = Counter(
    'retry_attempts_total',
    'Total number of retry attempts',
    ['component', 'operation', 'result']
)

RETRY_DELAY = Histogram(
    'retry_delay_seconds',
    'Delay between retry attempts',
    ['component', 'operation']
)

CIRCUIT_BREAKER_STATE = Gauge(
    'circuit_breaker_state',
    'Circuit breaker state (0=closed, 1=open, 2=half-open)',
    ['component', 'operation']
)

CIRCUIT_BREAKER_FAILURES = Counter(
    'circuit_breaker_failures_total',
    'Total failures counted by circuit breaker',
    ['component', 'operation']
)


class CircuitState(Enum):
    CLOSED = 0
    OPEN = 1
    HALF_OPEN = 2


@dataclass
class CircuitBreaker:
    failure_threshold: int = 5
    success_threshold: int = 2
    timeout: float = 30.0
    _state: CircuitState = field(default=CircuitState.CLOSED, init=False)
    _failure_count: int = field(default=0, init=False)
    _success_count: int = field(default=0, init=False)
    _last_failure_time: float = field(default=0, init=False)
    _lock: Lock = field(default_factory=Lock, init=False)

    @property
    def state(self) -> CircuitState:
        with self._lock:
            if self._state == CircuitState.OPEN:
                if time.time() - self._last_failure_time >= self.timeout:
                    self._state = CircuitState.HALF_OPEN
                    self._success_count = 0
                    logger.info(f"Circuit breaker transitioning to HALF_OPEN")
            return self._state

    def record_success(self):
        with self._lock:
            if self._state == CircuitState.HALF_OPEN:
                self._success_count += 1
                if self._success_count >= self.success_threshold:
                    self._state = CircuitState.CLOSED
                    self._failure_count = 0
                    logger.info(f"Circuit breaker CLOSED after {self._success_count} successes")
            elif self._state == CircuitState.CLOSED:
                self._failure_count = 0

    def record_failure(self):
        with self._lock:
            self._failure_count += 1
            self._last_failure_time = time.time()
            if self._state == CircuitState.HALF_OPEN:
                self._state = CircuitState.OPEN
                logger.warning(f"Circuit breaker OPENED after half-open failure")
            elif self._state == CircuitState.CLOSED and self._failure_count >= self.failure_threshold:
                self._state = CircuitState.OPEN
                logger.warning(f"Circuit breaker OPENED after {self._failure_count} failures")

    def is_available(self) -> bool:
        return self.state != CircuitState.OPEN


class CircuitBreakerOpenError(Exception):
    def __init__(self, component: str, operation: str):
        self.component = component
        self.operation = operation
        super().__init__(f"Circuit breaker OPEN for {component}.{operation}")


def calculate_backoff(
    attempt: int,
    base_delay: float = 1.0,
    max_delay: float = 30.0,
    exponential_base: float = 2.0,
    jitter: bool = True,
) -> float:
    delay = min(base_delay * (exponential_base ** attempt), max_delay)
    if jitter:
        delay *= (0.5 + random.random())
    return delay


def retry_with_backoff(
    max_retries: int = 3,
    base_delay: float = 1.0,
    max_delay: float = 30.0,
    exponential_base: float = 2.0,
    jitter: bool = True,
    retryable_exceptions: Tuple[Type[Exception], ...] = (
        ConnectionError,
        TimeoutError,
        OSError,
        IOError,
    ),
    circuit_breaker: Optional[CircuitBreaker] = None,
    component: str = "unknown",
    operation: str = "unknown",
):
    def decorator(func: Callable) -> Callable:
        @wraps(func)
        def wrapper(*args, **kwargs) -> Any:
            last_exception = None

            for attempt in range(max_retries + 1):
                if circuit_breaker and not circuit_breaker.is_available():
                    CIRCUIT_BREAKER_STATE.labels(
                        component=component, operation=operation
                    ).set(CircuitState.OPEN.value)
                    raise CircuitBreakerOpenError(component, operation)

                try:
                    result = func(*args, **kwargs)
                    if circuit_breaker:
                        circuit_breaker.record_success()
                        CIRCUIT_BREAKER_STATE.labels(
                            component=component, operation=operation
                        ).set(CircuitState.CLOSED.value)
                    if attempt > 0:
                        RETRY_ATTEMPTS.labels(
                            component=component, operation=operation, result="success"
                        ).inc()
                    return result

                except retryable_exceptions as e:
                    last_exception = e
                    if circuit_breaker:
                        circuit_breaker.record_failure()
                        CIRCUIT_BREAKER_FAILURES.labels(
                            component=component, operation=operation
                        ).inc()
                        CIRCUIT_BREAKER_STATE.labels(
                            component=component, operation=operation
                        ).set(circuit_breaker.state.value)

                    if attempt < max_retries:
                        delay = calculate_backoff(
                            attempt, base_delay, max_delay, exponential_base, jitter
                        )
                        logger.warning(
                            f"Retry {attempt + 1}/{max_retries} for {component}.{operation} "
                            f"after {delay:.2f}s: {e}"
                        )
                        RETRY_ATTEMPTS.labels(
                            component=component, operation=operation, result="retry"
                        ).inc()
                        RETRY_DELAY.labels(
                            component=component, operation=operation
                        ).observe(delay)
                        time.sleep(delay)
                    else:
                        logger.error(
                            f"All retries exhausted for {component}.{operation}: {e}"
                        )
                        RETRY_ATTEMPTS.labels(
                            component=component, operation=operation, result="failed"
                        ).inc()

            raise last_exception

        return wrapper
    return decorator


async def async_retry_with_backoff(
    max_retries: int = 3,
    base_delay: float = 1.0,
    max_delay: float = 30.0,
    exponential_base: float = 2.0,
    jitter: bool = True,
    retryable_exceptions: Tuple[Type[Exception], ...] = (
        ConnectionError,
        TimeoutError,
        OSError,
        IOError,
    ),
    circuit_breaker: Optional[CircuitBreaker] = None,
    component: str = "unknown",
    operation: str = "unknown",
):
    def decorator(func: Callable) -> Callable:
        @wraps(func)
        async def wrapper(*args, **kwargs) -> Any:
            last_exception = None

            for attempt in range(max_retries + 1):
                if circuit_breaker and not circuit_breaker.is_available():
                    CIRCUIT_BREAKER_STATE.labels(
                        component=component, operation=operation
                    ).set(CircuitState.OPEN.value)
                    raise CircuitBreakerOpenError(component, operation)

                try:
                    result = await func(*args, **kwargs)
                    if circuit_breaker:
                        circuit_breaker.record_success()
                        CIRCUIT_BREAKER_STATE.labels(
                            component=component, operation=operation
                        ).set(CircuitState.CLOSED.value)
                    if attempt > 0:
                        RETRY_ATTEMPTS.labels(
                            component=component, operation=operation, result="success"
                        ).inc()
                    return result

                except retryable_exceptions as e:
                    last_exception = e
                    if circuit_breaker:
                        circuit_breaker.record_failure()
                        CIRCUIT_BREAKER_FAILURES.labels(
                            component=component, operation=operation
                        ).inc()
                        CIRCUIT_BREAKER_STATE.labels(
                            component=component, operation=operation
                        ).set(circuit_breaker.state.value)

                    if attempt < max_retries:
                        delay = calculate_backoff(
                            attempt, base_delay, max_delay, exponential_base, jitter
                        )
                        logger.warning(
                            f"Retry {attempt + 1}/{max_retries} for {component}.{operation} "
                            f"after {delay:.2f}s: {e}"
                        )
                        RETRY_ATTEMPTS.labels(
                            component=component, operation=operation, result="retry"
                        ).inc()
                        RETRY_DELAY.labels(
                            component=component, operation=operation
                        ).observe(delay)
                        await asyncio.sleep(delay)
                    else:
                        logger.error(
                            f"All retries exhausted for {component}.{operation}: {e}"
                        )
                        RETRY_ATTEMPTS.labels(
                            component=component, operation=operation, result="failed"
                        ).inc()

            raise last_exception

        return wrapper
    return decorator


import asyncio


def get_circuit_breaker(component: str, operation: str) -> CircuitBreaker:
    key = f"{component}.{operation}"
    if not hasattr(get_circuit_breaker, '_breakers'):
        get_circuit_breaker._breakers = {}
    if key not in get_circuit_breaker._breakers:
        get_circuit_breaker._breakers[key] = CircuitBreaker()
    return get_circuit_breaker._breakers[key]