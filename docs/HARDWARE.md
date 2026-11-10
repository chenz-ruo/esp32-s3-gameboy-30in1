# 硬件映射与驱动踩坑记录

板卡：**正点原子 ESP32 AI BOX1 / DNESP32S3B**。以下内容都对照厂商 A 盘例程
（`10_lcd`、`02_key`、`23_music(ES8311)`）和实机现象核对过，不是推测。
唯一硬件真相在 `main/board.h`。

## 1. 引脚

### LCD：ST7789，8080 8 位并口（不是 SPI）

| 信号 | GPIO | 信号 | GPIO |
| --- | --- | --- | --- |
| CS | 1 | D0 | 40 |
| DC | 2 | D1 | 39 |
| WR | 42 | D2 | 38 |
| **RD** | **41** | D3 | 12 |
| D4 | 11 | D5 | 10 |
| D6 | 9 | D7 | 46 |

物理面板是 320×240，本项目以 **原生 240×320 竖屏**驱动
（`swap_xy(false)` + `mirror(false,false)` + `MADCTL 0x00`）。

### I2C / 扩展 IO / 音频

| 项 | 值 |
| --- | --- |
| I2C0 | SCL = 45，SDA = 48（XL9555、ES8311、触摸共用） |
| XL9555 | 地址 `0x20`；`bsp_i2c_bus()` 把总线句柄交给 `vendor/myiic.c` |
| ES8311 | I2S BCK = 21、WS = 13、DOUT = 14、DI = 47、**MCLK = NC**（用 BCLK） |
| 采样率 | 44100 Hz，16 bit 立体声 |

### XL9555 位定义与极性（容易反）

| 位 | 名称 | 极性 / 用途 |
| --- | --- | --- |
| P0_0 | `AP_INT` | 输入 |
| P0_1 | `QMA_INT` | 输入 |
| P0_2 | `BEEP` | **低有效**；上电必须写 1（静音），否则一直响 |
| P0_3 | `K2` | 输入，丝印 **K2** |
| P0_4 | `K1` | 输入，丝印 **K1** |
| P0_5 | `SPK_EN` | **高有效**：写 1 = 功放打开 |
| P0_6 | `CTP_RST` | — |
| P0_7 | `LCD_BL` | **高有效**：背光开关（仅开关，无 PWM） |
| P1_0 | `LED_R` | **低有效**，本项目写 1 关闭 |

> 厂商 BSP 的宏按扩展 IO 命名，和丝印**错开一位**：`KEY0_IO`(P0_4) 是丝印 K1，
> `KEY1_IO`(P0_3) 是丝印 K2，而丝印 **K0 是 GPIO0 的 BOOT 键**。
> 早期资料写"功放低有效"是错的：实际高有效，按低有效处理会一直没声音。
> 板载两个 LED 本项目都关闭：GPIO4 蓝 LED 写高、扩展 IO `LED_R` 写高。

## 2. 两个必须记住的初始化坑

1. **RD (GPIO41) 必须软件接管并拉高。**
   `esp_lcd` 的 i80 驱动只配置 DC / WR / 数据线 / CS，**不碰 RD**。RD 悬空时
   面板数据总线停在读态：命令偶尔能进、像素数据全被丢弃，表现为"花屏但
   DISPOFF 有反应"。必须在创建 i80 总线前：
   ```c
   gpio_config(&(gpio_config_t){ .mode = GPIO_MODE_INPUT_OUTPUT,
                                 .pin_bit_mask = 1ull << 41,
                                 .pull_up_en = GPIO_PULLUP_ENABLE });
   gpio_set_level(41, 1);
   ```
2. **COLMOD 用 `0x65` 而不是 `0x55`。**
   `esp_lcd` 的 st7789 驱动按 16bpp 发 `0x3A=0x55`，这块面板按厂商例程要
   `0x3A=0x65`；写错就是一片乱色。初始化顺序里同时显式写 `0x36=0x00`。

另外 `esp_lcd_panel_invert_color(panel, true)` 与厂商一致，颜色才正常。

## 3. 撕裂、竖屏与刷新

这块屏**没有 TE（ tearing effect ）引脚**，无法做到真正的帧同步，
只能尽量缩短传输窗口并让撕裂方向与内容同向：

| 手段 | 作用 |
| --- | --- |
| 原生竖屏（关 `swap_xy`） | 横屏 MV 旋转时"写一行 = 面板写一列"，撕裂是**从左到右的竖线 wipe**；竖屏后撕裂线变成横向，纵向列表里几乎看不出来 |
| `pclk_hz = 20 MHz` | 整屏 240×320×2 ≈ 150 KB，约 **7.7 ms**（10 MHz 时约 15 ms） |
| `gfx_present_band(y0,y1)` | 只刷菜单滚动涉及的横向脏行带，不推整屏 |
| `gfx_present_strip(x,y,w,h)` | 单独刷右侧进度条的窄竖条，打包成一次小传输 |
| 首帧 / 返回菜单强制整屏 | 上电显存是随机值，局部刷新会留下花屏和"两项同时选中"的假象 |
| 单帧跳超过一行时整列表刷新 | 避免快跳时中间位置的滑块残影 |

### DMA 同步的正确写法

`bsp_lcd_flush()` / `bsp_lcd_wait()` 用**传输完成计数**而不是二值信号量：

```c
/* flush：必须在 draw_bitmap 之后记录，否则存在竞态 */
esp_lcd_panel_draw_bitmap(s_panel, x, y, x+w, y+h, px);
s_pending_id = s_trans_done + 1;

/* wait */
while ((int32_t)(s_trans_done - s_pending_id) < 0 && guard++ < 150)
    vTaskDelay(1);
```

用 `xSemaphoreTake()` 会拿到**上一帧残留的 token**，让等待提前返回，
下一帧绘制直接覆盖 DMA 正在读的缓冲，表现为缺行和拖影。
回调里 `s_trans_done++`，差值用有符号比较以兼容 32 位回绕。

## 4. 音频链路

```
audio_task ──> i2s_channel_write(tx_handle) ──> ES8311 (I2C 配置) ──> 功放 (XL9555 P0_5=1)
```

从厂商 `23_music(ES8311)` 例程移植了 `es8311.c/h`、`myi2s.c/h`，另加
`myiic.c/h` shim 把 `myiic_init()` 接到项目已有的 `bsp_i2c_bus()`。
IDF 6.1 里 `driver/i2s.h` 已不存在，`myi2s.c` 改用 `driver/i2s_std.h`；
旧 `driver/i2c.h` 的弃用警告靠 `CONFIG_I2C_SUPPRESS_DEPRECATE_WARN=y` 消除。

踩过的坑，按出现顺序：

1. `es8311_init()` 结束时会 `es8311_set_voice_mute(1)` 且音量为 0 → 之后必须
   `es8311_set_voice_volume(65)` + `es8311_set_voice_mute(0)`。
2. 功放使能为**高有效**，早期按注释写成低有效 → 完全无声。
3. DAC 增益和数字幅度**串联调音量**会让 40% 以下接近静音；对照官方例程后改成
   DAC 固定 65、音量只在数字域调（数字增益 0.80，留 headroom 防削波）。
4. 常驻任务空闲时也写静音帧，否则 I2S underrun 会有"咔哒"声。
5. `tracks.c` 里若留 `{0}` 占位项，`audio_bgm(0)` 会取到 `steps == NULL` 并崩溃，
   表现为"进第一个游戏就重启"。索引必须与游戏编号严格对齐，并做空指针保护。

BGM 是方波（可调脉宽），SFX 用带 2 ms 去咔哒包络的正弦短音，SFX 期间让出声道。

## 5. 亮度

背光只由 XL9555 P0_7 做**开关**，`bsp_backlight_level()` 尝试过 ST7789 的
`WRDISBV (0x51)` 寄存器（35%、5%），这块模组没有把背光驱动接到控制器 PWM，
实测**无任何变化**。接口保留但启动时仍写 100；"变暗"靠深色石墨主题
（`main/gfx.h` 的 `THEME_DARK=1`）实现。
