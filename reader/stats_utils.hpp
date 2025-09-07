#pragma once

#include <algorithm>
#include <numeric>
#include <span>
#include <vector>

/**
 * @brief Calculate a trimmed mean (robust average).
 *
 * Sorts a copy of the input data and removes trim_percentage% of samples
 * from each side before averaging the remainder. Falls back to median if
 * too few samples remain after trimming.
 *
 * @param data A span over the sample data. A copy is made for sorting.
 * @param trim_percentage Percentage of samples to remove from each tail
 * (0..50).
 * @return Trimmed mean or median if trimming removed too many elements.
 */
static inline float calculate_trimmed_mean(std::span<const float> data,
                                           float trim_percentage) {
  if (data.empty())
    return 0.0f;

  // Copy the data from the span to a vector for sorting
  std::vector<float> sorted(data.begin(), data.end());
  std::ranges::sort(sorted);
  const size_t n = sorted.size();

  if (n == 0)
    return 0.0f;

  const size_t trim_count = static_cast<size_t>((trim_percentage / 100.0f) * n);
  if (2 * trim_count >= n) {
    // Not enough elements after trimming; return median as fallback
    if (n % 2 == 0) {
      return (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0f;
    }
    return sorted[n / 2];
  }
  const auto first = sorted.begin() + trim_count;
  const auto last = sorted.end() - trim_count;
  const double sum = std::accumulate(first, last, 0.0);
  const size_t count = std::distance(first, last);
  return static_cast<float>(sum / (count ? count : 1));
}