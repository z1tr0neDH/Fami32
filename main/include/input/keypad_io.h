#ifndef KEYPAD_IO_H
#define KEYPAD_IO_H

#include <stddef.h>
#include <stdint.h>

#define KEY_JUST_RELEASED (0)
#define KEY_JUST_PRESSED  (1)

union keypadEvent {
    struct {
        uint8_t KEY;
        uint8_t EVENT;
        uint8_t ROW;
        uint8_t COL;
    } bit;
    uint32_t reg;
};

class KeypadIO {
public:
#ifndef FAMI32_DESKTOP
    KeypadIO();
#endif
    KeypadIO(const uint8_t *keymap,
             const uint8_t *rowPins,
             const uint8_t *colPins,
             size_t numRows,
             size_t numCols);

    bool begin();
    void tick();

    bool justPressed(uint8_t key, bool clear = true);
    bool justReleased(uint8_t key);
    bool isPressed(uint8_t key) const;
    bool isReleased(uint8_t key) const;

    int available() const;
    keypadEvent read();
    void clear();

#ifdef FAMI32_DESKTOP
    void inject(uint8_t key, uint8_t event);
#else
    bool pcfReady() const;
    bool sdCardPresent() const;
    bool headphonesInserted() const;
    bool volumeUpPressed() const;
    bool volumeDownPressed() const;
    int takeVolumeDelta();
    int32_t encoderPosition(size_t encoder) const;
    uint64_t rawMatrixState() const;
    uint16_t portSnapshot() const;
    void setAudioReady(bool ready);
#endif

private:
    void pushEvent(const keypadEvent &event);
    int findKeyIndex(uint8_t key) const;

#ifndef FAMI32_DESKTOP
    bool writePcf(uint16_t value);
    bool readPcf(uint16_t *value);
    bool scanMatrix(uint64_t *raw_state);
    void updateMatrix(uint64_t raw_state);
    void updateAuxInputs(uint16_t port_value);
    void updateEncoders();
    void updateLogicalKey(uint8_t key, bool pressed, uint8_t row, uint8_t col);
    void emitEncoderStep(size_t encoder, int direction);
    uint16_t idlePcfValue() const;

    static constexpr size_t MATRIX_ROWS = 6;
    static constexpr size_t MATRIX_COLS = 6;
    static constexpr size_t MATRIX_KEYS = MATRIX_ROWS * MATRIX_COLS;

    uint64_t raw_matrix_state_;
    uint64_t stable_matrix_state_;
    uint8_t matrix_debounce_[MATRIX_KEYS];
    uint8_t logical_press_count_[12];
    uint16_t port_snapshot_;
    bool pcf_ready_;
    volatile bool audio_ready_;

    bool volume_minus_pressed_;
    bool volume_plus_pressed_;
    bool sd_card_present_;
    bool headphones_inserted_;
    uint8_t volume_minus_debounce_;
    uint8_t volume_plus_debounce_;
    uint8_t sd_debounce_;
    uint8_t hp_debounce_;
    int pending_volume_delta_;

    uint8_t encoder_state_[2];
    int8_t encoder_accumulator_[2];
    int32_t encoder_position_[2];
#endif

    const uint8_t *keymap_;
    const uint8_t *rowPins_;
    const uint8_t *colPins_;
    size_t numRows_;
    size_t numCols_;

    static constexpr size_t BUFFER_SIZE = 32;
    keypadEvent buffer_[BUFFER_SIZE];
    size_t head_;
    size_t tail_;
    size_t count_;

    static constexpr uint8_t STATE_PRESSED = (1U << 0);
    static constexpr uint8_t STATE_JUST_PRESSED = (1U << 1);
    static constexpr uint8_t STATE_JUST_RELEASED = (1U << 2);
    uint8_t keyStates_[32];
};

#endif // KEYPAD_IO_H
