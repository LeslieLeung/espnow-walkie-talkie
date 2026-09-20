#include "walkie/bsp.hpp"

#include <M5Unified.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace walkie {

bool BoardBsp::initialize() {
    auto config = M5.config();
    config.internal_imu = false;
    config.internal_rtc = false;
    config.internal_mic = true;
    config.internal_spk = true;
    config.clear_display = true;
    M5.begin(config);

    const auto detected = M5.getBoard();
    if (detected == m5::board_t::board_M5StickS3) {
        board_ = protocol::BoardType::StickS3;
        round_display_ = false;
        if (M5.Display.width() > M5.Display.height()) {
            M5.Display.setRotation(M5.Display.getRotation() ^ 1U);
        }
    } else if (detected == m5::board_t::board_M5StopWatch) {
        board_ = protocol::BoardType::StopWatch;
        round_display_ = true;
        has_touch_ = true;
        M5.Display.setRotation(0);
    } else {
        return false;
    }

    // LVGL's 16-bit draw buffer contains native RGB565 uint16_t values. M5GFX's
    // uint16_t pushImage overload otherwise treats the source as byte-swapped RGB565.
    M5.Display.setSwapBytes(true);
    M5.Display.setBrightness(128);
    display_awake_.store(true);
    M5.Speaker.end();
    return true;
}

const char* BoardBsp::name_prefix() const {
    switch (board_) {
        case protocol::BoardType::StopWatch: return "SW-";
        case protocol::BoardType::StickS3: return "S3-";
        default: return "WT-";
    }
}

ButtonEvents BoardBsp::poll_buttons() {
    M5.update();
    return ButtonEvents{M5.BtnA.wasPressed(), M5.BtnA.wasReleased(),
                        M5.BtnB.wasClicked(), M5.BtnB.wasHold()};
}

PointerSample BoardBsp::poll_pointer() {
    if (!has_touch_) return {};
    if (M5.Touch.getCount() == 0) {
        return PointerSample{true, false, 0, 0};
    }
    const auto detail = M5.Touch.getDetail(0);
    return PointerSample{true, detail.isPressed(), static_cast<int16_t>(detail.x),
                         static_cast<int16_t>(detail.y)};
}

int BoardBsp::battery_percent() const {
    const int level = M5.Power.getBatteryLevel();
    return level < 0 ? 0 : (level > 100 ? 100 : level);
}

bool BoardBsp::display_sleep() {
    if (!display_awake_.exchange(false)) return false;
    M5.Display.setBrightness(0);
    M5.Display.sleep();
    return true;
}

bool BoardBsp::display_wakeup() {
    if (display_awake_.load()) return false;
    M5.Display.wakeup();
    M5.Display.setBrightness(128);
    display_awake_.store(true);
    return true;
}

int BoardBsp::display_width() const { return M5.Display.width(); }
int BoardBsp::display_height() const { return M5.Display.height(); }

bool BoardBsp::display_flush(int x, int y, int width, int height, const uint16_t* pixels) {
    if (!display_awake_.load()) return false;
    M5.Display.startWrite();
    M5.Display.pushImage(x, y, width, height, pixels);
    M5.Display.endWrite();
    return true;
}

bool BoardBsp::start_capture() {
    M5.Speaker.end();
    auto config = M5.Mic.config();
    config.sample_rate = 16000;
    config.dma_buf_len = 320;
    config.dma_buf_count = 4;
    M5.Mic.config(config);
    return M5.Mic.begin();
}

void BoardBsp::stop_capture() {
    while (M5.Mic.isRecording()) vTaskDelay(pdMS_TO_TICKS(1));
    M5.Mic.end();
}

bool BoardBsp::queue_capture(int16_t* samples, size_t sample_count) {
    return M5.Mic.record(samples, sample_count, 16000, false);
}

bool BoardBsp::start_playback(uint8_t volume_percent) {
    M5.Mic.end();
    M5.Speaker.setVolume(static_cast<uint8_t>((static_cast<uint16_t>(volume_percent) * 255U) / 100U));
    return M5.Speaker.begin();
}

void BoardBsp::stop_playback() {
    M5.Speaker.stop();
    M5.Speaker.end();
}

bool BoardBsp::play(const int16_t* samples, size_t sample_count) {
    return M5.Speaker.playRaw(samples, sample_count, 16000, false, 1, 0, false);
}

bool BoardBsp::tone(uint16_t frequency_hz, uint32_t duration_ms) {
    if (!M5.Speaker.tone(static_cast<float>(frequency_hz), duration_ms, 0, true)) return false;
    if (duration_ms > 0) vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return true;
}

}  // namespace walkie
