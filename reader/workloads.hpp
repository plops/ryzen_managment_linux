// src/workloads.hpp
#pragma once

#include <cstdint>
#include <thread>
#include <pthread.h> // For Linux-specific thread affinity

// Simple helper to pin the calling thread to a specific CPU core
inline bool set_thread_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) == 0;
}

// The workload to be executed by the worker thread.
// Designed to saturate integer execution units.
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