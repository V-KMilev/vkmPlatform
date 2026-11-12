#include "vkm_platform/input/input_handle.h"

#include <algorithm>
#include <iterator>

namespace Vkm::Platform {

namespace {

size_t at(Key key) { return static_cast<size_t>(key); }
size_t at(MouseButton button) { return static_cast<size_t>(button); }

} // namespace

void InputHandle::onKeyEvent(Key key, bool pressed) {
    // A repeat is a press of a key already down: typed again, not struck.
    if (pressed && !m_keyState[at(key)]) m_keyStruck[at(key)] = true;
    if (pressed)                         m_keyTyped[at(key)]  = true;
    m_keyState[at(key)] = pressed;
}

void InputHandle::onText(char32_t codepoint) {
    m_text.push_back(codepoint);
}

bool InputHandle::isKeyPressed(Key key) const {
    return m_keyState[at(key)];
}

bool InputHandle::wasKeyStruck(Key key) const {
    return m_keyStruck[at(key)];
}

bool InputHandle::wasKeyRestruck(Key key) const {
    return m_keyStruck[at(key)] && m_keyHeld[at(key)];
}

bool InputHandle::wasKeyTyped(Key key) const {
    return m_keyTyped[at(key)];
}

void InputHandle::clearStrikes() {
    std::fill(std::begin(m_keyStruck), std::end(m_keyStruck), false);
    std::fill(std::begin(m_keyTyped), std::end(m_keyTyped), false);
    std::fill(std::begin(m_buttonStruck), std::end(m_buttonStruck), false);
    std::copy(std::begin(m_keyState), std::end(m_keyState), std::begin(m_keyHeld));
    std::copy(std::begin(m_buttonState), std::end(m_buttonState), std::begin(m_buttonHeld));
    m_text.clear();
}

void InputHandle::moveTo(double x, double y) {
    m_deltaX = x - m_x;
    m_deltaY = y - m_y;
    m_x      = x;
    m_y      = y;
}

void InputHandle::warpTo(double x, double y) {
    m_x = x;
    m_y = y;
}

void InputHandle::setButton(MouseButton button, bool pressed) {
    if (pressed && !m_buttonState[at(button)]) m_buttonStruck[at(button)] = true;
    m_buttonState[at(button)] = pressed;
}

bool InputHandle::wasButtonRestruck(MouseButton button) const {
    return m_buttonStruck[at(button)] && m_buttonHeld[at(button)];
}

bool InputHandle::isButtonPressed(MouseButton button) const {
    return m_buttonState[at(button)];
}

bool InputHandle::wasButtonStruck(MouseButton button) const {
    return m_buttonStruck[at(button)];
}

void InputHandle::addScroll(double yOffset) {
    m_scrollY += yOffset;
}

void InputHandle::resetScrollDelta() {
    m_scrollY = 0.0;
}

} // namespace Vkm::Platform
