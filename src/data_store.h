#pragma once

#include "sensor_data.h"
#include "statistics.h"

#include <deque>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <string>
#include <optional>
#include <cstdint>

namespace sensorstream {

// Maximum readings retained per sensor in the circular history
inline constexpr std::size_t MAX_HISTORY = 1000;

// Maximum anomaly events retained in the global log
inline constexpr std::size_t MAX_ANOMALIES = 200;

// A detected anomaly event — snapshot of the reading and its statistics at
// the moment of detection
struct AnomalyEvent {
    SensorReading reading;
    double        z_score;
    double        mean_at_detection;
    double        stddev_at_detection;
};

// Per-sensor state: history ring + live Welford statistics
struct SensorState {
    std::deque<SensorReading> history;   // bounded circular history
    WelfordStats              stats;     // live rolling mean + stddev
    double                    last_value{ 0.0 };
    int64_t                   last_timestamp_ns{ 0 };
};

// DataStore — the single source of truth for the API layer
//
// Written by the processing engine (one thread).
// Read by the HTTP API handlers (potentially multiple threads).
// Protected by a shared mutex — writers take unique_lock,
// readers take shared_lock (C++17 std::shared_mutex).
//
class DataStore {
public:
    DataStore();

    // Called by processing engine — records a reading and its statistics
    void record(const SensorReading& reading,
                double mean, double stddev, double z_score);

    // Called by API handlers — all return copies, never references
    // (references into the store would require holding the lock across
    //  serialisation, which would block the processing engine)

    [[nodiscard]]
    std::vector<std::string> sensor_ids() const;

    [[nodiscard]]
    std::optional<SensorReading> latest(const std::string& sensor_id) const;

    [[nodiscard]]
    std::optional<std::pair<double,double>> latest_stats(
        const std::string& sensor_id) const;

    [[nodiscard]]
    std::vector<SensorReading> history(
        const std::string& sensor_id, std::size_t limit) const;

    [[nodiscard]]
    std::vector<AnomalyEvent> recent_anomalies(std::size_t limit) const;

    [[nodiscard]] uint64_t total_readings() const noexcept;

private:
    mutable std::mutex                              mutex_;
    std::unordered_map<std::string, SensorState>   sensors_;
    std::deque<AnomalyEvent>                        anomalies_;
    std::atomic<uint64_t>                           total_readings_{ 0 };
};

} // namespace sensorstream
