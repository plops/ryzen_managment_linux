#pragma once
// Minimal RAII helper to promote the current thread to realtime + pin affinity and optionally lock memory.
// Restores previous scheduling and affinity on destruction and undoes mlockall.
#include <sched.h>

class RealtimeGuard {
public:
    // core_id: if >=0, pin to that core. priority: 1..99 for SCHED_FIFO. lock_memory: call mlockall if true.
    explicit RealtimeGuard(int core_id = -1, int priority = 80, bool lock_memory = false) noexcept;
    ~RealtimeGuard() noexcept;

    // non-copyable
    RealtimeGuard(const RealtimeGuard &) = delete;
    RealtimeGuard &operator=(const RealtimeGuard &) = delete;
    // movable
    RealtimeGuard(RealtimeGuard &&other) noexcept;
    RealtimeGuard &operator=(RealtimeGuard &&other) noexcept;

    bool active() const noexcept { return active_; }

private:
    bool active_;
    bool locked_memory_;
    int core_id_;
    int new_priority_;
    int old_policy_;
    struct sched_param old_param_;
    cpu_set_t old_cpuset_;
    bool saved_affinity_;
};

