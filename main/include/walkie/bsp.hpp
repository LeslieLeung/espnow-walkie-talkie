#pragma once

#include <cstddef>
#include <cstdint>

namespace walkie {

struct ButtonEvents {
    bool a_pressed{false};
    bool a_released{false};
    bool b_clicked{false};
    bool b_held{false};
};

class StickS3Bsp {
public:
    bool initialize();
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
};

}  // namespace walkie
