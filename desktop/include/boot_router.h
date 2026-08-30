#ifndef FAMI32_DESKTOP_BOOT_ROUTER_H
#define FAMI32_DESKTOP_BOOT_ROUTER_H

enum BOOT_MODE { USB_MSC, USB_AUDIO };
inline void boot_router_set_mode(BOOT_MODE) {}
inline void boot_router() {}

#endif
