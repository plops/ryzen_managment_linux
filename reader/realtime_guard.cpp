#include "realtime_guard.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <string>
#include <cerrno>
#include <pthread.h>
#include <sys/resource.h>


RealtimeGuard::RealtimeGuard(int core_id, int priority, bool lock_memory) noexcept
    : active_(false),
      locked_memory_(false),
      core_id_(core_id),
      new_priority_(priority),
      old_policy_(SCHED_OTHER),
      saved_affinity_(false) {
    pthread_t self = pthread_self();

    // Save current scheduling
    int ret = pthread_getschedparam(self, &old_policy_, &old_param_);
    if (ret != 0) {
        SPDLOG_WARN("pthread_getschedparam failed: {}", std::strerror(errno));
        // continue; we'll still try to set new params
    }

    // Save current affinity if requested
    CPU_ZERO(&old_cpuset_);
    if (core_id_ >= 0) {
        ret = pthread_getaffinity_np(self, sizeof(cpu_set_t), &old_cpuset_);
        if (ret == 0) saved_affinity_ = true;
        else {
            SPDLOG_WARN("pthread_getaffinity_np failed: {}", std::strerror(errno));
            saved_affinity_ = false;
        }
        // set requested affinity
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id_, &cpuset);
        ret = pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuset);
        if (ret != 0) {
            SPDLOG_WARN("pthread_setaffinity_np failed to pin to core {}: {}", core_id_, std::strerror(errno));
        }
    }

    // Set realtime scheduling policy
    struct sched_param param{};
    param.sched_priority = new_priority_;
    ret = pthread_setschedparam(self, SCHED_FIFO, &param);
    if (ret != 0) {
        SPDLOG_WARN("pthread_setschedparam(SCHED_FIFO) failed: {}. You may need root / CAP_SYS_NICE.",
                    std::strerror(errno));
        // don't return early; we still may have set affinity
    }

    // Optionally lock memory to avoid page faults
    if (lock_memory) {
        // Check RLIMIT_MEMLOCK first to avoid pointless mlockall calls that will fail or cause OOM.
        struct rlimit rl;
        if (getrlimit(RLIMIT_MEMLOCK, &rl) == 0) {
            if (rl.rlim_cur == 0) {
                SPDLOG_WARN("RLIMIT_MEMLOCK is 0: skipping mlockall. Consider increasing memlock limit or using per-buffer mlock.");
            } else {
                if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
                    SPDLOG_WARN("mlockall failed: {}. Avoid calling mlockall on large processes; prefer page-aligned per-buffer mlock (CAP_IPC_LOCK needed).",
                                std::strerror(errno));
                } else {
                    locked_memory_ = true;
                }
            }
        } else {
            // If getrlimit fails, try mlockall but warn
            if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
                SPDLOG_WARN("mlockall failed and getrlimit() failed as well: {}. Prefer per-buffer locking to avoid OOM.",
                            std::strerror(errno));
            } else {
                locked_memory_ = true;
            }
        }
    }

    active_ = true;
}

RealtimeGuard::~RealtimeGuard() noexcept {
    if (!active_) return;
    pthread_t self = pthread_self();

    // Restore scheduling policy
    int ret = pthread_setschedparam(self, old_policy_, &old_param_);
    if (ret != 0) {
        SPDLOG_WARN("Failed to restore thread scheduling: {}", std::strerror(errno));
    }

    // Restore affinity if we saved it
    if (saved_affinity_) {
        ret = pthread_setaffinity_np(self, sizeof(cpu_set_t), &old_cpuset_);
        if (ret != 0) {
            SPDLOG_WARN("Failed to restore thread affinity: {}", std::strerror(errno));
        }
    }

    // Unlock memory if we locked it
    if (locked_memory_) {
        if (munlockall() != 0) {
            SPDLOG_WARN("munlockall failed: {}", std::strerror(errno));
        }
    }
}

// Move operations: transfer the responsibility
RealtimeGuard::RealtimeGuard(RealtimeGuard &&other) noexcept {
    active_ = other.active_;
    locked_memory_ = other.locked_memory_;
    core_id_ = other.core_id_;
    new_priority_ = other.new_priority_;
    old_policy_ = other.old_policy_;
    old_param_ = other.old_param_;
    old_cpuset_ = other.old_cpuset_;
    saved_affinity_ = other.saved_affinity_;
    other.active_ = false;
    other.locked_memory_ = false;
    other.saved_affinity_ = false;
}

RealtimeGuard &RealtimeGuard::operator=(RealtimeGuard &&other) noexcept {
    if (this != &other) {
        // best-effort cleanup of current state
        if (active_) {
            // try to restore now; ignore errors
            pthread_setschedparam(pthread_self(), old_policy_, &old_param_);
            if (saved_affinity_) pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &old_cpuset_);
            if (locked_memory_) munlockall();
        }
        active_ = other.active_;
        locked_memory_ = other.locked_memory_;
        core_id_ = other.core_id_;
        new_priority_ = other.new_priority_;
        old_policy_ = other.old_policy_;
        old_param_ = other.old_param_;
        old_cpuset_ = other.old_cpuset_;
        saved_affinity_ = other.saved_affinity_;
        other.active_ = false;
        other.locked_memory_ = false;
        other.saved_affinity_ = false;
    }
    return *this;
}
