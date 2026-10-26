import os
import logging
from typing import Optional
from contextlib import contextmanager

from opentelemetry import trace
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import BatchSpanProcessor, ConsoleSpanExporter
from opentelemetry.sdk.resources import Resource, SERVICE_NAME
from opentelemetry.instrumentation.fastapi import FastAPIInstrumentor
from opentelemetry.instrumentation.httpx import HTTPXClientInstrumentor
from opentelemetry.exporter.otlp.proto.grpc.trace_exporter import OTLPSpanExporter
from opentelemetry.trace.propagation.tracecontext import TraceContextTextMapPropagator
from opentelemetry.propagate import inject, extract

from ..config import get_settings

logger = logging.getLogger(__name__)

_tracer: Optional[trace.Tracer] = None
_initialized = False


def init_tracing(app=None) -> trace.Tracer:
    """Initialize OpenTelemetry tracing."""
    global _tracer, _initialized
    
    if _initialized:
        return _tracer
    
    settings = get_settings()
    
    # Create resource with service name
    resource = Resource.create({
        SERVICE_NAME: "api-gateway",
        "service.version": "1.0.0",
        "deployment.environment": os.getenv("ENVIRONMENT", "development"),
    })
    
    # Create tracer provider
    provider = TracerProvider(resource=resource)
    
    # Add span processor
    otlp_endpoint = os.getenv("OTEL_EXPORTER_OTLP_ENDPOINT")
    if otlp_endpoint:
        try:
            otlp_exporter = OTLPSpanExporter(endpoint=otlp_endpoint, insecure=True)
            provider.add_span_processor(BatchSpanProcessor(otlp_exporter))
            logger.info(f"Configured OTLP exporter to {otlp_endpoint}")
        except Exception as e:
            logger.warning(f"Failed to configure OTLP exporter: {e}")
    
    if os.getenv("OTEL_CONSOLE_EXPORTER", "false").lower() in ("1", "true", "yes"):
        provider.add_span_processor(BatchSpanProcessor(ConsoleSpanExporter()))
    
    # Set global tracer provider
    trace.set_tracer_provider(provider)
    
    # Get tracer
    _tracer = trace.get_tracer(__name__)
    
    # Instrument FastAPI if app provided
    if app:
        FastAPIInstrumentor.instrument_app(app)
    
    # Instrument httpx
    HTTPXClientInstrumentor().instrument()
    
    _initialized = True
    logger.info("OpenTelemetry tracing initialized")
    
    return _tracer


def get_tracer() -> Optional[trace.Tracer]:
    """Get the initialized tracer."""
    return _tracer


@contextmanager
def start_span(name: str, attributes: dict = None):
    """Context manager to start a new span."""
    tracer = get_tracer()
    if not tracer:
        yield None
        return
    
    with tracer.start_as_current_span(name, attributes=attributes or {}) as span:
        yield span


def inject_trace_context(headers: dict) -> dict:
    """Inject trace context into headers for outgoing requests."""
    inject(headers)
    return headers


def extract_trace_context(headers: dict):
    """Extract trace context from incoming headers."""
    return extract(headers)


def add_span_attributes(attributes: dict):
    """Add attributes to the current span."""
    span = trace.get_current_span()
    if span and span.is_recording():
        for key, value in attributes.items():
            span.set_attribute(key, value)


def record_exception(exc: Exception):
    """Record an exception in the current span."""
    span = trace.get_current_span()
    if span and span.is_recording():
        span.record_exception(exc)
        span.set_status(trace.Status(trace.StatusCode.ERROR, str(exc)))