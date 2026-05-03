#include "data_store.h"
#include "sensor_data.h"
#include "statistics.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

using namespace sensorstream;

// Helper — creates a SensorReading with a specific value
static SensorReading make_reading(const std::string& id,
                                   SensorType type,
                                   double value)
{
    return SensorReading::create(id, type, value);
}

// ── Basic recording and retrieval ──────────────────────────────────────────

TEST(DataStoreTest, EmptyOnConstruction) {
    DataStore store;
    EXPECT_TRUE(store.sensor_ids().empty());
    EXPECT_EQ(store.total_readings(), 0u);
}

TEST(DataStoreTest, RecordCreatesNewSensor) {
    DataStore store;
    auto r = make_reading("sensor_001", SensorType::TEMPERATURE, 85.0);
    store.record(r, 85.0, 0.0, 0.0);

    auto ids = store.sensor_ids();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], "sensor_001");
    EXPECT_EQ(store.total_readings(), 1u);
}

TEST(DataStoreTest, LatestReturnsNulloptForUnknownSensor) {
    DataStore store;
    auto result = store.latest("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST(DataStoreTest, LatestReturnsCorrectReading) {
    DataStore store;
    auto r1 = make_reading("sensor_001", SensorType::PRESSURE, 4.5);
    auto r2 = make_reading("sensor_001", SensorType::PRESSURE, 4.8);
    store.record(r1, 4.5, 0.0, 0.0);
    store.record(r2, 4.8, 0.1, 0.5);

    auto latest = store.latest("sensor_001");
    ASSERT_TRUE(latest.has_value());
    EXPECT_DOUBLE_EQ(latest->value, 4.8);
}

TEST(DataStoreTest, MultipleSensorsTrackedIndependently) {
    DataStore store;
    store.record(make_reading("sensor_001", SensorType::TEMPERATURE, 85.0),
                 85.0, 0.0, 0.0);
    store.record(make_reading("sensor_002", SensorType::PRESSURE, 4.5),
                 4.5,  0.0, 0.0);
    store.record(make_reading("sensor_003", SensorType::VIBRATION, 2.8),
                 2.8,  0.0, 0.0);

    EXPECT_EQ(store.sensor_ids().size(), 3u);

    auto t = store.latest("sensor_001");
    auto p = store.latest("sensor_002");
    auto v = store.latest("sensor_003");

    ASSERT_TRUE(t.has_value()); EXPECT_DOUBLE_EQ(t->value, 85.0);
    ASSERT_TRUE(p.has_value()); EXPECT_DOUBLE_EQ(p->value, 4.5);
    ASSERT_TRUE(v.has_value()); EXPECT_DOUBLE_EQ(v->value, 2.8);
}

// ── History retrieval ──────────────────────────────────────────────────────

TEST(DataStoreTest, HistoryReturnsEmptyForUnknownSensor) {
    DataStore store;
    auto hist = store.history("nonexistent", 10);
    EXPECT_TRUE(hist.empty());
}

TEST(DataStoreTest, HistoryReturnsAllWhenUnderLimit) {
    DataStore store;
    for (int i = 0; i < 5; ++i) {
        store.record(
            make_reading("sensor_001", SensorType::TEMPERATURE,
                         static_cast<double>(i)),
            0.0, 0.0, 0.0);
    }
    auto hist = store.history("sensor_001", 100);
    EXPECT_EQ(hist.size(), 5u);
}

TEST(DataStoreTest, HistoryRespectsLimit) {
    DataStore store;
    for (int i = 0; i < 20; ++i) {
        store.record(
            make_reading("sensor_001", SensorType::TEMPERATURE,
                         static_cast<double>(i)),
            0.0, 0.0, 0.0);
    }
    auto hist = store.history("sensor_001", 5);
    ASSERT_EQ(hist.size(), 5u);
    // Should be the 5 most recent — values 15, 16, 17, 18, 19
    EXPECT_DOUBLE_EQ(hist[0].value, 15.0);
    EXPECT_DOUBLE_EQ(hist[4].value, 19.0);
}

TEST(DataStoreTest, HistoryBoundedAtMaxCapacity) {
    DataStore store;
    // Insert MAX_HISTORY + 50 readings — oldest must be evicted
    const std::size_t over = MAX_HISTORY + 50;
    for (std::size_t i = 0; i < over; ++i) {
        store.record(
            make_reading("sensor_001", SensorType::TEMPERATURE,
                         static_cast<double>(i)),
            0.0, 0.0, 0.0);
    }
    auto hist = store.history("sensor_001", MAX_HISTORY + 100);
    // History must never exceed MAX_HISTORY
    EXPECT_LE(hist.size(), MAX_HISTORY);
    // Most recent reading must be the last inserted value
    EXPECT_DOUBLE_EQ(hist.back().value, static_cast<double>(over - 1));
}

// ── Anomaly logging ────────────────────────────────────────────────────────

TEST(DataStoreTest, NoAnomaliesInitially) {
    DataStore store;
    EXPECT_TRUE(store.recent_anomalies(10).empty());
}

TEST(DataStoreTest, AnomalyRecordedWhenZScoreExceedsThreshold) {
    DataStore store;
    // mean=5.0, stddev=1.0, value=9.0 → Z=4.0 → anomaly
    auto r = make_reading("sensor_001", SensorType::VIBRATION, 9.0);
    store.record(r, 5.0, 1.0, 4.0);

    auto anomalies = store.recent_anomalies(10);
    ASSERT_EQ(anomalies.size(), 1u);
    EXPECT_DOUBLE_EQ(anomalies[0].reading.value, 9.0);
    EXPECT_DOUBLE_EQ(anomalies[0].z_score, 4.0);
    EXPECT_DOUBLE_EQ(anomalies[0].mean_at_detection,   5.0);
    EXPECT_DOUBLE_EQ(anomalies[0].stddev_at_detection, 1.0);
}

TEST(DataStoreTest, NormalReadingNotLoggedAsAnomaly) {
    DataStore store;
    // Z = (6.0 - 5.0) / 1.0 = 1.0 — well within 3-sigma, not an anomaly
    auto r = make_reading("sensor_001", SensorType::TEMPERATURE, 6.0);
    store.record(r, 5.0, 1.0, 1.0);
    EXPECT_TRUE(store.recent_anomalies(10).empty());
}

TEST(DataStoreTest, AnomalyLogBoundedAtMaxAnomalies) {
    DataStore store;
    // Insert MAX_ANOMALIES + 20 anomalies — oldest must be evicted
    const std::size_t over = MAX_ANOMALIES + 20;
    for (std::size_t i = 0; i < over; ++i) {
        auto r = make_reading("sensor_001", SensorType::TEMPERATURE,
                              static_cast<double>(i) + 100.0);
        store.record(r, 0.0, 1.0, 10.0); // Z=10 — always anomaly
    }
    auto anomalies = store.recent_anomalies(MAX_ANOMALIES + 100);
    EXPECT_LE(anomalies.size(), MAX_ANOMALIES);
}

// ── Thread safety ──────────────────────────────────────────────────────────

TEST(DataStoreTest, ConcurrentWritesDoNotCrash) {
    // Two threads writing to the same DataStore concurrently
    // This is not the intended usage pattern (single writer in production)
    // but the mutex must prevent data corruption regardless
    DataStore store;
    constexpr int N = 500;

    std::thread t1([&]() {
        for (int i = 0; i < N; ++i) {
            store.record(
                make_reading("sensor_001", SensorType::TEMPERATURE,
                             static_cast<double>(i)),
                50.0, 1.0, 0.5);
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < N; ++i) {
            store.record(
                make_reading("sensor_002", SensorType::PRESSURE,
                             static_cast<double>(i)),
                4.5, 0.1, 0.5);
        }
    });

    t1.join();
    t2.join();

    // Both sensors must be present and total must be 2*N
    EXPECT_EQ(store.total_readings(), static_cast<uint64_t>(2 * N));
    EXPECT_TRUE(store.latest("sensor_001").has_value());
    EXPECT_TRUE(store.latest("sensor_002").has_value());
}
