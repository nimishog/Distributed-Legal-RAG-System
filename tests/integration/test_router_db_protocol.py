"""
Integration tests for Router <-> Database protocol.

These tests verify the binary protocol communication between the C++ router
and the C++ database nodes, including the router's scatter-gather search,
replicated writes, and broadcast deletes.

Run against a live local stack:
    docker compose -f docker-compose.local.yml up -d
    python -m pytest tests/integration/test_router_db_protocol.py -v
"""

import json
import os
import socket
import struct
import uuid
from typing import List

import pytest

# Test configuration
ROUTER_HOST = os.getenv("ROUTER_HOST", "localhost")
ROUTER_PORT = int(os.getenv("ROUTER_PORT", "8080"))
# The router exposes HTTP health endpoints on a SEPARATE port from the binary
# protocol port (18084 locally, 8081 in prod).
ROUTER_HTTP_PORT = int(os.getenv("ROUTER_HTTP_PORT", "18084"))
DB_HOST = os.getenv("DB_HOST", "localhost")
DB_PORTS = [int(p) for p in os.getenv("DB_PORTS", "8081,8082,8083").split(",")]
EMBEDDING_DIM = int(os.getenv("EMBEDDING_DIM", "384"))


def new_id() -> str:
    return str(uuid.uuid4())


def one_hot(index: int) -> List[float]:
    vec = [0.0] * EMBEDDING_DIM
    vec[index % EMBEDDING_DIM] = 1.0
    return vec


class ProtocolClient:
    """Binary protocol client for testing."""

    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.sock: socket.socket = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(10)
        self.sock.connect((self.host, self.port))

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def send_frame(self, payload: dict) -> bool:
        data = json.dumps(payload).encode("utf-8")
        length = struct.pack(">I", len(data))
        try:
            self.sock.sendall(length + data)
            return True
        except Exception as e:
            print(f"Failed to send frame: {e}")
            return False

    def read_frame(self) -> dict:
        length_data = self.sock.recv(4)
        if len(length_data) != 4:
            raise ConnectionError("Failed to read frame length")
        length = struct.unpack(">I", length_data)[0]

        data = bytearray()
        while len(data) < length:
            chunk = self.sock.recv(length - len(data))
            if not chunk:
                raise ConnectionError("Connection closed")
            data.extend(chunk)
        return json.loads(data.decode("utf-8"))

    def send_request(self, payload: dict) -> dict:
        if not self.send_frame(payload):
            raise ConnectionError("Failed to send")
        return self.read_frame()


class TestRouterDBProtocol:
    """Integration tests for Router <-> DB protocol."""

    @pytest.fixture
    def router_client(self):
        client = ProtocolClient(ROUTER_HOST, ROUTER_PORT)
        client.connect()
        yield client
        client.close()

    @pytest.fixture
    def db_clients(self):
        clients = []
        for port in DB_PORTS:
            client = ProtocolClient(DB_HOST, port)
            client.connect()
            clients.append(client)
        yield clients
        for client in clients:
            client.close()

    def test_router_health_check(self):
        # HTTP health endpoints live on the router's HTTP port, not the binary
        # protocol port.
        client = ProtocolClient(ROUTER_HOST, ROUTER_HTTP_PORT)
        client.connect()
        try:
            request = b"GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n"
            client.sock.sendall(request)

            response = b""
            while b"\r\n\r\n" not in response:
                chunk = client.sock.recv(1024)
                if not chunk:
                    break
                response += chunk

            assert b"200 OK" in response
            assert b"healthy" in response
        finally:
            client.close()

    def test_router_binary_health_check(self, router_client):
        response = router_client.send_request({"op": "health"})
        assert response.get("status") == "ok"
        assert response.get("healthy") is True

    def test_database_health_check(self, db_clients):
        for client in db_clients:
            response = client.send_request({"op": "health"})
            assert response.get("status") == "ok"
            assert response.get("healthy") is True
            assert "total_chunks" in response

    def test_insert_and_search(self, router_client):
        doc_id = new_id()
        embedding = one_hot(1)

        response = router_client.send_request({
            "op": "insert",
            "id": doc_id,
            "text": "This is a test legal document about contract law.",
            "embedding": embedding,
            "metadata": {"source": "test", "section": "contracts"},
        })
        assert response.get("status") == "ok"

        response = router_client.send_request({
            "op": "search",
            "embedding": embedding,
            "top_k": 5,
        })
        assert response.get("status") == "ok"
        assert len(response["results"]) > 0
        assert response["results"][0]["id"] == doc_id
        assert response["results"][0]["score"] > 0.9

    def test_batch_insert(self, router_client):
        chunks = [{
            "id": new_id(),
            "text": f"Batch test document {i} about legal topic.",
            "embedding": one_hot(i + 10),
            "metadata": {"batch": True, "index": i},
        } for i in range(10)]

        response = router_client.send_request({"op": "batch_insert", "chunks": chunks})
        assert response.get("status") == "ok"

        response = router_client.send_request({
            "op": "search",
            "embedding": one_hot(10),
            "top_k": 5,
        })
        assert response.get("status") == "ok"
        assert len(response["results"]) > 0

    def test_delete_operation(self, router_client):
        doc_id = new_id()
        embedding = one_hot(7)

        response = router_client.send_request({
            "op": "insert",
            "id": doc_id,
            "text": "This document will be deleted.",
            "embedding": embedding,
            "metadata": {},
        })
        assert response.get("status") == "ok"

        response = router_client.send_request({
            "op": "search", "embedding": embedding, "top_k": 1,
        })
        assert response["results"][0]["id"] == doc_id

        # Deletes are broadcast to every shard.
        response = router_client.send_request({
            "op": "delete", "ids": [doc_id],
        })
        assert response.get("status") == "ok"

        response = router_client.send_request({
            "op": "search", "embedding": embedding, "top_k": 1,
        })
        if response["results"]:
            assert response["results"][0]["id"] != doc_id

    def test_search_fans_out_to_all_shards(self, router_client):
        doc_id = new_id()
        embedding = one_hot(20)

        router_client.send_request({
            "op": "insert",
            "id": doc_id,
            "text": "Fan-out test document.",
            "embedding": embedding,
            "metadata": {},
        })

        response = router_client.send_request({
            "op": "search", "embedding": embedding, "top_k": 5,
        })
        assert response.get("status") == "ok"
        # The router should have reached every healthy shard.
        assert response.get("partial") is False
        assert response["results"][0]["id"] == doc_id

    def test_invalid_uuid_rejected(self, router_client):
        response = router_client.send_request({
            "op": "insert",
            "id": "not-a-uuid",
            "text": "invalid id",
            "embedding": one_hot(1),
            "metadata": {},
        })
        assert response.get("status") == "error"
        assert response.get("code") == "INVALID_ARG"

    def test_router_distributes_documents(self, router_client):
        # Insert a batch of one-hot vectors; each lands on a shard chosen by
        # consistent hashing. Search fan-out must still retrieve them all.
        for i in range(20):
            router_client.send_request({
                "op": "insert",
                "id": new_id(),
                "text": f"Load balancing test document {i}",
                "embedding": one_hot(i),
                "metadata": {"test": "load_balancing"},
            })

        for i in range(20):
            response = router_client.send_request({
                "op": "search", "embedding": one_hot(i), "top_k": 1,
            })
            assert response.get("status") == "ok"
            assert len(response["results"]) == 1

    def test_sequential_requests(self, router_client):
        for i in range(20):
            response = router_client.send_request({
                "op": "insert",
                "id": new_id(),
                "text": f"Concurrent test document {i}",
                "embedding": one_hot(30 + i),
                "metadata": {"concurrent": True},
            })
            assert response.get("status") == "ok"

    def test_protocol_frame_format(self, router_client):
        data = json.dumps({"op": "health"}).encode("utf-8")
        length = struct.pack(">I", len(data))
        router_client.sock.sendall(length + data)
        response = router_client.read_frame()
        assert response.get("status") == "ok"
        assert response.get("healthy") is True

    def test_large_payload(self, router_client):
        large_text = "x" * (1024 * 1024)
        embedding = one_hot(99)
        doc_id = new_id()

        response = router_client.send_request({
            "op": "insert",
            "id": doc_id,
            "text": large_text,
            "embedding": embedding,
            "metadata": {"size": "1MB"},
        })
        assert response.get("status") == "ok"

        response = router_client.send_request({
            "op": "search", "embedding": embedding, "top_k": 1,
        })
        assert response.get("status") == "ok"
        assert len(response["results"]) == 1
        assert len(response["results"][0]["text"]) == 1024 * 1024

    def test_invalid_json_handling(self, router_client):
        # The protocol frames every message with a 4-byte big-endian length
        # prefix; the router reads the length first.
        payload = b"not valid json"
        router_client.sock.sendall(struct.pack(">I", len(payload)) + payload)
        response = router_client.read_frame()
        assert response.get("status") == "error"
        assert response.get("code") == "INVALID_ARG"

    def test_unknown_operation(self, router_client):
        response = router_client.send_request({"op": "unknown_operation"})
        assert response.get("status") == "error"
        assert response.get("code") == "INVALID_ARG"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
