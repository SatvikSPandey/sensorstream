#pragma once

#include "sensor_data.h"
#include "ring_buffer.h"

#include <thread>
#include <vector>
#include <string>
#include <functional>

namespace sensorstream {

// Capacity: 4096 slots — power of 2, fits ~82 seconds of backlog at 50 readings/sec
// If the processing engine falls behind by more than this, push() returns false
// and readings are dropped (back-pressure) rather than unbounded memory growth
using SensorBuffer = RingBuffer<SensorReading, 4096>;

// Configuration for one simulated sensor
struct SensorConfig {
    std::string sensor_id;
    SensorType  type;
    double      base_value;   // realistic resting value for this sensor type
    double      noise_amplitude; // ±range of normal random fluctuation
    double      drift_rate;   // slow linear drift per second (simulates wear)
};

// SensorSimulator — producer side of the pipeline
//
// Spawns one std::jthread that loops at ~10 Hz, generating one reading
// per configured sensor per tick. jthread automatically joins on destruction
// (RAII) and supports cooperative cancellation via stop_token — no manual
// stop flags, no condition variables, no join() calls in teardown code.
//
class SensorSimulator {
public:
    explicit SensorSimulator(SensorBuffer& buffer);
    ~SensorSimulator() = default;

    // Non-copyable — owns a jthread
    SensorSimulator(const SensorSimulator&)            = delete;
    SensorSimulator& operator=(const SensorSimulator&) = delete;

    void start();
    void stop();  // requests cancellation — jthread joins in destructor

    [[nodiscard]] uint64_t readings_produced() const noexcept;
    [[nodiscard]] uint64_t readings_dropped()  const noexcept;

private:
    void run(std::stop_token st); // jthread entry point
    double generate_value(const SensorConfig& cfg, double elapsed_seconds);

    SensorBuffer&             buffer_;
    std::jthread              thread_;
    std::vector<SensorConfig> sensors_;
    std::atomic<uint64_t>     readings_produced_{ 0 };
    std::atomic<uint64_t>     readings_dropped_ { 0 };
};

} // namespace sensorstream
