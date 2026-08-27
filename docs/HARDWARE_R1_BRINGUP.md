# FAMI32 现行实体硬件：编译、烧录与分阶段测试

本文对应 `Netlist_FAMI_807_2026-08-20.net`、OLED 子板网表及随附器件资料，目标模块为 ESP32-S3-WROOM-1-N16R8（16 MB Flash、8 MB Octal PSRAM）。所有实体引脚集中定义在 `main/include/hardware/fami32_pin.h`。

## 1. 已适配的硬件

| 功能 | 现行硬件连接 |
| --- | --- |
| I2C0 | GPIO38=SCL、GPIO39=SDA，400 kHz |
| PCF8575 | 地址 `0x20`，INT=GPIO40；矩阵、音量键、SD 检测及 MAX98357A 使能 |
| NAU88C22 | 地址 `0x1A`；I2S1：MCLK=GPIO7、BCLK=GPIO6、FS=GPIO5、DACIN=GPIO4 |
| MAX98357A | I2S0：BCLK=GPIO15、LRC=GPIO16、DIN=GPIO17；使能由 PCF8575 P06 控制 |
| 耳机检测 | GPIO18，低电平表示插入 |
| 编码器 1 | A=GPIO41、B=GPIO42；按压位于矩阵 H2/KEY3_IN |
| 编码器 2 | A=GPIO2、B=GPIO1；按压位于矩阵 H1/KEY3_IN |
| OLED | 128x64 SSD1306 SPI：SCK=GPIO9、MOSI=GPIO10、DC=GPIO11、RST=GPIO12、CS=GPIO13、PWR_EN=GPIO14 |
| SD 卡 | SDMMC 1-bit：CLK=GPIO21、CMD=GPIO47、D0=GPIO48；检测由 PCF8575 P07 提供 |
| 电池采样 | GPIO8/ADC1_CH7；分压为 1.8 MΩ / 100 kΩ |
| 原生 USB | GPIO19=D-、GPIO20=D+ |

固件固定使用 48 kHz、16-bit、Philips I2S。启动音频时先发送零样本，再解除 NAU88C22 静音并使能 MAX98357A；检测到耳机插入后会关闭扬声器功放。

ETA6002 的充电指示灯由芯片 `STAT` 引脚直接驱动，MCU 没有连接到该网络。它只能通过实际 USB 供电与充电状态验证，固件不会也不应模拟控制它。

## 2. 实体输入映射

PCF8575 以 H1-H6 为扫描行，以 L1-L5/KEY3_IN 为列。工厂模式会显示全部 6x6 原始状态；应用层只发送已有 UI 能理解的事件。

| 实体输入 | 固件动作 |
| --- | --- |
| 编码器 1 逆/顺时针 | `UP` / `DOWN` |
| 编码器 1 按压 | `OK` |
| 编码器 2 逆/顺时针 | `L` / `R` |
| 编码器 2 按压 | `BACK` |
| SW21 | `MENU` |
| SW22 | `NAVI` |
| SW23 | `S` |
| SW24 | `P` |
| SW25 | `OCTD` |
| SW26 | `OCTU` |
| SW1-SW14 | 14 个半音阶演奏/输入键 |
| SW15 | `NOTE END` |
| SW16 | `NOTE CUT` |
| SW17-SW20、SW27-SW28 | 保留空闲；仅在工厂模式显示原始矩阵状态 |
| VOL- / VOL+ | 主音量减/加，范围 0-64 |

网表没有提供面板丝印含义，因此所有推断映射集中在 `main/src/input/keypad_io.cpp` 顶部。若首板发现编码器机械方向与面板方向相反，只需交换对应编码器的顺/逆时针逻辑键；不要交换 GPIO 或改动矩阵扫描。

## 3. 存储行为

- `/flash` 是片内 FAT 分区，始终存放 `FM32CONF.CNF`，也是 USB MSC 暴露给电脑的磁盘。
- 开机时检测并挂载 FAT32 SD 卡到 `/sdcard`。挂载成功后，乐曲、导出文件和样本文件浏览优先使用 `/sdcard`。
- 未插卡或挂载失败时，数据文件自动回退到 `/flash`。
- 当前版本不做运行中热插拔重挂载。请在关机状态插拔 SD 卡，插卡后重新启动。

## 4. 编译

推荐使用仓库锁定时验证过的 ESP-IDF 5.5.x；`dependencies.lock` 当前记录 5.5.5。不要使用 Arduino IDE 编译此工程。

```sh
git clone --recursive https://github.com/z1tr0neDH/Fami32.git
cd Fami32
git submodule update --init --recursive
. "$IDF_PATH/export.sh"
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Windows PowerShell 先从 ESP-IDF 终端进入仓库，或运行对应安装目录的 `export.ps1`，其余 `idf.py` 命令相同。

构建配置应显示：

- target：`esp32s3`
- Flash：16 MB、DIO、80 MHz
- PSRAM：Octal、80 MHz、供 `malloc` 使用
- 自定义分区表：`partitions.csv`

分区布局如下：

| 分区 | 地址 | 大小 |
| --- | --- | --- |
| `nvs` | `0x9000` | `0x6000` |
| `app` | `0x10000` | `0x300000` |
| `flash` | `0x310000` | `0xCE0000` |
| `coredump` | `0xFF0000` | `0x10000` |

## 5. 首次烧录

使用支持数据传输的 USB 线连接原生 USB 接口。

1. 按住 `BOOT`。
2. 点按并释放 `RESET`。
3. 释放 `BOOT`，设备进入 ESP32-S3 ROM 下载模式。
4. 查找串口：Windows 通常为 `COMx`，Linux 通常为 `/dev/ttyACM0` 或 `/dev/ttyUSB0`。
5. 因为本适配把旧 8 MB 分区布局迁移为 16 MB，第一次必须擦除整片 Flash 后再烧录：

```sh
idf.py -p PORT erase-flash
idf.py -p PORT flash monitor
```

以后普通更新只需：

```sh
idf.py -p PORT flash monitor
```

将 `PORT` 替换为实际端口，例如 `COM7` 或 `/dev/ttyACM0`。监视器中按 `Ctrl+]` 退出。首次正常启动若找不到 `/flash/FM32CONF.CNF`，固件会创建默认配置并自动重启一次，这是预期行为。

## 6. 分阶段上板测试

### 阶段 0：不上电检查

- 万用表确认 3V3-GND、5V-GND 没有硬短路。
- 确认 OLED 排线方向、SD 卡方向、耳机座触点和扬声器极性。
- 首次供电建议使用限流电源；先确认 3V3 稳定，再连接电池和扬声器。

### 阶段 1：最小启动

- 不插 SD、耳机和扬声器，烧录后观察串口。
- OLED 应上电并出现启动画面。
- 日志应出现 I2C0 初始化、PCF8575 `0x20` 响应；不应循环复位或出现 brownout。
- 首次创建配置后只应自动重启一次。

### 阶段 2：工厂自检

冷启动或复位时同时按住 `VOL-` 与 `VOL+`，进入三页工厂模式。松开两键后操作：`OK` 翻页，`BACK` 重启退出。

第一页检查：

- `PCF8575: PASS`
- `NAU ID: 01A PASS`
- SD 插入时 `SD: PASS`
- 耳机插拔时 `HP: OUT/IN` 正确切换
- BAT 毫伏值与万用表实测接近；百分比只是 3.3-4.2 V 的粗略线性估算
- `CHG LED: HARDWARE` 仅说明该项需人工观察

### 阶段 3：矩阵、按键与编码器

- 工厂模式第二页逐个按键，确认每次只改变一个 H 行的一个位；串口同时打印 36-bit `matrix` 原始值。
- SW17-SW20、SW27-SW28 应在原始矩阵中响应，但不在正常 UI 产生动作。
- 第三页转动两个编码器，数值应每个机械刻度稳定变化 1；不应随机反跳或一次跳多格。
- 进入正常 UI 后核对 `UP/DOWN`、`L/R`、`OK/BACK` 及 SW21-SW26 的动作与上表一致。
- 分别按 VOL-/VOL+，串口中的主音量应单步减/加并限制在 0-64。

### 阶段 4：扬声器与耳机音频

- 先把主音量调低，再连接扬声器并演奏一个音符。
- 扬声器应由 MAX98357A 播放，无持续直流爆音；左右声道数据相同。
- 插入耳机后，扬声器应立即静音，耳机左右声道由 NAU88C22 播放。
- 拔出耳机后扬声器恢复。反复插拔时不应出现复位或长时间爆音。
- 若只有耳机无声，先在工厂页确认 NAU ID，再用示波器依次检查 GPIO7 MCLK、GPIO6 BCLK、GPIO5 FS、GPIO4 DACIN。

### 阶段 5：SD 与片内 FAT

- 关机插入 FAT32 SD 卡，重启后工厂页应显示 `SD: PASS`。
- 正常模式保存 `.ftm`、导出文件并重新打开，路径应位于 `/sdcard`。
- 关机拔卡后重启，再次保存应回退到 `/flash`。
- 进入 USB MSC 后，电脑看到的是片内 `/flash`，不是 SD 卡；退出前先在电脑安全弹出磁盘。

### 阶段 6：应用功能回归

- 逐一验证 Tracker、Channel、Frames、Instrument、DPCM、保存/另存为和 VGM/WAV 导出。
- SW1-SW14 应演奏音符；SW15/SW16 应分别录入 NOTE END/CUT。
- Channel 十六进制输入和屏幕键盘只接受 SW1-SW16，不受保留键干扰。
- 验证 USB MIDI 输入；启用 MIDI OUT 后验证 Note On/Off。

### 阶段 7：电源与充电

- 分别验证仅 USB、仅电池、USB+电池三种供电状态。
- USB 接入且 ETA6002 正在给电池充电时观察充电 LED；充满或未充电时其状态由 ETA6002 决定。
- 长时间播放后检查 TPS63020、TLV757P、NAU88C22、MAX98357A 和电感温升，确认没有异常发热或 brownout。

## 7. 故障定位顺序

按 `供电 -> 串口启动 -> I2C/PCF -> OLED -> 工厂矩阵 -> I2S 时钟 -> SD -> 完整 UI` 的顺序排查。不要在 I2C 或电源尚未通过时直接追查应用层页面；工厂模式和串口原始值就是首板定位的基准。
