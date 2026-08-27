#include "keypad_io.h"

#include <cstring>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "fami32_i2c.h"
#include "fami32_pin.h"
#include "touch_input.h"

namespace {

constexpr const char *TAG = "Fami32Input";
constexpr uint32_t kSettlingDelayUs = 20;
constexpr uint8_t kDebounceScans = 3;

constexpr uint8_t kColumnBits[6] = {
    PCF8575_L1_BIT,
    PCF8575_L2_BIT,
    PCF8575_L3_BIT,
    PCF8575_L4_BIT,
    PCF8575_L5_BIT,
    PCF8575_KEY3_IN_BIT,
};

/*
 * Physical switch mapping, isolated here because the netlist has reference
 * designators but no front-panel legends. H2..H6 x L1..L4 are SW1..SW20.
 * H1/H2 x KEY3_IN are the two encoder push switches.
 * SW17..SW20 and SW27..SW28 are scanned for diagnostics but intentionally
 * have no application event until the existing UI assigns them a function.
 */
constexpr int8_t kControlMap[6][6] = {
    {-1, -1, -1, -1, KEY_MENU, KEY_BACK},
    {-1, -1, -1, -1, KEY_NAVI, KEY_OK},
    {-1, -1, -1, -1, KEY_S,    KEY_OCTD},
    {-1, -1, -1, -1, KEY_P,    KEY_OCTU},
    {-1, -1, -1, -1, -1,       -1},
    {-1, -1, -1, -1, -1,       -1},
};

/* Direction is intentionally centralized for one-line field correction. */
constexpr uint8_t kEncoderClockwiseKey[2] = {KEY_DOWN, KEY_R};
constexpr uint8_t kEncoderCounterClockwiseKey[2] = {KEY_UP, KEY_L};

constexpr int8_t kQuadratureTable[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
};

inline bool bit_is_low(uint16_t value, uint8_t bit) {
    return (value & (static_cast<uint16_t>(1U) << bit)) == 0;
}

bool update_debounced_bool(bool sample, bool *stable, uint8_t *counter) {
    if (sample == *stable) {
        *counter = 0;
        return false;
    }
    if (++(*counter) < kDebounceScans) return false;
    *counter = 0;
    *stable = sample;
    return true;
}

} // namespace

KeypadIO::KeypadIO()
    : raw_matrix_state_(0),
      stable_matrix_state_(0),
      port_snapshot_(0xFFFF),
      pcf_ready_(false),
      audio_ready_(false),
      volume_minus_pressed_(false),
      volume_plus_pressed_(false),
      sd_card_present_(false),
      headphones_inserted_(false),
      volume_minus_debounce_(0),
      volume_plus_debounce_(0),
      sd_debounce_(0),
      hp_debounce_(0),
      pending_volume_delta_(0),
      head_(0),
      tail_(0),
      count_(0) {
    memset(matrix_debounce_, 0, sizeof(matrix_debounce_));
    memset(logical_press_count_, 0, sizeof(logical_press_count_));
    memset(encoder_state_, 0, sizeof(encoder_state_));
    memset(encoder_accumulator_, 0, sizeof(encoder_accumulator_));
    memset(encoder_position_, 0, sizeof(encoder_position_));
    memset(keyStates_, 0, sizeof(keyStates_));
}

bool KeypadIO::begin() {
    clear();
    raw_matrix_state_ = 0;
    stable_matrix_state_ = 0;
    memset(matrix_debounce_, 0, sizeof(matrix_debounce_));
    memset(logical_press_count_, 0, sizeof(logical_press_count_));
    pending_volume_delta_ = 0;
    audio_ready_ = false;

    gpio_config_t input_cfg = {};
    input_cfg.pin_bit_mask =
        (1ULL << PCF8575_INT_GPIO) |
        (1ULL << HP_DET_GPIO) |
        (1ULL << ENCODER_1_A_GPIO) |
        (1ULL << ENCODER_1_B_GPIO) |
        (1ULL << ENCODER_2_A_GPIO) |
        (1ULL << ENCODER_2_B_GPIO);
    input_cfg.mode = GPIO_MODE_INPUT;
    input_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    input_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input_cfg.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&input_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO input setup failed: %s", esp_err_to_name(err));
        return false;
    }

    err = fami32_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C setup failed: %s", esp_err_to_name(err));
        return false;
    }

    /* PCF8575 powers up high, which enables MAX98357A. Shut it down first. */
    pcf_ready_ = writePcf(idlePcfValue());
    if (!pcf_ready_) {
        ESP_LOGE(TAG, "PCF8575 not responding at 0x%02X", PCF8575_I2C_ADDRESS);
        return false;
    }

    encoder_state_[0] = static_cast<uint8_t>(
        (gpio_get_level(static_cast<gpio_num_t>(ENCODER_1_A_GPIO)) << 1) |
         gpio_get_level(static_cast<gpio_num_t>(ENCODER_1_B_GPIO)));
    encoder_state_[1] = static_cast<uint8_t>(
        (gpio_get_level(static_cast<gpio_num_t>(ENCODER_2_A_GPIO)) << 1) |
         gpio_get_level(static_cast<gpio_num_t>(ENCODER_2_B_GPIO)));

    uint16_t initial_port = 0xFFFF;
    if (readPcf(&initial_port)) {
        port_snapshot_ = initial_port;
        volume_minus_pressed_ = bit_is_low(initial_port, PCF8575_VOL_MINUS_BIT);
        volume_plus_pressed_ = bit_is_low(initial_port, PCF8575_VOL_PLUS_BIT);
        sd_card_present_ = bit_is_low(initial_port, PCF8575_SD_DET_BIT);
    }
    headphones_inserted_ =
        gpio_get_level(static_cast<gpio_num_t>(HP_DET_GPIO)) == 0;

    ESP_LOGI(TAG, "PCF8575 ready; SD=%s HP=%s",
             sd_card_present_ ? "present" : "absent",
             headphones_inserted_ ? "inserted" : "absent");
    return true;
}

void KeypadIO::tick() {
    uint64_t raw_state = 0;
    if (scanMatrix(&raw_state)) {
        raw_matrix_state_ = raw_state;
        updateMatrix(raw_state);
        updateAuxInputs(port_snapshot_);
        pcf_ready_ = true;
    } else {
        pcf_ready_ = false;
    }
    updateEncoders();
}

bool KeypadIO::writePcf(uint16_t value) {
    const uint8_t bytes[2] = {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
    };
    return fami32_i2c_write(PCF8575_I2C_ADDRESS, bytes, sizeof(bytes)) == ESP_OK;
}

bool KeypadIO::readPcf(uint16_t *value) {
    if (value == nullptr) return false;
    uint8_t bytes[2] = {};
    if (fami32_i2c_read(PCF8575_I2C_ADDRESS, bytes, sizeof(bytes)) != ESP_OK) return false;
    *value = static_cast<uint16_t>(bytes[0]) |
             (static_cast<uint16_t>(bytes[1]) << 8);
    return true;
}

uint16_t KeypadIO::idlePcfValue() const {
    uint16_t value = 0xFFFF;
    const bool hp_raw = gpio_get_level(static_cast<gpio_num_t>(HP_DET_GPIO)) == 0;
    if (!audio_ready_ || hp_raw) {
        value &= static_cast<uint16_t>(~(1U << PCF8575_MAX_SD_MODE_BIT));
    }
    return value;
}

bool KeypadIO::scanMatrix(uint64_t *raw_state) {
    if (raw_state == nullptr) return false;

    uint64_t result = 0;
    const uint16_t idle = idlePcfValue();
    for (size_t row = 0; row < MATRIX_ROWS; ++row) {
        const uint16_t row_value = static_cast<uint16_t>(idle & ~(1U << (PCF8575_H1_BIT + row)));
        if (!writePcf(row_value)) return false;
        esp_rom_delay_us(kSettlingDelayUs);

        uint16_t snapshot = 0xFFFF;
        if (!readPcf(&snapshot)) return false;
        port_snapshot_ = snapshot;
        for (size_t col = 0; col < MATRIX_COLS; ++col) {
            if (bit_is_low(snapshot, kColumnBits[col])) {
                result |= (1ULL << (row * MATRIX_COLS + col));
            }
        }
    }

    if (!writePcf(idle)) return false;
    if (!readPcf(&port_snapshot_)) return false;
    *raw_state = result;
    return true;
}

void KeypadIO::updateMatrix(uint64_t raw_state) {
    for (size_t index = 0; index < MATRIX_KEYS; ++index) {
        const bool sample = (raw_state & (1ULL << index)) != 0;
        const bool stable = (stable_matrix_state_ & (1ULL << index)) != 0;
        if (sample == stable) {
            matrix_debounce_[index] = 0;
            continue;
        }
        if (++matrix_debounce_[index] < kDebounceScans) continue;

        matrix_debounce_[index] = 0;
        if (sample) stable_matrix_state_ |= (1ULL << index);
        else stable_matrix_state_ &= ~(1ULL << index);

        const uint8_t row = static_cast<uint8_t>(index / MATRIX_COLS);
        const uint8_t col = static_cast<uint8_t>(index % MATRIX_COLS);
        if (row >= 1 && col < 4) {
            const uint8_t note_key = static_cast<uint8_t>((row - 1) * 4 + col);
            if (note_key < FAMI32_NOTE_KEY_COUNT) {
                touch_input_push_event(note_key, sample ? KEY_JUST_PRESSED : KEY_JUST_RELEASED);
            }
        } else {
            const int8_t logical_key = kControlMap[row][col];
            if (logical_key >= 0) {
                updateLogicalKey(static_cast<uint8_t>(logical_key), sample, row, col);
            }
        }
    }
}

void KeypadIO::updateLogicalKey(uint8_t key, bool pressed, uint8_t row, uint8_t col) {
    if (key >= sizeof(logical_press_count_)) return;
    const bool was_pressed = logical_press_count_[key] != 0;
    if (pressed) {
        if (logical_press_count_[key] != UINT8_MAX) ++logical_press_count_[key];
    } else if (logical_press_count_[key] != 0) {
        --logical_press_count_[key];
    }
    const bool is_pressed = logical_press_count_[key] != 0;
    if (is_pressed == was_pressed) return;

    keyStates_[key] &= static_cast<uint8_t>(~(STATE_JUST_PRESSED | STATE_JUST_RELEASED));
    keypadEvent event = {};
    event.bit.KEY = key;
    event.bit.EVENT = is_pressed ? KEY_JUST_PRESSED : KEY_JUST_RELEASED;
    event.bit.ROW = row;
    event.bit.COL = col;
    if (is_pressed) {
        keyStates_[key] |= static_cast<uint8_t>(STATE_PRESSED | STATE_JUST_PRESSED);
    } else {
        keyStates_[key] &= static_cast<uint8_t>(~STATE_PRESSED);
        keyStates_[key] |= STATE_JUST_RELEASED;
    }
    pushEvent(event);
}

void KeypadIO::updateAuxInputs(uint16_t port_value) {
    const bool minus_sample = bit_is_low(port_value, PCF8575_VOL_MINUS_BIT);
    const bool plus_sample = bit_is_low(port_value, PCF8575_VOL_PLUS_BIT);
    const bool sd_sample = bit_is_low(port_value, PCF8575_SD_DET_BIT);
    const bool hp_sample = gpio_get_level(static_cast<gpio_num_t>(HP_DET_GPIO)) == 0;

    if (update_debounced_bool(minus_sample, &volume_minus_pressed_, &volume_minus_debounce_) &&
        volume_minus_pressed_) {
        --pending_volume_delta_;
    }
    if (update_debounced_bool(plus_sample, &volume_plus_pressed_, &volume_plus_debounce_) &&
        volume_plus_pressed_) {
        ++pending_volume_delta_;
    }
    update_debounced_bool(sd_sample, &sd_card_present_, &sd_debounce_);
    update_debounced_bool(hp_sample, &headphones_inserted_, &hp_debounce_);
}

void KeypadIO::updateEncoders() {
    const int a_gpio[2] = {ENCODER_1_A_GPIO, ENCODER_2_A_GPIO};
    const int b_gpio[2] = {ENCODER_1_B_GPIO, ENCODER_2_B_GPIO};
    for (size_t i = 0; i < 2; ++i) {
        const uint8_t current = static_cast<uint8_t>(
            (gpio_get_level(static_cast<gpio_num_t>(a_gpio[i])) << 1) |
             gpio_get_level(static_cast<gpio_num_t>(b_gpio[i])));
        const uint8_t transition = static_cast<uint8_t>((encoder_state_[i] << 2) | current);
        encoder_state_[i] = current;
        encoder_accumulator_[i] = static_cast<int8_t>(
            encoder_accumulator_[i] + kQuadratureTable[transition & 0x0F]);
        if (encoder_accumulator_[i] >= 4) {
            encoder_accumulator_[i] = 0;
            emitEncoderStep(i, 1);
        } else if (encoder_accumulator_[i] <= -4) {
            encoder_accumulator_[i] = 0;
            emitEncoderStep(i, -1);
        }
    }
}

void KeypadIO::emitEncoderStep(size_t encoder, int direction) {
    if (encoder >= 2) return;
    encoder_position_[encoder] += direction;
    const uint8_t key = direction > 0
        ? kEncoderClockwiseKey[encoder]
        : kEncoderCounterClockwiseKey[encoder];

    keypadEvent event = {};
    event.bit.KEY = key;
    event.bit.ROW = static_cast<uint8_t>(0xE0 + encoder);
    event.bit.COL = direction > 0 ? 1 : 0;
    event.bit.EVENT = KEY_JUST_PRESSED;
    pushEvent(event);
    event.bit.EVENT = KEY_JUST_RELEASED;
    pushEvent(event);
}

bool KeypadIO::justPressed(uint8_t key, bool clear_state) {
    const int index = findKeyIndex(key);
    if (index < 0) return false;
    const bool value = (keyStates_[index] & STATE_JUST_PRESSED) != 0;
    if (clear_state) keyStates_[index] &= static_cast<uint8_t>(~STATE_JUST_PRESSED);
    return value;
}

bool KeypadIO::justReleased(uint8_t key) {
    const int index = findKeyIndex(key);
    if (index < 0) return false;
    const bool value = (keyStates_[index] & STATE_JUST_RELEASED) != 0;
    keyStates_[index] &= static_cast<uint8_t>(~STATE_JUST_RELEASED);
    return value;
}

bool KeypadIO::isPressed(uint8_t key) const {
    const int index = findKeyIndex(key);
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
    head_ = 0;
    tail_ = 0;
    count_ = 0;
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
    if (key >= 12) return -1;
    return key;
}

bool KeypadIO::pcfReady() const { return pcf_ready_; }
bool KeypadIO::sdCardPresent() const { return sd_card_present_; }
bool KeypadIO::headphonesInserted() const { return headphones_inserted_; }
bool KeypadIO::volumeUpPressed() const { return volume_plus_pressed_; }
bool KeypadIO::volumeDownPressed() const { return volume_minus_pressed_; }

int KeypadIO::takeVolumeDelta() {
    const int delta = pending_volume_delta_;
    pending_volume_delta_ = 0;
    return delta;
}

int32_t KeypadIO::encoderPosition(size_t encoder) const {
    return encoder < 2 ? encoder_position_[encoder] : 0;
}

uint64_t KeypadIO::rawMatrixState() const { return raw_matrix_state_; }
uint16_t KeypadIO::portSnapshot() const { return port_snapshot_; }
void KeypadIO::setAudioReady(bool ready) { audio_ready_ = ready; }
