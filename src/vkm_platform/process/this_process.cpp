#define VKM_LOG_CATEGORY "PLATFORM"

#include "vkm_platform/process/this_process.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>

#if !defined(_WIN32)
    #include <climits>
    #include <unistd.h>
#endif

#include "vkm_log/logger.h"

#include "vkm_platform/process/stop_event.h"
#include "vkm_platform/windows_api.h"

namespace Vkm::Platform {

namespace {

// Set from a signal handler, so lock-free, and the handler touches nothing else.
std::atomic<bool> g_stopRequested{false};

extern "C" void onStopSignal(int) { g_stopRequested.store(true, std::memory_order_relaxed); }

#if defined(_WIN32)
// Ctrl+C, Ctrl+Break and the console closing, on a thread the system starts. A close ends the
// process when the handler returns, so that one holds until the process leaves on its own, or
// the system's few seconds run out.
BOOL WINAPI onConsoleEvent(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && event != CTRL_CLOSE_EVENT) return FALSE;
    g_stopRequested.store(true, std::memory_order_relaxed);
    if (event == CTRL_CLOSE_EVENT) Sleep(INFINITE);
    return TRUE;
}

// The stop event set (ChildProcess::stop), on a thread of the system's pool.
void CALLBACK onStopEvent(void*, BOOLEAN) { g_stopRequested.store(true, std::memory_order_relaxed); }
#endif

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
#if defined(_WIN32)
        SetConsoleCtrlHandler(onConsoleEvent, TRUE);
        // Open as long as the process runs: its name is how a parent finds it.
        const std::wstring name = Detail::stopEventName(GetCurrentProcessId());
        const HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        HANDLE wait = nullptr;
        const bool heard = stop != nullptr
            && RegisterWaitForSingleObject(&wait, stop, onStopEvent, nullptr, INFINITE, WT_EXECUTEONLYONCE);
        if (!heard) {
            LOG_WARNING(
                "Cannot hear ChildProcess::stop (error %lu): it will end this process without asking",
                GetLastError()
            );
        }
#endif
        return true;
    }();
    static_cast<void>(s_listening);
    return g_stopRequested.load(std::memory_order_relaxed);
}

} // namespace Vkm::Platform
