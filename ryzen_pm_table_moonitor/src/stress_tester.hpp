#pragma once

#include <vector>
#include <thread>
#include <atomic>
#include <vector>
#include <spdlog/spdlog.h>
#include <chrono>

#ifdef __linux__
#include <pthread.h>
#endif

// Helper to pin a thread to a specific CPU core i want the threads to apply computational stress with different periods (so that the stress can occur simultaneously but can be separated using fft or something like that)
inline bool set_thread_affinity(std::thread& thread, int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int rc = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        spdlog::error("Error calling pthread_setaffinity_np: {}", rc);
        return false;
    }
    return true;
#else
    spdlog::warn("Thread affinity is only supported on Linux.");
    return false;
#endif
}

// A StressTester class to manage the per-core stress threads.
//
class StressTester {
public:
    StressTester() {
        num_cores_ = std::thread::hardware_concurrency()/2;
        // Generate a list of prime numbers for unique periods to minimize harmonic overlap
        periods_ms_ = generate_prime_periods(num_cores_);
    }

    ~StressTester() {
        stop();
    }

    void start() {
        if (is_running_) {
            return;
        }
        spdlog::info("Starting stress threads on {} cores...", num_cores_);
        is_running_ = true;
        stop_flags_.resize(num_cores_);
        threads_.resize(num_cores_);

        for (int i = 0; i < num_cores_; ++i) {
            stop_flags_[i] = std::make_unique<std::atomic<bool>>(false);
            threads_[i] = std::thread(&StressTester::stress_worker, this, i, periods_ms_[i], std::ref(*stop_flags_[i]));
            set_thread_affinity(threads_[i], i*2);
            spdlog::info("  - Core {} started with period {}ms", i, periods_ms_[i].count());
        }
    }

    void stop() {
        if (!is_running_) {
            return;
        }
        spdlog::info("Stopping stress threads...");
        for (int i = 0; i < num_cores_; ++i) {
            if(stop_flags_[i]) {
                stop_flags_[i]->store(true);
            }
        }
        for (int i = 0; i < num_cores_; ++i) {
            if (threads_[i].joinable()) {
                threads_[i].join();
            }
        }
        threads_.clear();
        stop_flags_.clear();
        is_running_ = false;
        spdlog::info("All stress threads stopped.");
    }

    bool is_running() const { return is_running_; }
    const std::vector<std::chrono::milliseconds>& get_periods() const { return periods_ms_; }
    unsigned int get_core_count() const { return num_cores_; }


private:
    void stress_worker(int core_id, std::chrono::milliseconds period, const std::atomic<bool>& stop_flag) {
        // Work for ~30% of the period
        const auto work_duration = period / 3;

        while (!stop_flag) {
            auto loop_start = std::chrono::steady_clock::now();
            auto work_end = loop_start + work_duration;
            auto loop_end = loop_start + period;

            // --- Computational Stress ---
            while (std::chrono::steady_clock::now() < work_end) {
                // Volatile to prevent compiler from optimizing the loop away
                volatile double val = 1.2345;
                for(int i=0; i<500; ++i) {
                    val *= 1.00001;
                    val /= 1.000009;
                }
            }
            // --- End Stress ---

            // Sleep for the rest of the period
            if (std::chrono::steady_clock::now() < loop_end) {
                std::this_thread::sleep_until(loop_end);
            }
        }
    }

    std::vector<std::chrono::milliseconds> generate_prime_periods(unsigned int n) {
        std::vector<std::chrono::milliseconds> primes;
        int num = 11; // Start from a prime number
        while (primes.size() < n) {
            bool is_prime = true;
            for (int i = 2; i * i <= num; ++i) {
                if (num % i == 0) {
                    is_prime = false;
                    break;
                }
            }
            if (is_prime) {
                primes.push_back(std::chrono::milliseconds(num));
            }
            num += 2; // Check odd numbers
        }
        return primes;
    }

    unsigned int num_cores_;
    std::atomic<bool> is_running_{false};
    std::vector<std::thread> threads_;
    std::vector<std::unique_ptr<std::atomic<bool>>> stop_flags_;
    std::vector<std::chrono::milliseconds> periods_ms_;
};