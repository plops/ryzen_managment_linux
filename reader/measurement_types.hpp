#pragma once
#include <vector>
#include <chrono>
#include <cstdint>

/**
 * @file measurement_types.hpp
 * @brief Fundamental data types used in the measurement harness.
 */

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
