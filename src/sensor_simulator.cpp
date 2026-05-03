#include "sensor_simulator.h"

#include <cmath>
#include <random>
#include <chrono>

namespace sensorstream {

SensorSimulator::SensorSimulator(SensorBuffer& buffer)
    : buffer_(buffer)
{
    // Five sensors matching real industrial equipment monitoring scenarios:
    // Values chosen to be realistic — interviewers from industrial backgrounds
    // will notice if a furnace temperature is 20°C or pressure is 1000 bar
    sensors_ = {
        { "sensor_001", SensorType::TEMPERATURE, 85.0,  2.0,  0.002 },
        { "sensor_002", SensorType::PRESSURE,    4.5,   0.15, 0.0005 },
        { "sensor_003", SensorType::VIBRATION,   2.8,   0.4,  0.001 },
        { "sensor_004", SensorType::FLOW_RATE,   120.0, 5.0,  0.01 },
        { "sensor_005", SensorType::HUMIDITY,    45.0,  3.0,  0.0 },
    };
}

void SensorSimulator::start() {
    // jthread constructor takes a callable — if it accepts std::stop_token
    // as first argument, jthread passes its internal stop_token automatically
    // No manual plumbing needed — this is the C++20 cooperative cancellation model
    thread_ = std::jthread([this](std::stop_token st) {
        run(std::move(st));
    });
}

void SensorSimulator::stop() {
    thread_.request_stop(); // signals stop_token — run() will exit cleanly
    // jthread joins automatically in its destructor — no thread_.join() needed
}

uint64_t SensorSimulator::readings_produced() const noexcept {
    return readings_produced_.load(std::memory_order_relaxed);
}

uint64_t SensorSimulator::readings_dropped() const noexcept {
    return readings_dropped_.load(std::memory_order_relaxed);
}

void SensorSimulator::run(std::stop_token st) {
    // Each thread gets its own RNG — no shared state, no mutex needed
    std::mt19937_64 rng{ std::random_device{}() };
    std::normal_distribution<double> noise{ 0.0, 1.0 }; // standard normal

    // Occasional spike injection — simulates a real anomaly event
    // ~1% probability per reading — roughly one anomaly every 2 seconds
    std::uniform_real_distribution<double> spike_chance{ 0.0, 1.0 };
    std::uniform_real_distribution<double> spike_magnitude{ 4.0, 7.0 };

    const auto start_time = std::chrono::steady_clock::now();
    const auto tick_interval = std::chrono::milliseconds(100); // 10 Hz
    auto next_tick = start_time + tick_interval;

    // stop_requested() checks the stop_token without blocking
    // When stop() is called from outside, this returns true on next check
    while (!st.stop_requested()) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();

        for (const auto& cfg : sensors_) {
            double value = generate_value(cfg, elapsed);

            // 1% chance of injecting a spike anomaly on any reading
            if (spike_chance(rng) < 0.01) {
                double direction = (spike_chance(rng) < 0.5) ? 1.0 : -1.0;
                value += direction * spike_magnitude(rng) * cfg.noise_amplitude * 3.0;
            }

            auto reading = SensorReading::create(cfg.sensor_id, cfg.type, value);

            if (buffer_.push(std::move(reading))) {
                readings_produced_.fetch_add(1, std::memory_order_relaxed);
            } else {
                // Buffer full — drop this reading and count it
                // Back-pressure: we do NOT block or sleep here
                // The processing engine is the bottleneck — we report it via metrics
                readings_dropped_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Precise sleep until next tick — accounts for processing time above
        // sleep_until is more accurate than sleep_for because it doesn't
        // accumulate drift across iterations
        std::this_thread::sleep_until(next_tick);
        next_tick += tick_interval;
    }
}

double SensorSimulator::generate_value(const SensorConfig& cfg, double elapsed_seconds) {
    // Each sensor value = base + drift + noise
    // drift: slow linear change simulating wear/degradation over time
    // noise: random fluctuation drawn from a scaled normal distribution
    // Using a thread_local RNG would also work — member RNG is cleaner here
    // because generate_value is always called from the same thread
    static thread_local std::mt19937_64 rng{ std::random_device{}() };
    static thread_local std::normal_distribution<double> dist{ 0.0, 1.0 };

    const double drift = cfg.drift_rate * elapsed_seconds;
    const double noise = dist(rng) * cfg.noise_amplitude;
    return cfg.base_value + drift + noise;
}

} // namespace sensorstream
