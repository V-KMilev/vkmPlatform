#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace Vkm::Platform {

/**
 * @brief Cross-platform handle to a loaded shared library (.dll / .so).
 *
 * Owns the OS handle and unloads on destruction, and the copy it loaded from when it loaded one.
 */
class DynamicLibrary {
    public:
        /**
         * @brief How loadCopy() went about loading a library.
         */
        enum class Loaded : uint8_t {
            Copied,   ///< From a copy beside it.
            InPlace,  ///< From the library itself: its folder takes no file.
            Refused,  ///< Not tried: the copy failed for another reason.
        };

        /**
         * @brief What loadCopy() did, and why it made no copy when it made none.
         */
        struct Outcome {
            Loaded          how = Loaded::Refused;
            std::error_code error;  ///< Why no copy was made; empty when one was.
        };

    public:
        DynamicLibrary() = default;
        ~DynamicLibrary();

        DynamicLibrary(const DynamicLibrary& other) = delete;
        DynamicLibrary& operator=(const DynamicLibrary& other) = delete;

        DynamicLibrary(DynamicLibrary && other) = delete;
        DynamicLibrary& operator=(DynamicLibrary && other) = delete;

    public:
        /**
         * @brief Load the library at @p path, unloading any loaded before it.
         *
         * A path, not a string: narrowing through the Windows code page cannot spell every
         * directory name, so the loader gets the native encoding.
         *
         * @param path Library file to open.
         * @return Whether it opened; a failure is logged.
         */
        [[nodiscard]] bool load(const std::filesystem::path& path);

        /**
         * @brief Load a copy of the library at @p built, unloading any loaded before it, so the original
         *        can be rebuilt while the copy is loaded: Windows locks a loaded library against writes.
         *
         * The copy is `<stem>.loaded.<n><extension>` beside it, n new to this process, stepping past a
         * name another process holds on Windows, where a loaded file cannot be deleted. A folder that
         * takes no file - read-only, as an installed program's - loads the original in place. Any
         * other failure to copy loads nothing, since the original would then be locked against its
         * rebuild. unload() deletes the copy.
         *
         * @param built The library as its build wrote it.
         * @return How it went about the load; isLoaded() says whether the loader took the file, and
         *         a loader's refusal is logged.
         */
        Outcome loadCopy(const std::filesystem::path& built);

        /**
         * @brief Delete the copies loadCopy() left beside @p built when a process ended without
         *        unloading, best effort: on Windows, one another process still holds open is skipped.
         *
         * @param built The library as its build wrote it.
         */
        static void sweepCopies(const std::filesystem::path& built);

        /**
         * @brief Unload the library if currently loaded, and delete the copy it was loaded from;
         *        idempotent.
         */
        void unload();

        /**
         * @brief Resolve @p name to a symbol address.
         *
         * @param name The exported symbol's name.
         * @return Its address, to be cast to the expected function-pointer type;
         *         nullptr if absent or nothing is loaded.
         */
        void* symbol(const char* name) const;

        /**
         * @brief Map a base name to its platform filename: "game" -> "game.dll"
         *        (Windows) or "libgame.so" (elsewhere).
         *
         * @param baseName The library's name without prefix or extension.
         * @return The filename the platform's loader expects.
         */
        static std::string platformName(const std::string& baseName);

        bool isLoaded() const { return m_handle != nullptr; }

    private:
        void*                 m_handle = nullptr;
        std::filesystem::path m_copy;  ///< What loadCopy() loaded from; empty for a library loaded in place.
};

} // namespace Vkm::Platform
