# ==========================================
# Multi-stage Dockerfile for cpp_database
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
    libpqxx-dev \
    libpq-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY CMakeLists.txt .
COPY include/ include/
COPY src/ src/
COPY tests/ tests/

RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) database_server

# ---- Runtime Stage ----
FROM gcr.io/distroless/cc-debian12:nonroot

COPY --from=builder /app/build/database_server /database_server

EXPOSE 8081

ENTRYPOINT ["/database_server"]