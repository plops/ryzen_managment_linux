/**
 * @file locked_buffer.cpp
 * @brief Implementation of LockedBuffer: allocation, optional mlock and
 * cleanup.
 */

#include "locked_buffer.hpp"

#include <cerrno>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

/**
 * @brief Construct and attempt to allocate and lock a buffer.
 *
 * Attempts mmap of a page-rounded size and then mlock if RLIMIT_MEMLOCK allows.
 * Falls back to malloc (unlocked) if mmap fails.
 *
 * @param bytes requested number of bytes (0 means no allocation)
 */
LockedBuffer::LockedBuffer(std::size_t bytes) noexcept
    : ptr_(nullptr), bytes_(0), locked_(false), mmaped_(false) {
  if (bytes == 0)
    return;

  long page_sz = sysconf(_SC_PAGESIZE);
  if (page_sz <= 0)
    page_sz = 4096;
  std::size_t pages = (bytes + static_cast<std::size_t>(page_sz) - 1) /
                      static_cast<std::size_t>(page_sz);
  std::size_t rounded = pages * static_cast<std::size_t>(page_sz);

  // Try mmap first (pages aligned by design)
  void *m = mmap(nullptr, rounded, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (m == MAP_FAILED) {
    SPDLOG_WARN("mmap for {} bytes failed (errno={}): falling back to malloc.",
                rounded, errno);
    void *h = std::malloc(bytes);
    if (!h) {
      SPDLOG_ERROR("malloc fallback failed allocating {} bytes.", bytes);
      return;
    }
    ptr_ = h;
    bytes_ = bytes;
    mmaped_ = false;
    locked_ = false;
    return;
  }

  // mmap succeeded
  ptr_ = m;
  bytes_ = rounded;
  mmaped_ = true;

  // Check RLIMIT_MEMLOCK before attempting to lock
  struct rlimit rl;
  if (getrlimit(RLIMIT_MEMLOCK, &rl) == 0) {
    if ((rl.rlim_cur != RLIM_INFINITY) &&
        (bytes_ > static_cast<std::size_t>(rl.rlim_cur))) {
      SPDLOG_WARN("Requested to mlock {} bytes but RLIMIT_MEMLOCK is {}. "
                  "Proceeding without lock.",
                  bytes_, static_cast<unsigned long>(rl.rlim_cur));
      locked_ = false;
      return;
    }
  }

  if (mlock(ptr_, bytes_) == 0) {
    locked_ = true;
    SPDLOG_DEBUG("Successfully mlocked {} bytes.", bytes_);
  } else {
    SPDLOG_WARN("mlock failed (errno={}): proceeding without locked memory.",
                errno);
    locked_ = false;
  }
}

/**
 * @brief Destructor - perform cleanup.
 *
 * Ensures any locked/mapped or allocated memory is released.
 */
LockedBuffer::~LockedBuffer() noexcept { cleanup(); }

/**
 * @brief Move constructor - transfer ownership from other.
 * @param o source object (left in empty state)
 */
LockedBuffer::LockedBuffer(LockedBuffer &&o) noexcept
    : ptr_(o.ptr_), bytes_(o.bytes_), locked_(o.locked_), mmaped_(o.mmaped_) {
  o.ptr_ = nullptr;
  o.bytes_ = 0;
  o.locked_ = false;
  o.mmaped_ = false;
}

/**
 * @brief Move-assignment - release current resources and take those from other.
 * @param o source object
 * @return reference to *this
 */
LockedBuffer &LockedBuffer::operator=(LockedBuffer &&o) noexcept {
  if (this != &o) {
    cleanup();
    ptr_ = o.ptr_;
    bytes_ = o.bytes_;
    locked_ = o.locked_;
    mmaped_ = o.mmaped_;
    o.ptr_ = nullptr;
    o.bytes_ = 0;
    o.locked_ = false;
    o.mmaped_ = false;
  }
  return *this;
}

/**
 * @brief Get underlying data pointer.
 * @return pointer to the allocated buffer or nullptr if none.
 */
void *LockedBuffer::data() const noexcept { return ptr_; }

/**
 * @brief Get allocated size in bytes.
 * @return size of the buffer, 0 if none. When mmaped, this is page-rounded.
 */
std::size_t LockedBuffer::size() const noexcept { return bytes_; }

/**
 * @brief Query whether memory was successfully locked.
 * @return true if mlock succeeded and memory is locked in RAM.
 */
bool LockedBuffer::locked() const noexcept { return locked_; }

/**
 * @brief Boolean test for whether allocation succeeded.
 * @return true when data() != nullptr.
 */
LockedBuffer::operator bool() const noexcept { return ptr_ != nullptr; }

/**
 * @brief Internal cleanup: unmap/munlock or free and reset state.
 *
 * This is noexcept and logs warnings on munlock/munmap failures.
 */
void LockedBuffer::cleanup() noexcept {
  if (!ptr_)
    return;
  if (mmaped_) {
    if (locked_) {
      if (munlock(ptr_, bytes_) != 0) {
        SPDLOG_WARN("munlock failed (errno={})", errno);
      }
    }
    if (munmap(ptr_, bytes_) != 0) {
      SPDLOG_WARN("munmap failed (errno={})", errno);
    }
  } else {
    std::free(ptr_);
  }
  ptr_ = nullptr;
  bytes_ = 0;
  locked_ = false;
  mmaped_ = false;
}
