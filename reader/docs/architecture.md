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
object Object1 {
Alpha
Bravo
}


object Object2 {
Charlie
Delta
}


object Object3 {
Echo
Foxtrot
}


Object1 <|-- Object2
Object1 <|-- Object3
\enduml