#include "keypad_io.h"
#include "desktop_platform.h"

#include <cstring>

KeypadIO::KeypadIO(const uint8_t *keymap,
                   const uint8_t *rowPins,
                   const uint8_t *colPins,
                   size_t numRows,
                   size_t numCols)
    : keymap_(keymap), rowPins_(rowPins), colPins_(colPins),
      numRows_(numRows), numCols_(numCols), head_(0), tail_(0), count_(0) {
    memset(keyStates_, 0, sizeof(keyStates_));
}

bool KeypadIO::begin() {
    clear();
    return keymap_ != nullptr && numRows_ > 0 && numCols_ > 0 &&
           numRows_ * numCols_ <= sizeof(keyStates_);
}

void KeypadIO::tick() { desktop_pump_events(); }

void KeypadIO::inject(uint8_t key, uint8_t event) {
    int index = findKeyIndex(key);
    if (index < 0) return;

    const bool pressed = event == KEY_JUST_PRESSED;
    const bool was_pressed = (keyStates_[index] & STATE_PRESSED) != 0;
    if (pressed == was_pressed) return;

    keyStates_[index] &= static_cast<uint8_t>(~(STATE_JUST_PRESSED | STATE_JUST_RELEASED));
    if (pressed) {
        keyStates_[index] |= static_cast<uint8_t>(STATE_PRESSED | STATE_JUST_PRESSED);
    } else {
        keyStates_[index] &= static_cast<uint8_t>(~STATE_PRESSED);
        keyStates_[index] |= STATE_JUST_RELEASED;
    }

    keypadEvent e = {};
    e.bit.KEY = key;
    e.bit.EVENT = event;
    e.bit.ROW = static_cast<uint8_t>(index / static_cast<int>(numCols_));
    e.bit.COL = static_cast<uint8_t>(index % static_cast<int>(numCols_));
    pushEvent(e);
}

bool KeypadIO::justPressed(uint8_t key, bool clear_state) {
    int index = findKeyIndex(key);
    if (index < 0) return false;
    bool value = (keyStates_[index] & STATE_JUST_PRESSED) != 0;
    if (clear_state) keyStates_[index] &= static_cast<uint8_t>(~STATE_JUST_PRESSED);
    return value;
}

bool KeypadIO::justReleased(uint8_t key) {
    int index = findKeyIndex(key);
    if (index < 0) return false;
    bool value = (keyStates_[index] & STATE_JUST_RELEASED) != 0;
    keyStates_[index] &= static_cast<uint8_t>(~STATE_JUST_RELEASED);
    return value;
}

bool KeypadIO::isPressed(uint8_t key) const {
    int index = findKeyIndex(key);
    return index >= 0 && (keyStates_[index] & STATE_PRESSED) != 0;
}

bool KeypadIO::isReleased(uint8_t key) const { return !isPressed(key); }
int KeypadIO::available() const { return static_cast<int>(count_); }

keypadEvent KeypadIO::read() {
    keypadEvent event = {};
    if (count_ == 0) return event;
    event = buffer_[tail_];
    tail_ = (tail_ + 1) % BUFFER_SIZE;
    --count_;
    return event;
}

void KeypadIO::clear() {
    head_ = tail_ = count_ = 0;
    memset(keyStates_, 0, sizeof(keyStates_));
}

void KeypadIO::pushEvent(const keypadEvent &event) {
    if (count_ >= BUFFER_SIZE) {
        tail_ = (tail_ + 1) % BUFFER_SIZE;
        --count_;
    }
    buffer_[head_] = event;
    head_ = (head_ + 1) % BUFFER_SIZE;
    ++count_;
}

int KeypadIO::findKeyIndex(uint8_t key) const {
    for (size_t i = 0; i < numRows_ * numCols_; ++i) {
        if (keymap_[i] == key) return static_cast<int>(i);
    }
    return -1;
}
