#include "statistics.h"

#include <gtest/gtest.h>
#include <cmath>
#include <vector>

using namespace sensorstream;

// ── WelfordStats ───────────────────────────────────────────────────────────

TEST(WelfordStatsTest, InitialStateIsZero) {
    WelfordStats s;
    EXPECT_EQ(s.count(), 0);
    EXPECT_DOUBLE_EQ(s.mean(), 0.0);
    EXPECT_DOUBLE_EQ(s.stddev(), 0.0);
}

TEST(WelfordStatsTest, SingleSampleMean) {
    WelfordStats s;
    s.update(42.0);
    EXPECT_EQ(s.count(), 1);
    EXPECT_DOUBLE_EQ(s.mean(), 42.0);
    // stddev of one sample is defined as 0
    EXPECT_DOUBLE_EQ(s.stddev(), 0.0);
}

TEST(WelfordStatsTest, TwoSampleMeanAndStddev) {
    WelfordStats s;
    s.update(10.0);
    s.update(20.0);
    EXPECT_DOUBLE_EQ(s.mean(), 15.0);
    // Population stddev of {10, 20}: variance = ((10-15)^2 + (20-15)^2) / 2 = 25
    // stddev = 5.0
    EXPECT_DOUBLE_EQ(s.stddev(), 5.0);
}

TEST(WelfordStatsTest, MeanMatchesNaiveSum) {
    // For a set of known values, Welford mean must match naive sum/count
    WelfordStats s;
    std::vector<double> values = {1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 7.7, 8.8, 9.9};
    double naive_sum = 0.0;
    for (double v : values) {
        s.update(v);
        naive_sum += v;
    }
    double naive_mean = naive_sum / static_cast<double>(values.size());
    EXPECT_NEAR(s.mean(), naive_mean, 1e-10);
}

TEST(WelfordStatsTest, NumericalStabilityLargeValues) {
    // Naive sum/count fails here: values cluster tightly around a large base
    // Welford must remain stable — stddev should be ~1.0, not corrupted by
    // catastrophic cancellation
    WelfordStats s;
    // 1000 values clustered around 1,000,000 with ±1 noise
    // Naive approach: sum ≈ 1e9, loses precision in the noise digits
    const double base = 1'000'000.0;
    const std::vector<double> offsets = {
        -1.0, +1.0, -1.0, +1.0, -1.0,
        +1.0, -1.0, +1.0, -1.0, +1.0
    };
    for (double o : offsets) s.update(base + o);

    EXPECT_NEAR(s.mean(), base, 1e-6);
    // Population stddev of alternating ±1 around mean = 1.0
    EXPECT_NEAR(s.stddev(), 1.0, 1e-6);
}

TEST(WelfordStatsTest, SummaryReturnsBothValues) {
    WelfordStats s;
    s.update(3.0);
    s.update(7.0);
    auto [mean, stddev] = s.summary();
    EXPECT_DOUBLE_EQ(mean,   5.0);
    EXPECT_DOUBLE_EQ(stddev, 2.0);
}

TEST(WelfordStatsTest, ResetClearsAllState) {
    WelfordStats s;
    s.update(100.0);
    s.update(200.0);
    s.reset();
    EXPECT_EQ(s.count(), 0);
    EXPECT_DOUBLE_EQ(s.mean(),   0.0);
    EXPECT_DOUBLE_EQ(s.stddev(), 0.0);
}

TEST(WelfordStatsTest, IncrementalMatchesBatchResult) {
    // Adding samples one at a time must give the same result as
    // computing mean and stddev over the full batch at once
    WelfordStats s;
    const std::vector<double> data = {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
    for (double v : data) s.update(v);

    // Known values for this dataset (population):
    // mean = 5.0,  stddev = sqrt(4.0) = 2.0
    EXPECT_DOUBLE_EQ(s.mean(), 5.0);
    EXPECT_NEAR(s.stddev(), 2.0, 1e-10);
}

// ── Z-score and anomaly detection ─────────────────────────────────────────

TEST(ZScoreTest, ZeroWhenAtMean) {
    EXPECT_DOUBLE_EQ(z_score(5.0, 5.0, 1.0), 0.0);
}

TEST(ZScoreTest, PositiveAboveMean) {
    // Value 2 stddevs above mean → Z = +2.0
    EXPECT_DOUBLE_EQ(z_score(7.0, 5.0, 1.0), 2.0);
}

TEST(ZScoreTest, NegativeBelowMean) {
    EXPECT_DOUBLE_EQ(z_score(3.0, 5.0, 1.0), -2.0);
}

TEST(ZScoreTest, NearZeroStddevReturnsZero) {
    // Guards against division by near-zero stddev
    EXPECT_DOUBLE_EQ(z_score(100.0, 100.0, 0.0),    0.0);
    EXPECT_DOUBLE_EQ(z_score(100.0, 100.0, 1e-11),   0.0);
}

TEST(AnomalyTest, NormalReadingNotFlagged) {
    // Value within 3 sigma — should not be an anomaly
    EXPECT_FALSE(is_anomaly(5.0, 5.0, 1.0));  // Z = 0
    EXPECT_FALSE(is_anomaly(7.9, 5.0, 1.0));  // Z = 2.9 — just inside threshold
}

TEST(AnomalyTest, ExactThresholdNotFlagged) {
    // Z = exactly 3.0 — threshold is strictly greater than, so this is NOT anomaly
    EXPECT_FALSE(is_anomaly(8.0, 5.0, 1.0));  // Z = 3.0 exactly
}

TEST(AnomalyTest, BeyondThresholdFlagged) {
    EXPECT_TRUE(is_anomaly(8.1, 5.0, 1.0));   // Z = 3.1 — anomaly
    EXPECT_TRUE(is_anomaly(1.9, 5.0, 1.0));   // Z = -3.1 — anomaly (negative side)
}

TEST(AnomalyTest, SpikeWellBeyondThreshold) {
    // A large spike — Z >> 3 — must always be flagged
    EXPECT_TRUE(is_anomaly(50.0, 5.0, 1.0));  // Z = 45.0
    EXPECT_TRUE(is_anomaly(-40.0, 5.0, 1.0)); // Z = -45.0
}
