#define VKM_LOG_CATEGORY "PLATFORM"

#include "vkm_platform/process/file_lock.h"

#include <utility>

#include "vkm_log/logger.h"

#include "vkm_platform/windows_api.h"

#if !defined(_WIN32)
    #include <cerrno>
    #include <cstring>
    #include <fcntl.h>
    #include <sys/file.h>
    #include <unistd.h>
#endif

namespace Vkm::Platform {

#if defined(_WIN32)
namespace {

// The one byte a claim locks: the last a signed 64-bit offset names, past any end a file reaches. A
// Windows lock is mandatory, so a byte a read or a write reaches would be refused to every other
// handle, this process's own too.
OVERLAPPED claimedByte() {
    OVERLAPPED byte = {};
    byte.Offset     = 0xFFFFFFFF;
    byte.OffsetHigh = 0x7FFFFFFF;
    return byte;
}

} // namespace
#endif

FileLock::FileLock(const std::filesystem::path& path) {
#if defined(_WIN32)
    // Not inheritable: a child process holds no claim of its parent's.
    const HANDLE file = ::CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (file == INVALID_HANDLE_VALUE) {
        LOG_WARNING("Cannot open '%s' to claim it (error %lu)", path.string().c_str(), ::GetLastError());
        return;
    }
    OVERLAPPED byte = claimedByte();
    if (!::LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &byte)) {
        // Refused for another's claim, which is no failure.
        const DWORD error = ::GetLastError();
        if (error != ERROR_LOCK_VIOLATION) {
            LOG_WARNING("Cannot claim '%s' (error %lu)", path.string().c_str(), error);
        }
        ::CloseHandle(file);
        return;
    }
    m_handle = reinterpret_cast<intptr_t>(file);
#else
    // Close-on-exec: a child process holds no claim of its parent's past its exec. The mode any file
    // the process writes takes (the umask decides it), so a log claimed is a log like the others.
    const int file = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (file < 0) {
        LOG_WARNING("Cannot open '%s' to claim it: %s", path.c_str(), std::strerror(errno));
        return;
    }
    if (::flock(file, LOCK_EX | LOCK_NB) != 0) {
        const int error = errno;
        if (error != EWOULDBLOCK) LOG_WARNING("Cannot claim '%s': %s", path.c_str(), std::strerror(error));
        ::close(file);
        return;
    }
    m_handle = file;
#endif
}

FileLock::~FileLock() {
    release();
}

FileLock::FileLock(FileLock && other) noexcept
    : m_handle(std::exchange(other.m_handle, INVALID)) {}

FileLock& FileLock::operator=(FileLock && other) noexcept {
    if (this != &other) {
        release();
        m_handle = std::exchange(other.m_handle, INVALID);
    }
    return *this;
}

void FileLock::release() noexcept {
    if (m_handle == INVALID) return;
#if defined(_WIN32)
    const HANDLE file = reinterpret_cast<HANDLE>(m_handle);
    OVERLAPPED byte   = claimedByte();
    ::UnlockFileEx(file, 0, 1, 0, &byte);
    ::CloseHandle(file);
#else
    ::flock(static_cast<int>(m_handle), LOCK_UN);
    ::close(static_cast<int>(m_handle));
#endif
    m_handle = INVALID;
}

} // namespace Vkm::Platform
