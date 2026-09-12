// FoloToy AI Passport (ESP32-C3) implementation of the BoardBsp HAL.
// Display/audio/battery go through the vendored ai-passport BSP; the three
// ADC resistor-ladder buttons are polled directly (debounced) instead of using
// the callback-based espressif/button component. Audio uses blocking
// esp_codec_dev reads/writes, which matches the 20 ms frame pacing of the
// capture/playback tasks. tone() also blocks for the full duration.
#include "walkie/bsp.hpp"
#include "walkie/button_ladder.hpp"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_pins.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"

namespace walkie {
namespace {

constexpr uint32_t kSampleRateHz = 16000;
constexpr size_t kToneSamples = 160;
constexpr int16_t kToneAmplitude = 8192;
constexpr float kPi = 3.14159265358979323846F;
constexpr uint32_t kFlushWaitMs = 100;
constexpr uint32_t kFlushWaitRetryMs = 150;

adc_oneshot_unit_handle_t g_adc;
adc_cali_handle_t g_adc_cali;
LadderButtonState g_buttons;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }

int detect_key(int mv) {
    static const uint16_t windows[BSP_BTN_COUNT][2] = BSP_BTN_MV_TABLE;
    for (int key = 0; key < BSP_BTN_COUNT; ++key) {
        if (mv >= windows[key][0] && mv < windows[key][1]) return key;
    }
    return kLadderNoKey;
}

int read_button_key() {
    int raw = 0;
    if (g_adc == nullptr) return kLadderInvalidSample;
    if (adc_oneshot_read(g_adc, BSP_BTN_ADC_CHANNEL, &raw) != ESP_OK) return kLadderInvalidSample;
    int mv = 0;
    if (g_adc_cali != nullptr) {
        if (adc_cali_raw_to_voltage(g_adc_cali, raw, &mv) != ESP_OK) return kLadderInvalidSample;
    } else {
        mv = raw * 3300 / 4095;
    }
    return detect_key(mv);
}

bool wait_color_idle(uint32_t timeout_ms) {
    return bsp_display_wait_color_trans_done(timeout_ms);
}

}  // namespace

bool BoardBsp::initialize() {
    if (bsp_display_init() != ESP_OK) return false;
    (void)bsp_display_clear(0);

    if (bsp_audio_init() != ESP_OK) return false;
    bsp_audio_set_volume(0);

    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BSP_BTN_ADC_UNIT,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &g_adc) != ESP_OK) return false;
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(g_adc, BSP_BTN_ADC_CHANNEL, &chan_cfg) != ESP_OK) return false;
    adc_cali_handle_t cali = nullptr;
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BSP_BTN_ADC_UNIT,
        .chan = BSP_BTN_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) == ESP_OK) g_adc_cali = cali;

    bsp_battery_init();  // Optional: fuel gauge failure degrades battery reads to 0%.
    bsp_display_backlight(50);

    board_ = protocol::BoardType::AiPassport;
    round_display_ = false;
    ok_and_direction_keys_ = true;
    display_awake_.store(true);
    g_buttons = LadderButtonState{};
    return true;
}

const char* BoardBsp::name_prefix() const { return "AP-"; }

ButtonEvents BoardBsp::poll_buttons() {
    return poll_ladder_buttons(g_buttons, read_button_key(), now_ms());
}

int BoardBsp::battery_percent() const {
    const int soc = bsp_battery_soc();
    return soc < 0 ? 0 : (soc > 100 ? 100 : soc);
}

bool BoardBsp::display_sleep() {
    if (!display_awake_.exchange(false)) return false;
    bsp_display_backlight(0);
    if (esp_lcd_panel_handle_t panel = bsp_display_panel(); panel != nullptr) {
        esp_lcd_panel_disp_on_off(panel, false);
    }
    return true;
}

bool BoardBsp::display_wakeup() {
    if (display_awake_.load()) return false;
    if (esp_lcd_panel_handle_t panel = bsp_display_panel(); panel != nullptr) {
        esp_lcd_panel_disp_on_off(panel, true);
    }
    bsp_display_backlight(50);
    display_awake_.store(true);
    return true;
}

int BoardBsp::display_width() const { return BSP_LCD_W; }
int BoardBsp::display_height() const { return BSP_LCD_H; }

bool BoardBsp::display_flush(int x, int y, int width, int height, const uint16_t* pixels) {
    if (!display_awake_.load()) return false;
    esp_lcd_panel_handle_t panel = bsp_display_panel();
    if (panel == nullptr || width <= 0 || height <= 0) return false;
    // Drain a stale DMA completion, then wait for this transfer. x_end/y_end are exclusive.
    (void)wait_color_idle(0);
    if (esp_lcd_panel_draw_bitmap(panel, x, y, x + width, y + height, pixels) != ESP_OK) {
        (void)wait_color_idle(kFlushWaitMs);
        return false;
    }
    if (wait_color_idle(kFlushWaitMs)) return true;
    return wait_color_idle(kFlushWaitRetryMs);
}

bool BoardBsp::start_capture() {
    if (bsp_audio_set_format(kSampleRateHz, 16, 1) != ESP_OK) return false;
    // PA is always on; mute the DAC so PTT does not howl into the mic.
    bsp_audio_set_volume(0);
    return true;
}

void BoardBsp::stop_capture() { bsp_audio_set_volume(0); }

bool BoardBsp::queue_capture(int16_t* samples, size_t sample_count) {
    return bsp_audio_read(samples, sample_count * sizeof(int16_t)) == ESP_OK;
}

bool BoardBsp::start_playback(uint8_t volume_percent) {
    if (bsp_audio_set_format(kSampleRateHz, 16, 1) != ESP_OK) return false;
    bsp_audio_set_volume(volume_percent);
    return true;
}

void BoardBsp::stop_playback() { bsp_audio_set_volume(0); }

bool BoardBsp::play(const int16_t* samples, size_t sample_count) {
    return bsp_audio_write(samples, sample_count * sizeof(int16_t)) == ESP_OK;
}

bool BoardBsp::tone(uint16_t frequency_hz, uint32_t duration_ms) {
    int16_t buffer[kToneSamples]{};
    const size_t total = static_cast<size_t>(kSampleRateHz) * duration_ms / 1000U;
    size_t written = 0;
    while (written < total) {
        const size_t chunk = total - written < kToneSamples ? total - written : kToneSamples;
        for (size_t i = 0; i < chunk; ++i) {
            const float phase = 2.0F * kPi * static_cast<float>(frequency_hz) *
                                static_cast<float>(written + i) / static_cast<float>(kSampleRateHz);
            buffer[i] = static_cast<int16_t>(std::lround(kToneAmplitude * std::sin(phase)));
        }
        if (bsp_audio_write(buffer, chunk * sizeof(int16_t)) != ESP_OK) return false;
        written += chunk;
    }
    return true;
}

}  // namespace walkie
