# Fami32

完整的界面说明与操作手册见 [docs/guide.md](docs/guide.md)。

## 1. 硬件风险警告

> [!CAUTION]
> - SD 卡接口无 ESD 防护，禁止热插拔。
> - 电源接口无防反接保护；电源座正面朝上时，左侧为正极。反接将损坏硬件。
> - 机械轴体不得接触洗板水或其他清洗溶剂。
> - 锂电池最低供电电压为 2.6 V。

## 2. 引脚定义

引脚定义以 [main/include/hardware/fami32_pin.h](main/include/hardware/fami32_pin.h) 为准。

### Ⅰ. 扫描矩阵与 I²C

PCF8575 与 NAU88C22 共用 I²C0 总线。

| 信号 | ESP32-S3 引脚 | 配置 |
| --- | ---: | --- |
| I²C SCL | GPIO38 | 400 kHz |
| I²C SDA | GPIO39 | 400 kHz |
| PCF8575 INT | GPIO40 | 输入中断 |
| PCF8575 地址 | — | `0x20` |
| NAU88C22 地址 | — | `0x1A` |

PCF8575 负责 6 × 6 按键矩阵、编码器按压、音量键、SD 卡检测及相关控制信号。

| PCF8575 位 | 信号 | 用途 |
| ---: | --- | --- |
| 0 | `L5` | 矩阵列 |
| 1 | `L4` | 矩阵列 |
| 2 | `L3` | 矩阵列 |
| 3 | `L2` | 矩阵列 |
| 4 | `L1` | 矩阵列 |
| 5 | `KEY3_IN` | 矩阵列 / 编码器按压 |
| 6 | `MAX_SD_MODE` | 扬声器与 SD 相关控制 |
| 7 | `SD_DET` | SD 卡插入检测 |
| 8 | `VOL-` | 音量减 |
| 9 | `VOL+` | 音量加 |
| 10–15 | `H1`–`H6` | 矩阵行 |

矩阵包含 16 个音符键和 `MENU`、`BACK`、`NAVI`、`OK`、`S`、`P`、`OCTD`、`OCTU` 等功能输入。SW17–SW20、SW27–SW28 暂未分配 UI 功能，仅保留扫描和自检能力。

### Ⅱ. 编码器

| 编码器 | A 相 | B 相 | 顺时针逻辑 | 逆时针逻辑 |
| --- | ---: | ---: | --- | --- |
| Encoder 1 | GPIO41 | GPIO42 | `DOWN` | `UP` |
| Encoder 2 | GPIO2 | GPIO1 | `R` | `L` |

两个编码器的按压开关通过 PCF8575 扫描矩阵输入。

### Ⅲ. USB

| 信号 | ESP32-S3 引脚 | 说明 |
| --- | ---: | --- |
| USB D− | GPIO19 | 原生 USB |
| USB D+ | GPIO20 | 原生 USB |

USB 用于固件烧录、USB MIDI 和 USB MSC。MSC 模式当前仅暴露片内 `/flash` 分区，不直接暴露 SD 卡。

### Ⅳ. OLED

显示屏使用 128 × 64、4 线 SPI 接口。

| 信号 | ESP32-S3 引脚 |
| --- | ---: |
| SCK / SCL | GPIO9 |
| MOSI / SDA | GPIO10 |
| DC | GPIO11 |
| RESET | GPIO12 |
| CS | GPIO13 |
| PWR_EN | GPIO14 |

### Ⅴ. 扬声器

MAX98357A 使用 I²S0。

| 信号 | ESP32-S3 引脚 |
| --- | ---: |
| BCLK | GPIO15 |
| WS / LRCLK | GPIO16 |
| DOUT | GPIO17 |

### Ⅵ. 耳机输出

NAU88C22 使用 I²S1，并通过共享 I²C0 总线配置。

| 信号 | ESP32-S3 引脚 |
| --- | ---: |
| DOUT | GPIO4 |
| WS / LRCLK | GPIO5 |
| BCLK | GPIO6 |
| MCLK | GPIO7 |
| HP_DET | GPIO18 |

### Ⅶ. SD 卡

SD 卡使用 SDMMC 1-bit 模式。

| 信号 | ESP32-S3 引脚 |
| --- | ---: |
| CLK | GPIO21 |
| CMD | GPIO47 |
| D0 | GPIO48 |
| Card Detect | PCF8575 bit 7 |

### Ⅷ. 电池检测

| 信号 | ESP32-S3 引脚 | 配置 |
| --- | ---: | --- |
| BAT_ADC | GPIO8 | VBAT → 1.8 MΩ → ADC → 100 kΩ → GND |

## 3. 分区

分区布局以 [partitions.csv](partitions.csv) 为准。

| 分区 | 类型 / 子类型 | 起始地址 | 大小 | 用途 |
| --- | --- | ---: | ---: | --- |
| `nvs` | data / nvs | `0x9000` | `0x6000`（24 KiB） | 非易失设置 |
| `app` | app / factory | `0x10000` | `0x300000`（3 MiB） | 固件程序 |
| `flash` | data / fat | `0x310000` | `0xCE0000`（12.875 MiB） | 内部 FAT 文件系统，挂载点为 `/flash` |
| `coredump` | data / coredump | `0xFF0000` | `0x10000`（64 KiB） | 崩溃转储 |
