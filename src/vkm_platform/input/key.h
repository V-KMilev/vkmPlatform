#pragma once

#include <cstdint>

namespace Vkm::Platform {

/**
 * @brief A keyboard key, by where it sits: the one a US layout prints there, whatever the layout.
 *
 * Declared in GLFW's code order, so a walk over the keys meets them as a walk over its codes does.
 */
enum class Key : uint8_t {
    Space, Apostrophe, Comma, Minus, Period, Slash,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    Semicolon, Equal,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket, Backslash, RightBracket, GraveAccent,
    World1, World2,  ///< The two keys a US keyboard does not have.
    Escape, Enter, Tab, Backspace, Insert, Delete, Right, Left, Down, Up, PageUp, PageDown, Home, End,
    CapsLock, ScrollLock, NumLock, PrintScreen, Pause,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13,
    F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25,
    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadDecimal, KeypadDivide, KeypadMultiply, KeypadSubtract, KeypadAdd, KeypadEnter, KeypadEqual,
    LeftShift, LeftControl, LeftAlt, LeftSuper, RightShift, RightControl, RightAlt, RightSuper, Menu,
    Count  ///< Sentinel; keep last.
};

/**
 * @brief A mouse button: the three every mouse has, then the rest by number.
 */
enum class MouseButton : uint8_t {
    Left, Right, Middle, Button4, Button5, Button6, Button7, Button8,
    Count  ///< Sentinel; keep last.
};

} // namespace Vkm::Platform
