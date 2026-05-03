#pragma once

#include "sensor_data.h"
#include "ring_buffer.h"
#include "sensor_simulator.h"
#include "data_store.h"
#include "statistics.h"

#include <thread>
#include <atomic>
#include <unordered_map>
#include <string>
#include <chrono>
#include <cstdint>

namespace sensorstream {

// ProcessingEngine — consumer side of the pipeline
//
// Owns one std::jthread that spins on the ring buffer, draining readings
// as fast as they arrive. For each reading it:
//   1. Updates per-sensor Welford statistics
//   2. Computes Z-score for anomaly detection
//   3. Forwards the reading + stats to DataStore for API serving
//
// Per-sensor WelfordStats live HERE, not in DataStore.
// DataStore is shared between this thread and the HTTP API threads.
// Keeping stats in ProcessingEngine means Welford updates are always
// single-threaded — no locking needed on the hot path.
// DataStore only receives the already-computed mean/stddev as scalars.
//
class ProcessingEngine {
public:
    explicit ProcessingEngine(SensorBuffer& buffer, DataStore& store);
    ~ProcessingEngine() = default;

    ProcessingEngine(const ProcessingEngine&)            = delete;
    ProcessingEngine& operator=(const ProcessingEngine&) = delete;

    void start();
    void stop();

    [[nodiscard]] uint64_t readings_processed() const noexcept;
    [[nodiscard]] uint64_t anomalies_detected() const noexcept;

    // Throughput in readings/second — computed over the last measurement window
    [[nodiscard]] double throughput_rps() const noexcept;

private:
    void run(std::stop_token st);

    SensorBuffer&                               buffer_;
    DataStore&                                  store_;
    std::jthread                                thread_;

    // Per-sensor Welford stats — only touched by the processing thread
    // unordered_map is fine here: no concurrent access, amortised O(1) lookup
    std::unordered_map<std::string, WelfordStats> per_sensor_stats_;

    std::atomic<uint64_t> readings_processed_{ 0 };
    std::atomic<uint64_t> anomalies_detected_ { 0 };

    // Throughput tracking
    std::atomic<uint64_t> window_count_      { 0 };
    std::atomic<double>   throughput_rps_    { 0.0 };
};

} // namespace sensorstream
