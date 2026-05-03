#pragma once

#include <cmath>
#include <cstdint>
#include <tuple>

namespace sensorstream {

// Welford's online algorithm for computing running mean and variance
// in a single pass with O(1) time and O(1) space per sample.
//
// Why not the naive approach (sum / count)?
//   Naive: accumulates a large sum, then divides. With floating-point,
//   large sums lose low-order bits — catastrophic cancellation when
//   mean is large and variance is small (e.g. temperature ~200°C, variance ~0.01).
//   Welford: updates mean incrementally, variance stays numerically stable
//   regardless of the magnitude of the values.
//
class WelfordStats {
public:
    // Feed one new sample into the running statistics
    void update(double value) noexcept {
        ++count_;
        // delta1: distance from old mean — used to update mean
        const double delta1 = value - mean_;
        mean_ += delta1 / static_cast<double>(count_);
        // delta2: distance from NEW mean — combined with delta1 for M2
        // M2 accumulates the sum of squared deviations from the running mean
        const double delta2 = value - mean_;
        m2_ += delta1 * delta2;
    }

    // Population standard deviation (divides by N)
    // Used here because we treat all readings as the full population
    // of observations for this sensor — not a sample from a larger set
    [[nodiscard]]
    double stddev() const noexcept {
        if (count_ < 2) return 0.0;
        return std::sqrt(m2_ / static_cast<double>(count_));
    }

    [[nodiscard]] double mean()     const noexcept { return mean_; }
    [[nodiscard]] int64_t count()   const noexcept { return count_; }

    // Returns {mean, stddev} as a pair for convenient structured binding:
    //   auto [m, s] = stats.summary();
    [[nodiscard]]
    std::tuple<double, double> summary() const noexcept {
        return {mean_, stddev()};
    }

    void reset() noexcept { count_ = 0; mean_ = 0.0; m2_ = 0.0; }

private:
    int64_t count_{ 0 };
    double  mean_ { 0.0 };
    double  m2_   { 0.0 }; // sum of squared deviations from the running mean
};

// ── Anomaly detection ──────────────────────────────────────────────────────

// Returns the Z-score of a value given mean and standard deviation.
// Z-score = how many standard deviations the value is from the mean.
// |Z| > 3.0 → statistically unusual for a normal distribution (~0.3% probability)
[[nodiscard]]
inline double z_score(double value, double mean, double stddev) noexcept {
    if (stddev < 1e-10) return 0.0; // avoid division by near-zero
    return (value - mean) / stddev;
}

// Threshold for anomaly classification — 3-sigma rule
// Covers 99.73% of normally distributed readings as "normal"
// Anything beyond ±3σ is flagged as an anomaly
inline constexpr double ANOMALY_THRESHOLD = 3.0;

[[nodiscard]]
inline bool is_anomaly(double value, double mean, double stddev) noexcept {
    return std::abs(z_score(value, mean, stddev)) > ANOMALY_THRESHOLD;
}

} // namespace sensorstream
