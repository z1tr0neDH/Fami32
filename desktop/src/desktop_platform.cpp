#include "desktop_platform.h"

#include "fami32_pin.h"
#include "keypad_io.h"
#include "touch_input.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

extern KeypadIO keypad;

namespace {
std::atomic<bool> quitting{false};

void inject_control(int key, bool pressed) {
    if (key >= 0) keypad.inject(static_cast<uint8_t>(key), pressed ? KEY_JUST_PRESSED : KEY_JUST_RELEASED);
}

void inject_note(int note, bool pressed) {
    if (note >= 0) touch_input_push_event(static_cast<uint8_t>(note), pressed ? KEY_JUST_PRESSED : KEY_JUST_RELEASED);
}
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

namespace {
HWND window_handle = nullptr;
uint32_t pixels[128 * 64];
BITMAPINFO bitmap_info = {};
HWAVEOUT wave_out = nullptr;
constexpr size_t kWaveBufferCount = 4;
WAVEHDR wave_headers[kWaveBufferCount] = {};
int16_t *wave_buffers[kWaveBufferCount] = {};
size_t wave_capacity = 0;
size_t wave_index = 0;

int control_for_key(WPARAM key) {
    switch (key) {
        case VK_UP: return KEY_UP;
        case VK_DOWN: return KEY_DOWN;
        case VK_LEFT: return KEY_L;
        case VK_RIGHT: return KEY_R;
        case VK_RETURN: return KEY_OK;
        case VK_BACK: return KEY_S;
        case VK_SPACE: return KEY_P;
        case VK_TAB: return KEY_MENU;
        case VK_HOME: return KEY_NAVI;
        case VK_ESCAPE: return KEY_BACK;
        case VK_PRIOR: return KEY_OCTU;
        case VK_NEXT: return KEY_OCTD;
        default: return -1;
    }
}

int note_for_key(WPARAM key) {
    static const WPARAM keys[16] = {'Z','S','X','D','C','V','G','B','H','N','J','M',VK_OEM_COMMA,'L',VK_OEM_PERIOD,VK_OEM_1};
    for (int i = 0; i < 16; ++i) if (keys[i] == key) return i;
    return -1;
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CLOSE:
            quitting = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            quitting = true;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP: {
            if ((lparam & (1L << 30)) && message == WM_KEYDOWN) return 0;
            bool pressed = message == WM_KEYDOWN;
            int control = control_for_key(wparam);
            if (control >= 0) inject_control(control, pressed);
            else inject_note(note_for_key(wparam), pressed);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            RECT rect;
            GetClientRect(hwnd, &rect);
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, 0, 0, rect.right, rect.bottom, 0, 0, 128, 64,
                          pixels, &bitmap_info, DIB_RGB_COLORS, SRCCOPY);
            EndPaint(hwnd, &paint);
            return 0;
        }
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}
}

bool desktop_platform_init(const char *title, int width, int height) {
    HINSTANCE instance = GetModuleHandle(nullptr);
    WNDCLASSA window_class = {};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = "Fami32DesktopWindow";
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassA(&window_class);

    RECT rect = {0, 0, width * 6, height * 6};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window_handle = CreateWindowA(window_class.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                                  nullptr, nullptr, instance, nullptr);
    if (window_handle == nullptr) return false;
    ShowWindow(window_handle, SW_SHOW);

    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = 128;
    bitmap_info.bmiHeader.biHeight = -64;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    return true;
}

void desktop_pump_events() {
    MSG message;
    while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
}

void desktop_present_frame(const uint8_t *buffer, int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool on = (buffer[x + (y / 8) * width] & (1U << (y & 7))) != 0;
            pixels[y * width + x] = on ? 0x00F2F2F2U : 0x000B1015U;
        }
    }
    InvalidateRect(window_handle, nullptr, FALSE);
    desktop_pump_events();
}

bool desktop_audio_init(int sample_rate) {
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = static_cast<DWORD>(sample_rate);
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = static_cast<DWORD>(sample_rate * 2);
    return waveOutOpen(&wave_out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
}

void desktop_audio_write(const int16_t *samples, size_t sample_count) {
    if (wave_out == nullptr || sample_count == 0) return;
    if (sample_count > wave_capacity) {
        for (size_t i = 0; i < kWaveBufferCount; ++i) {
            if (wave_headers[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wave_out, &wave_headers[i], sizeof(WAVEHDR));
            free(wave_buffers[i]);
            wave_buffers[i] = static_cast<int16_t *>(malloc(sample_count * sizeof(int16_t)));
        }
        wave_capacity = sample_count;
    }
    WAVEHDR &header = wave_headers[wave_index];
    while ((header.dwFlags & WHDR_PREPARED) && !(header.dwFlags & WHDR_DONE)) Sleep(1);
    if (header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wave_out, &header, sizeof(WAVEHDR));
    memcpy(wave_buffers[wave_index], samples, sample_count * sizeof(int16_t));
    memset(&header, 0, sizeof(header));
    header.lpData = reinterpret_cast<LPSTR>(wave_buffers[wave_index]);
    header.dwBufferLength = static_cast<DWORD>(sample_count * sizeof(int16_t));
    waveOutPrepareHeader(wave_out, &header, sizeof(header));
    waveOutWrite(wave_out, &header, sizeof(header));
    wave_index = (wave_index + 1) % kWaveBufferCount;
}

void desktop_platform_shutdown() {
    if (wave_out != nullptr) {
        waveOutReset(wave_out);
        waveOutClose(wave_out);
        wave_out = nullptr;
    }
}

#else

#include <SDL.h>

namespace {
SDL_Window *window_handle = nullptr;
SDL_Renderer *renderer = nullptr;
SDL_Texture *texture = nullptr;
SDL_AudioDeviceID audio_device = 0;
uint32_t pixels[128 * 64];

int control_for_key(SDL_Keycode key) {
    switch (key) {
        case SDLK_UP: return KEY_UP;
        case SDLK_DOWN: return KEY_DOWN;
        case SDLK_LEFT: return KEY_L;
        case SDLK_RIGHT: return KEY_R;
        case SDLK_RETURN: return KEY_OK;
        case SDLK_BACKSPACE: return KEY_S;
        case SDLK_SPACE: return KEY_P;
        case SDLK_TAB: return KEY_MENU;
        case SDLK_HOME: return KEY_NAVI;
        case SDLK_ESCAPE: return KEY_BACK;
        case SDLK_PAGEUP: return KEY_OCTU;
        case SDLK_PAGEDOWN: return KEY_OCTD;
        default: return -1;
    }
}

int note_for_key(SDL_Keycode key) {
    static const SDL_Keycode keys[16] = {SDLK_z,SDLK_s,SDLK_x,SDLK_d,SDLK_c,SDLK_v,SDLK_g,SDLK_b,
                                         SDLK_h,SDLK_n,SDLK_j,SDLK_m,SDLK_COMMA,SDLK_l,SDLK_PERIOD,SDLK_SEMICOLON};
    for (int i = 0; i < 16; ++i) if (keys[i] == key) return i;
    return -1;
}
}

bool desktop_platform_init(const char *title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return false;
    }
    window_handle = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     width * 6, height * 6, SDL_WINDOW_RESIZABLE);
    if (window_handle == nullptr) return false;
    renderer = SDL_CreateRenderer(window_handle, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) renderer = SDL_CreateRenderer(window_handle, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == nullptr) return false;
    SDL_RenderSetLogicalSize(renderer, width, height);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    return texture != nullptr;
}

void desktop_pump_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) quitting = true;
        if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) continue;
        if (event.key.repeat) continue;
        bool pressed = event.type == SDL_KEYDOWN;
        int control = control_for_key(event.key.keysym.sym);
        if (control >= 0) inject_control(control, pressed);
        else inject_note(note_for_key(event.key.keysym.sym), pressed);
    }
}

void desktop_present_frame(const uint8_t *buffer, int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool on = (buffer[x + (y / 8) * width] & (1U << (y & 7))) != 0;
            pixels[y * width + x] = on ? 0xFFF2F2F2U : 0xFF0B1015U;
        }
    }
    SDL_UpdateTexture(texture, nullptr, pixels, width * static_cast<int>(sizeof(uint32_t)));
    SDL_SetRenderDrawColor(renderer, 11, 16, 21, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    desktop_pump_events();
}

bool desktop_audio_init(int sample_rate) {
    SDL_AudioSpec wanted = {};
    wanted.freq = sample_rate;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 1;
    wanted.samples = 1024;
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    if (audio_device == 0) {
        fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(audio_device, 0);
    return true;
}

void desktop_audio_write(const int16_t *samples, size_t sample_count) {
    if (audio_device == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(sample_count * 1000000ULL / 72000ULL));
        return;
    }
    const uint32_t bytes = static_cast<uint32_t>(sample_count * sizeof(int16_t));
    while (SDL_GetQueuedAudioSize(audio_device) > bytes * 4 && !quitting) SDL_Delay(1);
    SDL_QueueAudio(audio_device, samples, bytes);
}

void desktop_platform_shutdown() {
    if (audio_device != 0) SDL_CloseAudioDevice(audio_device);
    if (texture != nullptr) SDL_DestroyTexture(texture);
    if (renderer != nullptr) SDL_DestroyRenderer(renderer);
    if (window_handle != nullptr) SDL_DestroyWindow(window_handle);
    SDL_Quit();
}

#endif

bool desktop_should_quit() { return quitting.load(); }

void desktop_delay(unsigned milliseconds) {
    desktop_pump_events();
    if (desktop_should_quit()) {
        desktop_platform_shutdown();
        std::exit(0);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void desktop_yield() { std::this_thread::yield(); }

[[noreturn]] void esp_restart() {
    desktop_platform_shutdown();
    std::exit(0);
}
