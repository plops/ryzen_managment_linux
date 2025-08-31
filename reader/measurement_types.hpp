#pragma once
#include <chrono>
#include <cstdint>
#include <vector>

/**
 * @file measurement_types.hpp
 * @brief Fundamental data types used in the measurement harness.
 */

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
