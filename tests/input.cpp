// What the keys, buttons, pointer and wheel did between two polls.

#include <string>

#include "vkm_platform/input/input_handle.h"

#include "harness/check.h"

using Vkm::Platform::InputHandle;
using Vkm::Platform::Key;
using Vkm::Platform::MouseButton;

TEST("a key that goes down is struck, one held and repeating is typed and not struck, and one let go and pressed again in a poll is restruck") {
    InputHandle input;
    input.onKeyEvent(Key::W, true);
    CHECK(input.isKeyPressed(Key::W));
    CHECK(input.wasKeyStruck(Key::W));
    CHECK(input.wasKeyTyped(Key::W));
    CHECK(!input.wasKeyRestruck(Key::W));

    // Held into the next poll, then repeating.
    input.clearStrikes();
    CHECK(input.isKeyPressed(Key::W));
    CHECK(!input.wasKeyStruck(Key::W));
    input.onKeyEvent(Key::W, true);
    CHECK(!input.wasKeyStruck(Key::W));
    CHECK(input.wasKeyTyped(Key::W));

    // Let go and pressed again before the next poll: a fresh press of a key that was held.
    input.clearStrikes();
    input.onKeyEvent(Key::W, false);
    input.onKeyEvent(Key::W, true);
    CHECK(input.wasKeyStruck(Key::W));
    CHECK(input.wasKeyRestruck(Key::W));

    input.clearStrikes();
    input.onKeyEvent(Key::W, false);
    CHECK(!input.isKeyPressed(Key::W));
    CHECK(!input.wasKeyStruck(Key::W));
    CHECK(!input.wasKeyTyped(Key::W));
    CHECK(!input.isKeyPressed(Key::A));
}

TEST("a mouse button that goes down is struck, one held is not, and one let go and pressed again in a poll is restruck") {
    InputHandle input;
    input.setButton(MouseButton::Left, true);
    CHECK(input.isButtonPressed(MouseButton::Left));
    CHECK(input.wasButtonStruck(MouseButton::Left));
    CHECK(!input.wasButtonRestruck(MouseButton::Left));

    input.clearStrikes();
    CHECK(input.isButtonPressed(MouseButton::Left));
    CHECK(!input.wasButtonStruck(MouseButton::Left));

    input.setButton(MouseButton::Left, false);
    input.setButton(MouseButton::Left, true);
    CHECK(input.wasButtonStruck(MouseButton::Left));
    CHECK(input.wasButtonRestruck(MouseButton::Left));
    CHECK(!input.isButtonPressed(MouseButton::Right));
}

TEST("typed text arrives in order and is gone after clearStrikes") {
    InputHandle input;
    for (const char32_t c : std::u32string(U"hé\U0001F600!")) input.onText(c);
    CHECK(input.text() == std::u32string(U"hé\U0001F600!"));
    input.clearStrikes();
    CHECK(input.text().empty());
}

TEST("moveTo records the step, warpTo moves without one, and the next step measures from where it warped") {
    InputHandle input;
    input.moveTo(100.0, 50.0);
    input.moveTo(110.0, 45.0);
    CHECK(input.getX() == 110.0);
    CHECK(input.getDeltaX() == 10.0);
    CHECK(input.getDeltaY() == -5.0);

    input.warpTo(400.0, 300.0);
    CHECK(input.getX() == 400.0);
    CHECK(input.getY() == 300.0);
    CHECK(input.getDeltaX() == 10.0);
    input.moveTo(403.0, 302.0);
    CHECK(input.getDeltaX() == 3.0);
    CHECK(input.getDeltaY() == 2.0);
}

TEST("scroll adds up within a frame, and resetScrollDelta clears it") {
    InputHandle input;
    input.addScroll(1.0);
    input.addScroll(0.5);
    input.addScroll(-2.0);
    CHECK(input.getScrollY() == -0.5);
    input.resetScrollDelta();
    CHECK(input.getScrollY() == 0.0);
}
