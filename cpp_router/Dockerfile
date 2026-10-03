# ==========================================
# Multi-stage Dockerfile for cpp_router
# Builder stage: compiles with all dev dependencies
# Runtime stage: minimal distroless image
# ==========================================

# ---- Builder Stage ----
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    g++ \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY CMakeLists.txt .
COPY include/ include/
COPY src/ src/
COPY tests/ tests/

RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) router

# ---- Runtime Stage ----
FROM gcr.io/distroless/cc-debian12:nonroot

COPY --from=builder /app/build/router /router

EXPOSE 8080

ENTRYPOINT ["/router"]