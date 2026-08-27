#ifndef FAMI32_PIN_H
#define FAMI32_PIN_H

#include <stdint.h>

/* Logical controls used by the tracker UI. */
#define KEY_L       0
#define KEY_OK      1
#define KEY_MENU    2
#define KEY_UP      3
#define KEY_S       4
#define KEY_NAVI    5
#define KEY_R       6
#define KEY_BACK    7
#define KEY_OCTD    8
#define KEY_DOWN    9
#define KEY_P       10
#define KEY_OCTU    11
#define FAMI32_LOGICAL_KEY_COUNT 12

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

/* Shared I2C0 bus: PCF8575 (0x20) and NAU88C22 (0x1A). */
#define FAMI32_I2C_PORT       0
#define FAMI32_I2C_SCL_GPIO   38
#define FAMI32_I2C_SDA_GPIO   39
#define FAMI32_I2C_FREQ_HZ    400000
#define PCF8575_I2C_ADDRESS   0x20
#define NAU88C22_I2C_ADDRESS  0x1A
#define PCF8575_INT_GPIO      40

/* PCF8575 port assignment from Netlist_FAMI_807_2026-08-20.net. */
#define PCF8575_L5_BIT          0
#define PCF8575_L4_BIT          1
#define PCF8575_L3_BIT          2
#define PCF8575_L2_BIT          3
#define PCF8575_L1_BIT          4
#define PCF8575_KEY3_IN_BIT     5
#define PCF8575_MAX_SD_MODE_BIT 6
#define PCF8575_SD_DET_BIT      7
#define PCF8575_VOL_MINUS_BIT   8
#define PCF8575_VOL_PLUS_BIT    9
#define PCF8575_H1_BIT          10
#define PCF8575_H2_BIT          11
#define PCF8575_H3_BIT          12
#define PCF8575_H4_BIT          13
#define PCF8575_H5_BIT          14
#define PCF8575_H6_BIT          15

/* Two EC11-style rotary encoders. */
#define ENCODER_1_A_GPIO 41
#define ENCODER_1_B_GPIO 42
#define ENCODER_2_A_GPIO 2
#define ENCODER_2_B_GPIO 1

/* NAU88C22 headphone I2S1 path. */
#define CODEC_I2S_PORT      1
#define CODEC_I2S_DOUT_GPIO 4
#define CODEC_I2S_WS_GPIO   5
#define CODEC_I2S_BCLK_GPIO 6
#define CODEC_I2S_MCLK_GPIO 7

/* MAX98357A speaker I2S0 path. */
#define SPEAKER_I2S_PORT      0
#define SPEAKER_I2S_BCLK_GPIO 15
#define SPEAKER_I2S_WS_GPIO   16
#define SPEAKER_I2S_DOUT_GPIO 17

#define HP_DET_GPIO 18

/* Battery divider: VBAT -- 1.8 MOhm -- ADC -- 100 kOhm -- GND. */
#define BAT_ADC_GPIO            8
#define BAT_ADC_DIVIDER_TOP_OHM 1800000
#define BAT_ADC_DIVIDER_BOT_OHM 100000

/* SSD1306, 128 x 64, 4-wire SPI with switched VBAT/charge-pump supply. */
#define DISPLAY_SCL            9
#define DISPLAY_SDA            10
#define DISPLAY_DC             11
#define DISPLAY_RESET          12
#define DISPLAY_CS             13
#define DISPLAY_POWER_EN       14

/* SDMMC in 1-bit mode. GPIO47/48 are 3.3 V I/O on the N16R8 module. */
#define SD_SCLK_GPIO  21
#define SD_CMD_GPIO   47
#define SD_DATA0_GPIO 48

#define FAMI32_NOTE_KEY_COUNT 16

#ifdef __cplusplus
extern "C" {
#endif
const char *fami32_storage_dir(void);
#ifdef __cplusplus
}
#endif

#define FAMI32_STORAGE_DIR fami32_storage_dir()

#define DBG_PRINTF(fmt, ...)            \
    do {                                \
        if (_debug_print) {             \
            printf(fmt, ##__VA_ARGS__); \
        }                               \
    } while (0)

#endif /* FAMI32_PIN_H */
