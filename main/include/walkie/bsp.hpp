#pragma once

#include "walkie/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>

namespace walkie {

struct ButtonEvents {
    bool a_pressed{false};
    bool a_released{false};
    bool b_clicked{false};
    bool b_held{false};
};

struct PointerSample {
    bool valid{false};
    bool pressed{false};
    int16_t x{0};
    int16_t y{0};
};

// Board-facing HAL used by the app and UI. StickS3/StopWatch are served by
// board_bsp_m5.cpp (M5Unified auto-detect, ESP32-S3 builds); AI Passport is
// served by board_bsp_passport.cpp (vendored ai-passport BSP, ESP32-C3
// builds); ESP-Mosaico is served by board_bsp_mosaico.cpp (ESP32-S31).
// Application code should not branch on board enums.
class BoardBsp {
public:
    bool initialize();
    protocol::BoardType board_type() const { return board_; }
    const char* name_prefix() const;
    bool round_display() const { return round_display_; }
    bool has_touch() const { return has_touch_; }
    bool uses_soft_keys() const { return has_touch_; }
    // QSPI panels (CO5300) need 4-pixel X alignment; other boards leave this at 1.
    int flush_align() const { return flush_align_; }
    int content_inset() const {
        if (!round_display_) return 6;
        const int width = display_width();
        return width > 0 ? (width * 56 + 233) / 466 : 56;
    }

    ButtonEvents poll_buttons();
    PointerSample poll_pointer();
    int battery_percent() const;
    bool display_sleep();
    bool display_wakeup();
    bool display_awake() const { return display_awake_.load(); }
    int display_width() const;
    int display_height() const;
    bool display_flush(int x, int y, int width, int height, const uint16_t* pixels);
    bool uses_ok_and_direction_keys() const { return ok_and_direction_keys_; }

    bool start_capture();
    void stop_capture();
    bool queue_capture(int16_t* samples, size_t sample_count);
    bool start_playback(uint8_t volume_percent);
    void stop_playback();
    bool play(const int16_t* samples, size_t sample_count);
    // Blocks until the tone has finished playing. Caller must start_playback first
    // and must not delay again for duration_ms.
    bool tone(uint16_t frequency_hz, uint32_t duration_ms);

private:
    protocol::BoardType board_{protocol::BoardType::Unknown};
    bool round_display_{false};
    bool has_touch_{false};
    bool ok_and_direction_keys_{false};
    int flush_align_{1};
    std::atomic<bool> display_awake_{true};
};

}  // namespace walkie
