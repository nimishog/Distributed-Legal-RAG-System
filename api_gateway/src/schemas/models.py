from pydantic import BaseModel, Field
from typing import List, Optional, Dict, Any
from datetime import datetime


class SearchResult(BaseModel):
    id: str
    text: str
    score: float
    metadata: Dict[str, Any] = {}


class Citation(BaseModel):
    index: int
    source_id: str
    text: str
    score: float
    metadata: Dict[str, Any] = {}


class AskRequest(BaseModel):
    question: str = Field(..., min_length=1, max_length=2000)
    top_k: int = Field(default=5, ge=1, le=20)
    include_citations: bool = Field(default=True)


class AskResponse(BaseModel):
    answer: str
    citations: List[Citation] = []
    search_latency_ms: float
    llm_latency_ms: float
    total_latency_ms: float
    model: str
    timestamp: datetime = Field(default_factory=datetime.utcnow)


class HealthResponse(BaseModel):
    status: str
    healthy: bool
    router_connected: bool
    embedding_model_loaded: bool
    timestamp: datetime = Field(default_factory=datetime.utcnow)


class SystemHealthResponse(BaseModel):
    status: str
    cpu_percent: float
    memory_percent: float
    memory_available_mb: float
    swap_percent: float
    disk_percent: float
    uptime_seconds: float
    timestamp: datetime = Field(default_factory=datetime.utcnow)