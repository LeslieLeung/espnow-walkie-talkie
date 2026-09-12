# bsp_passport

FoloToy [AI Passport](https://github.com/FoloToy/ai-passport) 板级支持的供应商子集，MIT 许可（见上游 [LICENSE](https://github.com/FoloToy/ai-passport/blob/master/LICENSE)）。

来源：`components/bsp` @ master（2026-09），移植时做了如下裁剪与补丁：

- 移除 `bsp_display_lvgl.c`（依赖 LVGL 9 / esp_lvgl_port，本工程用 LVGL 8 直连 `esp_lcd_panel_draw_bitmap`）
- 移除 `bsp_button.c` / `bsp_button.h`（依赖 espressif/button 回调模型，本工程在 `main/board_bsp_passport.cpp` 内直接轮询 ADC）
- `bsp_display.c`：新增 `bsp_display_wait_color_trans_done()` / `bsp_display_clear()`；颜色完成信号量在注册 SPI 回调之前创建；`trans_queue_depth = 1` 对齐单缓冲 wait
- `bsp_battery.c`：初始化不再阻塞等待首次 SOC（读失败由上层显示 0%）
- 其余文件（`bsp_pins.h` / `bsp_i2c` / `bsp_audio`）保持原样
