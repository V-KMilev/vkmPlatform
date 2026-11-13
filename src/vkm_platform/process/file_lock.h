#pragma once

#include <cstdint>
#include <filesystem>

namespace Vkm::Platform {

/**
 * @brief An exclusive claim on a file, held while the object lives: a second claim on the same file,
 *        from this process or another, is refused at once rather than waited for.
 *
 * Advisory: it stops another FileLock, not a read or a write. The system lets the claim go when the
 * process ends, however it ends, so a crash leaves no stale claim. Not inherited by a child process.
 */
class FileLock {
    public:
        FileLock() = delete;
        ~FileLock();

        FileLock(const FileLock& other) = delete;
        FileLock& operator=(const FileLock& other) = delete;

        FileLock(FileLock && other) noexcept;
        FileLock& operator=(FileLock && other) noexcept;

        /**
         * @brief Create @p path if it is missing, and claim it if no one else holds it.
         *
         * @param path The file claimed; its folder must exist. A file that cannot be opened, or
         *             claimed for any reason but another's claim, is logged.
         */
        explicit FileLock(const std::filesystem::path& path);

    public:
        /// Whether this object holds the claim: false when another held it, the file would not open, or
        /// the system refused it for another reason, which is logged.
        bool held() const { return m_handle != INVALID; }

    private:
        static constexpr intptr_t INVALID = -1;  ///< No file: -1 is both a bad descriptor and Windows'
                                                 ///< INVALID_HANDLE_VALUE.

    private:
        /// Let the claim go and close the file.
        void release() noexcept;

    private:
        intptr_t m_handle = INVALID;  ///< The open file: a descriptor on Linux, a HANDLE on Windows.
};

} // namespace Vkm::Platform
