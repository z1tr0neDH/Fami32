# PC 模拟器

PC 模拟器独立位于 `desktop/`，使用根目录 `Makefile` 构建。实体机的 ESP-IDF 构建不包含该目录。

## 构建

### Linux

依赖：C/C++ 编译器、GNU Make、pkg-config、SDL2 开发包。

```sh
make linux
./build/linux/fami32
```

输出：`build/linux/fami32`

### Windows

依赖：MinGW-w64 和 GNU Make。

```sh
make windows
```

输出：`build/windows/fami32.exe`

## 键盘映射

| PC 按键 | FAMI32 功能 |
| --- | --- |
| `↑` / `↓` / `←` / `→` | `UP` / `DOWN` / `L` / `R` |
| `Enter` | `OK` |
| `Esc` | `BACK` |
| `Tab` | `MENU` |
| `Home` | `NAVI` |
| `Backspace` | `S` |
| `Space` | `P` |
| `Page Up` / `Page Down` | `OCTU` / `OCTD` |
| `Z S X D C V G B H N J M , L . ;` | 16 个琴键，按音高顺序排列 |

模拟器配置和文件保存在运行目录下的 `fami32_data/`。
