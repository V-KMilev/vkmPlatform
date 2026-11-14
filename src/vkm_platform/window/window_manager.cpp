#define VKM_LOG_CATEGORY "WINDOW"

#include "vkm_platform/window/window_manager.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <stdexcept>

#include "vkm_platform/glfw_api.h"

#include "vkm_platform/input/input_handle.h"
#include "vkm_platform/profile.h"
#include "vkm_platform/window/frame_limiter.h"

#include "vkm_log/logger.h"

namespace Vkm::Platform {

namespace {

constexpr int DEFAULT_WINDOW_WIDTH  = 1920;
constexpr int DEFAULT_WINDOW_HEIGHT = 1080;

// GLFW's per-thread error state, so a failed startup says what actually failed.
const char* glfwErrorDescription() {
    const char* description = nullptr;
    glfwGetError(&description);
    return description ? description : "no description";
}

GLFWmonitor* getCurrentMonitor(GLFWwindow* window) {
    int windowX, windowY, windowWidth, windowHeight;
    glfwGetWindowPos(window, &windowX, &windowY);
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    int monitorCount;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

    GLFWmonitor* bestMonitor = nullptr;
    int bestOverlap = 0;

    for (int i = 0; i < monitorCount; i++) {
        // A display unplugged since the last poll still has a handle, but its mode is null.
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode) continue;

        int monitorX, monitorY;
        glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);

        const int overlapRight  = std::min(windowX + windowWidth, monitorX + mode->width);
        const int overlapBottom = std::min(windowY + windowHeight, monitorY + mode->height);
        int overlapX = std::max(0, overlapRight - std::max(windowX, monitorX));
        int overlapY = std::max(0, overlapBottom - std::max(windowY, monitorY));
        int overlap  = overlapX * overlapY;

        if (overlap > bestOverlap) {
            bestOverlap = overlap;
            bestMonitor = monitors[i];
        }
    }

    return bestMonitor ? bestMonitor : glfwGetPrimaryMonitor();
}

/**
 * @brief The video mode of the monitor the window is on, or null if there isn't one.
 *
 * Found by overlap, as glfwGetWindowMonitor names one only when fullscreen. Null-safe for a
 * program with no window or a machine with no output, since GLFW asserts on a null handle.
 */
const GLFWvidmode* currentVideoMode(GLFWwindow* window) {
    if (!window) return nullptr;
    GLFWmonitor* monitor = getCurrentMonitor(window);
    return monitor ? glfwGetVideoMode(monitor) : nullptr;
}
} // namespace

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() {
    // The surface first: what it holds of the window goes before the window.
    m_surface.reset();
    if (m_windowHandle) {
        glfwDestroyWindow(m_windowHandle);
        m_windowHandle = nullptr;
    }
    glfwTerminate();

    LOG_TRACE("Destructed Window '%s'", m_title.c_str());
}

void WindowManager::createWindow(const std::string& title, std::unique_ptr<WindowSurface> surface) {
    m_title = title;
    if (!surface) throw std::runtime_error("A window needs the surface a renderer draws through");
    m_surface = std::move(surface);

    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW: %s", glfwErrorDescription());
        throw std::runtime_error("Failed to initialize GLFW");
    }

    m_surface->hint();

    // Null monitor = windowed mode.
    m_windowHandle = glfwCreateWindow(
        DEFAULT_WINDOW_WIDTH,
        DEFAULT_WINDOW_HEIGHT,
        m_title.c_str(),
        nullptr,
        nullptr
    );

    if (!m_windowHandle) {
        // Usually a surface the driver cannot give, but also no display or a missing platform;
        // GLFW's text tells.
        LOG_ERROR(
            "Failed to create window (requested %s): %s",
            m_surface->describe().c_str(),
            glfwErrorDescription()
        );
        throw std::runtime_error("Failed to create window");
    }

    // Its context current and its API loaded, with vsync off until setVSync.
    m_surface->attach(m_windowHandle);

    glfwGetFramebufferSize(m_windowHandle, &m_width, &m_height);

    LOG_TRACE("Constructed Window '%s'", m_title.c_str());

    // Set before any callback that reads it is registered.
    glfwSetWindowUserPointer(m_windowHandle, this);

    // Framebuffer, not window, size: this also catches DPI changes.
    glfwSetFramebufferSizeCallback(m_windowHandle, [](GLFWwindow* w, int width, int height) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->setSize(width, height);
        }
    });

    // Kept by callback, not asked per frame: on X11 each question is a server round trip.
    glfwGetWindowSize(m_windowHandle, &m_windowWidth, nullptr);
    glfwSetWindowSizeCallback(m_windowHandle, [](GLFWwindow* w, int width, int) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->m_windowWidth = width;
        }
    });

    // The titlebar X, into the same field requestClose() writes.
    glfwSetWindowCloseCallback(m_windowHandle, [](GLFWwindow* w) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->requestClose();
        }
    });

    // A code no key has (GLFW_KEY_UNKNOWN) is dropped here.
    glfwSetKeyCallback(m_windowHandle, [](GLFWwindow* w, int code, int, int action, int) {
        auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
        const std::optional<Key> key = keyFromGlfw(code);
        if (manager && key) {
            const bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
            manager->getInputHandle().onKeyEvent(*key, pressed);
        }
    });

    // Characters with layout and modifiers applied, for text fields.
    glfwSetCharCallback(m_windowHandle, [](GLFWwindow* w, unsigned int codepoint) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->getInputHandle().onText(static_cast<char32_t>(codepoint));
        }
    });

    // An event, not a poll, so a click pressed and released within one poll is still seen.
    glfwSetMouseButtonCallback(m_windowHandle, [](GLFWwindow* w, int code, int action, int) {
        auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
        const std::optional<MouseButton> button = mouseButtonFromGlfw(code);
        if (manager && button) manager->getInputHandle().setButton(*button, action == GLFW_PRESS);
    });

    // Horizontal scroll is unused.
    glfwSetScrollCallback(m_windowHandle, [](GLFWwindow* w, double, double yOffset) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->getInputHandle().addScroll(yOffset);
        }
    });

    glfwSetCursorPosCallback(m_windowHandle, [](GLFWwindow* w, double x, double y) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->m_cursorX = x;
            manager->m_cursorY = y;
        }
    });

    glfwSetDropCallback(m_windowHandle, [](GLFWwindow* w, int count, const char** paths) {
        auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
        if (!manager || !manager->m_keepDropped) return;
        for (int i = 0; i < count; ++i) manager->m_dropped.emplace_back(paths[i]);
    });

    // Leaving reports no position; the last one reported is still inside the window.
    glfwSetCursorEnterCallback(m_windowHandle, [](GLFWwindow* w, int entered) {
        auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
        if (manager && !entered) glfwGetCursorPos(w, &manager->m_cursorX, &manager->m_cursorY);
    });

    // Seeds the delta's origin, or the first updateInput() reports the whole distance to the
    // pointer. updateInput overwrites the seed's own delta before anything reads it.
    glfwGetCursorPos(m_windowHandle, &m_cursorX, &m_cursorY);
    m_inputHandle.moveTo(m_cursorX, m_cursorY);

    LOG_INFO("Created window '%s' (%dx%d, refresh %dHz)", title.c_str(), m_width, m_height, getRefreshRate());
}

void WindowManager::setIcon(const uint8_t* rgba, int width, int height) {
    if (!m_windowHandle) return;
    // GLFW copies the pixels, and reads them through a non-const pointer it never writes.
    GLFWimage image{width, height, const_cast<unsigned char*>(rgba)};
    glfwSetWindowIcon(m_windowHandle, 1, &image);
    LOG_INFO("Window icon set (%dx%d)", width, height);
}

bool WindowManager::shouldClose() const {
    return m_closeRequested;
}

void WindowManager::requestClose() {
    LOG_INFO("Close requested");
    m_closeRequested = true;

    // Mirrored for GLFW readers; the loop reads the field, so a program with no window can stop.
    if (m_windowHandle) glfwSetWindowShouldClose(m_windowHandle, GLFW_TRUE);
}

void WindowManager::cancelClose() {
    LOG_VERBOSE("Pending close canceled");
    m_closeRequested = false;
    if (m_windowHandle) glfwSetWindowShouldClose(m_windowHandle, GLFW_FALSE);
}

void WindowManager::setTitle(const std::string& title) {
    // Kept: the shutdown trace names m_title.
    m_title = title;
    if (m_windowHandle) glfwSetWindowTitle(m_windowHandle, m_title.c_str());
}

void WindowManager::swapBuffers() {
    {
        // A CPU outrunning the GPU blocks here: a fat zone means a GPU-bound frame.
        VKM_PLATFORM_ZONE("SwapBuffers");
        if (m_windowHandle) m_surface->present(m_windowHandle);
    }

    {
        // Its own zone, so a cap sleep is never mistaken for a swap stall.
        VKM_PLATFORM_ZONE("FrameLimiter");
        m_frameLimiter.endFrame();
    }
}

void WindowManager::updateMode(WindowMode windowMode) {
    // Headless: no window is not a monitor failure.
    if (!m_windowHandle) return;

    GLFWmonitor* monitor = getCurrentMonitor(m_windowHandle);
    if (!monitor) {
        LOG_ERROR("Failed to get current monitor");
        return;
    }

    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) {
        LOG_ERROR("Failed to get video mode for current monitor");
        return;
    }

    // Windowed is a centered 75% rect: a full-monitor one would look like borderless fullscreen.
    int targetX       = 0;
    int targetY       = 0;
    int targetW       = mode->width;
    int targetH       = mode->height;
    int targetRefresh = mode->refreshRate;

    switch (windowMode) {
        case WindowMode::Fullscreen:
            break;
        case WindowMode::Windowed: {
            int monitorX = 0;
            int monitorY = 0;
            glfwGetMonitorPos(monitor, &monitorX, &monitorY);
            monitor       = nullptr;
            targetW       = static_cast<int>(mode->width  * 0.75);
            targetH       = static_cast<int>(mode->height * 0.75);
            targetX       = monitorX + (mode->width  - targetW) / 2;
            targetY       = monitorY + (mode->height - targetH) / 2;
            targetRefresh = 0;  // ignored for windowed mode
            break;
        }
        default:
            LOG_ERROR("Invalid window mode: %d", static_cast<int>(windowMode));
            return;
    }

    glfwSetWindowMonitor(m_windowHandle, monitor, targetX, targetY, targetW, targetH, targetRefresh);

    m_windowMode = windowMode;

    const char* named = windowMode == WindowMode::Fullscreen ? "Fullscreen" : "Windowed";
    LOG_INFO("Mode -> %s (%dx%d @ %dHz)", named, targetW, targetH, targetRefresh);
}

std::vector<WindowSize> WindowManager::renderResolutions() const {
    if (!m_windowHandle) return {};
    GLFWmonitor* monitor   = getCurrentMonitor(m_windowHandle);
    const GLFWvidmode* own = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!own) return {};
    int count = 0;
    const GLFWvidmode* modes = glfwGetVideoModes(monitor, &count);
    std::vector<WindowSize> sizes;
    for (int i = 0; i < count; ++i) sizes.push_back({modes[i].width, modes[i].height});
    return fittingWindowSizes(sizes, {own->width, own->height});
}

namespace {

constexpr int GLFW_KEYS[] = {
    GLFW_KEY_SPACE, GLFW_KEY_APOSTROPHE, GLFW_KEY_COMMA, GLFW_KEY_MINUS, GLFW_KEY_PERIOD, GLFW_KEY_SLASH,
    GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
    GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9,
    GLFW_KEY_SEMICOLON, GLFW_KEY_EQUAL,
    GLFW_KEY_A, GLFW_KEY_B, GLFW_KEY_C, GLFW_KEY_D, GLFW_KEY_E, GLFW_KEY_F, GLFW_KEY_G,
    GLFW_KEY_H, GLFW_KEY_I, GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L, GLFW_KEY_M, GLFW_KEY_N,
    GLFW_KEY_O, GLFW_KEY_P, GLFW_KEY_Q, GLFW_KEY_R, GLFW_KEY_S, GLFW_KEY_T, GLFW_KEY_U,
    GLFW_KEY_V, GLFW_KEY_W, GLFW_KEY_X, GLFW_KEY_Y, GLFW_KEY_Z,
    GLFW_KEY_LEFT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_GRAVE_ACCENT,
    GLFW_KEY_WORLD_1, GLFW_KEY_WORLD_2,
    GLFW_KEY_ESCAPE, GLFW_KEY_ENTER, GLFW_KEY_TAB, GLFW_KEY_BACKSPACE, GLFW_KEY_INSERT, GLFW_KEY_DELETE,
    GLFW_KEY_RIGHT, GLFW_KEY_LEFT, GLFW_KEY_DOWN, GLFW_KEY_UP, GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN,
    GLFW_KEY_HOME, GLFW_KEY_END,
    GLFW_KEY_CAPS_LOCK, GLFW_KEY_SCROLL_LOCK, GLFW_KEY_NUM_LOCK, GLFW_KEY_PRINT_SCREEN, GLFW_KEY_PAUSE,
    GLFW_KEY_F1, GLFW_KEY_F2, GLFW_KEY_F3, GLFW_KEY_F4, GLFW_KEY_F5, GLFW_KEY_F6, GLFW_KEY_F7,
    GLFW_KEY_F8, GLFW_KEY_F9, GLFW_KEY_F10, GLFW_KEY_F11, GLFW_KEY_F12, GLFW_KEY_F13, GLFW_KEY_F14,
    GLFW_KEY_F15, GLFW_KEY_F16, GLFW_KEY_F17, GLFW_KEY_F18, GLFW_KEY_F19, GLFW_KEY_F20, GLFW_KEY_F21,
    GLFW_KEY_F22, GLFW_KEY_F23, GLFW_KEY_F24, GLFW_KEY_F25,
    GLFW_KEY_KP_0, GLFW_KEY_KP_1, GLFW_KEY_KP_2, GLFW_KEY_KP_3, GLFW_KEY_KP_4,
    GLFW_KEY_KP_5, GLFW_KEY_KP_6, GLFW_KEY_KP_7, GLFW_KEY_KP_8, GLFW_KEY_KP_9,
    GLFW_KEY_KP_DECIMAL, GLFW_KEY_KP_DIVIDE, GLFW_KEY_KP_MULTIPLY, GLFW_KEY_KP_SUBTRACT, GLFW_KEY_KP_ADD,
    GLFW_KEY_KP_ENTER, GLFW_KEY_KP_EQUAL,
    GLFW_KEY_LEFT_SHIFT, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_ALT, GLFW_KEY_LEFT_SUPER,
    GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_RIGHT_CONTROL, GLFW_KEY_RIGHT_ALT, GLFW_KEY_RIGHT_SUPER, GLFW_KEY_MENU,
};  ///< Each key's GLFW code, in Key's order.

constexpr int GLFW_BUTTONS[] = {
    GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_MOUSE_BUTTON_4,
    GLFW_MOUSE_BUTTON_5, GLFW_MOUSE_BUTTON_6, GLFW_MOUSE_BUTTON_7, GLFW_MOUSE_BUTTON_8,
};  ///< Each button's GLFW code, in MouseButton's order.

template<size_t N>
constexpr bool rising(const int (&codes)[N]) {
    for (size_t i = 1; i < N; ++i) {
        if (codes[i] <= codes[i - 1]) return false;
    }
    return true;
}

constexpr size_t KEYS    = static_cast<size_t>(Key::Count);
constexpr size_t BUTTONS = static_cast<size_t>(MouseButton::Count);

static_assert(std::size(GLFW_KEYS) == KEYS, "a code per key");
static_assert(std::size(GLFW_BUTTONS) == BUTTONS, "a code per button");
// Rising, as the enums are declared in GLFW's order, so a code is found by bisection.
static_assert(rising(GLFW_KEYS) && rising(GLFW_BUTTONS), "keys and buttons in GLFW's order");
// A GLFW with a key or button past these needs a name for it.
static_assert(GLFW_KEYS[KEYS - 1] == GLFW_KEY_LAST, "a key per GLFW key");
static_assert(GLFW_BUTTONS[BUTTONS - 1] == GLFW_MOUSE_BUTTON_LAST, "a button per GLFW button");

/// The control whose code is @p code in @p codes, a table above; nothing when none is.
template<typename Control, size_t N>
std::optional<Control> fromGlfw(const int (&codes)[N], int code) {
    const int* found = std::lower_bound(std::begin(codes), std::end(codes), code);
    if (found == std::end(codes) || *found != code) return std::nullopt;
    return static_cast<Control>(found - std::begin(codes));
}

} // namespace

std::optional<Key> keyFromGlfw(int code) {
    return fromGlfw<Key>(GLFW_KEYS, code);
}

std::optional<MouseButton> mouseButtonFromGlfw(int code) {
    return fromGlfw<MouseButton>(GLFW_BUTTONS, code);
}

std::vector<WindowSize> fittingWindowSizes(const std::vector<WindowSize>& modes, WindowSize monitor) {
    std::vector<WindowSize> fit;
    for (const WindowSize& size : modes) {
        const bool fits = size.width > 0 && size.height > 0 && size.width <= monitor.width
            && size.height <= monitor.height;
        if (fits && std::find(fit.begin(), fit.end(), size) == fit.end()) fit.push_back(size);
    }
    std::sort(fit.begin(), fit.end(), [](const WindowSize& a, const WindowSize& b) {
        return a.width * a.height > b.width * b.height
            || (a.width * a.height == b.width * b.height && a.width > b.width);
    });
    return fit;
}

void WindowManager::updateInput() {
    if (!m_windowHandle) return;

    // The scroll callback accumulates.
    m_inputHandle.resetScrollDelta();

    glfwPollEvents();

    // Once per frame: the handle's delta is the frame's movement.
    m_inputHandle.moveTo(m_cursorX, m_cursorY);
}

void WindowManager::pumpEvents() {
    if (m_windowHandle) glfwPollEvents();
}

bool WindowManager::beginFrame() {
    m_frameLimiter.beginFrame();

    return !shouldClose();
}

void WindowManager::setVSync(bool enabled) {
    // Independent of the software FPS cap.
    if (!m_windowHandle) return;

    m_surface->setVSync(m_windowHandle, enabled);
    m_vsync = enabled;
    LOG_INFO("VSync %s", enabled ? "ON" : "OFF");
}

void WindowManager::setFramerate(int framerate) {
    m_frameLimiter.setTargetFramerate(framerate);
    if (framerate > 0) {
        LOG_INFO("FPS cap = %d", framerate);
    } else {
        LOG_INFO("FPS cap removed (unlimited)");
    }
}

void WindowManager::setCursorMode(CursorMode mode) {
    m_cursorMode = mode;
    applyCursor();
}

void WindowManager::setCursorFree(bool free) {
    if (free == m_cursorFree) return;
    m_cursorFree = free;
    applyCursor();
}

void WindowManager::applyCursor() {
    if (!m_windowHandle) return;

    const CursorMode mode = m_cursorFree ? CursorMode::Normal : m_cursorMode;
    auto glfwMode = GLFW_CURSOR_NORMAL;
    switch (mode) {
        case CursorMode::Normal:
            glfwMode = GLFW_CURSOR_NORMAL;
            break;
        case CursorMode::Hidden:
            glfwMode = GLFW_CURSOR_HIDDEN;
            break;
        case CursorMode::Disabled:
            glfwMode = GLFW_CURSOR_DISABLED;
            break;
        case CursorMode::Captured:
            glfwMode = GLFW_CURSOR_CAPTURED;
            break;
    }
    glfwSetInputMode(m_windowHandle, GLFW_CURSOR, glfwMode);

    // Entering and leaving Disabled warps the cursor, which is no movement of the user's.
    glfwGetCursorPos(m_windowHandle, &m_cursorX, &m_cursorY);
    m_inputHandle.warpTo(m_cursorX, m_cursorY);
}

size_t WindowManager::getWidth() const {
    if (!m_windowHandle) return 0;
    return m_width;
}

size_t WindowManager::getHeight() const {
    if (!m_windowHandle) return 0;
    return m_height;
}

void WindowManager::setSize(int width, int height) {
    m_width  = width;
    m_height = height;
}

int WindowManager::displayHeight() const {
    const GLFWvidmode* mode = currentVideoMode(m_windowHandle);
    return mode ? mode->height : 0;
}

int WindowManager::getRefreshRate() const {
    const GLFWvidmode* mode = currentVideoMode(m_windowHandle);
    if (!mode) {
        LOG_ERROR("Failed to get video mode for current monitor");
        return 0;
    }
    return mode->refreshRate;
}

float WindowManager::framebufferScale() const {
    // Silent: asked every frame, and 1.0f is what a caller falls back to anyway.
    if (!m_windowHandle) return 1.0f;

    if (m_windowWidth <= 0) return 1.0f;

    return static_cast<float>(m_width) / static_cast<float>(m_windowWidth);
}

GLFWwindow* WindowManager::getWindowContext() const {
    return m_windowHandle;
}

} // namespace Vkm::Platform
