# Fami32 desktop port
#   make linux    - clang/clang++ + SDL2
#   make windows  - MinGW-w64 + native Win32 multimedia APIs

LINUX_CC  := clang
LINUX_CXX := clang++
WIN_CC    := x86_64-w64-mingw32-gcc
WIN_CXX   := x86_64-w64-mingw32-g++

TARGET_LINUX := build/linux/fami32
TARGET_WIN   := build/windows/fami32.exe

INCLUDES := \
	-Idesktop/include \
	-Imain/include -Imain/include/app -Imain/include/assets -Imain/include/audio \
	-Imain/include/config -Imain/include/fami32core -Imain/include/gui \
	-Imain/include/hardware -Imain/include/input -Imain/include/storage \
	-Imain/include/usb -Imain/include/utils \
	-Icomponents/gfx_ssd1306/include \
	-Icomponents/Adafruit-GFX-Library

C_SOURCES := \
	main/src/audio/dpcm.c \
	main/src/audio/emu2413.c \
	main/src/audio/note2freq.c \
	main/src/audio/src_config.c \
	main/src/audio/wave_table.c \
	main/src/config/micro_config.c \
	components/Adafruit-GFX-Library/srdlib_noniso.c \
	desktop/src/stdlib_compat.c

CXX_SOURCES := \
	main/src/audio/gen_env.cpp \
	main/src/audio/vrc7_synth.cpp \
	main/src/app/main.cpp \
	$(wildcard main/src/core/*.cpp) \
	$(wildcard main/src/gui/*.cpp) \
	$(filter-out main/src/storage/fami32_storage.cpp,$(wildcard main/src/storage/*.cpp)) \
	components/Adafruit-GFX-Library/Adafruit_GFX.cpp \
	components/Adafruit-GFX-Library/Print.cpp \
	components/Adafruit-GFX-Library/WString.cpp \
	components/gfx_ssd1306/src/gfx_oled_ssd1306.cpp \
	desktop/src/desktop_platform.cpp \
	desktop/src/keypad_io_desktop.cpp \
	desktop/src/print_printf.cpp \
	desktop/src/touch_input_desktop.cpp

LINUX_C_OBJECTS   := $(patsubst %.c,build/linux/%.o,$(C_SOURCES))
LINUX_CXX_OBJECTS := $(patsubst %.cpp,build/linux/%.o,$(CXX_SOURCES))
WIN_C_OBJECTS     := $(patsubst %.c,build/windows/%.o,$(C_SOURCES))
WIN_CXX_OBJECTS   := $(patsubst %.cpp,build/windows/%.o,$(CXX_SOURCES))

COMMON_CPPFLAGS := -DFAMI32_DESKTOP=1 $(INCLUDES)
COMMON_CFLAGS   := -O2 -Wall -Wextra -MMD -MP
COMMON_CXXFLAGS := $(COMMON_CFLAGS) -std=gnu++20 -pthread
LINUX_SDL_FLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
LINUX_SDL_LIBS  := $(shell pkg-config --libs sdl2 2>/dev/null)

.PHONY: all linux windows clean

all: linux

linux: $(TARGET_LINUX)

windows: $(TARGET_WIN)

$(TARGET_LINUX): $(LINUX_C_OBJECTS) $(LINUX_CXX_OBJECTS)
	@mkdir -p $(@D)
	$(LINUX_CXX) $^ -o $@ $(LINUX_SDL_LIBS) -pthread -lm -Wl,--defsym,main=app_main
	@echo "Built $@"

$(TARGET_WIN): $(WIN_C_OBJECTS) $(WIN_CXX_OBJECTS)
	@mkdir -p $(@D)
	$(WIN_CXX) $^ -o $@ -static -static-libgcc -static-libstdc++ -lwinmm -lgdi32 -luser32 -pthread -Wl,--defsym,main=app_main
	@echo "Built $@"

build/linux/%.o: %.c
	@mkdir -p $(@D)
	$(LINUX_CC) $(COMMON_CPPFLAGS) $(LINUX_SDL_FLAGS) $(COMMON_CFLAGS) -std=gnu11 -c $< -o $@

build/linux/%.o: %.cpp
	@mkdir -p $(@D)
	$(LINUX_CXX) $(COMMON_CPPFLAGS) $(LINUX_SDL_FLAGS) $(COMMON_CXXFLAGS) -c $< -o $@

build/windows/%.o: %.c
	@mkdir -p $(@D)
	$(WIN_CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -std=gnu11 -c $< -o $@

build/windows/%.o: %.cpp
	@mkdir -p $(@D)
	$(WIN_CXX) $(COMMON_CPPFLAGS) $(COMMON_CXXFLAGS) -c $< -o $@

clean:
	rm -rf build/linux build/windows

-include $(LINUX_C_OBJECTS:.o=.d) $(LINUX_CXX_OBJECTS:.o=.d)
-include $(WIN_C_OBJECTS:.o=.d) $(WIN_CXX_OBJECTS:.o=.d)
