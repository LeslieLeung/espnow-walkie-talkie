#include "walkie/bsp.hpp"

#include <M5Unified.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace walkie {

bool StickS3Bsp::initialize() {
    auto config = M5.config();
    config.internal_imu = false;
    config.internal_rtc = false;
    config.internal_mic = true;
    config.internal_spk = true;
    config.clear_display = true;
    M5.begin(config);
    if (M5.getBoard() != m5::board_t::board_M5StickS3) return false;
    if (M5.Display.width() > M5.Display.height()) {
        M5.Display.setRotation(M5.Display.getRotation() ^ 1U);
    }
    // LVGL's 16-bit draw buffer contains native RGB565 uint16_t values. M5GFX's
    // uint16_t pushImage overload otherwise treats the source as byte-swapped RGB565.
    M5.Display.setSwapBytes(true);
    M5.Display.setBrightness(128);
    M5.Speaker.end();
    return true;
}

ButtonEvents StickS3Bsp::poll_buttons() {
    M5.update();
    return ButtonEvents{M5.BtnA.wasPressed(), M5.BtnA.wasReleased(),
                        M5.BtnB.wasClicked(), M5.BtnB.wasHold()};
}

int StickS3Bsp::battery_percent() const {
    const int level = M5.Power.getBatteryLevel();
    return level < 0 ? 0 : (level > 100 ? 100 : level);
}

void StickS3Bsp::set_backlight(bool enabled) {
    M5.Display.setBrightness(enabled ? 128 : 0);
}

int StickS3Bsp::display_width() const { return M5.Display.width(); }
int StickS3Bsp::display_height() const { return M5.Display.height(); }

void StickS3Bsp::display_flush(int x, int y, int width, int height, const uint16_t* pixels) {
    M5.Display.startWrite();
    M5.Display.pushImage(x, y, width, height, pixels);
    M5.Display.endWrite();
}

bool StickS3Bsp::start_capture() {
    M5.Speaker.end();
    auto config = M5.Mic.config();
    config.sample_rate = 16000;
    config.dma_buf_len = 320;
    config.dma_buf_count = 4;
    M5.Mic.config(config);
    return M5.Mic.begin();
}

void StickS3Bsp::stop_capture() {
    while (M5.Mic.isRecording()) vTaskDelay(pdMS_TO_TICKS(1));
    M5.Mic.end();
}

bool StickS3Bsp::queue_capture(int16_t* samples, size_t sample_count) {
    return M5.Mic.record(samples, sample_count, 16000, false);
}

bool StickS3Bsp::start_playback(uint8_t volume_percent) {
    M5.Mic.end();
    M5.Speaker.setVolume(static_cast<uint8_t>((static_cast<uint16_t>(volume_percent) * 255U) / 100U));
    return M5.Speaker.begin();
}

void StickS3Bsp::stop_playback() {
    M5.Speaker.stop();
    M5.Speaker.end();
}

bool StickS3Bsp::play(const int16_t* samples, size_t sample_count) {
    return M5.Speaker.playRaw(samples, sample_count, 16000, false, 1, 0, false);
}

bool StickS3Bsp::tone(uint16_t frequency_hz, uint32_t duration_ms) {
    return M5.Speaker.tone(static_cast<float>(frequency_hz), duration_ms, 0, true);
}

}  // namespace walkie
