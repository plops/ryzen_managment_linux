#pragma once
#include <cstddef>

/**
 * @brief RAII wrapper that allocates a page-rounded buffer and optionally locks it into RAM.
 *
 * Behavior:
 *  - Prefer mmap + mlock (if RLIMIT_MEMLOCK permits).
 *  - If mmap fails, fall back to malloc (unlocked).
 *  - Destructor munlocks+munmaps or frees as appropriate.
 *
 * Non-copyable, movable.
 */
class LockedBuffer {
public:
    explicit LockedBuffer(std::size_t bytes) noexcept;
    ~LockedBuffer() noexcept;

    // non-copyable
    LockedBuffer(const LockedBuffer&) = delete;
    LockedBuffer& operator=(const LockedBuffer&) = delete;

    // movable
    LockedBuffer(LockedBuffer&&) noexcept;
    LockedBuffer& operator=(LockedBuffer&&) noexcept;

    void* data() const noexcept;
    std::size_t size() const noexcept;
    bool locked() const noexcept;
    explicit operator bool() const noexcept;

private:
    void cleanup() noexcept;

    void* ptr_{nullptr};
    std::size_t bytes_{0};
    bool locked_{false};
    bool mmaped_{false};
};

