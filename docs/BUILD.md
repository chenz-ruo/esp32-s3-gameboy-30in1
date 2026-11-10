# 构建、烧录与调试

## 1. 环境

| 项 | 本项目实测值 |
| --- | --- |
| ESP-IDF | **v6.1.0**，`C:\esp\v6.1\esp-idf` |
| 工具链 | xtensa-esp-elf `esp-15.2.0_20251204`，在 `C:\Espressif\tools` |
| Python | IDF 自带 venv `C:\Espressif\tools\python\v6.1\venv` |
| 目标芯片 | `esp32s3`（`sdkconfig.defaults` 已固定） |
| 串口 | CH343 USB 转串口，Windows 上是 `COM4`，控制台 **115200** |

板子是 **16 MB Flash + 8 MB Octal PSRAM @80 MHz**（从启动日志核对过），
因此 `sdkconfig.defaults` 里打开了 `CONFIG_SPIRAM_MODE_OCT` 和 16 MB flash。
换板子时先核对这三项，否则 PSRAM 里的 240×320 帧缓冲分配不出来。

分区表 `partitions.csv`：`nvs 0x6000` / `phy_init 0x1000` /
`factory 0x300000` / `storage(fat) 0x100000`。

## 2. 一键脚本（Windows）

```powershell
cd <仓库根目录>
.\build.ps1 build                  # 编译
.\build.ps1 flash -Port COM4       # 编译并烧录
.\build.ps1 monitor -Port COM4     # 打开监视器（Ctrl+] 退出）
.\build.ps1 clean                  # fullclean
```

若 PowerShell 报「禁止运行脚本」（ExecutionPolicy 限制），用：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 build
```

脚本做三件事：设置 `IDF_PATH` / `IDF_TOOLS_PATH` / `IDF_PYTHON_ENV_PATH` /
`ESP_IDF_VERSION` / `ESP_ROM_ELF_DIR`、把 xtensa / cmake / ninja / ccache / venv
加进 PATH、调用 `idf.py -C <脚本所在目录>`。这些变量**如果已在环境里设置就不会被覆盖**，
换机器直接设环境变量即可，不用改脚本；项目路径取 `$PSScriptRoot`，
clone 到任意目录都能用。

> `ESP_IDF_VERSION` 不能省。`idf_component_manager` 会拿它和 `6.0.1` 做版本比较，
> 缺失时 `idf.py` 会在加载阶段直接抛
> `TypeError: expected string or bytes-like object, got 'NoneType'`。
> `ESP_ROM_ELF_DIR` 指向 `C:\Espressif\tools\esp-rom-elfs\20241011`，缺了会在
> CMake 阶段报 `Error while generating esp_rom gdbinit`（不影响出固件，但会少了调试符号脚本）。

板卡厂商的 EIM 激活脚本因为缺可选的 `riscv32-esp-elf-gdb` 组件会中途退出，
所以这里绕开它直接调 `idf.py`。

## 3. 标准命令（任意平台）

```bash
cd <仓库根目录>
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> -b 921600 flash monitor
```

`-b 921600` 是 **esptool 烧录波特率**，与固件 console 波特率无关。
`sdkconfig` 里 `CONFIG_ESP_CONSOLE_UART_BAUDRATE` 固定 **115200**：
试过写 921600，confgen 会把它复位，不必浪费时间。

`CONFIG_I2C_SUPPRESS_DEPRECATE_WARN=y` 是必须的：`main/vendor/es8311.h` 仍包含
旧版 `driver/i2c.h`，IDF 6.x 会给出弃用警告，而本工程按 `-Werror` 编译。
`main/vendor/myiic.c` 是个 shim，把厂商驱动的 `myiic_init()` 接到项目自己的
`bsp_i2c_bus()`，避免开第二条 I2C 总线。

## 4. 字体（只在改文案时需要）

`main/font_data.c`（约 750 KB）由 `tools/gen_font_box.py` 生成，**已提交**，
所以正常编译不需要 Python、Pillow 或字体文件。

```bash
pip install Pillow
python tools/gen_font_box.py
```

脚本扫描 `main/*.c`、`main/*.h`（先剥掉注释）里实际出现的字符，用
**MiSans-Regular / MiSans-Semibold** 光栅化出 `font_14 / 18 / 28 / 52` 四套
4bpp 抗锯齿字模，并在 `build/` 下留预览图。字体查找顺序：
`C:/Windows/Fonts` → `%LOCALAPPDATA%/Microsoft/Windows/Fonts`（小米字体通常装在后一处）。

`font_52` 的 `extra` 字符串 `红绿蓝←●▲■◆★○△□◇` **不能删**：大字游戏用的
中文字和几何符号不在 "digits" 基础字符集里，删掉会让 7 个游戏的刺激变成空白。

## 5. 串口调试协议

固件在主循环里非阻塞读取 UART0，单字节命令：

| 字节 | 作用 |
| --- | --- |
| `s` / `S` | 回传当前帧缓冲 |
| `p` | 注入一次 K2 短按（`held=40 ms`） |
| `P` | 注入一次 K2 长按 |
| `n` | 菜单下移一项 |
| `i` | 打印 `>where=MENU` / `>where=INTRO:REFLEX` / `>where=PLAY:REFLEX` / `>where=RESULT:REFLEX` |

截图帧格式（小端）：

```
'SS' | w:u16 | h:u16 | len:u32 | len 字节的 RLE
RLE 三元组: [run_count:u8][rgb565_lo:u8][rgb565_hi:u8]，run_count <= 255
```

```bash
pip install pyserial
python tools/serial_shot.py --port COM4 --out menu.png
python tools/serial_shot.py --port COM4 --cmd nnn --out scroll.png
python tools/serial_shot.py --port COM4 --info
```

`serial_shot.py` 只用标准库写 PNG（不需要 Pillow），脚本会先找 `SS` 同步头、
再校验宽高和长度，避免把混进来的日志文本当成图像数据。

## 6. 常见问题

| 现象 | 原因 / 处理 |
| --- | --- |
| `Could not open COM4, the port is busy or doesn't exist` | 监视器或上一次 esptool 还占着口；关掉后重新插拔 USB，等 CH343 重新枚举 |
| 设备管理器里只剩 COM1/COM2 | 板子没枚举出来，换 USB 口 / 数据线，或按 BOOT 重新插 |
| `idf.py` 报 `TypeError ... got 'NoneType'` | 没设 `ESP_IDF_VERSION=6.1.0`；用 `build.ps1` 或手动 export |
| PowerShell 报「禁止运行脚本」 | 用 `powershell -ExecutionPolicy Bypass -File .\build.ps1 build` |
| 编译报 `driver/i2c.h` deprecated | 确认 `sdkconfig` 里 `CONFIG_I2C_SUPPRESS_DEPRECATE_WARN=y` |
| 编译报 `Track.step_us` 溢出 | `tracks.h` 里 `step_us` 必须是 `uint32_t`（慢曲 70000–120000 µs 超过 u16） |
| 上电花屏 / 菜单只有半屏 | 首帧必须整屏刷新；`app.c` 的 `enter(ST_MENU)` 已置 `s_full_frame` |
| 完全没声音 | 检查 `es8311_set_voice_mute(0)`、功放使能写 **1**、NVS 里的音量是否被调到很低 |
| 字是空白 | 重跑 `gen_font_box.py`，确认 `font_52` 的 `extra` 字符集 |
| 屏幕调不暗 | 这块模组的背光是 XL9555 纯开关，`0x51` 寄存器无效；用深色主题代替 |

## 7. 成绩与音量

- 成绩：NVS 命名空间 `box1rec`，键 `b0..b29`（最佳）、`p0..p29`（次数）。
  菜单里 **K0+K1+K2 按住 1 秒** 全部清空。
- 音量：NVS 命名空间 `box1aud`，键 `vol`，游戏中 K0/K1 以 10% 步进调整。
  开机若读到 `< 40` 会自动恢复到 80 并写回（见 `docs/QA.md` 的已知问题）。
