
#include <atomic>
#include <chrono>
#include <ctime>
#include <thread>
#include <unistd.h>
#include <vector>

#include "popl.hpp"
#include <folly/ProducerConsumerQueue.h>
#include <folly/stats/StreamingStats.h>
#include <spdlog/spdlog.h>

#include "gui_runner.hpp"
#include "measurement_types.hpp"
#include "pm_table_reader.hpp"
#include "realtime_guard.hpp"
#include "shared_data_types.hpp"
#include "workloads.hpp"

using namespace std::chrono_literals;

// --- Global Atomic Flags for Thread Synchronization ---
// These are used by multiple threads to coordinate.
std::atomic<bool> g_run_measurement = false;
std::atomic<int> g_worker_state = 0; // 0 for idle, 1 for busy

// --- Helper for hybrid sleep/spin ---
static inline void cpu_relax() { asm volatile("pause" ::: "memory"); }

static void wait_until(const Clock::time_point &deadline) {
  using namespace std::chrono;
  const auto now = Clock::now();
  if (deadline <= now)
    return;

  const auto spin_threshold = 200us; // Should be very small
  if (const auto remaining = deadline - now; remaining > spin_threshold) {
    const auto wake_time = deadline - spin_threshold;
    const auto ns =
        time_point_cast<nanoseconds>(wake_time).time_since_epoch().count();
    const timespec ts = {static_cast<time_t>(ns / 1'000'000'000),
                         static_cast<long>(ns % 1'000'000'000)};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
  }

  while (Clock::now() < deadline) {
    cpu_relax();
  }
}

// --- Thread Function Definitions ---

/**
 * @brief (FIX) Provides the implementation for the measurement thread.
 *
 * This lean, real-time function runs on an isolated core. Its only job is to
 * sample the PM table at a precise 1kHz interval and push the raw data into
 * the lock-free SPSC queue for the processing thread to consume.
 */
void measurement_thread_func(int core_id,
                             folly::ProducerConsumerQueue<RawSample> &queue,
                             PmTableReader &pm_table_reader) {
  RealtimeGuard thread_rt(core_id, /*priority=*/98);

  while (!g_run_measurement.load(std::memory_order_acquire)) {
    cpu_relax(); // Wait for the signal to start
  }

  const auto sample_period = 1ms;
  auto next_sample_time = Clock::now();

  const size_t num_floats = pm_table_reader.getPmTableSize() / sizeof(float);
  if (num_floats > PM_TABLE_MAX_FLOATS) {
    SPDLOG_ERROR("PM Table size ({}) exceeds RawSample buffer size ({}).",
                 num_floats, PM_TABLE_MAX_FLOATS);
    return;
  }

  while (g_run_measurement.load(std::memory_order_acquire)) {
    wait_until(next_sample_time);
    next_sample_time += sample_period;

    RawSample sample;
    sample.timestamp = Clock::now();
    sample.worker_state = g_worker_state.load(std::memory_order_relaxed);

    pm_table_reader.read(reinterpret_cast<char *>(sample.measurements.data()));
    sample.num_measurements = num_floats;

    while (!queue.write(sample)) {
      // This case means the processing thread is falling behind.
      // Spinning here is the correct behavior to not lose data, assuming
      // the backlog is temporary.
      cpu_relax();
    }
  }
}

/**
 * @brief (FIX) Provides the implementation for the worker thread.
 *
 * This function is called repeatedly by the GuiRunner's worker management loop.
 * It pins itself to the specified core and executes a fixed number of busy/wait
 * cycles, signaling its state via the global atomic g_worker_state.
 */

// NOTE: Ensure worker_thread_func always sets g_worker_state to 0 when it
// finishes to avoid a stale "busy" state.

void worker_thread_func(int core_id, int period_ms, int duty_cycle_percent,
                        int num_cycles) {
  if (!set_thread_affinity(core_id)) {
    SPDLOG_WARN("Failed to set worker thread affinity to core {}", core_id);
  }

  const auto period = std::chrono::milliseconds(period_ms);
  const auto busy_duration = period * duty_cycle_percent / 100;
  const auto wait_duration = period - busy_duration;

  for (int i = 0; i < num_cycles; ++i) {
    g_worker_state.store(1, std::memory_order_relaxed);
    auto busy_start = Clock::now();
    while ((Clock::now() - busy_start) < busy_duration) {
      integer_alu_workload(1000);
    }

    g_worker_state.store(0, std::memory_order_relaxed);
    std::this_thread::sleep_for(wait_duration);
  }
  // Ensure state is idle when the burst is done
  g_worker_state.store(0, std::memory_order_relaxed);
}
// --- Main Program Logic ---

int main(int argc, char **argv) {
  using namespace popl;

  if (geteuid() != 0) {
    SPDLOG_WARN("This program works best with root privileges for low-latency "
                "sysfs access and real-time scheduling.");
  }

  OptionParser op("Allowed options");
  auto help_option = op.add<Switch>("h", "help", "produce help message");
  auto all_option = op.add<Switch>("a", "all", "capture all sensor values");
  auto period_opt =
      op.add<Value<int>>("p", "period", "Period of the worker task in ms", 150);
  auto duty_cycle_opt = op.add<Value<int>>("d", "duty-cycle",
                                           "Duty cycle in percent (10-90)", 50);
  auto cycles_opt =
      op.add<Value<int>>("c", "cycles", "Busy/wait cycles per run", 30);

  op.parse(argc, argv);

  if (help_option->is_set()) {
    std::cout << op << std::endl;
    return 0;
  }

  // --- Experiment Setup ---
  const int num_hardware_threads = std::thread::hardware_concurrency();
  constexpr int measurement_core = 0;
  SPDLOG_INFO("System has {} hardware threads. Measurement thread will be "
              "pinned to core {}.",
              num_hardware_threads, measurement_core);

  PmTableReader pm_table_reader;
  const size_t n_measurements =
      pm_table_reader.getPmTableSize() / sizeof(float);

  std::vector<int> interesting_index;
  if (all_option->is_set()) {
    interesting_index.resize(n_measurements);
    std::iota(interesting_index.begin(), interesting_index.end(), 0);
  } else {
    // Find which sensors are actively changing
    RealtimeGuard precheck_rt(measurement_core, 98);
    std::vector<float> measurements(n_measurements);
    std::vector<folly::StreamingStats<float, float>> stats(n_measurements);
    constexpr int n_samples = 1000;
    for (int count = 0; count < n_samples; count++) {
      pm_table_reader.readi(reinterpret_cast<char *>(measurements.data()));
      for (size_t i = 0; i < n_measurements; ++i) {
        stats[i].add(measurements[i]);
      }
      std::this_thread::sleep_for(1ms);
    }

    for (size_t i = 0; i < n_measurements; ++i) {
      if (stats[i].sampleVariance() > 1e-9f) {
        interesting_index.push_back(i);
      }
    }
    SPDLOG_INFO("Found {} changing sensors out of {}.",
                interesting_index.size(), n_measurements);
  }

  // --- Launch the GUI ---
  GuiRunner runner(num_hardware_threads, measurement_core, period_opt->value(),
                   duty_cycle_opt->value(), cycles_opt->value(),
                   pm_table_reader, n_measurements, interesting_index);

  int result = runner.run();

  spdlog::shutdown();
  return result;
}