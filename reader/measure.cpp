/** @file measure.cpp
 *  @brief Measurement harness: spawns worker and measurement threads, records
 * samples and analyzes eye diagrams.
 *
 *  This file coordinates the experiment loop, pre-allocates buffers and writes
 * results.
 */

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <span> // <-- added
#include <sys/mman.h>
#include <sys/resource.h>
#include <thread>
#include <time.h> // <-- added for clock_nanosleep / timespec
#include <unistd.h>
#include <vector>

#include "popl.hpp"
#include <spdlog/spdlog.h>

#include "eye_capturer.hpp"
#include "eye_diagram.hpp"
#include "folly/stats/StreamingStats.h"
#include "measurement_types.hpp"
#include "pm_table_reader.hpp"
#include "workloads.hpp"

#include "gui_components.hpp" // <-- NEW: Include our new GUI header
#include "gui_runner.hpp"     // <-- NEW: Include the GUI runner
#include "locked_buffer.hpp" // <-- added: RAII wrapper for mmap/mlock allocations
#include "realtime_guard.hpp" // NEW: RAII helper for realtime + mlockall
#include "stats_utils.hpp"    // <-- NEW: Include stats utils

// --- Global Atomic Flags for Thread Synchronization ---
// These are used to signal start/stop without using mutexes
/**
 * @brief Global flag to control measurement thread execution.
 *
 * When true the measurement thread samples sensors at 1ms intervals.
 */
std::atomic<bool> g_run_measurement = false;

/**
 * @brief Global flag to control worker thread execution.
 *
 * When true the worker thread begins its busy/wait cycles.
 */
std::atomic<bool> g_run_worker = false;

/**
 * @brief Global worker state communicated to the measurement thread.
 *
 * Value is 0 when the worker is idle (waiting) and 1 when the worker is busy.
 */
std::atomic<int> g_worker_state =
    0; // The single point of communication during a run

/*
 * a state machine to help capture the eye diagram
 * the internal states are (at least these active states): off_0, on, off_2
 * in these active states we bin the incoming data into
 * everytime g_workers state transitions from 0 to 1 we
 * will store into storage[].measurements.eye_{off0,on,off1}
 * these storage containers act like a 2D histogram, containing
 * the number of observed measurements at each 1ms
 */

// --- New: safe page-aligned locked buffer helpers ---

// --- New: lightweight per-sample view pointing to slices in the locked buffer
// ---
struct LocalMeasurement {
  TimePoint timestamp;
  int worker_state;
  float *measurements; // points into a big locked buffer
};

// --- New: helper for hybrid sleep/spin and light-weight cpu relax ---
// Brief: On Linux (gcc, x86_64/ryzen) use clock_nanosleep for coarse sleep
// and the PAUSE instruction in the final spin to reduce power/jitter.
static inline void cpu_relax() {
  // x86 PAUSE to reduce power and improve spin-loop performance
  asm volatile("pause" ::: "memory");
}

static void wait_until(const Clock::time_point &deadline) {
  using namespace std::chrono;
  auto now = Clock::now();
  if (deadline <= now)
    return;

  // How long we will spin actively at the end. Tuneable:
  const auto spin_threshold = microseconds(
      200); // 200us gives a good tradeoff on many CPUs; tweak as needed

  auto remaining = deadline - now;
  if (remaining > spin_threshold) {
    // Sleep until (deadline - spin_threshold) using CLOCK_MONOTONIC absolute
    // sleep
    auto wake_time = deadline - spin_threshold;
    auto ns =
        time_point_cast<nanoseconds>(wake_time).time_since_epoch().count();
    timespec ts;
    ts.tv_sec = static_cast<time_t>(ns / 1'000'000'000);
    ts.tv_nsec = static_cast<long>(ns % 1'000'000'000);
    // ignore EINTR here; if interrupted we fall through to the spin phase
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
  }

  // Final short spin using PAUSE
  while (Clock::now() < deadline) {
    cpu_relax();
  }
}

// --- Thread Functions ---

/**
 * @brief Measurement thread function.
 *
 * Samples the pm_table at a fixed 1ms rate, stores a MeasurementSample in the
 * provided buffer, and forwards samples to the EyeCapturer for binning.
 *
 * @param core_id CPU core to pin the measurement thread to.
 * @param storage Pre-allocated vector of MeasurementSample to fill.
 * @param sample_count Out parameter updated with the number of samples written.
 * @param pm_table_reader Reader object for the pm_table sysfs blob.
 * @param capturer EyeCapturer that bins samples into the EyeDiagramStorage.
 */
void measurement_thread_func(int core_id,
                             std::vector<LocalMeasurement> &storage,
                             size_t &sample_count,
                             PmTableReader &pm_table_reader,
                             EyeCapturer &capturer) {
  // if (!set_thread_affinity(core_id)) {
  //     std::cerr << "Warning: Failed to set measurement thread affinity to
  //     core " << core_id << std::endl;
  // }

  // Promote this measurement thread to realtime for the duration of the
  // sampling loop
  RealtimeGuard thread_rt(core_id, /*priority=*/98, /*lock_memory=*/false);

  // Wait for the signal to start (use a polite spin)
  while (!g_run_measurement.load(std::memory_order_acquire)) {
    cpu_relax();
  }

  const auto sample_period = std::chrono::milliseconds(1);
  auto next_sample_time = Clock::now();
  sample_count = 0;
  TimePoint old_timestamp = next_sample_time;

  while (g_run_measurement.load(std::memory_order_acquire)) {
    // Record timestamp and state immediately
    TimePoint timestamp = Clock::now();

    auto wait_time_ms = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            timestamp - old_timestamp)
                            .count() /
                        1e6f;
    old_timestamp = timestamp;

    // if (timestamp > next_sample_time) {
    //     auto overtime_ms =
    //     std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp-next_sample_time).count()/1e6f;
    //     SPDLOG_WARN("wait_until took too long: overtime={:.1g}ms
    //     wait_time={:.6g}ms",overtime_ms,wait_time_ms);
    // }
    int worker_state = g_worker_state.load(std::memory_order_relaxed);
    // Store in pre-allocated buffer
    TimePoint start;

    if (sample_count < storage.size()) {
      auto &target = storage[sample_count];
      auto charPtr = reinterpret_cast<char *>(target.measurements);
      // Read sensors directly into the locked buffer slice
      pm_table_reader.read(charPtr);
      start = Clock::now();
      target.timestamp = timestamp;
      target.worker_state = worker_state;
      // process into eye capturer (bins per sensor)
      // forward as std::span<const float> to avoid copy

      capturer.process_sample(
          timestamp, worker_state,
          std::span<const float>(target.measurements,
                                 pm_table_reader.getPmTableSize() /
                                     sizeof(float)));
      sample_count++;
    }
    auto end = Clock::now();

    assert(sample_count < storage.size());

    next_sample_time += sample_period;
    auto end2 = Clock::now();
    auto read_time_ms =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start)
            .count() /
        1e6f;
    auto proc_time_ms =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count() /
        1e6f;
    if (end2 > next_sample_time) {
      SPDLOG_ERROR("Cannot maintain sample rate: late by {:.1}ms, read took "
                   "{:.1}ms, processing took {:.1}ms",
                   (end2 - next_sample_time).count() / 1e6f, read_time_ms,
                   proc_time_ms);
    } else {
      // SPDLOG_ERROR("Can maintain sample rate: early by {:.1}ms, read took
      // {:.1}ms, processing took {:.1}ms",
      //              (next_sample_time-end2).count()/1e6f,
      //              read_time_ms,
      //              proc_time_ms);
    }
    // std::this_thread::sleep_until(next_sample_time-std::chrono::nanoseconds(30'000));
    // Replace tight busy-wait with hybrid sleep-then-spin
    wait_until(next_sample_time);
  }

  // thread_rt destructor will restore scheduling/affinity
}

/**
 * @brief Worker thread function implementing the busy/wait workload.
 *
 * The worker toggles g_worker_state between busy (1) and wait (0) according to
 * the provided period and duty-cycle. Each transition is optionally stored in
 * the provided storage vector.
 *
 * @param core_id CPU core to pin the worker thread to.
 * @param period_ms Length of one duty cycle in milliseconds.
 * @param duty_cycle_percent Percentage of the period spent busy (1..99).
 * @param num_cycles Number of busy/wait cycles to execute.
 * @param transition_count Out parameter updated with the number of transitions
 * recorded.
 */
void worker_thread_func(int core_id, int period_ms, int duty_cycle_percent,
                        int num_cycles, size_t &transition_count) {
  if (!set_thread_affinity(core_id)) {
    std::cerr << "Warning: Failed to set worker thread affinity to core "
              << core_id << std::endl;
  }

  const auto period = std::chrono::milliseconds(period_ms);
  const auto busy_duration = period * duty_cycle_percent / 100;
  const auto wait_duration = period - busy_duration;
  transition_count = 0;

  // Wait for the signal to start (polite spin)
  while (!g_run_worker.load(std::memory_order_acquire)) {
    cpu_relax();
  }

  for (int i = 0; i < num_cycles; ++i) {
    // --- BUSY PHASE ---
    g_worker_state.store(1, std::memory_order_relaxed);

    auto busy_start = Clock::now();
    while ((Clock::now() - busy_start) < busy_duration) {
      integer_alu_workload(1000); // Inner loop to keep CPU busy
    }

    // --- WAITING PHASE ---
    g_worker_state.store(0, std::memory_order_relaxed);

    std::this_thread::sleep_for(wait_duration);
  }
}

/**
 * @brief Analyze collected measurements and print results.
 *
 * Computes simple busy/wait means (for sensor 17) and prints eye-diagram
 * statistics (median and trimmed mean) for every sensor stored in
 * EyeDiagramStorage.
 *
 * @param core_id Core under test (for labeling).
 * @param measurements Vector of MeasurementSample containing raw capture data.
 * @param sample_count Number of samples actually recorded.
 * @param transitions Vector of WorkerTransition events recorded by the worker
 * thread.
 * @param transition_count Number of transitions recorded.
 * @param eye_storage Pre-populated EyeDiagramStorage which contains both the
 *                    binned data and the mapping to original sensor indices.
 */
void analyze_and_print_results(
    int core_id, const std::vector<LocalMeasurement> &measurements,
    size_t sample_count, size_t transition_count,
    const EyeDiagramStorage &eye_storage) {
  std::cout << "--- Analyzing Core " << core_id << " ---" << std::endl;
  std::cout << "Collected " << sample_count << " measurement samples."
            << std::endl;
  std::cout << "Recorded " << transition_count << " worker transitions."
            << std::endl;

  // compute simple busy/wait means for sensor 17 as before (unchanged)
  std::vector<double> busy_samples;
  std::vector<double> wait_samples;
  for (size_t i = 0; i < sample_count; ++i) {
    if (measurements[i].worker_state == 1)
      busy_samples.push_back(measurements[i].measurements[17]);
    else
      wait_samples.push_back(measurements[i].measurements[17]);
  }
  if (busy_samples.empty() || wait_samples.empty()) {
    std::cout << "Not enough data to compute statistics." << std::endl;
  } else {
    double busy_mean =
        std::accumulate(busy_samples.begin(), busy_samples.end(), 0.0) /
        busy_samples.size();
    double wait_mean =
        std::accumulate(wait_samples.begin(), wait_samples.end(), 0.0) /
        wait_samples.size();
    std::cout << "Mean THM value while BUSY:   " << busy_mean << std::endl;
    std::cout << "Mean THM value while WAITING: " << wait_mean << std::endl;
    std::cout << "Mean Correlation (Difference): " << busy_mean - wait_mean
              << std::endl;
  }

  // --- Eye Diagram output for every sensor ---
  const float trim_percent = 10.0f;
  // The number of interesting sensors is the size of the outer vector in bins.
  size_t n_interesting_sensors = eye_storage.bins.size();

  if (0)
    for (size_t storage_idx = 0; storage_idx < n_interesting_sensors;
         ++storage_idx) {
      // Get original sensor index from the mapping now stored in eye_storage
      int sensor = eye_storage.original_sensor_indices[storage_idx];
      std::cout << "\n--- Sensor v" << sensor << " Eye Diagram (Median) ---"
                << std::endl;
      std::cout << "Captured " << eye_storage.event_count
                << " rising edge events." << std::endl;
      std::cout << "Time(ms)\tMedian\tSamples" << std::endl;
      // iterate using runtime-configured number of bins
      for (int i = 0; i < eye_storage.num_bins; ++i) {
        // Access is now bins[sensor_storage_index][bin_index]
        const auto &bin = eye_storage.bins[storage_idx][i];
        if (!bin.empty()) {
          int relative_time_ms = i - eye_storage.zero_offset_bins;
          std::vector<float> sorted_bin = bin;
          std::sort(sorted_bin.begin(), sorted_bin.end());
          float median;
          size_t n = sorted_bin.size();
          if (n % 2 == 0)
            median = 0.5f * (sorted_bin[n / 2 - 1] + sorted_bin[n / 2]);
          else
            median = sorted_bin[n / 2];
          std::cout << relative_time_ms << "\t\t" << std::fixed
                    << std::setprecision(4) << median << "\t" << bin.size()
                    << std::endl;
        }
      }

      std::cout << "\n--- Sensor v" << sensor << " Eye Diagram (TrimmedMean "
                << trim_percent << "%) ---" << std::endl;
      std::cout << "Time(ms)\tTrimmedMean\tSamples" << std::endl;
      for (int i = 0; i < eye_storage.num_bins; ++i) {
        const auto &bin = eye_storage.bins[storage_idx][i];
        if (!bin.empty()) {
          int relative_time_ms = i - eye_storage.zero_offset_bins;
          float robust_mean = calculate_trimmed_mean(bin, trim_percent);
          std::cout << relative_time_ms << "\t\t" << std::fixed
                    << std::setprecision(4) << robust_mean << "\t" << bin.size()
                    << std::endl;
        }
      }
    }
  std::cout << "-----------------------\n" << std::endl;
}

// --- Main Program Logic ---

/**
 * @brief Program entrypoint: parses CLI, allocates buffers and runs
 * experiments.
 *
 * The main function pre-allocates measurement buffers, constructs the pm_table
 * reader, eye storage and capturer, and iterates through rounds/cores executing
 * the workload.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status (0 on success).
 */
int main(int argc, char **argv) {
  using namespace popl;

  if (geteuid() != 0) {
    SPDLOG_WARN("Warning: This program works better with root privileges to "
                "read from sysfs with low latency.");
    SPDLOG_WARN("Please run with sudo.");
  }

  // --- Command Line Parsing ---
  OptionParser op("Allowed options");
  auto help_option = op.add<Switch>("h", "help", "produce help message");
  auto period_opt = op.add<Value<int>>(
      "p", "period", "Period of the worker task in milliseconds", 150);
  auto duty_cycle_opt = op.add<Value<int>>(
      "d", "duty-cycle", "Duty cycle of the worker task in percent (10-90)",
      50);
  auto cycles_opt = op.add<Value<int>>(
      "c", "cycles", "How many busy/wait cycles to run per core", 13);
  auto rounds_opt = op.add<Value<int>>(
      "r", "rounds", "How many times to cycle through all cores", 1);
  auto outfile_opt = op.add<Value<std::string>>(
      "o", "output", "Output filename for results", "results/output.csv");
  // --- REMOVED: GUI option is now default ---
  // auto gui_opt = op.add<Switch>("g", "gui", "Enable live GUI display");

  op.parse(argc, argv);

  if (help_option->is_set()) {
    std::cout << op << std::endl;
    return 0;
  }

  // --- Experiment Setup ---
  const int num_hardware_threads = std::thread::hardware_concurrency();
  const int measurement_core = 0; // Pinned core for readout
  SPDLOG_INFO("System has {} hardware threads.", num_hardware_threads);
  SPDLOG_INFO("Measurement thread will be pinned to core {}.",
              measurement_core);

  PmTableReader pm_table_reader;
  const size_t n_measurements =
      pm_table_reader.getPmTableSize() / sizeof(float);

  std::vector<int> interesting_index;
  int count_interesting = 0;
  {
    // Promote main thread to realtime for the short pre-sampling check.
    // NOTE: we do NOT request mlockall here anymore; memory locking 444is
    // handled explicitly by alloc_locked_buffer for the measurement buffer to
    // avoid locking the entire process (which can cause OOM / thrashing).
    RealtimeGuard precheck_rt(measurement_core, /*priority=*/98,
                              /*lock_memory=*/false);

    // Read a few times to determine the pm_table entries that are changing,
    // stores result in interesting_index
    std::vector<float> measurements(n_measurements);
    std::vector<folly::StreamingStats<float, float>> stats(n_measurements);
    std::vector<bool> interesting(n_measurements, false);
    const auto sample_period = std::chrono::milliseconds(1);
    auto next_sample_time = Clock::now();
    int missed_samples = 0;
    int missed_sample = -1;
    int n_samples = 997;
    for (int count = 0; count < n_samples; count++) {
      // Read sensors
      pm_table_reader.readi(reinterpret_cast<char *>(measurements.data()));
      for (int i = 0; i < n_measurements; ++i) {
        stats[i].add(measurements[i]);
      }
      next_sample_time += sample_period;
      if (next_sample_time < Clock::now()) {
        missed_samples++;
        missed_sample = count;
      }
      // std::this_thread::sleep_until(next_sample_time);
      // Replace tight busy-wait with hybrid sleep-spin for the precheck loop as
      // well
      wait_until(next_sample_time);
    }
    // precheck_rt destructor runs here -> restores scheduling/affinity and
    // munlockall

    count_interesting = 0;
    for (int i = 0; i < n_measurements; ++i) {
      if (stats[i].sampleVariance() > 0.f) {
        interesting[i] = true;
        count_interesting++;
      }
    }
    interesting_index.resize(count_interesting, -1);
    count_interesting = 0;
    for (int i = 0; i < n_measurements; ++i) {
      if (stats[i].sampleVariance() > 0.f) {
        interesting_index[count_interesting] = i;
        count_interesting++;
      }
    }
    SPDLOG_INFO(
        "The pm_table on this platform contains {} entries. {} of these were "
        "changing when reading {} samples with a period of {} ms.",
        n_measurements, count_interesting, n_samples, sample_period.count());

    auto first_it = std::find(interesting.begin(), interesting.end(), true);

    if (first_it == interesting.end()) {
      SPDLOG_INFO("No changing values found in the pm_table.");
    } else {
      auto first = std::distance(interesting.begin(), first_it);
      ssize_t last = -1;
      for (ssize_t i = n_measurements - 1; i >= 0; --i) {
        if (interesting[i]) {
          last = i;
          break;
        }
      }
      auto num = (last >= first) ? (last - first + 1) : 0;
      SPDLOG_INFO("The consecutive range of changing values includes the "
                  "entries from index {} until {}, {} values overall.",
                  first, last, num);
    }

    if (missed_samples)
      SPDLOG_WARN("Of the {} samples {} were late ({:.0g}%), e.g. sample {}. "
                  "Your CPU cannnot sample itself with the expected rate.",
                  n_samples, missed_samples, missed_samples * 100.f / n_samples,
                  missed_sample);
    else
      SPDLOG_INFO("All samples were on time.");
  }

  // --- Pre-allocation of Memory ---
  const size_t max_samples_per_run =
      (period_opt->value() / 1) * cycles_opt->value() + 1000;
  const size_t max_transitions_per_run = cycles_opt->value() * 2;

  // Allocate one big page-aligned buffer for all float sensor values and
  // attempt to lock it.
  size_t total_floats = max_samples_per_run * n_measurements;
  size_t total_bytes = total_floats * sizeof(float);

  // Use RAII LockedBuffer
  LockedBuffer locked_buf(total_bytes);
  if (!locked_buf) {
    SPDLOG_ERROR(
        "Failed to allocate measurement buffer (mmap + malloc both failed).");
    return 1;
  }
  SPDLOG_INFO("Allocated {} bytes for measurement buffer (locked={}).",
              locked_buf.size(), locked_buf.locked());

  // Create LocalMeasurement views pointing into slices of the big buffer
  std::vector<LocalMeasurement> measurement_view;
  measurement_view.reserve(max_samples_per_run);
  for (size_t i = 0; i < max_samples_per_run; ++i) {
    LocalMeasurement lm;
    lm.timestamp = Clock::now();
    lm.worker_state = 0;
    lm.measurements =
        reinterpret_cast<float *>(locked_buf.data()) + (i * n_measurements);
    // Optionally zero first sample slice
    // memset(lm.measurements, 0, n_measurements * sizeof(float));
    measurement_view.push_back(lm);
  }

  // --- NEW: GUI Mode Logic ---
  // The GUI is now the only mode of operation for this executable.
  GuiRunner gui_runner(rounds_opt->value(), num_hardware_threads,
                       measurement_core, period_opt->value(),
                       duty_cycle_opt->value(), cycles_opt->value(),
                       measurement_view, pm_table_reader,
                       n_measurements, interesting_index);
  return gui_runner.run();

  // --- Main Experiment Loop (Original non-GUI path) ---
  // THIS PATH IS NO LONGER REACHABLE AND CAN BE REMOVED
  /*
  std::ofstream outfile(outfile_opt->value());
  outfile << "round,core_id,timestamp_ns,worker_state";
  // Only write headers for the interesting sensors
  for (int sensor_idx : interesting_index) {
      outfile << ",v" << std::to_string(sensor_idx);
  }
  outfile << std::endl;

  // --- Main Experiment Loop ---
  for (int round = 0; round < rounds_opt->value(); ++round) {
      std::cout << "========== STARTING ROUND " << round + 1 << " OF " <<
  rounds_opt->value() << " ==========" << std::endl;

      for (int core_to_test = 1; core_to_test < num_hardware_threads;
  ++core_to_test) { if (core_to_test == measurement_core) continue;

          std::cout << "Starting test on core " << core_to_test << std::endl;

          // Reset state for the new run
          g_run_measurement = false;
          g_run_worker = false;
          actual_sample_count = 0;
          actual_transition_count = 0;
          eye_storage.clear(); // call per run to reset while keeping allocation

          // --- Launch Threads for the current core test ---
          std::thread measurement_thread(measurement_thread_func,
  measurement_core, std::ref(measurement_view), std::ref(actual_sample_count),
                                         std::ref(pm_table_reader),
                                         std::ref(capturer));

          std::thread worker_thread(worker_thread_func, core_to_test,
                                    period_opt->value(),
  duty_cycle_opt->value(), cycles_opt->value(),
                                    std::ref(actual_transition_count));

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
          analyze_and_print_results(core_to_test, measurement_view,
  actual_sample_count, actual_transition_count, eye_storage);

          // Write the raw data for this run to the file
          for (size_t i = 0; i < actual_sample_count; ++i) {
              auto const &s = measurement_view[i];
              outfile << round << ","
                      << core_to_test << ","
                      <<
  std::chrono::duration_cast<std::chrono::nanoseconds>(s.timestamp.time_since_epoch()).
                      count() << ","
                      << s.worker_state;
              // Only write values for the interesting sensors
              for (int sensor_idx : interesting_index) {
                  outfile << "," << s.measurements[sensor_idx];
              }
              outfile << std::endl;
          }
      }
  }

  std::cout << "========== EXPERIMENT COMPLETE ==========" << std::endl;
  outfile.close();
  std::cout << "Results saved to " << outfile_opt->value() << std::endl;

  // LockedBuffer destructor will munlock/munmap or free as needed here when it
  goes out of scope.

  spdlog::shutdown(); // Flush all logs before exiting
  return 0;
  */
}
