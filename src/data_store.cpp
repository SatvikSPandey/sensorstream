#include "data_store.h"
#include "statistics.h"

#include <algorithm>

namespace sensorstream {

DataStore::DataStore() = default;

void DataStore::record(const SensorReading& reading,
                       double mean, double stddev, double z_scr)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto& state = sensors_[reading.sensor_id];

    // Update Welford stats with the new value
    state.stats.update(reading.value);
    state.last_value        = reading.value;
    state.last_timestamp_ns = reading.timestamp_ns;

    // Append to bounded history — evict oldest if at capacity
    state.history.push_back(reading);
    if (state.history.size() > MAX_HISTORY) {
        state.history.pop_front();
    }

    // Record anomaly if threshold exceeded
    if (is_anomaly(reading.value, mean, stddev)) {
        AnomalyEvent event{ reading, z_scr, mean, stddev };
        anomalies_.push_back(std::move(event));
        if (anomalies_.size() > MAX_ANOMALIES) {
            anomalies_.pop_front();
        }
    }

    total_readings_.fetch_add(1, std::memory_order_relaxed);
}

std::vector<std::string> DataStore::sensor_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(sensors_.size());
    for (const auto& [id, _] : sensors_) {
        ids.push_back(id);
    }
    return ids;
}

std::optional<SensorReading> DataStore::latest(
    const std::string& sensor_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sensors_.find(sensor_id);
    if (it == sensors_.end() || it->second.history.empty()) {
        return std::nullopt;
    }
    return it->second.history.back(); // copy — lock released after return
}

std::optional<std::pair<double,double>> DataStore::latest_stats(
    const std::string& sensor_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sensors_.find(sensor_id);
    if (it == sensors_.end()) return std::nullopt;
    const auto& [mean, stddev] = it->second.stats.summary();
    return std::make_pair(mean, stddev);
}

std::vector<SensorReading> DataStore::history(
    const std::string& sensor_id, std::size_t limit) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sensors_.find(sensor_id);
    if (it == sensors_.end()) return {};

    const auto& hist = it->second.history;
    std::size_t count = std::min(limit, hist.size());

    // Return the 'count' most recent readings
    return std::vector<SensorReading>(hist.end() - count, hist.end());
}

std::vector<AnomalyEvent> DataStore::recent_anomalies(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = std::min(limit, anomalies_.size());
    return std::vector<AnomalyEvent>(anomalies_.end() - count, anomalies_.end());
}

uint64_t DataStore::total_readings() const noexcept {
    return total_readings_.load(std::memory_order_relaxed);
}

} // namespace sensorstream
