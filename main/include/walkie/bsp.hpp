#pragma once

#include "walkie/protocol.hpp"

#include <cstddef>
#include <cstdint>

namespace walkie {

struct ButtonEvents {
    bool a_pressed{false};
    bool a_released{false};
    bool b_clicked{false};
    bool b_held{false};
};

// Board-facing HAL used by the app and UI. Supports StickS3 and StopWatch via
// M5Unified auto-detect; application code should not branch on M5 board enums.
class BoardBsp {
public:
    bool initialize();
    protocol::BoardType board_type() const { return board_; }
    const char* name_prefix() const;
    bool round_display() const { return round_display_; }
    int content_inset() const { return round_display_ ? 56 : 6; }

    ButtonEvents poll_buttons();
    int battery_percent() const;
    void set_backlight(bool enabled);
    int display_width() const;
    int display_height() const;
    void display_flush(int x, int y, int width, int height, const uint16_t* pixels);

    bool start_capture();
    void stop_capture();
    bool queue_capture(int16_t* samples, size_t sample_count);
    bool start_playback(uint8_t volume_percent);
    void stop_playback();
    bool play(const int16_t* samples, size_t sample_count);
    bool tone(uint16_t frequency_hz, uint32_t duration_ms);

private:
    protocol::BoardType board_{protocol::BoardType::Unknown};
    bool round_display_{false};
};

}  // namespace walkie
