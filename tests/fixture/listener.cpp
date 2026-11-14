// A program for the suite to start and stop. As a host's frame does, it asks each few milliseconds
// whether it should stop, and says so when it is asked; given "deaf" it never asks. Either way it
// says it has started, and ends by itself after ten seconds. Given "leave" and a file, it first
// starts a copy of itself, given "late" and the file, that hears no asking and, a second on, writes
// "finished" there and ends. Given "flood" it writes without a pause, ten seconds long; given "spill"
// it starts a copy of itself given "flood" and, a moment on, says it is exiting and exits with 4.
// Given "load" and a library, it first loads a copy of it and never asks, so stop() ends it with the
// copy loaded, as a crash would.

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#if defined(_WIN32)
    #include "vkm_platform/windows_api.h"
#else
    #include <unistd.h>
#endif

#include "vkm_platform/process/dynamic_library.h"
#include "vkm_platform/process/this_process.h"

namespace {

/// Start @p self given @p mode and, unless null, @p file, in this program's job or group, sharing its
/// output.
void startCopy(char* self, const char* mode, char* file) {
#if defined(_WIN32)
    std::string line = std::string("\"") + self + "\" " + mode;
    if (file) line += std::string(" \"") + file + "\"";
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info)) {
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
    }
#else
    char* args[] = {self, const_cast<char*>(mode), file, nullptr};
    if (fork() == 0) {
        execv(self, args);
        _exit(127);
    }
#endif
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "flood") == 0) {
        static const std::string block(65536, 'x');
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < until) std::fwrite(block.data(), 1, block.size(), stdout);
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[1], "late") == 0) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::ofstream(argv[2]) << "finished";
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "spill") == 0) {
        startCopy(argv[0], "flood", nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::printf("exiting\n");
        return 4;
    }
    if (argc > 2 && std::strcmp(argv[1], "leave") == 0) {
#if !defined(_WIN32)
        // Ignored from before the copy runs, as an ignored signal stays so through exec; stopRequested()
        // below listens for this program alone.
        std::signal(SIGTERM, SIG_IGN);
#endif
        startCopy(argv[0], "late", argv[2]);
    }

    Vkm::Platform::DynamicLibrary library;
    const bool loads = argc > 2 && std::strcmp(argv[1], "load") == 0;
    if (loads && library.loadCopy(argv[2]).how != Vkm::Platform::DynamicLibrary::Loaded::Copied) return 6;

    const bool listens = argc < 2 || (std::strcmp(argv[1], "deaf") != 0 && !loads);
    if (listens) Vkm::Platform::stopRequested();
    std::printf("started\n");
    std::fflush(stdout);
    for (int i = 0; i < 1000; ++i) {
        if (listens && Vkm::Platform::stopRequested()) {
            std::printf("asked to stop\n");
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 5;
}
