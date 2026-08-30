#include "Print.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

size_t Print::vprintf(const char *format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (length <= 0) return 0;

    char stack_buffer[128];
    char *buffer = stack_buffer;
    if (static_cast<size_t>(length + 1) > sizeof(stack_buffer)) {
        buffer = static_cast<char *>(malloc(static_cast<size_t>(length + 1)));
        if (buffer == nullptr) return 0;
    }
    vsnprintf(buffer, static_cast<size_t>(length + 1), format, args);
    size_t result = write(reinterpret_cast<const uint8_t *>(buffer), static_cast<size_t>(length));
    if (buffer != stack_buffer) free(buffer);
    return result;
}

size_t Print::printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    size_t result = vprintf(format, args);
    va_end(args);
    return result;
}

size_t Print::printf(const __FlashStringHelper *format, ...) {
    va_list args;
    va_start(args, format);
    size_t result = vprintf(reinterpret_cast<const char *>(format), args);
    va_end(args);
    return result;
}
