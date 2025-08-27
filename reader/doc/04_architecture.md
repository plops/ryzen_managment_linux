create an architecture for a multithreaded program, that cycles through a workload (Integer ALU) for each in each of the cores of the processor (only one core should be busy, plus the one that performs the readout of the pm_table).

global parameters shall be run period of worker core and duty cycle, how many cycles to run per core, how many times to go through all cores. use popl.h for cmdline parsing.

the pm_table reader may store his results plus timestamps.
the worker core may store his timestamps (start busy, sto busy timestamps)
both threads shall not influence each other during the measurement (e.g. by waiting for mutexes)

statistics and eye diagram bins shall be compute after a worker core has finished its run. then the next worker core shall be started.
results shall be stored in pre-allocated memory and at the end of the program into a file.


Of course. Here is a complete architectural design for the multi-threaded measurement program you've described.

This architecture emphasizes the key requirements:
1.  **Separation of Concerns:** A dedicated measurement thread and a worker thread.
2.  **Real-time Friendly:** Lock-free communication between threads during the measurement phase to avoid jitter.
3.  **Pre-allocation:** All memory for data storage is allocated upfront to prevent unpredictable delays from memory allocation during the experiment.
4.  **Orchestration:** The main thread acts as a conductor, setting up each run, triggering analysis, and saving results.
5.  **Configuration:** Uses the lightweight, header-only `popl.h` for easy command-line parsing.

### Project Structure

```
.
├── src/
│   ├── main.cpp
│   ├── popl.h          // Header-only library for command-line parsing
│   └── workloads.hpp
├── CMakeLists.txt
└── results/            // Output directory for measurement files
```

### 1. `popl.h`
First, download `popl.h` from its repository (e.g., GitHub) and place it in the `src/` directory. It's a single header file, making it easy to include.

### 2. `src/workloads.hpp`
This file will contain the workload function(s) and any helper functions for setting thread affinity.

```cpp
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
inline void integer_alu_workload(uint64_t iterations) {
    // Using volatile prevents the compiler from optimizing the loop away
    volatile uint64_t a = 0, b = 1, c = 2, d = 3;
    for (uint64_t i = 0; i < iterations; ++i) {
        a += i;
        b += a;
        c -= b;
        d *= c;
    }
}
```

### 3. `src/main.cpp` - The Core Architecture

This is the main file containing the program logic.

```cpp
// src/main.cpp
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <fstream>
#include <iomanip>

#include "popl.h"
#include "workloads.hpp"

// Use steady_clock for monotonic time measurements
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::duration<double, std::nano>;

// --- Data Structures for Pre-allocated Storage ---

// Represents a single sample from the measurement thread
struct MeasurementSample {
    TimePoint timestamp;
    int worker_state; // 0 for waiting, 1 for busy
    float mock_sensor_1; // Placeholder for PM table data
    float mock_sensor_2;
};

// Represents a state transition event from the worker thread
struct WorkerTransition {
    TimePoint timestamp;
    int new_state; // 0 for waiting, 1 for busy
};

// --- Mock PM Table Reader ---
// Replace this with your actual PM table reading logic
void read_pm_table(float& val1, float& val2) {
    // In a real scenario, this would read from sysfs or a driver
    val1 = 10.0f;
    val2 = 20.0f;
}

// --- Global Atomic Flags for Thread Synchronization ---
// These are used to signal start/stop without using mutexes
std::atomic<bool> g_run_measurement = false;
std::atomic<bool> g_run_worker = false;
std::atomic<int> g_worker_state = 0; // The single point of communication during a run

// --- Thread Functions ---

void measurement_thread_func(int core_id,
                             std::vector<MeasurementSample>& storage,
                             size_t& sample_count) {
    if (!set_thread_affinity(core_id)) {
        std::cerr << "Warning: Failed to set measurement thread affinity to core " << core_id << std::endl;
    }

    // Wait for the signal to start
    while (!g_run_measurement.load(std::memory_order_acquire));

    const auto sample_period = std::chrono::milliseconds(1);
    auto next_sample_time = Clock::now();
    sample_count = 0;

    while (g_run_measurement.load(std::memory_order_acquire)) {
        // Record timestamp and state immediately
        TimePoint timestamp = Clock::now();
        int worker_state = g_worker_state.load(std::memory_order_relaxed);
        
        // Read sensors
        float s1, s2;
        read_pm_table(s1, s2);

        // Store in pre-allocated buffer
        if (sample_count < storage.size()) {
            storage[sample_count++] = {timestamp, worker_state, s1, s2};
        }

        // Wait until the next 1ms interval
        next_sample_time += sample_period;
        std::this_thread::sleep_until(next_sample_time);
    }
}

void worker_thread_func(int core_id,
                        int period_ms,
                        int duty_cycle_percent,
                        int num_cycles,
                        std::vector<WorkerTransition>& storage,
                        size_t& transition_count) {
    if (!set_thread_affinity(core_id)) {
        std::cerr << "Warning: Failed to set worker thread affinity to core " << core_id << std::endl;
    }

    const auto period = std::chrono::milliseconds(period_ms);
    const auto busy_duration = period * duty_cycle_percent / 100;
    const auto wait_duration = period - busy_duration;
    transition_count = 0;

    // Wait for the signal to start
    while (!g_run_worker.load(std::memory_order_acquire));

    for (int i = 0; i < num_cycles; ++i) {
        // --- BUSY PHASE ---
        g_worker_state.store(1, std::memory_order_relaxed);
        if (transition_count < storage.size()) {
            storage[transition_count++] = {Clock::now(), 1};
        }
        
        auto busy_start = Clock::now();
        while ((Clock::now() - busy_start) < busy_duration) {
            integer_alu_workload(1000); // Inner loop to keep CPU busy
        }

        // --- WAITING PHASE ---
        g_worker_state.store(0, std::memory_order_relaxed);
        if (transition_count < storage.size()) {
            storage[transition_count++] = {Clock::now(), 0};
        }

        std::this_thread::sleep_for(wait_duration);
    }
}


// --- Analysis Function ---
void analyze_and_print_results(int core_id,
                             const std::vector<MeasurementSample>& measurements, size_t sample_count,
                             const std::vector<WorkerTransition>& transitions, size_t transition_count) {
    std::cout << "--- Analyzing Core " << core_id << " ---" << std::endl;
    std::cout << "Collected " << sample_count << " measurement samples." << std::endl;
    std::cout << "Recorded " << transition_count << " worker transitions." << std::endl;
    
    // This is where you would implement your full statistics (mean, stddev, median correlation)
    // and eye-diagram binning logic.
    // For this example, we'll just show a simple analysis.
    
    std::vector<double> busy_samples;
    std::vector<double> wait_samples;

    for (size_t i = 0; i < sample_count; ++i) {
        if (measurements[i].worker_state == 1) {
            busy_samples.push_back(measurements[i].mock_sensor_1);
        } else {
            wait_samples.push_back(measurements[i].mock_sensor_1);
        }
    }
    
    if (busy_samples.empty() || wait_samples.empty()) {
        std::cout << "Not enough data to compute statistics." << std::endl;
        return;
    }

    double busy_mean = std::accumulate(busy_samples.begin(), busy_samples.end(), 0.0) / busy_samples.size();
    double wait_mean = std::accumulate(wait_samples.begin(), wait_samples.end(), 0.0) / wait_samples.size();
    
    std::cout << "Mean sensor value while BUSY:   " << busy_mean << std::endl;
    std::cout << "Mean sensor value while WAITING: " << wait_mean << std::endl;
    std::cout << "Mean Correlation (Difference): " << busy_mean - wait_mean << std::endl;
    std::cout << "-----------------------\n" << std::endl;
}


// --- Main Program Logic ---

int main(int argc, char** argv) {
    using namespace popl;

    // --- Command Line Parsing ---
    OptionParser op("Allowed options");
    auto help_option = op.add<Switch>("h", "help", "produce help message");
    auto period_opt = op.add<Value<int>>("p", "period", "Period of the worker task in milliseconds", 100);
    auto duty_cycle_opt = op.add<Value<int>>("d", "duty-cycle", "Duty cycle of the worker task in percent (10-90)", 50);
    auto cycles_opt = op.add<Value<int>>("c", "cycles", "How many busy/wait cycles to run per core", 100);
    auto rounds_opt = op.add<Value<int>>("r", "rounds", "How many times to cycle through all cores", 1);
    auto outfile_opt = op.add<Value<std::string>>("o", "output", "Output filename for results", "results/output.csv");

    op.parse(argc, argv);

    if (help_option->is_set()) {
        std::cout << op << std::endl;
        return 0;
    }

    // --- Experiment Setup ---
    const int num_hardware_threads = std::thread::hardware_concurrency();
    const int measurement_core = 0; // Pinned core for readout
    std::cout << "System has " << num_hardware_threads << " hardware threads." << std::endl;
    std::cout << "Measurement thread will be pinned to core " << measurement_core << "." << std::endl;

    // --- Pre-allocation of Memory ---
    const size_t max_samples_per_run = (period_opt->value() / 1) * cycles_opt->value() + 1000; // 1ms sample rate + buffer
    const size_t max_transitions_per_run = cycles_opt->value() * 2;
    
    std::vector<MeasurementSample> measurement_storage(max_samples_per_run);
    std::vector<WorkerTransition> transition_storage(max_transitions_per_run);
    size_t actual_sample_count = 0;
    size_t actual_transition_count = 0;

    // File for final output
    std::ofstream outfile(outfile_opt->value());
    outfile << "round,core_id,timestamp_ns,worker_state,sensor1,sensor2\n";


    // --- Main Experiment Loop ---
    for (int round = 0; round < rounds_opt->value(); ++round) {
        std::cout << "========== STARTING ROUND " << round + 1 << " OF " << rounds_opt->value() << " ==========" << std::endl;

        for (int core_to_test = 1; core_to_test < num_hardware_threads; ++core_to_test) {
            if (core_to_test == measurement_core) continue;

            std::cout << "Starting test on core " << core_to_test << std::endl;

            // Reset state for the new run
            g_run_measurement = false;
            g_run_worker = false;
            g_worker_state = 0;
            actual_sample_count = 0;
            actual_transition_count = 0;


            // --- Launch Threads for the current core test ---
            std::thread measurement_thread(measurement_thread_func, measurement_core,
                                           std::ref(measurement_storage), std::ref(actual_sample_count));
            
            std::thread worker_thread(worker_thread_func, core_to_test,
                                      period_opt->value(), duty_cycle_opt->value(), cycles_opt->value(),
                                      std::ref(transition_storage), std::ref(actual_transition_count));
            
            // Give threads a moment to initialize and set affinity
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // Signal threads to start the measurement and workload
            g_run_measurement.store(true, std::memory_order_release);
            g_run_worker.store(true, std::memory_order_release);

            // Wait for the worker to finish its cycles
            worker_thread.join();

            // Signal the measurement thread to stop and wait for it to finish
            g_run_measurement.store(false, std::memory_order_release);
            measurement_thread.join();

            std::cout << "Finished test on core " << core_to_test << std::endl;

            // --- Analyze and Store Results (between core runs) ---
            analyze_and_print_results(core_to_test, measurement_storage, actual_sample_count,
                                      transition_storage, actual_transition_count);
            
            // Write the raw data for this run to the file
            for (size_t i = 0; i < actual_sample_count; ++i) {
                auto const& s = measurement_storage[i];
                outfile << round << ","
                        << core_to_test << ","
                        << std::chrono::duration_cast<std::chrono::nanoseconds>(s.timestamp.time_since_epoch()).count() << ","
                        << s.worker_state << ","
                        << s.mock_sensor_1 << ","
                        << s.mock_sensor_2 << "\n";
            }
        }
    }

    std::cout << "========== EXPERIMENT COMPLETE ==========" << std::endl;
    outfile.close();
    std::cout << "Results saved to " << outfile_opt->value() << std::endl;

    return 0;
}
```

### 4. `CMakeLists.txt`
This file is used to build the project.

```cmake
cmake_minimum_required(VERSION 3.10)
project(RealtimeMeasurement)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(measurement_tool src/main.cpp)

# We need pthreads for thread affinity and std::thread
target_link_libraries(measurement_tool PRIVATE Threads::Threads)

# Enable optimizations for release builds
set_target_properties(measurement_tool PROPERTIES
    COMPILE_FLAGS "-O3 -march=native"
)

# Create an output directory for results
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/results)
```

### How to Build and Run

1.  **Build:**
    ```bash
    mkdir build
    cd build
    cmake ..
    make
    ```

2.  **Run:**
    The executable will be in the `build` directory. You can run it with default parameters or specify your own. **It's recommended to run with `sudo` or as root to ensure thread priorities and affinities are set correctly.**

    ```bash
    # Run with default settings (100ms period, 50% duty, 100 cycles, 1 round)
    sudo ./measurement_tool

    # Run a longer test with custom parameters
    sudo ./measurement_tool --period 50 --duty-cycle 75 --cycles 200 --rounds 3 --output results/my_test.csv
    ```

This architecture provides a solid, extensible foundation for your experiment. You can now focus on implementing the detailed statistical analysis in the `analyze_and_print_results` function and replacing the mock PM table reader with your actual sensor readout logic.