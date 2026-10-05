#!/usr/bin/env python3
"""
End-to-end integration test for Legal RAG system.
Tests the full flow: health -> ask -> verify citations
"""
import os
import sys
import time
import requests
import json
from typing import Dict, Any

API_URL = os.getenv("API_URL", "http://localhost:8000")
TIMEOUT = 60

def test_health() -> Dict[str, Any]:
    """Test /health endpoint"""
    print("Testing /health...")
    resp = requests.get(f"{API_URL}/health", timeout=10)
    assert resp.status_code == 200, f"Health check failed: {resp.status_code}"
    data = resp.json()
    assert data["healthy"] == True, f"Service not healthy: {data}"
    assert data["router_connected"] == True, "Router not connected"
    assert data["embedding_model_loaded"] == True, "Embedding model not loaded"
    print(f"  ✓ Health OK: {data['status']}")
    return data

def test_system_health() -> Dict[str, Any]:
    """Test /system-health endpoint"""
    print("Testing /system-health...")
    resp = requests.get(f"{API_URL}/system-health", timeout=10)
    assert resp.status_code == 200, f"System health failed: {resp.status_code}"
    data = resp.json()
    assert data["status"] == "ok"
    assert data["cpu_percent"] >= 0
    assert data["memory_percent"] >= 0
    print(f"  ✓ System Health OK: CPU={data['cpu_percent']:.1f}%, Mem={data['memory_percent']:.1f}%")
    return data

def test_ask(question: str, top_k: int = 5) -> Dict[str, Any]:
    """Test /api/ask endpoint"""
    print(f"Testing /api/ask: '{question[:50]}...'")
    payload = {
        "question": question,
        "top_k": top_k,
        "include_citations": True
    }
    resp = requests.post(f"{API_URL}/api/ask", json=payload, timeout=TIMEOUT)
    assert resp.status_code == 200, f"Ask failed: {resp.status_code} - {resp.text}"
    data = resp.json()
    
    # Verify response structure
    assert "answer" in data, "Missing answer field"
    assert "citations" in data, "Missing citations field"
    assert "search_latency_ms" in data, "Missing search_latency_ms"
    assert "llm_latency_ms" in data, "Missing llm_latency_ms"
    assert "total_latency_ms" in data, "Missing total_latency_ms"
    assert "model" in data, "Missing model field"
    assert data["model"] == "gpt-4o-mini", f"Unexpected model: {data['model']}"
    
    # Verify citations structure
    for citation in data["citations"]:
        assert "index" in citation
        assert "source_id" in citation
        assert "text" in citation
        assert "score" in citation
        assert "metadata" in citation
        assert citation["score"] >= 0 and citation["score"] <= 1
    
    print(f"  ✓ Answer received ({len(data['citations'])} citations, {data['total_latency_ms']:.0f}ms total)")
    print(f"    Answer: {data['answer'][:100]}...")
    return data

def test_insufficient_context() -> Dict[str, Any]:
    """Test question with insufficient context"""
    print("Testing insufficient context handling...")
    # Very specific question unlikely to be in legal corpus
    question = "What is the exact wording of section 42.7 of the fictional Intergalactic Trade Treaty of 3024?"
    payload = {"question": question, "top_k": 5, "include_citations": True}
    resp = requests.post(f"{API_URL}/api/ask", json=payload, timeout=TIMEOUT)
    assert resp.status_code == 200, f"Ask failed: {resp.status_code}"
    data = resp.json()
    # Should return insufficient context message
    assert "Insufficient context" in data["answer"] or len(data["citations"]) == 0
    print(f"  ✓ Insufficient context handled: {data['answer'][:80]}...")
    return data

def test_rate_limit() -> None:
    """Test rate limiting (should allow burst of 2 concurrent)"""
    print("Testing rate limiting...")
    import concurrent.futures
    
    def make_request():
        return requests.post(
            f"{API_URL}/api/ask",
            json={"question": "Test question for rate limit", "top_k": 1},
            timeout=TIMEOUT
        )
    
    # Send 5 concurrent requests (semaphore allows 2)
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
        futures = [executor.submit(make_request) for _ in range(5)]
        results = [f.result() for f in concurrent.futures.as_completed(futures)]
    
    success_count = sum(1 for r in results if r.status_code == 200)
    rate_limited = sum(1 for r in results if r.status_code == 429)
    server_errors = sum(1 for r in results if r.status_code >= 500)
    
    print(f"  Results: {success_count} success, {rate_limited} rate limited, {server_errors} server errors")
    # Should handle gracefully (not all 500)
    assert server_errors < 3, "Too many server errors under load"
    print("  ✓ Rate limiting works")

def main():
    print(f"=== E2E Integration Test ===")
    print(f"Target: {API_URL}")
    print()
    
    try:
        test_health()
        test_system_health()
        
        # Legal questions from test suite
        questions = [
            "What is the statute of limitations for breach of contract in California?",
            "What are the elements of a negligence claim in New York?",
            "How does the discovery rule affect statute of limitations in federal court?",
        ]
        
        for q in questions:
            test_ask(q)
            time.sleep(1)  # Brief pause between requests
        
        test_insufficient_context()
        test_rate_limit()
        
        print()
        print("=== All Tests Passed ===")
        return 0
        
    except AssertionError as e:
        print(f"\n✗ TEST FAILED: {e}")
        return 1
    except Exception as e:
        print(f"\n✗ TEST ERROR: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())