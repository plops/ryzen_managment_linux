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
        // Pre-allocate all memory needed for the samples upfront.
        // This vector will not be resized during the sampling phase.
        all_periods_us_.resize(report_interval_);
        reset();
    }

    /**
     * @brief Records a new sample period. This function is designed to be lightweight and avoid memory allocations.
     * @param period_us The measured duration of the last period in microseconds.
     */
    void record_sample(long long period_us) {
        // Use direct index access into the pre-allocated vector instead of push_back.
        // This guarantees no memory re-allocations or moves happen in this hot path.
        if (sample_count_ < report_interval_) {
            all_periods_us_[sample_count_] = period_us;
        }

        // The rest of the operations are simple arithmetic and are very fast.
        sum_periods_us_ += period_us;
        sum_squared_periods_us_ += static_cast<double>(period_us) * period_us;

        if (period_us < min_period_us_) min_period_us_ = period_us;
        if (period_us > max_period_us_) max_period_us_ = period_us;

        long long jitter_us = period_us - target_period_us_;
        int bin_index = jitter_us + histogram_range_us_;
        if (bin_index >= 0 && bin_index < jitter_histogram_.size()) {
            jitter_histogram_[bin_index]++;
        }

        sample_count_++; // Increment after all operations for the current sample.

        if (sample_count_ >= report_interval_) {
            report_and_reset();
        }
    }

private:
    /**
     * @brief Calculates statistics, prints a formatted report, and resets the state.
     *        This is where all the heavy work (sorting, logging) is done, keeping it
     *        out of the high-frequency sampling path.
     */
    void report_and_reset() {
        if (sample_count_ == 0) {
            return;
        }

        double mean_period_us = sum_periods_us_ / sample_count_;
        double variance = std::max(0.0, (sum_squared_periods_us_ / sample_count_) - (mean_period_us * mean_period_us));
        double std_dev_us = std::sqrt(variance);

        SPDLOG_INFO("--- Jitter Stats (last {} samples) ---", sample_count_);
        SPDLOG_INFO("Period Avg: {:.3f} us | StdDev: {:.3f} us", mean_period_us, std_dev_us);
        SPDLOG_INFO("Period Min: {} us | Max: {} us", min_period_us_, max_period_us_);

        // Sort the collected data to calculate percentiles.
        // This is the most expensive operation and it's intentionally done here,
        // infrequently, rather than in the real-time path.
        std::sort(all_periods_us_.begin(), all_periods_us_.begin() + sample_count_);

        auto p1_index = static_cast<size_t>(0.01 * (sample_count_ - 1));
        auto median_index = static_cast<size_t>(0.50 * (sample_count_ - 1));
        auto p99_index = static_cast<size_t>(0.99 * (sample_count_ - 1));

        SPDLOG_INFO("Percentiles: 1st: {} us | 50th (Median): {} us | 99th: {} us",
                     all_periods_us_[p1_index],
                     all_periods_us_[median_index],
                     all_periods_us_[p99_index]);

        SPDLOG_INFO("Jitter Distribution (deviation from {}us):", target_period_us_);

        for (int i = 0; i < jitter_histogram_.size(); ++i) {
            if (jitter_histogram_[i] > 0) {
                long long deviation = i - histogram_range_us_;
                SPDLOG_INFO("  Jitter [{:4d} us]: {} hits", deviation, jitter_histogram_[i]);
            }
        }

        reset();
    }

    /**
     * @brief Resets all statistics for the next reporting interval.
     *        Does not de-allocate or re-allocate any memory for the sample vector.
     */
    void reset() {
        sample_count_ = 0;
        sum_periods_us_ = 0.0;
        sum_squared_periods_us_ = 0.0;
        min_period_us_ = std::numeric_limits<long long>::max();
        max_period_us_ = 0;

        // Histogram size is 2 * range (for +/-) + 1 for the zero-jitter bin
        jitter_histogram_.assign(2 * histogram_range_us_ + 1, 0);
    }

    // Configuration
    const long long target_period_us_;
    const int report_interval_;
    const int histogram_range_us_;

    // State
    int sample_count_;
    double sum_periods_us_;
    double sum_squared_periods_us_;
    long long min_period_us_;
    long long max_period_us_;
    std::vector<int> jitter_histogram_;

    // This vector is intentionally not resized or reallocated after construction
    // to ensure predictable performance in the `record_sample` method.
    std::vector<long long> all_periods_us_;
};