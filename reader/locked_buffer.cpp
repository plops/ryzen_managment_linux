#include "locked_buffer.hpp"

#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#include <spdlog/spdlog.h>

LockedBuffer::LockedBuffer(std::size_t bytes) noexcept
    : ptr_(nullptr), bytes_(0), locked_(false), mmaped_(false) {
    if (bytes == 0) return;

    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 4096;
    std::size_t pages = (bytes + static_cast<std::size_t>(page_sz) - 1) / static_cast<std::size_t>(page_sz);
    std::size_t rounded = pages * static_cast<std::size_t>(page_sz);

    // Try mmap first (pages aligned by design)
    void* m = mmap(nullptr, rounded, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        SPDLOG_WARN("mmap for {} bytes failed (errno={}): falling back to malloc.", rounded, errno);
        void* h = std::malloc(bytes);
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
        if ((rl.rlim_cur != RLIM_INFINITY) && (bytes_ > static_cast<std::size_t>(rl.rlim_cur))) {
            SPDLOG_WARN("Requested to mlock {} bytes but RLIMIT_MEMLOCK is {}. Proceeding without lock.",
                        bytes_, static_cast<unsigned long>(rl.rlim_cur));
            locked_ = false;
            return;
        }
    }

    if (mlock(ptr_, bytes_) == 0) {
        locked_ = true;
        SPDLOG_DEBUG("Successfully mlocked {} bytes.", bytes_);
    } else {
        SPDLOG_WARN("mlock failed (errno={}): proceeding without locked memory.", errno);
        locked_ = false;
    }
}

LockedBuffer::~LockedBuffer() noexcept {
    cleanup();
}

LockedBuffer::LockedBuffer(LockedBuffer&& o) noexcept
    : ptr_(o.ptr_), bytes_(o.bytes_), locked_(o.locked_), mmaped_(o.mmaped_) {
    o.ptr_ = nullptr;
    o.bytes_ = 0;
    o.locked_ = false;
    o.mmaped_ = false;
}

LockedBuffer& LockedBuffer::operator=(LockedBuffer&& o) noexcept {
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

void* LockedBuffer::data() const noexcept { return ptr_; }
std::size_t LockedBuffer::size() const noexcept { return bytes_; }
bool LockedBuffer::locked() const noexcept { return locked_; }
LockedBuffer::operator bool() const noexcept { return ptr_ != nullptr; }

void LockedBuffer::cleanup() noexcept {
    if (!ptr_) return;
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

