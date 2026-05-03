#pragma once

#include <atomic>
#include <array>
#include <optional>
#include <cstddef>

namespace sensorstream {

// Lock-free Single-Producer Single-Consumer (SPSC) ring buffer
//
// Design constraints (enforced by the type system):
//   - Exactly ONE thread calls push()  — the sensor simulator
//   - Exactly ONE thread calls pop()   — the processing engine
//   - No mutex, no condition variable, no heap allocation
//   - Capacity must be a power of 2 — enables bitmask indexing
//
// Memory ordering:
//   - head_ (write index) is written by producer, read by consumer
//   - tail_ (read  index) is written by consumer, read by producer
//   - acquire/release pairs form a happens-before relationship:
//     the producer's store(release) synchronises with the consumer's load(acquire)
//     guaranteeing the consumer sees fully-written slot data
//
template<typename T, std::size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
        "RingBuffer capacity must be a power of 2");
    static_assert(Capacity >= 2,
        "RingBuffer capacity must be at least 2");

public:
    RingBuffer() : head_(0), tail_(0) {}

    // Producer calls this — returns false if buffer is full (back-pressure signal)
    // Never blocks, never allocates
    [[nodiscard]]
    bool push(T item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask_;

        // Buffer full — consumer hasn't caught up yet
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[head] = std::move(item);

        // Release: makes the written slot visible to the consumer
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer calls this — returns empty optional if buffer is empty
    // Never blocks, never allocates
    [[nodiscard]]
    std::optional<T> pop() noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);

        // Buffer empty — producer hasn't written anything new
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        T item = std::move(buffer_[tail]);

        // Release: signals producer that this slot is now free
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return item;
    }

    // Approximate size — not exact under concurrent access, safe for monitoring
    [[nodiscard]]
    std::size_t size_approx() const noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        return (head - tail) & mask_;
    }

    [[nodiscard]]
    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) ==
               tail_.load(std::memory_order_relaxed);
    }

    [[nodiscard]]
    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t mask_ = Capacity - 1;

    // Padding prevents false sharing: head_ and tail_ on separate cache lines
    // Without padding, writing head_ invalidates the cache line containing tail_
    // on the OTHER core — causing a hidden performance cliff under load
    alignas(64) std::atomic<std::size_t> head_;
    alignas(64) std::atomic<std::size_t> tail_;

    std::array<T, Capacity> buffer_;
};

} // namespace sensorstream
