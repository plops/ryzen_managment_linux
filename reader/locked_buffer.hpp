/**
 * @file locked_buffer.hpp
 * @brief RAII wrapper for a page-rounded buffer optionally locked into RAM.
 *
 * This header declares LockedBuffer, a non-copyable, movable RAII type that
 * prefers mmap + mlock (if permitted by RLIMIT_MEMLOCK). On mmap failure it
 * falls back to malloc (unlocked). The destructor undoes mlock/munmap or free
 * as appropriate.
 */

#pragma once
#include <cstddef>

/**
 * @class LockedBuffer
 * @brief Allocates a page-rounded buffer and optionally locks it into physical memory.
 *
 * Behavior:
 *  - Prefer mmap + mlock (if RLIMIT_MEMLOCK permits).
 *  - If mmap fails, fall back to malloc (unlocked).
 *  - Destructor munlocks+munmaps or frees as appropriate.
 *
 * The type is non-copyable and movable.
 */
class LockedBuffer {
public:
    /**
     * @brief Construct a LockedBuffer requesting the given size in bytes.
     *
     * The requested size will be rounded up to the system page size when mmap
     * is used. If bytes == 0 no allocation is performed.
     *
     * @param bytes number of bytes to allocate (may be rounded up)
     * @note Prefer mmap + mlock. Falls back to malloc if mmap fails.
     * @note This constructor is noexcept; allocation failures are reported via logs
     *       and the object will evaluate false.
     */
    explicit LockedBuffer(std::size_t bytes) noexcept;

    /**
     * @brief Destroy the buffer, releasing/munlocking memory as appropriate.
     *
     * This will munlock+munmap when mmap was used and munlock succeeded,
     * or free when the malloc fallback was used.
     */
    ~LockedBuffer() noexcept;

    // non-copyable
    LockedBuffer(const LockedBuffer&) = delete;
    LockedBuffer& operator=(const LockedBuffer&) = delete;

    // movable
    /**
     * @brief Move-construct from another LockedBuffer.
     * @param other source object; its resources are transferred.
     */
    LockedBuffer(LockedBuffer&&) noexcept;

    /**
     * @brief Move-assign from another LockedBuffer.
     * @param other source object; its resources are transferred.
     * @return reference to *this
     */
    LockedBuffer& operator=(LockedBuffer&&) noexcept;

    /**
     * @brief Get the underlying pointer to the allocated memory (may be nullptr).
     * @return pointer to buffer or nullptr if allocation failed or size == 0.
     */
    void* data() const noexcept;

    /**
     * @brief Get the allocated size in bytes.
     * @return size of the buffer (0 if none). If mmap was used, this is page-rounded.
     */
    std::size_t size() const noexcept;

    /**
     * @brief Query whether the buffer is locked in RAM (mlock succeeded).
     * @return true if the memory is locked; false otherwise.
     */
    bool locked() const noexcept;

    /**
     * @brief Boolean test for whether allocation succeeded.
     * @return true when data() != nullptr.
     */
    explicit operator bool() const noexcept;

private:
    /**
     * @brief Internal cleanup helper used by destructor and assignment.
     *
     * Releases any locked and/or mapped memory or frees malloced memory, and
     * resets internal state.
     */
    void cleanup() noexcept;

    void* ptr_{nullptr};
    std::size_t bytes_{0};
    bool locked_{false};
    bool mmaped_{false};
};
