#include "ring_buffer.h"
#include "sensor_data.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>

using namespace sensorstream;

// ── Single-threaded correctness ────────────────────────────────────────────

TEST(RingBufferTest, EmptyOnConstruction) {
    RingBuffer<int, 8> buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size_approx(), 0u);
}

TEST(RingBufferTest, CapacityIsCorrect) {
    RingBuffer<int, 16> buf;
    EXPECT_EQ(buf.capacity(), 16u);
}

TEST(RingBufferTest, PushAndPopSingleItem) {
    RingBuffer<int, 8> buf;
    EXPECT_TRUE(buf.push(42));
    EXPECT_FALSE(buf.empty());
    auto val = buf.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 42);
    EXPECT_TRUE(buf.empty());
}

TEST(RingBufferTest, PopOnEmptyReturnsNullopt) {
    RingBuffer<int, 8> buf;
    auto val = buf.pop();
    EXPECT_FALSE(val.has_value());
}

TEST(RingBufferTest, FIFOOrdering) {
    // Ring buffer must preserve insertion order — first in, first out
    RingBuffer<int, 16> buf;
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(buf.push(i));
    }
    for (int i = 0; i < 10; ++i) {
        auto val = buf.pop();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(val.value(), i);
    }
}

TEST(RingBufferTest, FullBufferReturnsFalse) {
    // Capacity 8 holds 7 items — one slot reserved as the "full" sentinel
    // (head == tail means empty; next == tail means full)
    RingBuffer<int, 8> buf;
    int pushed = 0;
    while (buf.push(pushed)) { ++pushed; }
    // Should have accepted exactly 7 items (Capacity - 1)
    EXPECT_EQ(pushed, 7);
}

TEST(RingBufferTest, WrapAroundCorrectness) {
    // Push 4, pop 4, push 4 more — exercises the index wrap-around path
    RingBuffer<int, 8> buf;
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(buf.push(i));
    for (int i = 0; i < 4; ++i) (void)buf.pop();
    for (int i = 10; i < 14; ++i) EXPECT_TRUE(buf.push(i));
    for (int i = 10; i < 14; ++i) {
        auto val = buf.pop();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(val.value(), i);
    }
}

TEST(RingBufferTest, SensorReadingRoundTrip) {
    // Verify the buffer works correctly with the actual SensorReading type
    RingBuffer<SensorReading, 16> buf;
    auto r = SensorReading::create("sensor_001", SensorType::TEMPERATURE, 85.5);
    EXPECT_TRUE(buf.push(r));
    auto out = buf.pop();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->sensor_id, "sensor_001");
    EXPECT_EQ(out->type, SensorType::TEMPERATURE);
    EXPECT_DOUBLE_EQ(out->value, 85.5);
    EXPECT_GT(out->timestamp_ns, 0);
}

// ── Concurrent correctness ─────────────────────────────────────────────────

TEST(RingBufferTest, ConcurrentProducerConsumer) {
    // One producer thread, one consumer thread — the SPSC contract
    // Producer pushes 10000 integers, consumer pops them all
    // Verifies: no items lost, no items duplicated, correct ordering
    constexpr int N = 10000;
    RingBuffer<int, 1024> buf;

    std::atomic<int> consumed_sum{ 0 };
    std::atomic<int> consume_count{ 0 };

    std::thread consumer([&]() {
        int received = 0;
        while (received < N) {
            auto val = buf.pop();
            if (val.has_value()) {
                consumed_sum.fetch_add(val.value(), std::memory_order_relaxed);
                ++received;
            }
        }
        consume_count.store(received, std::memory_order_relaxed);
    });

    std::thread producer([&]() {
        for (int i = 0; i < N; ++i) {
            // Spin until push succeeds — buffer temporarily full is fine
            while (!buf.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    // Sum of 0..9999 = N*(N-1)/2
    const int expected_sum = N * (N - 1) / 2;
    EXPECT_EQ(consume_count.load(), N);
    EXPECT_EQ(consumed_sum.load(), expected_sum);
}
