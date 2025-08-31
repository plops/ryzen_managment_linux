/** @file workloads.hpp
 *  @brief Lightweight helpers for setting thread affinity and running a CPU
 * workload.
 */

#pragma once

#include <cstdint>
#include <pthread.h> // For Linux-specific thread affinity
#include <thread>

/**
 * @brief Pin the calling thread to a specific CPU core.
 *
 * Internally uses pthread_setaffinity_np. Returns true on success.
 *
 * @param core_id Logical CPU core id to bind to.
 * @return True if the affinity was set successfully.
 */
inline bool set_thread_affinity(int core_id) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  pthread_t current_thread = pthread_self();
  return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) ==
         0;
}

/**
 * @brief Small integer workload intended to keep integer ALUs busy.
 *
 * The function performs a tight loop using volatile variables to prevent
 * compiler optimizations that would otherwise remove the work.
 *
 * @param iterations Number of loop iterations to execute.
 */
inline void integer_alu_workload(int64_t iterations) {
  // Using volatile prevents the compiler from optimizing the loop away
  volatile int64_t a = 0, b = 1, c = 2, d = 3;
  for (int64_t i = 0; i < iterations; ++i) {
    a += i;
    b += a;
    c -= b;
    d *= c;
  }
}