# SensorStream

**Real-Time Industrial Sensor Processing Engine — C++20**

A production-grade C++20 service that ingests high-frequency industrial sensor data through a lock-free pipeline, performs real-time statistical anomaly detection, and serves results through a REST API with a live web dashboard.

[![CI](https://github.com/SatvikSPandey/sensorstream/actions/workflows/ci.yml/badge.svg)](https://github.com/SatvikSPandey/sensorstream/actions/workflows/ci.yml)

**Live Demo:** https://sensorstream-satvik.onrender.com

---

## Why C++?

This project demonstrates the domain where C++ is the correct choice over Python or Java:

| Concern | Python/Java | C++20 |
|---|---|---|
| Sensor ingestion | GIL / GC pauses | Lock-free SPSC ring buffer |
| Latency | Non-deterministic | Sub-millisecond, predictable |
| Memory | Heap allocations per sample | Stack-allocated buffer, zero malloc in steady state |
| Thread lifecycle | Manual join / interrupt | `std::jthread` + `std::stop_token` (RAII) |
| Numerical stability | Naïve sum/count drift | Welford's online algorithm |

---

## Architecture

Sensor Simulator (jthread, 10 Hz)
→ Lock-Free SPSC Ring Buffer (4096 slots, atomic head/tail)
→ Processing Engine (jthread, Welford stats, Z-score anomaly detection)
→ In-Memory Time-Series Store (1000 readings/sensor, mutex-protected)
→ REST API (cpp-httplib, 6 endpoints)
→ Live Web Dashboard (Chart.js, 1-second polling)

---

## Features

- **C++20** — `std::jthread`, `std::stop_token`, concepts, structured bindings, `std::span`
- **Lock-free SPSC ring buffer** — template class with compile-time power-of-2 capacity, `alignas(64)` false-sharing prevention, acquire/release memory ordering
- **Welford's online algorithm** — numerically stable rolling mean and standard deviation in O(1) per sample
- **Z-score anomaly detection** — 3-sigma rule, configurable threshold, all 5 sensor types
- **REST API** — 6 endpoints: sensors list, latest reading, history, anomaly log, metrics
- **Live dashboard** — dark-themed, Chart.js sparklines per sensor, real-time anomaly log
- **Multi-stage Docker build** — builder stage (GCC + CMake) → runtime stage (~80MB image)
- **Google Test** — 39 unit tests covering ring buffer, statistics, and data store
- **GitHub Actions CI** — build, test, and Docker image validation on every push

---

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Live web dashboard |
| GET | `/api/sensors` | All sensor latest readings + statistics |
| GET | `/api/sensors/{id}/latest` | Single sensor reading (pretty JSON) |
| GET | `/api/sensors/{id}/history?limit=N` | Last N readings for a sensor |
| GET | `/api/anomalies?limit=N` | Recent anomaly events with Z-scores |
| GET | `/api/metrics` | Throughput, drops, buffer utilisation |

---

## Tech Stack

- **Language:** C++20
- **Build:** CMake 3.24+ with FetchContent, Ninja
- **HTTP:** cpp-httplib (header-only)
- **JSON:** nlohmann/json (header-only)
- **Testing:** Google Test 1.14
- **Container:** Docker (multi-stage, debian:bookworm-slim)
- **CI/CD:** GitHub Actions
- **Deployment:** Render (Docker web service, Singapore)

---

## Local Development

### Prerequisites
- GCC 12+ or MSVC 2022 (C++20 required)
- CMake 3.24+
- Ninja
- Docker (optional)

### Build and run
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sensorstream
```

### Run tests
```bash
cd build
ctest --output-on-failure
```

### Docker
```bash
docker build -t sensorstream:latest .
docker run --rm -p 8080:8080 sensorstream:latest
```

Open `http://localhost:8080` in your browser.

---

## Project Structure

sensorstream/
├── src/
│   ├── main.cpp                 # Entry point, signal handling, graceful shutdown
│   ├── sensor_data.h            # SensorReading struct, SensorType enum
│   ├── ring_buffer.h            # Lock-free SPSC ring buffer (template)
│   ├── statistics.h             # Welford's algorithm, Z-score detection
│   ├── sensor_simulator.h/.cpp  # Producer thread — 5 sensors @ 10 Hz
│   ├── processing_engine.h/.cpp # Consumer thread — stats + anomaly detection
│   ├── data_store.h/.cpp        # Thread-safe in-memory time-series store
│   ├── api_server.h/.cpp        # REST API — cpp-httplib routes
│   └── dashboard.h              # Embedded HTML/JS dashboard
├── tests/
│   ├── test_ring_buffer.cpp     # 9 tests — SPSC correctness + concurrency
│   ├── test_statistics.cpp      # 13 tests — Welford + Z-score + boundary cases
│   └── test_data_store.cpp      # 17 tests — storage, history, anomaly, thread safety
├── third_party/
│   └── httplib.h                # cpp-httplib (header-only, committed)
├── CMakeLists.txt
├── Dockerfile
└── .github/workflows/ci.yml

---

## Author

**Satvik Pandey**
GitHub: [github.com/SatvikSPandey](https://github.com/SatvikSPandey)
LinkedIn: [linkedin.com/in/satvikpandey-433555365](https://linkedin.com/in/satvikpandey-433555365)
Portfolio: [satvikspandey.netlify.app](https://satvikspandey.netlify.app)
