# `pm_measure` Architecture

`pm_measure` is a tool designed to measure and analyze the impact of CPU core activity on power management (PM) sensor values on AMD Ryzen systems. It operates by creating a controlled workload on one core while a high-frequency measurement thread on another core samples sensor data from the `ryzen_smu` driver's `pm_table`.

The primary goal is to generate "eye diagrams" that show how sensor values change in response to the worker thread transitioning between busy and idle states.

## Core Components

The application consists of three main threads:

1.  **Main Thread**:
    *   Parses command-line arguments.
    *   Pre-allocates a large, page-aligned memory buffer (`LockedBuffer`) for measurement data and attempts to lock it into RAM using `mlock` to prevent page faults during the critical measurement loop.
    *   Initializes the `EyeDiagramStorage` and `EyeCapturer`.
    *   Manages the main experiment loop, iterating through specified CPU cores and rounds.
    *   Spawns the worker and measurement threads for each test.
    *   Collects results, performs analysis, and writes output to a CSV file.

2.  **Worker Thread (`worker_thread_func`)**:
    *   Pinned to a specific CPU core under test.
    *   Executes a simple busy/wait cycle with a configurable period and duty cycle.
    *   Communicates its state (0 for idle, 1 for busy) to the measurement thread via a global atomic flag (`g_worker_state`).

3.  **Measurement Thread (`measurement_thread_func`)**:
    *   Pinned to a dedicated, isolated CPU core (typically core 0).
    *   Promoted to a realtime scheduling policy (`SCHED_FIFO`) to ensure consistent timing.
    *   Runs a tight loop, sampling the entire `pm_table` from sysfs at a fixed interval (typically 1ms).
    *   Uses a hybrid sleep-then-spin wait (`wait_until`) to precisely time its samples.
    *   Writes timestamp, worker state, and sensor data directly into slices of the pre-allocated `LockedBuffer`.
    *   Feeds each sample into an `EyeCapturer` instance, which detects rising edges of the worker state and bins the sensor data into an `EyeDiagramStorage` relative to the time of the edge.

## Data Flow and Key Structures

The diagram below illustrates the interaction between these components.

\startuml
!theme vibrant

package "Main Thread" {
  actor User
  usecase "Run Experiment" as Run

  rectangle "main()" as Main {
    rectangle Buffer [
      <b>LockedBuffer</b>
      --
      Large, page-aligned buffer
      for all sensor samples.
      (mmap + mlock)
    ]
    rectangle EyeStorage [
      <b>EyeDiagramStorage</b>
      --
      Holds binned data for
      the eye diagram analysis.
    ]
    rectangle Capturer [
      <b>EyeCapturer</b>
      --
      State machine to detect
      worker edges and fill
      EyeDiagramStorage.
    ]
  }
}

package "Threads" {
  rectangle "Worker Thread" as Worker {
    usecase "Busy/Wait Loop"
  }
  rectangle "Measurement Thread" as Measurement {
    usecase "Sample Loop (1ms)" as SampleLoop
  }
}

package "Shared State" {
  rectangle "g_worker_state" [
    <b>g_worker_state</b>
    --
    atomic<int>
  ]
}

cloud "Kernel / HW" {
    database "sysfs\n/../pm_table" as pm_table
    rectangle "CPU Cores" as Cores
}

User -> Run
Run -> Main

Main -> Buffer : creates
Main -> EyeStorage : creates
Main -> Capturer : creates

Main ..> Worker : spawns
Main ..> Measurement : spawns

Worker .> Cores : pinned
Measurement .> Cores : pinned

Worker -> g_worker_state : writes 0 or 1
g_worker_state -> Measurement : reads

Measurement -> pm_table : reads
Measurement -> Buffer : writes samples
Measurement -> Capturer : process_sample()
Capturer -> EyeStorage : add_sample()

\enduml

