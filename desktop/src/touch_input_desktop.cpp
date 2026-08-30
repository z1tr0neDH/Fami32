#include "touch_input.h"

#include <mutex>

namespace {
constexpr size_t kCapacity = 64;
touch_input_event_t events[kCapacity];
size_t head = 0;
size_t tail = 0;
size_t count = 0;
std::mutex event_mutex;
}

void touch_input_init() { touch_input_flush(); }

bool touch_input_push_event(uint8_t key, uint8_t event) {
    std::lock_guard<std::mutex> lock(event_mutex);
    if (count == kCapacity) {
        tail = (tail + 1) % kCapacity;
        --count;
    }
    events[head] = {key, event};
    head = (head + 1) % kCapacity;
    ++count;
    return true;
}

bool touch_input_pop_event(touch_input_event_t *event) {
    if (event == nullptr) return false;
    std::lock_guard<std::mutex> lock(event_mutex);
    if (count == 0) return false;
    *event = events[tail];
    tail = (tail + 1) % kCapacity;
    --count;
    return true;
}

void touch_input_flush() {
    std::lock_guard<std::mutex> lock(event_mutex);
    head = tail = count = 0;
}
