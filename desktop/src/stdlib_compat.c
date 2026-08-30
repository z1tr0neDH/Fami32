#include "stdlib_noniso.h"

char *itoa(int value, char *result, int base) {
    return ltoa((long)value, result, base);
}

char *utoa(unsigned int value, char *result, int base) {
    return ultoa((unsigned long)value, result, base);
}
