#pragma once

#include <spdlog/spdlog.h>
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm> // For std::sort and std::fill
#include <limits>    // For std::numeric_limits

class JitterMonitor {
public:
    /**
     * @brief Constructs a JitterMonitor.
     * @param target_period_us The target loop period in microseconds, used as the center for jitter calculations.
     * @param report_interval The number of samples to collect before generating a report.
     * @param histogram_range_us The +/- range around the target period to capture in the histogram.
     */
    JitterMonitor(long long target_period_us, int report_interval, int histogram_range_us = 100)
        : target_period_us_(target_period_us),
          report_interval_(report_interval),
          histogram_range_us_(histogram_range_us) {
        reset();
    }

    /**
     * @brief Records a new sample period and triggers a report if the interval is reached.
     * @param period_us The measured duration of the last period in microseconds.
     */
    void record_sample(long long period_us) {
        // Store every sample to allow for accurate percentile calculations.
        all_periods_us_.push_back(period_us);

        sample_count_++;
        sum_periods_us_ += period_us;
        sum_squared_periods_us_ += static_cast<double>(period_us) * period_us;

        if (period_us < min_period_us_) min_period_us_ = period_us;
        if (period_us > max_period_us_) max_period_us_ = period_us;

        // Update histogram for visualization.
        long long jitter_us = period_us - target_period_us_;
        int bin_index = jitter_us + histogram_range_us_; // Offset to make the target the center of the vector
        if (bin_index >= 0 && bin_index < jitter_histogram_.size()) {
            jitter_histogram_[bin_index]++;
        }

        if (sample_count_ >= report_interval_) {
            report_and_reset();
        }
    }

private:
    /**
     * @brief Calculates statistics, prints a formatted report, and resets the state.
     */
    void report_and_reset() {
        if (sample_count_ == 0) {
            return;
        }

        double mean_period_us = sum_periods_us_ / sample_count_;
        // For numerical stability, ensure variance is not negative due to floating point errors.
        double variance = std::max(0.0, (sum_squared_periods_us_ / sample_count_) - (mean_period_us * mean_period_us));
        double std_dev_us = std::sqrt(variance);

        spdlog::info("--- Jitter Stats (last {} samples) ---", report_interval_);
        spdlog::info("Period Avg: {:.3f} us | StdDev: {:.3f} us", mean_period_us, std_dev_us);
        spdlog::info("Period Min: {} us | Max: {} us", min_period_us_, max_period_us_);

        // --- Percentile Calculation ---
        if (!all_periods_us_.empty()) {
            // Sort the collected data to calculate percentiles.
            std::sort(all_periods_us_.begin(), all_periods_us_.end());

            // Calculate the index for each percentile.
            auto p1_index = static_cast<size_t>(0.01 * (all_periods_us_.size() - 1));
            auto median_index = static_cast<size_t>(0.50 * (all_periods_us_.size() - 1));
            auto p99_index = static_cast<size_t>(0.99 * (all_periods_us_.size() - 1));

            spdlog::info("Percentiles: 1st: {} us | 50th (Median): {} us | 99th: {} us",
                         all_periods_us_[p1_index],
                         all_periods_us_[median_index],
                         all_periods_us_[p99_index]);
        }
        // --- End of Percentile Calculation ---

        spdlog::info("Jitter Distribution (deviation from {}us):", target_period_us_);

        // Print histogram for bins with non-zero counts.
        for (int i = 0; i < jitter_histogram_.size(); ++i) {
            if (jitter_histogram_[i] > 0) {
                long long deviation = i - histogram_range_us_;
                spdlog::info("  Jitter [{:4d} us]: {} hits", deviation, jitter_histogram_[i]);
            }
        }

        reset();
    }

    /**
     * @brief Resets all statistics to their initial state for the next reporting interval.
     */
    void reset() {
        sample_count_ = 0;
        sum_periods_us_ = 0.0;
        sum_squared_periods_us_ = 0.0;
        min_period_us_ = std::numeric_limits<long long>::max();
        max_period_us_ = 0;

        jitter_histogram_.assign(2 * histogram_range_us_, 0);

        // Clear the vector of all periods and reserve space to avoid reallocations.
        all_periods_us_.clear();
        if (report_interval_ > 0) {
            all_periods_us_.reserve(report_interval_);
        }
    }

    // Configuration
    long long target_period_us_;
    int report_interval_;
    int histogram_range_us_;

    // State
    int sample_count_;
    double sum_periods_us_;
    double sum_squared_periods_us_;
    long long min_period_us_;
    long long max_period_us_;
    std::vector<int> jitter_histogram_;
    std::vector<long long> all_periods_us_; // To store all samples for percentile calculation.
};