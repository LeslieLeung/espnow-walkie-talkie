# ESP-NOW Walkie-Talkie

基于 ESP-NOW 的半双工对讲机固件，面向 [M5Stack StickS3](https://docs.m5stack.com/en/core/StickS3)。多台刷同一固件的设备可自动加入频道，无需 Wi-Fi 热点、手机或服务器。

## 功能

- 四路逻辑频道（CH1–CH4），按住说话、松开结束
- 同一频道同一时间仅一人发言，冲突时自动仲裁
- 在线设备列表（名称、电量、状态）
- 音量：静音 / 25% / 50% / 75%（上限 75%，避免电池供电下过载重启）
- 频道与音量写入 NVS，重启后保留
- 背光 30 秒无操作自动关闭；来电或按键可唤醒
- 设计目标：每频道最多约 8 台在线设备

## 硬件

| 项目 | StickS3 |
| --- | --- |
| 主控 | ESP32-S3-PICO-1-N8R8 |
| 显示 | 135×240 ST7789 |
| 音频 | ES8311（麦克风 + 扬声器） |
| 按键 | A / B |
| 电池 | 250 mAh |

所有参与设备必须使用**同一固件**，并配置**相同的物理 Wi-Fi 信道**（默认 6）。界面中的 CH1–CH4 是逻辑频道，不会切换射频信道。

## 使用

### 主界面

| 操作 | 行为 |
| --- | --- |
| 按住 A | 申请发言并开始对讲（最长 30 秒） |
| 松开 A | 结束发言 |
| 短按 B | 切换 CH1 → CH2 → CH3 → CH4 |
| 长按 B | 打开菜单 |

背光关闭时：按 A 会唤醒并立刻进入对讲；按 B 仅唤醒，不切换频道；收到有效对讲会自动亮屏。

主界面状态包括：`IDLE`、`REQUESTING`、`TALKING`、`RECEIVING`、`BUSY`、`SIGNAL WEAK`、`NO DEVICES`。

### 菜单

长按 B 进入菜单后：

- 短按 B：下一项
- 短按 A：确认 / 进入
- 长按 B：返回
- 10 秒无操作回到主界面
- 菜单打开时禁用对讲，避免误发

菜单项：

- **DEVICES** — 当前逻辑频道上的在线设备
- **SETTINGS** — 音量（MUTE / 25% / 50% / 75%）
- **EXIT** — 返回主界面

设备名默认为 `S3-` + MAC 后四位十六进制。首次启动默认 CH1、音量 50%。

## 原理概要

```text
麦克风 → 16 kHz PCM → IMA-ADPCM → ESP-NOW 广播
                                    ↓
扬声器 ← PCM ← ADPCM 解码 ← 抖动缓冲 ←
```

- 传输：ESP-NOW 广播，固定 2.4 GHz 物理信道
- 音频：约 20 ms/包，端到端延迟目标约 100–200 ms
- 发言权：`TALK_CLAIM` 仲裁 → `TALK_START` / `AUDIO` / `TALK_END`
- 在线：约每 2 秒心跳；约 5–6 秒无包则从列表移除

v1 **不提供**加密、鉴权、一对一私聊、OTA、跨物理信道发现。附近兼容设备可能侦听或注入流量；逻辑频道只隔离应用行为。

## 构建与烧录

需要 ESP-IDF **v5.4.x**（开发与验证基于 v5.4.4）。

```sh
source "$HOME/.espressif/v5.4.4/esp-idf/export.sh"
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

每台 StickS3 烧录同一镜像。物理射频信道可在 `idf.py menuconfig` → **ESP-NOW Walkie-Talkie** → **Physical Wi-Fi channel** 中修改（默认 6）。

## 主机测试

不依赖板子即可跑协议与状态机相关测试：

```sh
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## 限制与预期

- 室内穿一堵普通墙约 10–20 m；开阔约 50 m 为期望值，非硬性指标
- 每逻辑频道超过约 8 台设备不保证稳定
- 丢包表现为短暂音频缺陷，不应卡死或复位
- 发射端丢失 `TALK_END` 时，接收端会在短超时后释放发言权
