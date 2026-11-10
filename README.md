# GAMEBOY · AI BOX1 30 合 1

一块 **正点原子 ESP32 AI BOX1（DNESP32S3B）** 上的掌机风格反应/判断力训练合集：
**30 个小游戏、单列长列表、丝滑惯性滚动、每局一条专属 chiptune、成绩掉电保存**。

屏幕以 **240×320 原生竖屏**驱动，深色石墨配色，顶部是 Game Boy Color 风格的
`GAMEBOY` 彩虹字标。所有游戏平铺在一个列表里，不分组、不回卷。

> 固件基于 ESP-IDF v6.1 / C，只依赖 IDF 自带组件，没有额外的第三方库。
> UI 字体为小米 MiSans，
> 字模已生成并随仓库提交，编译固件不需要 Python 或字体文件。
>
> 仓库：<https://github.com/chenz-ruo/esp32-s3-gameboy-30in1>

---

## 1. 一眼看懂

| 项 | 值 |
| --- | --- |
| 主控 | ESP32-S3 v0.2，16 MB Flash，8 MB Octal PSRAM @80 MHz |
| 显示 | ST7789，8080 **8 位并口**，物理 320×240，本项目按原生 **240×320 竖屏**使用 |
| 刷新 | `pclk = 20 MHz`，整屏约 7.7 ms；菜单按脏行带局部刷新 |
| 音频 | ES8311 + I2S，44.1 kHz 立体声，30 条手写方波旋律 + 短音效 |
| 输入 | 三键：K0（BOOT/GPIO0）、K1（XL9555 P0_4）、K2（XL9555 P0_3） |
| 存档 | NVS 命名空间 `box1rec`（成绩）/ `box1aud`（音量） |
| 状态机 | `MENU → INTRO → PLAY → RESULT → MENU` |

## 2. 操作

| 键 | 菜单里 | 游戏里 |
| --- | --- | --- |
| K0 | 向下选一项；**长按**连续加速滚动（0.3 s 后每 60 ms 一步，0.9 s 后每 26 ms 一步） | 音量 +10% |
| K1 | 向上选一项；长按同上 | 音量 −10% |
| K2 短按 | 进入游戏 / 结算页返回 | 游戏主操作（按下、抢答、确认） |
| K2 **长按 > 0.6 s** | — | 放弃本局、返回菜单 |
| **K0+K1+K2 同时按住 1 s** | 清空全部成绩（有提示音） | 不生效 |

列表滑到底会**停住**，不会绕回顶部；右侧进度条常显并跟随缓动位置。

## 3. 30 个游戏

平铺单列，顺序即 `main/app.c` 里 `GAMES[]` 的顺序：

| # | 游戏 | 类型 | # | 游戏 | 类型 |
| --- | --- | --- | --- | --- | --- |
| 01 | 极限反应 REFLEX | 反应 | 16 | 同色判断 SWATCH | 判定 |
| 02 | 抢跑抑制 NO-GO | 抑制 | 17 | 箭头一致 ARROW | 判定 |
| 03 | 节奏环 RHYTHM | 节奏 | 18 | 递增判断 GROW | 判定 |
| 04 | 移动区间 ZONE | 时机 | 19 | 字母数字 ALPHA | 判定 |
| 05 | 停表 STOPWATCH | 时间感 | 20 | 双重条件 DOUBLE | 判定 |
| 06 | 连打 TAP | 手速 | 21 | 时间感知 TIME | 判定 |
| 07 | 等长按压 HOLD | 时长感 | 22 | 重复符号 REPEAT | 记忆 |
| 08 | 奇偶判定 PARITY | 判定 | 23 | 大数判断 BIG | 判定 |
| 09 | 大小比较 COMPARE | 判定 | 24 | 奇数判断 ODD | 判定 |
| 10 | 计数 COUNT | 记忆 | 25 | 尾数七 SEVEN | 判定 |
| 11 | 延迟匹配 MATCH | 记忆 | 26 | 三倍数 TIMES3 | 判定 |
| 12 | 抗干扰 STROOP | 抑制 | 27 | 尾数过半 HIGH | 判定 |
| 13 | 心算 MATH | 计算 | 28 | 区间判断 MID | 判定 |
| 14 | 规则反转 FLIP | 灵活度 | 29 | 元音判断 VOWEL | 判定 |
| 15 | 数位和 DIGSUM | 判定 | 30 | 图形判断 SHAPE | 判定 |

每个游戏的规则、回合数、指标方向和评级方式见 **[docs/GAMES.md](docs/GAMES.md)**。

## 4. 编译与烧录

本机开发环境（Windows + ESP-IDF v6.1，工具链在 `C:\esp` / `C:\Espressif`）：

```powershell
cd <仓库根目录>
.\build.ps1 build                 # 或 .\build.ps1 flash -Port COM4
.\build.ps1 monitor -Port COM4
```

若 PowerShell 报「禁止运行脚本」，改用
`powershell -ExecutionPolicy Bypass -File .\build.ps1 build`。

`build.ps1` 只是设好 `IDF_PATH` / `IDF_TOOLS_PATH` / `IDF_PYTHON_ENV_PATH` /
`ESP_IDF_VERSION` / `ESP_ROM_ELF_DIR`、把工具链加进 PATH 后调用 `idf.py`，项目路径取脚本所在目录；
换机器用环境变量覆盖这几个值即可。也可以直接用标准命令：

```bash
idf.py -C . set-target esp32s3
idf.py -C . build
idf.py -C . -p COM4 -b 921600 flash monitor
```

**注意**：串口控制台固定 **115200**（`sdkconfig.defaults` 里的其他值会被 confgen 复位）；
`-b 921600` 是烧录波特率，与控制台无关。CH343 桥接的 COM 口偶尔需要重新插拔才枚举。

详见 **[docs/BUILD.md](docs/BUILD.md)** 与 **[docs/HARDWARE.md](docs/HARDWARE.md)**。

## 5. 目录

```
esp32box/
  main/
    board.h          引脚与 XL9555 位定义（硬件真相）
    bsp.c/h          i80 屏 / DMA 同步 / 扩展 IO / 按键 / 背光
    gfx.c/h          240×320 RGB565 帧缓冲、绘制原语、局部刷新
    ui.c/h           页头、卡片、说明行、GAMEBOY 字标、UTF-8 自动换行
    app.c            菜单动画、状态机、成绩、30 个游戏的注册表
    game.h           Game 结构体契约；games.h 平铺声明 30 个实例
    games_reflex.c   01 极限反应
    games_react.c    02-04 抢跑抑制 / 节奏环 / 移动区间
    games_misc.c     05-06 停表 / 连打
    games_batch2.c   07-09 等长按压 / 奇偶判定 / 大小比较
    games_batch3.c   10-14 计数 / 延迟匹配 / 抗干扰 / 心算 / 规则反转
    games_quickfire.c 15-30 通用判定引擎 + 16 个游戏
    audio.c/h        音效、音量、方波 BGM 音序器
    tracks.c/h       30 条手写旋律（索引与游戏编号一一对应）
    font_data.c      tools/gen_font_box.py 生成，勿手改
    vendor/          官方 23_music(ES8311) 例程的 es8311 / I2S / I2C shim
  tools/
    gen_font_box.py  扫描源码字符 → 生成 font_data.c（需 Pillow + MiSans）
    serial_shot.py   串口抓帧 → PNG（需 pyserial）
  docs/              GAMES / BUILD / HARDWARE / QA
  sdkconfig.defaults / sdkconfig / partitions.csv
  build.ps1          一键构建 / 烧录
```

## 6. 几个实现要点

- **为什么是竖屏**：横屏靠 `swap_xy` 旋转时，写一行等于面板写一列，无 TE 引脚下
  撕裂表现为"从左到右逐步消失"的竖线 wipe。改原生竖屏后撕裂线变成横向，
  与纵向列表同向，观感大幅改善；`pclk` 同时提到 20 MHz 缩短传输窗口。
- **局部刷新**：`gfx_present_band(y0,y1)` 刷脏行带，`gfx_present_strip()` 刷右侧
  进度条窄带；首帧和从游戏返回菜单时强制整屏，避免上电花屏和残影。
  菜单缓动用 `k = 1 - exp(-dt/85ms)`，动画期间不再叠加固定 60 Hz 延时。
- **DMA 同步**：`bsp_lcd_flush()/wait()` 用 `s_trans_done` / `s_pending_id`
  传输计数等待，**不能**用二值信号量（残留 token 会让等待提前返回）。
- **字体**：`font_14/18/28/52` 四套 4bpp 抗锯齿字模。`font_52` 必须显式带上
  `红绿蓝←●▲■◆★○△□◇`，否则「抗干扰」等 7 个游戏的大字会空白。
  改过任何 UI 文案后要重跑 `python tools/gen_font_box.py`。
- **音频**：`es8311_init()` 结束时是静音且音量为 0，必须在之后显式
  `es8311_set_voice_volume(65)` + `es8311_set_voice_mute(0)`；功放使能是
  **高电平有效**。DAC 增益固定，音量只在数字域调，串联两级衰减会让 40% 近乎无声。
  常驻 `audio_task` 空闲时写静音防 I2S underrun；SFX 优先级高于 BGM。
- **成绩**：`METRIC_LOWER_BETTER_MS`（时间/误差）取最小值，其余取最大值；
  加载时会丢弃"越小越好"类型里的负数旧纪录，避免不可打破的死纪录。

## 7. 串口调试协议

115200 8N1，向 UART0 发单字节命令：

| 字节 | 作用 |
| --- | --- |
| `s` / `S` | 回传当前帧：`SS` + w(u16) + h(u16) + len(u32) + `[count][lo][hi]` RLE 游程 |
| `p` / `P` | 注入一次短按 / 长按（等同 K2） |
| `n` | 菜单向下移一项 |
| `i` | 打印 `>where=MENU` 或 `>where=PLAY:REFLEX` 等状态 |

```bash
python tools/serial_shot.py --port COM4 --out menu.png
python tools/serial_shot.py --port COM4 --cmd p --out play.png
python tools/serial_shot.py --port COM4 --info
```

## 8. 已知边界与后续

- 背光由 XL9555 **纯开关**控制，ST7789 的 `0x51` 亮度寄存器在这块模组上不起作用；
  "调暗"靠深色主题实现。上电偶发读到按键按住会让音量逐级 −10%，
  目前用「音量 < 40 自动回到 80」兜底，根因未彻底定位。
- 「计数 COUNT」一局偏长（5 回合），24 s 自动化窗口内常打不完，可压到 4 回合。
- 判级阈值（闪电/优秀/良好/一般/迟钝）已按脚本概率分布校验逻辑，真人手感仍可再调。
- 原计划 99 个游戏，目前完成 30 个；后续新增游戏仍保持单列平铺、不分组。
- 完整验收记录与已修 bug 清单见 **[docs/QA.md](docs/QA.md)**。

## 9. 来源与许可

- `main/vendor/` 下的 ES8311 / I2S / I2C 驱动移植自板卡厂商官方
  `23_music(ES8311)` 例程，仅做了 IDF 6.1 头文件适配与项目内 I2C 总线对接。
- UI 字模由小米 **MiSans** 生成，使用前请遵守小米官方的 MiSans 字体授权。若换字体，重跑
  `tools/gen_font_box.py` 即可；生成结果 `main/font_data.c` 已随仓库提供。
- 其余代码为本项目的原创实现。硬件资料以板卡厂商 A 盘例程为准。
