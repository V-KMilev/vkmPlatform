// The frame cap: held to its rate, or off.

#include <chrono>

#include "vkm_platform/window/frame_limiter.h"

#include "harness/check.h"

namespace {

using Clock = std::chrono::steady_clock;

/// How long @p frames empty frames take under @p limiter.
Clock::duration framesTake(Vkm::Platform::FrameLimiter& limiter, int frames) {
    const Clock::time_point start = Clock::now();
    for (int i = 0; i < frames; ++i) {
        limiter.beginFrame();
        limiter.endFrame();
    }
    return Clock::now() - start;
}

} // namespace

TEST("a frame cap holds each frame to its rate, and 0 runs uncapped") {
    using std::chrono::milliseconds;
    Vkm::Platform::FrameLimiter limiter;
    limiter.setTargetFramerate(100);
    CHECK(limiter.targetFramerate() == 100);
    // Twenty frames at 100 a second: never sooner than the nineteen gaps between them, and late only by
    // what the machine's scheduler adds.
    const Clock::duration capped = framesTake(limiter, 20);
    CHECK(capped >= milliseconds(185));
    CHECK(capped < milliseconds(2000));

    limiter.setTargetFramerate(0);
    CHECK(limiter.targetFramerate() == 0);
    CHECK(framesTake(limiter, 20) < milliseconds(50));
    limiter.setTargetFramerate(-30);
    CHECK(limiter.targetFramerate() == 0);
}
