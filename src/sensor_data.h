#pragma once

#include <chrono>
#include <string>
#include <cstdint>

namespace sensorstream {

// Five industrial sensor types — each maps to a physical measurement unit
enum class SensorType {
    TEMPERATURE,  // degrees Celsius     — furnace/motor thermal monitoring
    PRESSURE,     // bar                 — hydraulic/pneumatic line pressure
    VIBRATION,    // mm/s RMS            — rotating machinery health
    FLOW_RATE,    // L/min              — coolant/fluid throughput
    HUMIDITY      // % relative humidity — corrosion risk monitoring
};

// Converts enum value to its string name — used in JSON API responses
inline std::string sensor_type_to_string(SensorType type) {
    switch (type) {
        case SensorType::TEMPERATURE: return "TEMPERATURE";
        case SensorType::PRESSURE:    return "PRESSURE";
        case SensorType::VIBRATION:   return "VIBRATION";
        case SensorType::FLOW_RATE:   return "FLOW_RATE";
        case SensorType::HUMIDITY:    return "HUMIDITY";
        default:                      return "UNKNOWN";
    }
}

// Returns the physical unit label for each sensor type
inline std::string sensor_type_to_unit(SensorType type) {
    switch (type) {
        case SensorType::TEMPERATURE: return "degC";
        case SensorType::PRESSURE:    return "bar";
        case SensorType::VIBRATION:   return "mm/s";
        case SensorType::FLOW_RATE:   return "L/min";
        case SensorType::HUMIDITY:    return "%RH";
        default:                      return "";
    }
}

// A single sensor reading — the atomic unit of data flowing through the pipeline
// Kept small deliberately: fits in two cache lines (128 bytes) on x86_64
struct SensorReading {
    std::string sensor_id;    // e.g. "sensor_001"
    SensorType  type;         // what physical quantity this measures
    double      value;        // the measured value in the unit above
    int64_t     timestamp_ns; // nanoseconds since steady_clock epoch

    // Factory function — always stamps the reading at the exact moment of creation
    // Using steady_clock: monotonically increasing, never jumps backward (unlike system_clock)
    // This guarantees readings are always in chronological order in the ring buffer
    [[nodiscard]]
    static SensorReading create(std::string id, SensorType t, double v) {
        auto now = std::chrono::steady_clock::now();
        auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       now.time_since_epoch()).count();
        return SensorReading{std::move(id), t, v, ns};
    }
};

} // namespace sensorstream
