#ifndef FAMI32_DESKTOP_ARDUINO_H
#define FAMI32_DESKTOP_ARDUINO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "WString.h"
#include "Print.h"

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PSTR
#define PSTR(s) (s)
#endif

inline void yield() {}

#endif
