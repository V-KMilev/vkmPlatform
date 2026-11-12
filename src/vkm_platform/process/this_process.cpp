#include "vkm_platform/process/this_process.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "vkm_platform/windows_api.h"

#if !defined(_WIN32)
    #include <climits>
    #include <unistd.h>
#endif

namespace Vkm::Platform {

namespace {

// Set from a signal handler, so lock-free, and the handler touches nothing else.
std::atomic<bool> g_stopRequested{false};

extern "C" void onStopSignal(int) { g_stopRequested.store(true, std::memory_order_relaxed); }

// An environment variable as a path, or empty when unset or blank. Wide on Windows: the narrow
// environment mangles a non-ANSI user name.
#if defined(_WIN32)
std::filesystem::path envPath(const wchar_t* name) {
    const wchar_t* value = ::_wgetenv(name);
    return (value && *value) ? std::filesystem::path(value) : std::filesystem::path{};
}
#else
std::filesystem::path envPath(const char* name) {
    const char* value = std::getenv(name);
    return (value && *value) ? std::filesystem::path(value) : std::filesystem::path{};
}
#endif

} // namespace

std::filesystem::path executableDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD len = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};  // failed or truncated
    return std::filesystem::path(buf, buf + len).parent_path();
#else
    char buf[PATH_MAX];
    const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf));
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(buf)) return {};
    return std::filesystem::path(buf, buf + len).parent_path();
#endif
}

std::filesystem::path userConfigDir() {
#if defined(_WIN32)
    return envPath(L"APPDATA");
#else
    const std::filesystem::path xdg = envPath("XDG_CONFIG_HOME");
    if (!xdg.empty()) return xdg;
    const std::filesystem::path home = envPath("HOME");
    return home.empty() ? std::filesystem::path{} : home / ".config";
#endif
}

std::filesystem::path userStateDir() {
#if defined(_WIN32)
    return envPath(L"LOCALAPPDATA");
#else
    const std::filesystem::path xdg = envPath("XDG_STATE_HOME");
    if (!xdg.empty()) return xdg;
    const std::filesystem::path home = envPath("HOME");
    return home.empty() ? std::filesystem::path{} : home / ".local" / "state";
#endif
}

void attachParentConsole() {
#if defined(_WIN32)
    const auto given = [](DWORD stream) {
        const HANDLE handle = GetStdHandle(stream);
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    };
    const bool outGiven = given(STD_OUTPUT_HANDLE);
    const bool errGiven = given(STD_ERROR_HANDLE);
    if (GetConsoleWindow() == nullptr && !(outGiven && errGiven) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!outGiven) static_cast<void>(std::freopen("CONOUT$", "w", stdout));
        if (!errGiven) static_cast<void>(std::freopen("CONOUT$", "w", stderr));
    }
#endif
}

bool stopRequested() {
    // Installed once, by whichever call comes first.
    static const bool s_listening = [] {
        std::signal(SIGINT, onStopSignal);
        std::signal(SIGTERM, onStopSignal);
        return true;
    }();
    static_cast<void>(s_listening);
    return g_stopRequested.load(std::memory_order_relaxed);
}

} // namespace Vkm::Platform
