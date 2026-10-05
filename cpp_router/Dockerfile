# ==========================================
# Dockerfile for cpp_router
# Single stage with all dependencies
# ==========================================

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install everything needed
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    g++ \
    libssl-dev \
    libssl3 \
    ca-certificates \
    git \
    python3 \
    pkg-config \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/* \
    && ldconfig

WORKDIR /app

COPY CMakeLists.txt .
COPY include/ include/
COPY src/ src/
COPY tests/ tests/

RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF && \
    make -j$(nproc) router

EXPOSE 8080

ENTRYPOINT ["/app/build/router"]