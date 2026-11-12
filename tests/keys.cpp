// The keys and buttons: GLFW's codes read as the platform's own.

#include <climits>
#include <optional>
#include <vector>

#include "vkm_platform/glfw_api.h"
#include "vkm_platform/input/key.h"
#include "vkm_platform/window/window_manager.h"

#include "harness/check.h"

using Vkm::Platform::Key;
using Vkm::Platform::MouseButton;
using Vkm::Platform::keyFromGlfw;
using Vkm::Platform::mouseButtonFromGlfw;

TEST("each key has exactly one GLFW code, and a code GLFW gives no key reads as none") {
    std::vector<int> codes(static_cast<size_t>(Key::Count), 0);
    for (int code = -1; code <= GLFW_KEY_LAST + 64; ++code) {
        if (const std::optional<Key> key = keyFromGlfw(code)) ++codes[static_cast<size_t>(*key)];
    }
    int unmapped = 0;
    int twice    = 0;
    for (const int count : codes) {
        unmapped += count == 0;
        twice += count > 1;
    }
    CHECK(unmapped == 0);
    CHECK(twice == 0);

    CHECK(keyFromGlfw(GLFW_KEY_SPACE) == std::optional<Key>(Key::Space));
    CHECK(keyFromGlfw(GLFW_KEY_A) == std::optional<Key>(Key::A));
    CHECK(keyFromGlfw(GLFW_KEY_WORLD_2) == std::optional<Key>(Key::World2));
    CHECK(keyFromGlfw(GLFW_KEY_ESCAPE) == std::optional<Key>(Key::Escape));
    CHECK(keyFromGlfw(GLFW_KEY_F25) == std::optional<Key>(Key::F25));
    CHECK(keyFromGlfw(GLFW_KEY_KP_EQUAL) == std::optional<Key>(Key::KeypadEqual));
    CHECK(keyFromGlfw(GLFW_KEY_MENU) == std::optional<Key>(Key::Menu));
    for (const int code : {GLFW_KEY_UNKNOWN, 0, 31, 33, GLFW_KEY_LAST + 1, 1000, INT_MIN, INT_MAX}) {
        CHECK(!keyFromGlfw(code).has_value());
    }
}

TEST("each mouse button has exactly one GLFW code, and a code GLFW gives no button reads as none") {
    CHECK(mouseButtonFromGlfw(GLFW_MOUSE_BUTTON_LEFT) == std::optional<MouseButton>(MouseButton::Left));
    CHECK(mouseButtonFromGlfw(GLFW_MOUSE_BUTTON_RIGHT) == std::optional<MouseButton>(MouseButton::Right));
    CHECK(mouseButtonFromGlfw(GLFW_MOUSE_BUTTON_MIDDLE) == std::optional<MouseButton>(MouseButton::Middle));
    CHECK(mouseButtonFromGlfw(GLFW_MOUSE_BUTTON_4) == std::optional<MouseButton>(MouseButton::Button4));
    CHECK(mouseButtonFromGlfw(GLFW_MOUSE_BUTTON_8) == std::optional<MouseButton>(MouseButton::Button8));
    std::vector<int> codes(static_cast<size_t>(MouseButton::Count), 0);
    for (int code = -1; code <= GLFW_MOUSE_BUTTON_LAST + 8; ++code) {
        if (const auto button = mouseButtonFromGlfw(code)) ++codes[static_cast<size_t>(*button)];
    }
    CHECK(codes == std::vector<int>(static_cast<size_t>(MouseButton::Count), 1));
    CHECK(!mouseButtonFromGlfw(-1).has_value());
    CHECK(!mouseButtonFromGlfw(GLFW_MOUSE_BUTTON_LAST + 1).has_value());
}

TEST("the keys are in GLFW's code order: walking the codes meets them as walking the keys does") {
    int next  = 0;
    int wrong = 0;
    for (int code = 0; code <= GLFW_KEY_LAST; ++code) {
        const std::optional<Key> key = keyFromGlfw(code);
        if (!key) continue;
        wrong += static_cast<int>(*key) != next;
        next = static_cast<int>(*key) + 1;
    }
    CHECK(wrong == 0);
    CHECK(next == static_cast<int>(Key::Count));
}
