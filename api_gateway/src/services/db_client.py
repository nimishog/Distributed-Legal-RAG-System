import asyncio
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
        self.last_used = asyncio.get_event_loop().time()

    async def send_frame(self, payload: dict) -> bool:
        data = json.dumps(payload).encode("utf-8")
        length = struct.pack("<I", len(data))
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
            length = struct.unpack("<I", length_data)[0]
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


_router_circuit_breaker = get_circuit_breaker("api_gateway", "router")


class RouterClient:
    def __init__(
        self,
        host: str,
        port: int,
        pool_size: int = 2,
        connect_timeout: float = 10.0,
        request_timeout: float = 30.0,
    ):
        self.host = host
        self.port = port
        self.pool_size = pool_size
        self.connect_timeout = connect_timeout
        self.request_timeout = request_timeout
        self._pool: asyncio.Queue[RouterConnection] = asyncio.Queue(maxsize=pool_size)
        self._lock = asyncio.Lock()
        self._initialized = False

    async def initialize(self):
        async with self._lock:
            if self._initialized:
                return
            for _ in range(self.pool_size):
                conn = await self._create_connection()
                if conn:
                    await self._pool.put(conn)
            self._initialized = True
            logger.info(f"Router client pool initialized with {self._pool.qsize()} connections")

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
        try:
            conn = await asyncio.wait_for(self._pool.get(), timeout=5.0)
            if not await self._ensure_connected(conn):
                await self._pool.put(await self._create_connection())
                return await self.acquire()
            conn.in_use = True
            conn.last_used = asyncio.get_event_loop().time()
            return conn
        except asyncio.TimeoutError:
            logger.error("Timeout acquiring connection from pool")
            return None

    async def release(self, conn: RouterConnection):
        conn.in_use = False
        conn.last_used = asyncio.get_event_loop().time()
        await self._pool.put(conn)

    @async_retry_with_backoff(
        max_retries=3,
        base_delay=1.0,
        max_delay=30.0,
        exponential_base=2.0,
        jitter=True,
        retryable_exceptions=(ConnectionError, TimeoutError, OSError, IOError),
        circuit_breaker=_router_circuit_breaker,
        component="api_gateway",
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
        conn = await self.acquire()
        if not conn:
            return False
        try:
            payload = {"op": "health"}
            if not await conn.send_frame(payload):
                return False
            response = await asyncio.wait_for(conn.read_frame(), timeout=5.0)
            return response and response.get("healthy", False)
        finally:
            await self.release(conn)

    async def close(self):
        while not self._pool.empty():
            try:
                conn = self._pool.get_nowait()
                conn.close()
                await conn.wait_closed()
            except asyncio.QueueEmpty:
                break
            except Exception as e:
                logger.error(f"Error closing connection: {e}")