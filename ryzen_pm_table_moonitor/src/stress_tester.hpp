#pragma once

#include <vector>
#include <thread>
#include <atomic>
#include <spdlog/spdlog.h>
#include <chrono>

#ifdef __linux__
#include <pthread.h>
#endif

// Helper to pin a thread to a specific CPU core
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

// Manages per-core stress threads.
class StressTester {
public:
    StressTester() {
        num_cores_ = std::thread::hardware_concurrency();
        periods_ms_ = generate_prime_periods(num_cores_);
        // This vector holds the state of whether a thread *should* be busy.
        // It's used by the GUI and persists even when threads are stopped and started.
        thread_busy_states_.resize(num_cores_, true);
    }

    ~StressTester() {
        stop();
    }

    void start() {
        if (is_running_) {
            return;
        }
        spdlog::info("Starting stress threads on {} cores...", num_cores_);
        start_time_ = std::chrono::steady_clock::now(); // Record start time
        is_running_ = true;
        stop_flags_.resize(num_cores_);
        threads_.resize(num_cores_);
        thread_is_actually_busy_flags_.resize(num_cores_); // The live atomic flags for the workers

        for (int i = 0; i < num_cores_; ++i) {
            stop_flags_[i] = std::make_unique<std::atomic<bool>>(false);
            // The live flag is initialized from the persistent state vector
            thread_is_actually_busy_flags_[i] = std::make_unique<std::atomic<bool>>(thread_busy_states_[i]);
            // Pass the new "is busy" flag to the worker
            threads_[i] = std::thread(&StressTester::stress_worker, this, i, periods_ms_[i], std::ref(*stop_flags_[i]), std::ref(*thread_is_actually_busy_flags_[i]));
            set_thread_affinity(threads_[i], i);
            spdlog::info("  - Core {} started with period {}ms", i, periods_ms_[i].count());
        }
    }

    void stop() {
        if (!is_running_) {
            return;
        }
        spdlog::info("Stopping stress threads...");
        for (int i = 0; i < num_cores_; ++i) {
            if (stop_flags_[i]) {
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
        thread_is_actually_busy_flags_.clear(); // Clear the live flags
        is_running_ = false;
        spdlog::info("All stress threads stopped.");
    }

    // Method for the GUI to change the busy state of a thread.
    void set_thread_busy_state(int core_id, bool is_busy) {
        if (core_id >= 0 && core_id < num_cores_) {
            // Update the persistent state
            thread_busy_states_[core_id] = is_busy;
            // If the threads are running, update the live atomic flag as well.
            if (is_running_ && thread_is_actually_busy_flags_[core_id]) {
                thread_is_actually_busy_flags_[core_id]->store(is_busy);
            }
        }
    }

    [[nodiscard]] bool is_running() const { return is_running_; }
    [[nodiscard]] const std::vector<std::chrono::milliseconds>& get_periods() const { return periods_ms_; }
    [[nodiscard]] unsigned int get_core_count() const { return num_cores_; }
    [[nodiscard]] std::chrono::steady_clock::time_point get_start_time() const { return start_time_; }
    // Getter for the GUI to read the persistent state for its checkboxes.
    [[nodiscard]] bool get_thread_busy_state(int core_id) const {
        if (core_id >= 0 && core_id < num_cores_) {
            return thread_busy_states_[core_id];
        }
        return false;
    }


private:
    // The worker function now accepts an additional atomic flag.
    void stress_worker(int core_id, std::chrono::milliseconds period, const std::atomic<bool>& stop_flag, const std::atomic<bool>& is_busy_flag) {
        const auto work_duration = period / 3;
        while (!stop_flag) {
            auto loop_start = std::chrono::steady_clock::now();
            auto work_end = loop_start + work_duration;
            auto loop_end = loop_start + period;

            // If this thread is supposed to be busy, do the work.
            if (is_busy_flag.load(std::memory_order_relaxed)) {
                while (std::chrono::steady_clock::now() < work_end) {
                    volatile double val = 1.2345;
                    for (int i = 0; i < 500; ++i) {
                        val *= 1.00001;
                        val /= 1.000009;
                    }
                }
            }

            // In both cases (busy or idle), sleep until the end of the period.
            if (std::chrono::steady_clock::now() < loop_end) {
                std::this_thread::sleep_until(loop_end);
            }
        }
    }

    std::vector<std::chrono::milliseconds> generate_prime_periods(unsigned int n) {
        std::vector<std::chrono::milliseconds> primes;
        int num = 11;
        while (primes.size() < n) {
            bool is_prime = true;
            for (int i = 2; i * i <= num; ++i) {
                if (num % i == 0) {
                    is_prime = false;
                    break;
                }
            }
            if (is_prime) {
                primes.push_back(std::chrono::milliseconds(num*2));
            }
            num += 2;
        }
        return primes;
    }

    unsigned int num_cores_;
    std::atomic<bool> is_running_{false};
    std::vector<std::thread> threads_;
    std::vector<std::unique_ptr<std::atomic<bool>>> stop_flags_;
    // The "live" atomic flags that the worker threads actually check.
    std::vector<std::unique_ptr<std::atomic<bool>>> thread_is_actually_busy_flags_;
    // The persistent state that the GUI interacts with. This is kept so that if you stop
    // and start the stress tester, it remembers which threads were disabled.
    std::vector<bool> thread_busy_states_;
    std::vector<std::chrono::milliseconds> periods_ms_;
    std::chrono::steady_clock::time_point start_time_;
};