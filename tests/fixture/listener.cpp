// A program for the suite to start and stop. As a host's frame does, it asks each few milliseconds
// whether it should stop, and says so when it is asked; given "deaf" it never asks. Either way it
// says it has started, and ends by itself after ten seconds.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "vkm_platform/process/this_process.h"

int main(int argc, char** argv) {
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
