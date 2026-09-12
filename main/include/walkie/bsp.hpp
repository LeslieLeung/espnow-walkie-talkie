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

// Board-facing HAL used by the app and UI. StickS3/StopWatch are served by
// board_bsp_m5.cpp (M5Unified auto-detect, ESP32-S3 builds); AI Passport is
// served by board_bsp_passport.cpp (vendored ai-passport BSP, ESP32-C3
// builds). Application code should not branch on board enums.
class BoardBsp {
public:
    bool initialize();
    protocol::BoardType board_type() const { return board_; }
    const char* name_prefix() const;
    bool round_display() const { return round_display_; }
    int content_inset() const { return round_display_ ? 56 : 6; }

    ButtonEvents poll_buttons();
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
    bool ok_and_direction_keys_{false};
    std::atomic<bool> display_awake_{true};
};

}  // namespace walkie
