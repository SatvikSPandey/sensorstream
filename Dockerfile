# ── Stage 1: Builder ──────────────────────────────────────────────────────
# Full build environment — GCC, CMake, Ninja, Git for FetchContent
# This stage is discarded after compilation — never shipped
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy dependency manifests first — maximises Docker layer cache
# If only source files change, CMake FetchContent layer is reused
COPY CMakeLists.txt .
COPY src/            src/
COPY tests/          tests/
COPY third_party/    third_party/

# Configure — downloads GTest and nlohmann/json via FetchContent
RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20

# Build main executable only — tests not needed in runtime image
RUN cmake --build build --target sensorstream -j$(nproc)

# ── Stage 2: Runtime ──────────────────────────────────────────────────────
# Minimal Debian — only what the binary needs to run
# No compiler, no CMake, no Git, no build tools
FROM debian:bookworm-slim AS runtime

# libstdc++ and libgcc are the only runtime dependencies of a GCC-compiled binary
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    libgcc-s1 \
    && rm -rf /var/lib/apt/lists/*

# Non-root user — security best practice for containerised services
RUN useradd --create-home --shell /bin/bash sensorstream
USER sensorstream
WORKDIR /home/sensorstream

# Copy only the compiled binary from the builder stage
# Everything else — source, headers, build tools — stays in stage 1
COPY --from=builder /app/build/sensorstream .

# Render injects $PORT at runtime — default 8080 for local runs
ENV PORT=8080
EXPOSE 8080

# Tini-less entrypoint — sensorstream handles SIGTERM directly
# via its signal_handler in main.cpp
ENTRYPOINT ["./sensorstream"]
