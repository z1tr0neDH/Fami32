#ifndef FAMI32_DESKTOP_PLATFORM_H
#define FAMI32_DESKTOP_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef void *TaskHandle_t;
typedef void *i2s_chan_handle_t;
typedef int esp_err_t;

#define ESP_OK 0
#define ESP_ERR_INVALID_ARG (-1)
#define ESP_ERR_NO_MEM (-2)
#define ESP_ERR_INVALID_STATE (-3)

#define ESP_LOGI(tag, fmt, ...) fprintf(stdout, "[%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "[%s] " fmt "\n", tag, ##__VA_ARGS__)

bool desktop_platform_init(const char *title, int width, int height);
void desktop_platform_shutdown();
void desktop_present_frame(const uint8_t *buffer, int width, int height);
void desktop_pump_events();
bool desktop_should_quit();
void desktop_delay(unsigned milliseconds);
void desktop_yield();
bool desktop_audio_init(int sample_rate);
void desktop_audio_write(const int16_t *samples, size_t sample_count);

inline void vTaskDelay(unsigned milliseconds) { desktop_delay(milliseconds); }
inline void taskYIELD() { desktop_yield(); }
inline void vTaskSuspend(TaskHandle_t) {}
inline void vTaskResume(TaskHandle_t) {}
[[noreturn]] void esp_restart();

#endif
