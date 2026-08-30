#ifndef FAMI32_DESKTOP_USBMIDI_H
#define FAMI32_DESKTOP_USBMIDI_H

#include <stdint.h>

typedef struct {
    uint8_t header;
    uint8_t byte1;
    uint8_t byte2;
    uint8_t byte3;
} midiEventPacket_t;

typedef void (*midi_callback_t)(midiEventPacket_t);

class USBMIDI {
public:
    bool begin() { return true; }
    void setCallback(midi_callback_t callback) { callback_ = callback; }
    void noteOn(uint8_t, uint8_t = 127, uint8_t = 0) {}
    void noteOff(uint8_t, uint8_t = 0, uint8_t = 0) {}
    void controlChange(uint8_t, uint8_t, uint8_t = 0) {}
    void programChange(uint8_t, uint8_t = 0) {}
    bool readPacket(midiEventPacket_t *) { return false; }
    bool writePacket(midiEventPacket_t *) { return false; }
    bool isConnected() { return false; }

private:
    midi_callback_t callback_ = nullptr;
};

#endif
