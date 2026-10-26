import asyncio
import contextvars
import json
import struct
import logging
from typing import List, Optional, Dict, Any
from dataclasses import dataclass

from ..config import get_settings
from ..utils.retry import async_retry_with_backoff, get_circuit_breaker

logger = logging.getLogger(__name__)


@dataclass
class SearchResult:
    id: str
    text: str
    score: float
    metadata: Dict[str, Any]


class RouterConnection:
    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        self.reader = reader
        self.writer = writer
        self.in_use = False
        try:
            self.last_used = asyncio.get_running_loop().time()
        except RuntimeError:
            self.last_used = 0.0

    async def send_frame(self, payload: dict) -> bool:
        data = json.dumps(payload).encode("utf-8")
        length = struct.pack(">I", len(data))
        try:
            self.writer.write(length + data)
            await self.writer.drain()
            return True
        except Exception as e:
            logger.error(f"Failed to send frame: {e}")
            return False

    async def read_frame(self) -> Optional[dict]:
        try:
            length_data = await self.reader.readexactly(4)
            if len(length_data) != 4:
                return None
            length = struct.unpack(">I", length_data)[0]
            if length > 16 * 1024 * 1024:
                logger.error(f"Frame too large: {length}")
                return None

            data = await self.reader.readexactly(length)
            return json.loads(data.decode("utf-8"))
        except Exception as e:
            logger.error(f"Failed to read frame: {e}")
            return None

    def close(self):
        if not self.writer.is_closing():
            self.writer.close()

    async def wait_closed(self):
        await self.writer.wait_closed()


_router_circuit_breaker = get_circuit_breaker("router", "search")


_search_partial_var: contextvars.ContextVar[bool] = contextvars.ContextVar("search_partial", default=False)


class RouterClient:
    def __init__(
        self,
        host: str,
        port: int,
        pool_size: int = 2,
        connect_timeout: float = 10.0,
        request_timeout: float = 30.0,
        acquire_timeout: float = 10.0,
    ):
        self.host = host
        self.port = port
        self.pool_size = pool_size
        self.connect_timeout = connect_timeout
        self.request_timeout = request_timeout
        self.acquire_timeout = acquire_timeout
        self._pool: asyncio.Queue[RouterConnection] = asyncio.Queue(maxsize=pool_size)
        self._lock = asyncio.Lock()
        self._initialized = False
        self._closed = False
        self._total_connections = 0

    @property
    def last_search_partial(self) -> bool:
        return _search_partial_var.get()

    async def initialize(self):
        async with self._lock:
            if self._initialized:
                return
            self._initialized = True
            self._total_connections = 0
            logger.info("Router client pool initialized (lazy connections)")

    async def _create_connection(self) -> Optional[RouterConnection]:
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(self.host, self.port),
                timeout=self.connect_timeout,
            )
            return RouterConnection(reader, writer)
        except Exception as e:
            logger.error(f"Failed to create router connection: {e}")
            return None

    async def _ensure_connected(self, conn: RouterConnection) -> bool:
        if conn.writer.is_closing():
            try:
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection(self.host, self.port),
                    timeout=self.connect_timeout,
                )
                conn.reader = reader
                conn.writer = writer
                return True
            except Exception as e:
                logger.error(f"Failed to reconnect: {e}")
                return False
        return True

    async def acquire(self) -> Optional[RouterConnection]:
        if self._closed:
            logger.error("Cannot acquire connection: client is closed")
            return None

        # Try to get an idle connection from the pool without blocking.
        try:
            conn = self._pool.get_nowait()
        except asyncio.QueueEmpty:
            conn = None

        if conn is None:
            # Pool empty: create a new connection if under the pool_size limit.
            async with self._lock:
                if self._total_connections < self.pool_size:
                    self._total_connections += 1
                    should_create = True
                else:
                    should_create = False

            if should_create:
                conn = await self._create_connection()
                if not conn:
                    async with self._lock:
                        self._total_connections -= 1
                    return None
            else:
                # Wait for an existing connection to be released.
                try:
                    conn = await asyncio.wait_for(self._pool.get(), timeout=self.acquire_timeout)
                except asyncio.TimeoutError:
                    logger.error(f"Timeout acquiring connection from pool after {self.acquire_timeout}s")
                    return None

        if not await self._ensure_connected(conn):
            new_conn = await self._create_connection()
            if not new_conn:
                async with self._lock:
                    self._total_connections -= 1
                logger.error("Failed to create replacement router connection")
                return None
            conn = new_conn

        conn.in_use = True
        conn.last_used = asyncio.get_running_loop().time()
        return conn

    async def release(self, conn: RouterConnection):
        if self._closed:
            conn.close()
            await conn.wait_closed()
            async with self._lock:
                self._total_connections -= 1
            return
        conn.in_use = False
        conn.last_used = asyncio.get_running_loop().time()
        await self._pool.put(conn)

    @async_retry_with_backoff(
        max_retries=3,
        base_delay=1.0,
        max_delay=30.0,
        exponential_base=2.0,
        jitter=True,
        retryable_exceptions=(ConnectionError, TimeoutError, OSError, IOError),
        circuit_breaker=_router_circuit_breaker,
        component="router",
        operation="search",
    )
    async def search(
        self,
        embedding: List[float],
        top_k: int = 10,
        filter_metadata: Optional[Dict[str, Any]] = None,
    ) -> List[SearchResult]:
        conn = await self.acquire()
        if not conn:
            raise ConnectionError("No available router connections")

        try:
            payload = {
                "op": "search",
                "embedding": embedding,
                "top_k": top_k,
                "filter": filter_metadata or {},
            }

            if not await conn.send_frame(payload):
                raise ConnectionError("Failed to send search request")

            response = await asyncio.wait_for(conn.read_frame(), timeout=self.request_timeout)
            if not response:
                raise ConnectionError("No response from router")

            if response.get("status") != "ok":
                raise ConnectionError(f"Router error: {response}")

            _search_partial_var.set(bool(response.get("partial", False)))

            results = []
            for r in response.get("results", []):
                results.append(
                    SearchResult(
                        id=r["id"],
                        text=r["text"],
                        score=r["score"],
                        metadata=r.get("metadata", {}),
                    )
                )
            return results
        finally:
            await self.release(conn)

    async def health_check(self) -> bool:
        # The router closes the connection after a health op, so use a
        # dedicated short-lived connection to avoid poisoning the pool.
        conn = await self._create_connection()
        if not conn:
            return False
        try:
            payload = {"op": "health"}
            if not await conn.send_frame(payload):
                return False
            response = await asyncio.wait_for(conn.read_frame(), timeout=5.0)
            return bool(response and response.get("healthy", False))
        finally:
            try:
                conn.close()
                await conn.wait_closed()
            except Exception as e:
                logger.debug(f"Error closing health check connection: {e}")

    async def close(self):
        self._closed = True
        logger.info("Draining router client connection pool...")

        # Wait for in-use connections to be released (with timeout)
        max_wait_time = 30.0
        try:
            start_time = asyncio.get_running_loop().time()
        except RuntimeError:
            start_time = 0.0

        while True:
            if self._pool.qsize() >= self._total_connections:
                break
            await asyncio.sleep(0.5)
            try:
                if asyncio.get_running_loop().time() - start_time > max_wait_time:
                    logger.warning("Timeout waiting for connections to be released, forcing close")
                    break
            except RuntimeError:
                break

        # Close all connections in pool
        while not self._pool.empty():
            try:
                conn = self._pool.get_nowait()
                conn.close()
                await conn.wait_closed()
            except asyncio.QueueEmpty:
                break
            except Exception as e:
                logger.error(f"Error closing connection: {e}")

        async with self._lock:
            self._total_connections = 0

        logger.info("Router client connection pool closed")
