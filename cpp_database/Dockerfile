# ==========================================
# Dockerfile for cpp_database
# Single stage - keep all deps for simplicity
# ==========================================

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install everything needed
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    g++ \
    libpqxx-dev \
    libpq-dev \
    libpqxx-6.4 \
    libpq5 \
    ca-certificates \
    git \
    python3 \
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
    make -j$(nproc) database_server

EXPOSE 8081

ENTRYPOINT ["/app/build/database_server"]