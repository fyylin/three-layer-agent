#pragma once
// =============================================================================
// include/utils/file_lock.hpp
//
// Cross-platform RAII file lock.
// Used for:
//   - workspace/shared/ mutual exclusion
//   - global.log append serialization (across processes if needed)
//
// Windows: uses LockFileEx on an advisory lock file (<path>.lock)
// POSIX:   uses fcntl(F_SETLK) on the same file
//
// Design: FileLock is a scoped guard.  Use try_lock(timeout_ms) to acquire.
// The lock is released on destruction.
// =============================================================================

#include <string>

namespace agent {

class FileLock {
public:
    // Constructs without acquiring.  lock_path is the path to a .lock file
    // (created if it does not exist).
    explicit FileLock(const std::string& lock_path);
    ~FileLock();

    FileLock(const FileLock&)            = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&&)                 = delete;

    // Attempt to acquire the lock.
    // Returns true on success, false on timeout.
    // timeout_ms=0 means try-once (non-blocking).
    [[nodiscard]] bool try_lock(int timeout_ms = 5000);

    // Release the lock.  Safe to call even if not held.
    void unlock();

    [[nodiscard]] bool is_held() const noexcept { return held_; }

private:
    std::string path_;
    bool        held_ = false;

#ifdef _WIN32
    void* handle_ = nullptr;   // HANDLE
#else
    int   fd_     = -1;
#endif
};

// -----------------------------------------------------------------------------
// ScopedFileLock: acquire on construction, release on destruction
// -----------------------------------------------------------------------------
class ScopedFileLock {
public:
    explicit ScopedFileLock(const std::string& lock_path, int timeout_ms = 5000)
        : lock_(lock_path)
    {
        acquired_ = lock_.try_lock(timeout_ms);
    }
    ~ScopedFileLock() { if (acquired_) lock_.unlock(); }

    [[nodiscard]] bool acquired() const noexcept { return acquired_; }

private:
    FileLock lock_;
    bool     acquired_ = false;
};

} // namespace agent
