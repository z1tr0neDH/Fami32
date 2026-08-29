# FAMI32 AI 修改索引

用于定位 AI 参与实体硬件适配后新增、修改或删除的文件，同时保留仓库结构与 Flash 分区速查。

优先查阅：[适配新增](#5-实体硬件适配新增文件) · [适配修改](#6-原文件适配修改) · [适配删除](#7-已删除)

## 1. 根目录

| 文件或目录 | 作用 |
| --- | --- |
| `README.md` | 硬件风险、引脚、按键矩阵和 Flash 分区速查 |
| `docs/` | 操作手册、硬件测试及工程说明 |
| `main/` | FAMI32 主程序和硬件驱动 |
| `components/` | 图形、USB MIDI、Arduino 兼容层等组件 |
| `CMakeLists.txt` | ESP-IDF 工程入口和编译设置 |
| `partitions.csv` | 16 MiB Flash 分区表 |
| `sdkconfig` | 当前 ESP-IDF 完整配置 |
| `sdkconfig.defaults` | 新构建目录使用的关键默认配置 |
| `dependencies.lock` | 固定组件依赖版本 |
| `.vscode/`、`.clangd` | 编辑器、补全和调试配置 |
| `.gitignore` | Git 忽略规则 |

## 2. 主程序

头文件位于 `main/include/`，实现文件位于 `main/src/`。

| 分区 | 主要内容 |
| --- | --- |
| `app/` | 程序入口、启动模式、崩溃检查、版本信息 |
| `assets/` | OLED 图标和字体 |
| `audio/` | DPCM、波表、包络、VRC7/YM2413、音高和采样配置 |
| `fami32core/`、`core/` | 通道、乐器、音序和播放器 |
| `gui/` | Tracker、菜单、文件、设置、音色及可视化界面 |
| `hardware/` | 引脚、I²C、电池、NAU88C22 和工厂测试 |
| `input/` | PCF8575 按键矩阵、编码器及触摸输入 |
| `storage/` | SD 卡、内部 FAT、FTM 文件和 VGM 导出 |
| `usb/` | TinyUSB、USB Audio 和 USB MSC |
| `utils/` | 通用环形缓冲区 |

### 关键文件

| 文件 | 作用 |
| --- | --- |
| `main/src/app/main.cpp` | 初始化显示、输入、音频、USB、存储和系统任务 |
| `main/include/hardware/fami32_pin.h` | 当前 PCB 引脚、器件地址和硬件参数 |
| `main/src/input/keypad_io.cpp` | 6 × 6 矩阵、编码器、音量键和检测信号扫描 |
| `main/src/hardware/fami32_i2c.cpp` | PCF8575、NAU88C22 共用 I²C0 |
| `main/src/hardware/nau88c22.cpp` | 耳机 Codec 初始化和控制 |
| `main/src/hardware/fami32_battery.cpp` | 电池 ADC 采样和电量估算 |
| `main/src/hardware/fami32_factory_test.cpp` | OLED、输入、SD、耳机和电池自检 |
| `main/src/storage/fami32_storage.cpp` | SDMMC 挂载；失败时使用内部 `/flash` |
| `main/src/usb/usb_msc_mode.cpp` | USB 大容量存储；当前暴露内部 `/flash` |
| `main/src/app/boot_check.cpp` | 崩溃转储检查和 CRASH 页面判定 |

`MPR121_Keypad.*` 为旧输入实现，当前未列入 `main/CMakeLists.txt`。

## 3. 组件

| 组件 | 作用 |
| --- | --- |
| `Adafruit-GFX-Library` | 图形和字体接口 |
| `Adafruit_BusIO` | SPI/I²C 兼容接口 |
| `Adafruit_Keypad` | 键盘输入辅助库 |
| `USBMIDI` | USB MIDI 接口 |
| `arduino-esp32-lite` | Arduino API 兼容层 |
| `gfx_ssd1306` | OLED 显示驱动 |

## 4. Flash 分区

| 分区 | 起始地址 | 大小 | 作用 |
| --- | ---: | ---: | --- |
| `nvs` | `0x009000` | 24 KiB | 非易失设置 |
| `app` | `0x010000` | 3 MiB | 主固件 |
| `flash` | `0x310000` | 12.875 MiB | 内部 FAT，挂载为 `/flash` |
| `coredump` | `0xFF0000` | 64 KiB | 崩溃转储 |

Flash 总容量为 16 MiB。Bootloader 和分区表位于应用分区之前。

## 5. 实体硬件适配新增文件

适配基线：`306a4d9200`；首个完整适配提交：`11b9b71ada`。

| 新增文件 | 作用 |
| --- | --- |
| `docs/HARDWARE_R1_BRINGUP.md` | 实体硬件编译、烧录和测试 |
| `fami32_battery.h/.cpp` | 电池检测 |
| `fami32_factory_test.h/.cpp` | 工厂自检 |
| `fami32_i2c.h/.cpp` | 共用 I²C 驱动 |
| `nau88c22.h/.cpp` | 耳机 Codec 驱动 |
| `fami32_storage.h/.cpp` | SD 卡和内部存储选择 |
| `sdkconfig.defaults` | ESP32-S3 N16R8 默认构建配置 |

`docs/guide.md` 为适配后新增的原 README 内容备份。

## 6. 原文件适配修改

| 文件 | 修改内容 |
| --- | --- |
| `fami32_pin.h` | 当前 PCB 引脚和器件地址 |
| `keypad_io.h/.cpp` | PCF8575 矩阵和编码器输入 |
| `main.cpp` | 新硬件初始化、存储和双路音频 |
| `main/CMakeLists.txt` | 新驱动和 ESP-IDF 依赖 |
| `partitions.csv` | 16 MiB Flash 布局 |
| `sdkconfig`、`idf_component.yml` | ESP32-S3、PSRAM 和 TinyUSB |
| `boot_check.cpp` | CRASH 页面判定 |
| `gfx_ssd1306` | OLED 180°旋转 |
| `README.md` | 硬件速查文档 |

## 7. 已删除

| 文件或目录 | 原用途 |
| --- | --- |
| `desktop/` | PC 桌面模拟器 |
| `Makefile` | 旧构建入口 |
| `sdkconfig.old` | 旧 ESP-IDF 配置备份 |
