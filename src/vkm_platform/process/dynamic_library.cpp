#define VKM_LOG_CATEGORY "PLATFORM"

#include "vkm_platform/process/dynamic_library.h"

#include <atomic>

#include "vkm_log/logger.h"

#include "vkm_platform/windows_api.h"

#if !defined(_WIN32)
    #include <dlfcn.h>
#endif

namespace Vkm::Platform {

namespace {

// Numbers each copy loadCopy makes, so two loads in one process never share a name.
std::atomic<unsigned> g_copies{0};

// The name every copy of @p built starts with: "<stem>.loaded.".
std::string copyPrefix(const std::filesystem::path& built) {
    return built.stem().string() + ".loaded.";
}

// Delete @p copy, a copy loadCopy made, saying so when it stays.
void deleteCopy(const std::filesystem::path& copy) {
    std::error_code ec;
    std::filesystem::remove(copy, ec);
    if (ec) LOG_WARNING("Cannot delete '%s': %s", copy.string().c_str(), ec.message().c_str());
}

} // namespace

DynamicLibrary::~DynamicLibrary() {
    unload();
}

bool DynamicLibrary::load(const std::filesystem::path& path) {
    unload();
#if defined(_WIN32)
    // c_str() is already the wide native form; nothing is converted.
    m_handle = ::LoadLibraryW(path.c_str());
    if (!m_handle) {
        LOG_ERROR("LoadLibrary failed for '%s' (error %lu)", path.string().c_str(), ::GetLastError());
        return false;
    }
#else
    // RTLD_NOW: a missing symbol fails at load. RTLD_LOCAL: a library loaded again stays apart from
    // the one before it; it finds what it links through its own DT_NEEDED.
    m_handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_handle) {
        const char* err = ::dlerror();
        LOG_ERROR("dlopen failed for '%s': %s", path.c_str(), err ? err : "unknown error");
        return false;
    }
#endif
    return true;
}

DynamicLibrary::Outcome DynamicLibrary::loadCopy(const std::filesystem::path& built) {
    namespace fs = std::filesystem;
    unload();

    // Sixteen names is a ceiling, not a dependency: one is taken only while another process holds it.
    Outcome outcome;
    fs::path copy;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::string name = copyPrefix(built) + std::to_string(g_copies++) + built.extension().string();
        copy = built.parent_path() / name;
        std::error_code ignored;
        fs::remove(copy, ignored);
        outcome.error.clear();
        fs::copy_file(built, copy, fs::copy_options::overwrite_existing, outcome.error);
        if (!outcome.error) break;
    }

    if (!outcome.error) {
        outcome.how = Loaded::Copied;
        if (load(copy)) {
            m_copy = copy;
        } else {
            deleteCopy(copy);
        }
        return outcome;
    }

    // In place only where the folder refuses writes; anywhere else the build would be locked by the
    // library it is meant to replace.
    const std::error_code& error = outcome.error;
    const bool readOnly = error == std::errc::permission_denied
        || error == std::errc::read_only_file_system
        || error == std::errc::operation_not_permitted;
    if (!readOnly) return outcome;

    outcome.how = Loaded::InPlace;
    static_cast<void>(load(built));
    return outcome;
}

void DynamicLibrary::sweepCopies(const std::filesystem::path& built) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string prefix    = copyPrefix(built);
    const fs::path    extension = built.extension();
    for (fs::directory_iterator it(built.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(prefix, 0) == 0 && it->path().extension() == extension) {
            // Best effort, so said at Info: on Windows another process holding its copy is ordinary.
            std::error_code held;
            fs::remove(it->path(), held);
            if (held) LOG_INFO("Cannot delete '%s': %s", it->path().string().c_str(), held.message().c_str());
        }
    }
    // A folder that is not there holds no copy.
    if (ec && ec != std::errc::no_such_file_or_directory) {
        LOG_WARNING("Cannot look for copies of '%s': %s", built.string().c_str(), ec.message().c_str());
    }
}

void DynamicLibrary::unload() {
    if (m_handle) {
#if defined(_WIN32)
        ::FreeLibrary(static_cast<HMODULE>(m_handle));
#else
        // Unmaps only a library with no STB_GNU_UNIQUE symbol (built with GCC's -fno-gnu-unique), and
        // never under Tracy, which makes dlclose a no-op. Nothing may keep a pointer into it after.
        ::dlclose(m_handle);
#endif
        m_handle = nullptr;
    }

    // After the unload, which held it locked on Windows.
    if (!m_copy.empty()) {
        deleteCopy(m_copy);
        m_copy.clear();
    }
}

void* DynamicLibrary::symbol(const char* name) const {
    if (!m_handle) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(m_handle), name));
#else
    return ::dlsym(m_handle, name);
#endif
}

std::string DynamicLibrary::platformName(const std::string& baseName) {
#if defined(_WIN32)
    return baseName + ".dll";
#else
    return "lib" + baseName + ".so";
#endif
}

} // namespace Vkm::Platform
