import os
import json
import socket
import struct
from pathlib import Path
from typing import List, Dict, Any

from ..utils.retry import retry_with_backoff, get_circuit_breaker

ROUTER_HOST = os.getenv("ROUTER_HOST", "cpp_router")
ROUTER_PORT = int(os.getenv("ROUTER_PORT", "8080"))
BATCH_SIZE = int(os.getenv("INSERT_BATCH_SIZE", "100"))
MAX_INGESTION_CHUNKS = int(os.getenv("MAX_INGESTION_CHUNKS", "1000"))

RETRY_MAX_ATTEMPTS = int(os.getenv("RETRY_MAX_ATTEMPTS", "3"))
RETRY_BASE_DELAY = float(os.getenv("RETRY_BASE_DELAY", "1.0"))
RETRY_MAX_DELAY = float(os.getenv("RETRY_MAX_DELAY", "30.0"))
RETRY_EXPONENTIAL_BASE = float(os.getenv("RETRY_EXPONENTIAL_BASE", "2.0"))
RETRY_JITTER = os.getenv("RETRY_JITTER", "true").lower() == "true"

CIRCUIT_FAILURE_THRESHOLD = int(os.getenv("CIRCUIT_FAILURE_THRESHOLD", "5"))
CIRCUIT_SUCCESS_THRESHOLD = int(os.getenv("CIRCUIT_SUCCESS_THRESHOLD", "2"))
CIRCUIT_TIMEOUT = float(os.getenv("CIRCUIT_TIMEOUT", "30.0"))

DATA_DIR = Path(os.getenv("DATA_DIR", "/app/data"))
INPUT_FILE = DATA_DIR / "embedded_chunks.json"

_router_circuit_breaker = get_circuit_breaker("ingestion", "router")


def send_frame(sock: socket.socket, payload: str) -> bool:
    data = payload.encode('utf-8')
    length = struct.pack('<I', len(data))
    try:
        sock.sendall(length + data)
        return True
    except Exception:
        return False


def read_frame(sock: socket.socket) -> str:
    length_data = sock.recv(4)
    if len(length_data) != 4:
        raise ConnectionError("Failed to read frame length")
    length = struct.unpack('<I', length_data)[0]

    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise ConnectionError("Connection closed")
        data.extend(chunk)
    return data.decode('utf-8')


@retry_with_backoff(
    max_retries=RETRY_MAX_ATTEMPTS,
    base_delay=RETRY_BASE_DELAY,
    max_delay=RETRY_MAX_DELAY,
    exponential_base=RETRY_EXPONENTIAL_BASE,
    jitter=RETRY_JITTER,
    retryable_exceptions=(ConnectionError, TimeoutError, OSError, IOError, socket.timeout),
    circuit_breaker=_router_circuit_breaker,
    component="ingestion",
    operation="send_request",
)
def send_request(payload: dict) -> dict:
    with socket.create_connection((ROUTER_HOST, ROUTER_PORT), timeout=10) as sock:
        sock.settimeout(30)
        if not send_frame(sock, json.dumps(payload)):
            raise ConnectionError("Failed to send")
        response = read_frame(sock)
        result = json.loads(response)
        if result.get("status") != "ok":
            raise ConnectionError(f"Router error: {result}")
        return result


def insert_batch(chunks: List[Dict[str, Any]]) -> bool:
    for chunk in chunks:
        payload = {
            "op": "insert",
            "id": chunk.get("id", f"chunk-{hash(chunk['text'])}"),
            "text": chunk["text"],
            "embedding": chunk["embedding"],
            "metadata": chunk["metadata"]
        }
        try:
            response = send_request(payload)
            if response.get("status") != "ok":
                print(f"Insert failed: {response}")
                return False
        except Exception as e:
            print(f"Insert failed after retries: {e}")
            return False
    return True


def main():
    print(f"Loading embedded chunks from {INPUT_FILE}...")
    with open(INPUT_FILE) as f:
        chunks = json.load(f)

    chunks = chunks[:MAX_INGESTION_CHUNKS]
    print(f"Inserting {len(chunks)} chunks into router at {ROUTER_HOST}:{ROUTER_PORT}...")

    for i in range(0, len(chunks), BATCH_SIZE):
        batch = chunks[i:i + BATCH_SIZE]
        print(f"  Batch {i//BATCH_SIZE + 1}/{(len(chunks)-1)//BATCH_SIZE + 1} ({len(batch)} chunks)...")

        if not insert_batch(batch):
            print(f"Failed to insert batch starting at index {i}")
            return

    print("All chunks inserted successfully!")

    print("\nVerifying with a test search...")
    test_embedding = chunks[0]["embedding"]
    response = send_request({
        "op": "search",
        "embedding": test_embedding,
        "top_k": 3
    })
    print(f"Search results: {len(response.get('results', []))} chunks found")


if __name__ == "__main__":
    main()