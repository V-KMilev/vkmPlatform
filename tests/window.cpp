// The window manager with no window: what a program with no window, or a test, relies on.

#include <sstream>
#include <string>
#include <vector>

#include "vkm_log/logger.h"

#include "vkm_platform/window/window_manager.h"

#include "harness/check.h"
#include "harness/scratch.h"

using Vkm::Platform::CursorMode;
using Vkm::Platform::WindowManager;
using Vkm::Platform::WindowMode;
using Vkm::Platform::WindowSize;

TEST("a window never created takes every request and performs none: its reports are zero and its scale one") {
    WindowManager window;
    CHECK(!window.isOpen());
    window.setTitle("headless");
    window.setVSync(true);
    window.setCursorMode(CursorMode::Disabled);
    window.setCursorFree(true);
    window.updateMode(WindowMode::Fullscreen);
    window.setIcon(nullptr, 0, 0);
    window.updateInput();
    window.pumpEvents();
    window.swapBuffers();

    CHECK(!window.isOpen());
    CHECK(window.getWidth() == 0u);
    CHECK(window.getHeight() == 0u);
    CHECK(window.displayHeight() == 0);
    CHECK(window.framebufferScale() == 1.0f);
    CHECK(window.renderResolutions().empty());
    CHECK(window.getWindowContext() == nullptr);
    CHECK(!window.vsync());

    // A loop over no window still stops when asked.
    CHECK(window.beginFrame());
    window.requestClose();
    CHECK(window.shouldClose());
    CHECK(!window.beginFrame());
    window.cancelClose();
    CHECK(window.beginFrame());
}

TEST("the window sizes offered are those that fit the monitor, each once, largest first, ties by width") {
    const std::vector<WindowSize> modes = {
        {1280, 720}, {1920, 1080}, {2560, 1440}, {1280, 720}, {1080, 1920}, {800, 600},  {0, 600},
        {1024, 768}, {1440, 900},  {900, 1440},  {1920, 1200}, {1200, 1200}, {1600, 900},
    };
    const std::vector<WindowSize> fit = Vkm::Platform::fittingWindowSizes(modes, {1920, 1200});
    // 1600 by 900 and 1200 by 1200 are the same area: the wider first.
    const std::vector<WindowSize> expected = {
        {1920, 1200}, {1920, 1080}, {1600, 900}, {1200, 1200}, {1440, 900}, {1280, 720}, {1024, 768},
        {800, 600},
    };
    CHECK(fit.size() == expected.size());
    CHECK(fit == expected);
    CHECK(Vkm::Platform::fittingWindowSizes({}, {1920, 1080}).empty());
}

TEST("a line the library logs carries VKM-PLATFORM and its category") {
    WindowManager window;
    window.setFramerate(144);
    window.setFramerate(0);
    CHECK(window.framerate() == 0);

    std::istringstream log(Vkm::Test::logText());
    int lines = 0;
    for (std::string text; std::getline(log, text);) {
        const auto line = Vkm::Log::parseLine(text);
        REQUIRE(line.has_value());
        if (line->message.find("FPS cap") == std::string_view::npos) continue;
        ++lines;
        CHECK(line->suffix == "VKM-PLATFORM");
        CHECK(line->category == "WINDOW");
    }
    CHECK(lines == 2);
}
