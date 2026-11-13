// A program for the suite to start and stop. As a host's frame does, it asks each few milliseconds
// whether it should stop, and says so when it is asked; given "deaf" it never asks. Either way it
// says it has started, and ends by itself after ten seconds. Given "leave" and a file, it first
// starts a copy of itself, given "late" and the file, that hears no asking and, a second on, writes
// "finished" there and ends.

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

#include "vkm_platform/process/this_process.h"

namespace {

/// Start @p self given "late" and @p file, in this program's job or group, sharing its output.
void leave(char* self, char* file) {
#if defined(_WIN32)
    std::string line = std::string("\"") + self + "\" late \"" + file + "\"";
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info)) {
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
    }
#else
    // Ignored from before it runs, as an ignored signal stays so through exec; stopRequested() below
    // listens for this program alone.
    std::signal(SIGTERM, SIG_IGN);
    char  late[] = "late";
    char* args[] = {self, late, file, nullptr};
    if (fork() == 0) {
        execv(self, args);
        _exit(127);
    }
#endif
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2 && std::strcmp(argv[1], "late") == 0) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::ofstream(argv[2]) << "finished";
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[1], "leave") == 0) leave(argv[0], argv[2]);

    const bool listens = argc < 2 || std::strcmp(argv[1], "deaf") != 0;
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
