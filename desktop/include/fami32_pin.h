#ifndef FAMI32_DESKTOP_PIN_H
#define FAMI32_DESKTOP_PIN_H

#include <stdint.h>
#include <stdio.h>

/* Logical controls shared with the device UI. PC key codes stay in desktop/. */
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

#define KEYPAD_ROWS 4
#define KEYPAD_COLS 3
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define FAMI32_NOTE_KEY_COUNT 16
#define FAMI32_STORAGE_DIR "./fami32_data"

static const uint8_t KEYPAD_MAP[KEYPAD_ROWS][KEYPAD_COLS] = {
    {KEY_L,    KEY_OK,   KEY_MENU},
    {KEY_UP,   KEY_S,    KEY_NAVI},
    {KEY_R,    KEY_BACK, KEY_OCTD},
    {KEY_DOWN, KEY_P,    KEY_OCTU},
};

#define DBG_PRINTF(fmt, ...)            \
    do {                                \
        if (_debug_print) {             \
            printf(fmt, ##__VA_ARGS__); \
        }                               \
    } while (0)

#endif
