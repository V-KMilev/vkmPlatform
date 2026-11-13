#pragma once

#include <cstddef>
#include <string>

#include "vkm_platform/input/key.h"

namespace Vkm::Platform {

/**
 * @brief One frame of raw device state: keys and buttons down, cursor position and wheel turn.
 *
 * Source-agnostic, which keeps the windowing library out of the input path. Main thread only, so
 * unsynchronized. A key or button also records a strike since the last clearStrikes(): one poll
 * can deliver a press and its release together, which the level alone would miss.
 */
class InputHandle {
    public:
        InputHandle() = default;
        ~InputHandle() = default;

        InputHandle(const InputHandle& other) = delete;
        InputHandle& operator=(const InputHandle& other) = delete;

        InputHandle(InputHandle && other) = delete;
        InputHandle& operator=(InputHandle && other) = delete;

    public:
        /**
         * @brief Check whether the specified key is currently held down.
         *
         * @param key The key to query.
         * @return True if the key is currently pressed.
         */
        bool isKeyPressed(Key key) const;

        /**
         * @brief Whether the key went down since the last clearStrikes().
         *
         * True even when it is up again: a tap shorter than one poll.
         *
         * @param key The key to query.
         * @return True if a press arrived.
         */
        bool wasKeyStruck(Key key) const;

        /**
         * @brief Whether the key was let go and struck again since the last clearStrikes().
         *
         * Held at that clearStrikes() and struck since: a release and a press one sample
         * heard neither of, the level being down at both.
         *
         * @param key The key to query.
         * @return True if it was struck again.
         */
        bool wasKeyRestruck(Key key) const;

        /**
         * @brief Whether the key went down or auto-repeated since the last clearStrikes().
         *
         * @param key The key to query.
         * @return True if a press or a repeat arrived.
         */
        bool wasKeyTyped(Key key) const;

        /**
         * @brief Record a key's held state.
         *
         * A key going down is also recorded as struck.
         *
         * @param key     The key that changed.
         * @param pressed True when now held. A press of a key already held is a repeat:
         *                typed, not struck.
         */
        void onKeyEvent(Key key, bool pressed);

        /**
         * @brief Record a character the keyboard produced.
         *
         * @param codepoint A Unicode codepoint.
         */
        void onText(char32_t codepoint);

        /**
         * @brief The characters typed since the last clearStrikes(), in order, as UTF-32.
         *
         * Layout, Shift and dead keys already applied.
         *
         * @return The characters, oldest first.
         */
        const std::u32string& text() const { return m_text; }

        /**
         * @brief Move the cursor to @p x, @p y, recording the step it took.
         *
         * The delta is derived, so position and movement cannot disagree.
         *
         * @param x Cursor X in window pixels, from the left.
         * @param y Cursor Y in window pixels, from the top.
         */
        void moveTo(double x, double y);

        /**
         * @brief Put the cursor at @p x, @p y without a step: it was moved, not by the user.
         *
         * The next moveTo() measures from here.
         *
         * @param x Cursor X in window pixels, from the left.
         * @param y Cursor Y in window pixels, from the top.
         */
        void warpTo(double x, double y);

        /**
         * @brief Record a mouse button's held state.
         *
         * A button going down is also recorded as struck, as for a key.
         *
         * @param button  The button that changed.
         * @param pressed True while it is down.
         */
        void setButton(MouseButton button, bool pressed);

        /**
         * @brief Check if the specified mouse button is pressed.
         *
         * @param button The button to query.
         * @return True if button is pressed.
         */
        bool isButtonPressed(MouseButton button) const;

        /**
         * @brief Whether the button went down since the last clearStrikes(),
         *        even if it is up again.
         *
         * @param button The button to query.
         * @return True if a press arrived.
         */
        bool wasButtonStruck(MouseButton button) const;

        /**
         * @brief Whether the button was let go and struck again since the last clearStrikes().
         *
         * @param button The button to query.
         * @return True if it was struck again.
         */
        bool wasButtonRestruck(MouseButton button) const;

        /**
         * @brief Forget every key and button press, repeat and character recorded so far.
         *
         * What is held now is kept, for wasKeyRestruck() and wasButtonRestruck().
         */
        void clearStrikes();

        /**
         * @brief Zero the scroll delta before the window polls for this frame's.
         *
         * The scroll callback adds to it; without this one notch would turn every frame after.
         */
        void resetScrollDelta();

        /**
         * @brief Add to this frame's scroll, which resetScrollDelta() clears.
         *
         * Accumulated: a frame can carry several scroll events.
         *
         * @param yOffset Notches turned, positive away from the viewer.
         */
        void addScroll(double yOffset);

        /**
         * @brief The absolute cursor X position.
         *
         * @return Cursor X in window pixels, from the left.
         */
        double getX() const { return m_x; }

        /**
         * @brief The absolute cursor Y position.
         *
         * @return Cursor Y in window pixels, from the top.
         */
        double getY() const { return m_y; }

        /**
         * @brief The X movement the last moveTo recorded.
         *
         * @return Pixels moved along X.
         */
        double getDeltaX() const { return m_deltaX; }

        /**
         * @brief The Y movement the last moveTo recorded.
         *
         * @return Pixels moved along Y.
         */
        double getDeltaY() const { return m_deltaY; }

        /**
         * @brief The scroll accumulated since resetScrollDelta().
         *
         * @return Notches turned, positive away from the viewer.
         */
        double getScrollY() const { return m_scrollY; }

    private:
        static constexpr size_t KEYS    = static_cast<size_t>(Key::Count);
        static constexpr size_t BUTTONS = static_cast<size_t>(MouseButton::Count);

        bool m_keyState[KEYS]  = {};
        bool m_keyStruck[KEYS] = {};  ///< Went down since the last clearStrikes().
        bool m_keyTyped[KEYS]  = {};  ///< Went down or repeated since then.
        bool m_keyHeld[KEYS]   = {};  ///< Down at the last clearStrikes().

        std::u32string m_text;  ///< Capacity reused.

        bool m_buttonState[BUTTONS]  = {};
        bool m_buttonStruck[BUTTONS] = {};  ///< Went down since the last clearStrikes().
        bool m_buttonHeld[BUTTONS]   = {};  ///< Down at the last clearStrikes().

        double m_x      = 0.0;
        double m_y      = 0.0;
        double m_deltaX = 0.0;
        double m_deltaY = 0.0;

        double m_scrollY = 0.0;
};

} // namespace Vkm::Platform
