// components/bsp/include/bsp_display.h
// ST7789P3 240x320 显示:SPI 面板初始化 + 厂商专属寄存器 + LEDC 背光调光。
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 SPI 总线、面板、厂商寄存器、背光 LEDC。成功后屏幕已上电、背光仍为 0。
esp_err_t bsp_display_init(void);

// 用 RGB565 填满整屏并等到 DMA 结束。开机去花屏时在拉高背光之前调用。
esp_err_t bsp_display_clear(uint16_t rgb565);

// 取底层面板句柄。想直接 esp_lcd_panel_draw_bitmap 画,或接 LVGL 以外的 GUI 时用。
// 未初始化返回 NULL。
esp_lcd_panel_handle_t bsp_display_panel(void);

// 取底层 panel io 句柄(LVGL 接入需要)。未初始化返回 NULL。
esp_lcd_panel_io_handle_t bsp_display_io(void);

// 背光亮度 0..100(%)。LEDC PWM,0=全灭。
void bsp_display_backlight(uint8_t percent);

// esp32c3-walkie 移植补丁:阻塞等待最近一次 esp_lcd_panel_draw_bitmap 的颜色
// 传输完成(DMA 已读完整块用户缓冲)。单缓冲 LVGL flush 必须在返回前调用。
// timeout_ms 内未完成返回 false。
bool bsp_display_wait_color_trans_done(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
