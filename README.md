# ESP-NOW Walkie-Talkie

半双工对讲机固件。设备之间用 ESP-NOW 直连，不需要 Wi-Fi 热点、手机或服务器。刷入互相兼容的固件、物理射频信道一致即可自动入网。

支持四块板：

- [M5Stack StopWatch](https://docs.m5stack.com/zh_CN/core/StopWatch) 与 [StickS3](https://docs.m5stack.com/en/core/StickS3)（ESP32-S3，M5Unified 自动识别，共用一个镜像，IDF 5.5.3）
- [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)（ESP32-C3，单独编译，IDF 5.5.3）
- [ESP-Mosaico](../../bsps/esp-mosaico-bsp/README.md)（ESP32-S31，单独编译，IDF 6.1）

StopWatch、StickS3、AI Passport 与 Mosaico 在协议版本和物理射频信道一致时可以互通。

## 功能

- 四路逻辑频道 CH1–CH4；按住说话，松开结束，单次最长 30 秒
- 可选 VOX：空闲时用 WebRTC VAD 检测说话，自动申请发言权；灵敏度 LOW / MED / HIGH 三档，按键仍可覆盖
- 同一频道同一时间仅一人发言，冲突时自动仲裁
- 在线列表：名称、电量、状态；设计目标为每频道约 8 台
- 音量 MUTE / 25% / 50% / 75%（上限 75%，避免电池供电过载重启）
- 频道、音量与 VOX 写入 NVS，重启后保留；出厂默认 CH1、50%、VOX 关
- 30 秒无操作后关背光并让面板进入 Sleep；按键、声控发射或来电唤醒后完整重绘

## 硬件

| 项目 | StopWatch | StickS3 | AI Passport | Mosaico |
| --- | --- | --- | --- | --- |
| 主控 | ESP32-S3R8 | ESP32-S3-PICO-1-N8R8 | ESP32-C3 | ESP32-S31 |
| Flash / PSRAM | 16MB / 8MB | 8MB / 8MB | 8MB / 无 | 16MB / octal |
| 显示 | 1.75" 圆形 AMOLED 466×466 | 135×240 ST7789 | 240×320 ST7789P3 | 480×480 CO5300 |
| 触屏 | CST820B | 无 | 无 | CST9217 |
| 音频 | ES8311（麦克风 + 扬声器） | 同左 | 同左 | 同左 |
| 按键 | KEYA / KEYB + 屏上 TALK/CH/MENU | A / B | 上 / 下 / 确定（ADC 分压） | AI（TALK）+ 屏上 TALK/CH/MENU |
| 电池 | 450 mAh（M5PM1） | 250 mAh | 520 mAh（CW2017 电量计） | BQ27220 |
| 设备名 | `SW-` + MAC 后四位 | `S3-` + MAC 后四位 | `AP-` + MAC 后四位 | `MO-` + MAC 后四位 |
| IDF | 5.5.3 | 5.5.3 | 5.5.3 | 6.1 |

StopWatch / StickS3 / AI Passport 镜像按 8MB Flash 打包，StopWatch 的 16MB 也能烧。Mosaico 镜像按 16MB 打包。AI Passport 板级支持来自 `folotoy/ai-passport` 的 BSP 子集，见 `components/bsp_passport/README.md`。界面统一用 LVGL 8.4 自刷，不走 mosaico 的 LVGL adapter。

**互通条件：** 协议版本一致，且 **物理 Wi-Fi 信道相同**（默认 6）。界面上的 CH1–CH4 只是逻辑频道，不会改射频。

## 按键

固件把输入抽象成 A（对讲）和 B（频道 / 菜单）。有电容触屏的板（StopWatch、Mosaico）另外提供屏上 TALK / CH / MENU，可与实体键同时用。

| 动作 | StopWatch | StickS3 | AI Passport | Mosaico |
| --- | --- | --- | --- | --- |
| 按住对讲 | 按住 KEYA 或 TALK | 按住 A | 按住 确定 | 按住 AI 或 TALK |
| 结束对讲 | 松开 KEYA / TALK | 松开 A | 松开 确定 | 松开 AI / TALK |
| 切换频道 CH1→CH4 | 短按 KEYB 或点 CH | 短按 B | 短按 上 或 下 | 点 CH |
| 打开菜单 | 长按 KEYB 或点 MENU | 长按 B | 长按 上 或 下 | 点 MENU |

息屏时：A / 确定 / TALK / AI 会唤醒并立刻进入对讲；B / 上 / 下 / CH / MENU 只唤醒，不切频道、不进菜单。收到有效对讲或 VOX 触发发射也会自动亮屏。触屏板上点空白处同样只亮屏。

长按 B / 上 / 下在任意状态下都可以打开菜单；本机正在发言（含 VOX）时会先结束发射。点 MENU 行为相同。短按切频道只在空闲时生效。菜单打开时禁用对讲和 VOX。菜单内：

| 动作 | StopWatch / StickS3 | AI Passport | StopWatch / Mosaico 触屏 |
| --- | --- | --- | --- |
| 下一项 / 滚动列表 | 短按 B | 短按 上 或 下 | 点列表 / 点下一项 |
| 确认 / 进入 / 保存音量或 VOX | 短按 A | 短按 确定 | 直接点对应行（音量/VOX 点即保存） |
| 返回 | 长按 B | 长按 上 或 下 | 点 BACK |

10 秒无操作回到主界面。

## 界面

主界面显示频道、电量、在线人数和当前状态。电量 ≤15% 时标 `LOW BAT`。

| 状态 | 含义 |
| --- | --- |
| `IDLE` | 空闲，可对讲 |
| `REQUESTING` | 正在申请发言权 |
| `TALKING` | 本机正在发言 |
| `RECEIVING` | 正在收听他人 |
| `BUSY` | 频道已被占用（本机按住对讲也会进入此状态） |
| `SIGNAL WEAK` | 信号弱或接收队列丢帧，音频可能断续 |
| `NO DEVICES` | 当前逻辑频道没有其他在线设备 |

菜单项：

- **DEVICES** — 当前逻辑频道上的在线设备（名称、电量、状态）
- **SETTINGS** — 音量 MUTE / 25% / 50% / 75%
- **VOX** — 声控发射 OFF / LOW / MED / HIGH。LOW 按贴麦说话设计，办公室里别人说话、自己对同事讲话不应触发；HIGH 最灵敏（接近旧版 ON）。旧固件的 VOX ON 升级后变为 MED。
- **EXIT** — 返回主界面

VOX 打开时主界面显示 `VOX L` / `VOX M` / `VOX H`。HIGH 开口约 60 ms、MED 约 160 ms、LOW 约 240 ms 后申请发言权；打开 LOW/MED 或改档时会先用约 0.4 s 估环境底噪，之后底噪会跨听麦会话保留。静音约 600 ms 后结束；开头约 240 ms 会从预录缓冲补上（只保留听麦音频，不含上一轮发射）。按住对讲键仍可强制发射，松开立即结束。收听他人时不会用说话抢麦。单次说到 30 秒上限后会响提示音，并约 1.5 s 内不再自动抢麦。LOW 需要对着设备说话。

## 编译与烧录

用 [eim](https://docs.espressif.com/projects/idf-im-cli/en/latest/) 管理 ESP-IDF。StopWatch / StickS3 / AI Passport 用 **v5.5.3**。ESP-Mosaico 用 **v6.1**（S31 仍是 preview target，要加 `--preview`）。不要把 S3/C3 工程放到 IDF 6 下编译。

S3、C3、S31 使用独立 CMake preset（`build/esp32s3`、`build/esp32c3`、`build/esp32s31` 及各自的 `sdkconfig`）。IDF 5.5.3 的 `idf.py` 还没有 `--preset`，用参数文件 `@presets/<target>`。IDF 6.1 用原生 `--preset`；`CMakePresets.json` 里的 `binaryDir` 必须是相对路径（例如 `build/esp32s31`），6.1 的 `idf.py` 不会展开 `${sourceDir}`。

```sh
# M5Stack StopWatch / StickS3
eim run 'idf.py "@presets/esp32s3" build' v5.5.3
eim run 'idf.py "@presets/esp32s3" -p <PORT> flash monitor' v5.5.3

# FoloToy AI Passport
eim run 'idf.py "@presets/esp32c3" build' v5.5.3
eim run 'idf.py "@presets/esp32c3" -p <PORT> flash monitor' v5.5.3

# ESP-Mosaico（IDF 6.1，S31 preview）
eim run 'idf.py --preview --preset esp32s31 build' v6.1
eim run 'idf.py --preview --preset esp32s31 -p <PORT> flash monitor' v6.1
```

`menuconfig` 同样要带 preset，例如 `idf.py "@presets/esp32s3" menuconfig` 或 `idf.py --preview --preset esp32s31 menuconfig`。不要用 `idf.py set-target` 切换。若本地还有旧的默认 `build/`（产物直接在 `build/` 根下），删掉后再用 preset，以免和 `build/esp32s3` 混在同一棵目录里。

StopWatch 进入下载模式：USB Type-C 接上电脑后，**长按电源键约 2 秒**直到绿色 LED 亮起再松开，然后 flash。

物理射频信道在 `idf.py menuconfig` → **ESP-NOW Walkie-Talkie** → **Physical Wi-Fi channel** 修改（默认 6）。同菜单里的 runtime diagnostics 默认关闭，生产固件不要打开。

AI Passport 控制台走 USB Serial/JTAG（GPIO18/19）。UART0 默认 TX 是 GPIO21，和 LEDC 背光脚冲突，不要改回 UART 控制台。

## 原理

```text
麦克风 → 16 kHz PCM →（空闲时 WebRTC VAD / 预录）→ IMA-ADPCM → ESP-NOW 广播
                                     ↓
扬声器 ← PCM ← ADPCM 解码 ← 抖动缓冲 ←
```

- 传输：ESP-NOW 广播，固定 2.4 GHz 物理信道
- 音频：约 20 ms/包，端到端延迟目标约 100–200 ms
- 发言权：`TALK_CLAIM` 仲裁 → `TALK_START` / `AUDIO` / `TALK_END`；对端丢失 `TALK_END` 时约 800 ms 后释放
- 在线：心跳约 1.8–2.2 秒一次；约 6 秒无包从列表移除

v1 **不提供**加密、鉴权、一对一私聊、OTA、跨物理信道发现。附近兼容设备可以侦听或注入流量；逻辑频道只隔离应用行为。

## 限制

- 室内穿一堵普通墙约 10–20 m，开阔约 50 m，这是期望值不是硬指标
- 每逻辑频道超过约 8 台不保证稳定
- 丢包表现为短暂音频缺陷，不应卡死或复位

AI Passport 额外注意：

- 电量计不可用或尚未就绪时显示 0%
- 按键是 ADC 分压，同一时间只识别一个键；松开需约 50 ms 稳定采样，避免发射时误松 PTT

## 主机测试

不依赖板子即可跑协议与状态机测试：

```sh
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```
