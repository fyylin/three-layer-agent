// =============================================================================
// src/utils/file_lock.cpp
// =============================================================================
#include "utils/file_lock.hpp"

#include <chrono>
#include <fstream>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/stat.h>
#endif

namespace agent {

// -- FileLock ------------------------------------------------------------------

FileLock::FileLock(const std::string& lock_path)
    : path_(lock_path)
{
#ifdef _WIN32
    handle_ = nullptr;
#else
    fd_ = -1;
#endif
}

FileLock::~FileLock() {
    unlock();
}

bool FileLock::try_lock(int timeout_ms) {
    if (held_) return true;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (true) {
#ifdef _WIN32
        // Create/open the lock file
        HANDLE h = CreateFileA(
            path_.c_str(),
            GENERIC_WRITE,
            0,           // no sharing = exclusive
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
            nullptr);

        if (h != INVALID_HANDLE_VALUE) {
            OVERLAPPED ov{};
            if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                           0, 1, 0, &ov)) {
                handle_ = h;
                held_ = true;
                return true;
            }
            CloseHandle(h);
        }
#else
        fd_ = open(path_.c_str(),
                   O_CREAT | O_WRONLY | O_CLOEXEC,
                   0600);
        if (fd_ >= 0) {
            struct flock fl{};
            fl.l_type   = F_WRLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start  = 0;
            fl.l_len    = 1;
            if (fcntl(fd_, F_SETLK, &fl) == 0) {
                held_ = true;
                return true;
            }
            close(fd_);
            fd_ = -1;
        }
#endif
        if (timeout_ms == 0 || std::chrono::steady_clock::now() >= deadline)
            return false;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void FileLock::unlock() {
    if (!held_) return;
#ifdef _WIN32
    if (handle_) {
        OVERLAPPED ov{};
        UnlockFileEx((HANDLE)handle_, 0, 1, 0, &ov);
        CloseHandle((HANDLE)handle_);
        handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        struct flock fl{};
        fl.l_type   = F_UNLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start  = 0;
        fl.l_len    = 1;
        fcntl(fd_, F_SETLK, &fl);
        close(fd_);
        fd_ = -1;
    }
#endif
    held_ = false;
}

} // namespace agent
