#include "processing_engine.h"

#include <chrono>
#include <thread>

namespace sensorstream {

ProcessingEngine::ProcessingEngine(SensorBuffer& buffer, DataStore& store)
    : buffer_(buffer), store_(store)
{}

void ProcessingEngine::start() {
    thread_ = std::jthread([this](std::stop_token st) {
        run(std::move(st));
    });
}

void ProcessingEngine::stop() {
    thread_.request_stop();
}

uint64_t ProcessingEngine::readings_processed() const noexcept {
    return readings_processed_.load(std::memory_order_relaxed);
}

uint64_t ProcessingEngine::anomalies_detected() const noexcept {
    return anomalies_detected_.load(std::memory_order_relaxed);
}

double ProcessingEngine::throughput_rps() const noexcept {
    return throughput_rps_.load(std::memory_order_relaxed);
}

void ProcessingEngine::run(std::stop_token st) {
    using clock     = std::chrono::steady_clock;
    using duration  = std::chrono::duration<double>;

    // Throughput measurement window — recalculate every second
    auto     window_start  = clock::now();
    uint64_t window_count  = 0;

    // Spin interval when buffer is empty — yields CPU rather than busy-waiting
    // 1ms sleep keeps CPU usage near 0% when idle
    // When readings are flowing at 50/sec, the buffer is rarely empty so
    // the sleep is almost never hit — latency stays sub-millisecond
    constexpr auto idle_sleep = std::chrono::milliseconds(1);

    while (!st.stop_requested()) {
        auto item = buffer_.pop();

        if (!item.has_value()) {
            // Buffer empty — yield CPU briefly then check again
            std::this_thread::sleep_for(idle_sleep);
            continue;
        }

        const SensorReading& reading = item.value();

        // ── Hot path: runs for every single reading ──────────────────────
        // Per-sensor stats lookup — operator[] inserts default WelfordStats
        // on first encounter (same pattern as DataStore for new sensors)
        auto& stats = per_sensor_stats_[reading.sensor_id];
        stats.update(reading.value);

        auto [mean, stddev] = stats.summary();
        double zscore = z_score(reading.value, mean, stddev);

        if (is_anomaly(reading.value, mean, stddev)) {
            anomalies_detected_.fetch_add(1, std::memory_order_relaxed);
        }

        // Forward to DataStore — this acquires DataStore's mutex briefly
        store_.record(reading, mean, stddev, zscore);

        readings_processed_.fetch_add(1, std::memory_order_relaxed);
        ++window_count;

        // ── Throughput calculation — outside the per-reading hot path ────
        auto now     = clock::now();
        double secs  = duration(now - window_start).count();
        if (secs >= 1.0) {
            double rps = static_cast<double>(window_count) / secs;
            throughput_rps_.store(rps, std::memory_order_relaxed);
            window_count  = 0;
            window_start  = now;
        }
    }
}

} // namespace sensorstream
