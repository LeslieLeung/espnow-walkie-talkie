#include "walkie/bsp.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "bsp/audio.h"
#include "bsp/battery.h"
#include "bsp/esp_mosaico.h"
#include "bsp/power.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "esp_codec_dev.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace walkie {
namespace {

constexpr char kTag[] = "walkie_mosaico";
constexpr uint32_t kSampleRateHz = 16000;
constexpr size_t kToneSamples = 160;
constexpr size_t kMaxMonoSamples = 320;
constexpr int16_t kToneAmplitude = 8192;
constexpr float kPi = 3.14159265358979323846F;
constexpr uint32_t kFlushWaitMs = 100;
constexpr uint32_t kButtonDebounceMs = 30;
constexpr int kLcdWidth = 480;
constexpr int kLcdHeight = 480;

esp_codec_dev_handle_t g_mic = nullptr;
esp_codec_dev_handle_t g_speaker = nullptr;
esp_lcd_panel_handle_t g_panel = nullptr;
esp_lcd_panel_io_handle_t g_panel_io = nullptr;
esp_lcd_touch_handle_t g_touch = nullptr;
SemaphoreHandle_t g_color_done = nullptr;
bool g_audio_open = false;
bool g_panel_sleeping = false;
bool g_ai_pressed = false;
bool g_ai_pending = false;
uint32_t g_ai_pending_ms = 0;
int16_t g_stereo_io[kMaxMonoSamples * 2]{};

const co5300_lcd_init_cmd_t kVendorInit[] = {
    {0x11, nullptr, 0, 600},
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x29, nullptr, 0, 600},
};

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }

bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*,
                                   void*) {
    BaseType_t hp = pdFALSE;
    if (g_color_done != nullptr) xSemaphoreGiveFromISR(g_color_done, &hp);
    return hp == pdTRUE;
}

bool wait_color_idle(uint32_t timeout_ms) {
    return g_color_done != nullptr && xSemaphoreTake(g_color_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool open_audio() {
    if (g_audio_open) return true;
    if (g_mic == nullptr || g_speaker == nullptr) return false;
    esp_codec_dev_sample_info_t info{};
    info.bits_per_sample = 16;
    info.channel = 2;
    info.channel_mask = 0;
    info.sample_rate = kSampleRateHz;
    info.mclk_multiple = 0;
    if (esp_codec_dev_open(g_mic, &info) != ESP_CODEC_DEV_OK) return false;
    if (esp_codec_dev_open(g_speaker, &info) != ESP_CODEC_DEV_OK) {
        (void)esp_codec_dev_close(g_mic);
        return false;
    }
    (void)esp_codec_dev_set_in_gain(g_mic, 30.0f);
    (void)esp_codec_dev_set_out_vol(g_speaker, 0);
    g_audio_open = true;
    return true;
}

esp_err_t apply_qspi_drive(gpio_num_t lcd_scl) {
    const gpio_drive_cap_t strength = static_cast<gpio_drive_cap_t>(CONFIG_BSP_LCD_QSPI_DRIVE_CAP);
    const gpio_num_t pins[] = {lcd_scl, BSP_LCD_DATA0, BSP_LCD_DATA1, BSP_LCD_DATA2, BSP_LCD_DATA3};
    for (gpio_num_t pin : pins) {
        if (gpio_set_drive_capability(pin, strength) != ESP_OK) return ESP_FAIL;
    }
    return ESP_OK;
}

// C designated-init helpers (CO5300_PANEL_* / CST9220 / I2S_STD_*) are C-only.
// This TU is C++ with -Werror, so fill the same fields as mosaico display.c / audio.c.
spi_bus_config_t qspi_bus_config(gpio_num_t lcd_scl) {
    spi_bus_config_t bus_config{};
    bus_config.sclk_io_num = lcd_scl;
    bus_config.data0_io_num = BSP_LCD_DATA0;
    bus_config.data1_io_num = BSP_LCD_DATA1;
    bus_config.data2_io_num = BSP_LCD_DATA2;
    bus_config.data3_io_num = BSP_LCD_DATA3;
    bus_config.data4_io_num = -1;
    bus_config.data5_io_num = -1;
    bus_config.data6_io_num = -1;
    bus_config.data7_io_num = -1;
    bus_config.max_transfer_sz = kLcdWidth * kLcdHeight * 2;
    return bus_config;
}

esp_lcd_panel_io_spi_config_t qspi_panel_io_config() {
    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num = BSP_LCD_CS;
    io_config.dc_gpio_num = GPIO_NUM_NC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = 1;
    io_config.flags.psram_dma_direct = 1;
    return io_config;
}

i2s_std_config_t walkie_i2s_config() {
    i2s_std_config_t cfg{};
    cfg.clk_cfg.sample_rate_hz = kSampleRateHz;
    cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    cfg.clk_cfg.ext_clk_freq_hz = 0;
    cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    cfg.clk_cfg.bclk_div = 8;
    cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    cfg.slot_cfg.ws_width = 16;
    cfg.slot_cfg.ws_pol = false;
    cfg.slot_cfg.bit_shift = true;
    cfg.slot_cfg.left_align = true;
    cfg.slot_cfg.big_endian = false;
    cfg.slot_cfg.bit_order_lsb = false;
    cfg.gpio_cfg.mclk = BSP_AUDIO_I2S_MCLK;
    cfg.gpio_cfg.bclk = BSP_AUDIO_I2S_SCLK;
    cfg.gpio_cfg.ws = BSP_AUDIO_I2S_LRCLK;
    cfg.gpio_cfg.dout = BSP_AUDIO_I2S_SDOUT;
    cfg.gpio_cfg.din = BSP_AUDIO_I2S_DSIN;
    return cfg;
}

bool init_panel() {
    bsp_board_variant_t variant = BSP_BOARD_VARIANT_V1_0;
    if (bsp_board_variant_get(&variant) != ESP_OK) return false;
    const gpio_num_t lcd_scl = variant == BSP_BOARD_VARIANT_V1_0 ? BSP_LCD_SCL_V1_0 : BSP_LCD_SCL_V1_2;
    const gpio_num_t lcd_rst = variant == BSP_BOARD_VARIANT_V1_0 ? BSP_LCD_RST_V1_0 : BSP_LCD_RST_V1_2;

    const spi_bus_config_t bus_config = qspi_bus_config(lcd_scl);
    if (spi_bus_initialize(BSP_LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    if (apply_qspi_drive(lcd_scl) != ESP_OK) return false;

    const esp_lcd_panel_io_spi_config_t io_config = qspi_panel_io_config();
    const auto spi_bus = static_cast<esp_lcd_spi_bus_handle_t>(static_cast<int>(BSP_LCD_SPI_HOST));
    if (esp_lcd_new_panel_io_spi(spi_bus, &io_config, &g_panel_io) != ESP_OK) {
        return false;
    }

    co5300_vendor_config_t vendor_config{};
    vendor_config.init_cmds = kVendorInit;
    vendor_config.init_cmds_size = sizeof(kVendorInit) / sizeof(kVendorInit[0]);
    vendor_config.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = lcd_rst;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;
    if (esp_lcd_new_panel_co5300(g_panel_io, &panel_config, &g_panel) != ESP_OK) return false;
    if (esp_lcd_panel_reset(g_panel) != ESP_OK) return false;
    if (esp_lcd_panel_init(g_panel) != ESP_OK) return false;
    if (esp_lcd_panel_set_gap(g_panel, 0, 0) != ESP_OK) return false;
    if (esp_lcd_panel_swap_xy(g_panel, false) != ESP_OK) return false;
    if (esp_lcd_panel_mirror(g_panel, false, false) != ESP_OK) return false;
    if (esp_lcd_panel_disp_on_off(g_panel, true) != ESP_OK) return false;
    (void)esp_lcd_panel_co5300_set_brightness(g_panel, 50);

    g_color_done = xSemaphoreCreateBinary();
    if (g_color_done == nullptr) return false;
    esp_lcd_panel_io_callbacks_t io_callbacks{};
    io_callbacks.on_color_trans_done = on_color_trans_done;
    return esp_lcd_panel_io_register_event_callbacks(g_panel_io, &io_callbacks, nullptr) == ESP_OK;
}

bool init_touch() {
    if (bsp_i2c_init() != ESP_OK) return false;
    esp_lcd_touch_config_t touch_config{};
    touch_config.x_max = kLcdWidth - 1;
    touch_config.y_max = kLcdHeight - 1;
    touch_config.rst_gpio_num = BSP_LCD_TOUCH_RST;
    touch_config.int_gpio_num = BSP_LCD_TOUCH_INT;
    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST9220_ADDRESS;
    io_config.scl_speed_hz = 400000;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.flags.disable_control_phase = 1;
    io_config.transaction_timeout_ms = BSP_LCD_TOUCH_I2C_TIMEOUT_MS;
    esp_lcd_panel_io_handle_t touch_io = nullptr;
    if (esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &io_config, &touch_io) != ESP_OK) {
        return false;
    }
    if (esp_lcd_touch_new_i2c_cst9220(touch_io, &touch_config, &g_touch) != ESP_OK) {
        (void)esp_lcd_panel_io_del(touch_io);
        return false;
    }
    return true;
}

}  // namespace

bool BoardBsp::initialize() {
    bsp_board_variant_t variant = BSP_BOARD_VARIANT_V1_0;
    if (bsp_board_variant_get(&variant) != ESP_OK) return false;
    if (bsp_power_init() != ESP_OK) return false;
    if (bsp_power_set_vcc_3v3(true) != ESP_OK) return false;

    const i2s_std_config_t i2s_config = walkie_i2s_config();
    if (bsp_audio_init(&i2s_config) != ESP_OK) return false;
    g_mic = bsp_audio_codec_microphone_init();
    g_speaker = bsp_audio_codec_speaker_init();
    if (g_mic == nullptr || g_speaker == nullptr) return false;
    if (!open_audio()) return false;

    if (!init_panel()) return false;
    if (!init_touch()) {
        ESP_LOGW(kTag, "Touch init failed; continuing with AI button only");
        g_touch = nullptr;
    }

    gpio_config_t ai_config{};
    ai_config.pin_bit_mask = 1ULL << static_cast<uint32_t>(BSP_BUTTON_AI_GPIO);
    ai_config.mode = GPIO_MODE_INPUT;
    ai_config.pull_up_en = GPIO_PULLUP_ENABLE;
    ai_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ai_config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&ai_config) != ESP_OK) return false;

    (void)bsp_battery_init();

    board_ = protocol::BoardType::Mosaico;
    round_display_ = true;
    has_touch_ = g_touch != nullptr;
    flush_align_ = 4;
    display_awake_.store(true);
    return true;
}

const char* BoardBsp::name_prefix() const { return "MO-"; }

ButtonEvents BoardBsp::poll_buttons() {
    ButtonEvents events{};
    const bool down = gpio_get_level(BSP_BUTTON_AI_GPIO) == BSP_BUTTON_ACTIVE_LEVEL;
    const uint32_t now = now_ms();
    if (down != g_ai_pending) {
        g_ai_pending = down;
        g_ai_pending_ms = now;
    } else if (static_cast<uint32_t>(now - g_ai_pending_ms) >= kButtonDebounceMs &&
               down != g_ai_pressed) {
        g_ai_pressed = down;
        if (down) events.a_pressed = true;
        else events.a_released = true;
    }
    return events;
}

PointerSample BoardBsp::poll_pointer() {
    if (g_touch == nullptr) return {};
    if (esp_lcd_touch_read_data(g_touch) != ESP_OK) {
        return PointerSample{true, false, 0, 0};
    }
    uint8_t count = 0;
    esp_lcd_touch_point_data_t points[1]{};
    if (esp_lcd_touch_get_data(g_touch, points, &count, 1) != ESP_OK || count == 0) {
        return PointerSample{true, false, 0, 0};
    }
    return PointerSample{true, true, static_cast<int16_t>(points[0].x),
                         static_cast<int16_t>(points[0].y)};
}

int BoardBsp::battery_percent() const {
    bsp_battery_status_t status{};
    if (bsp_battery_read(&status) != ESP_OK) return 0;
    return status.state_of_charge > 100 ? 100 : status.state_of_charge;
}

bool BoardBsp::display_sleep() {
    if (!display_awake_.exchange(false)) return false;
    if (g_panel != nullptr) {
        (void)esp_lcd_panel_co5300_set_brightness(g_panel, 0);
        (void)esp_lcd_panel_disp_on_off(g_panel, false);
        if (!g_panel_sleeping && esp_lcd_panel_disp_sleep(g_panel, true) == ESP_OK) {
            g_panel_sleeping = true;
        }
    }
    return true;
}

bool BoardBsp::display_wakeup() {
    if (display_awake_.load()) return false;
    if (g_panel != nullptr) {
        if (g_panel_sleeping && esp_lcd_panel_disp_sleep(g_panel, false) == ESP_OK) {
            g_panel_sleeping = false;
        }
        (void)esp_lcd_panel_disp_on_off(g_panel, true);
        (void)esp_lcd_panel_co5300_set_brightness(g_panel, 50);
    }
    display_awake_.store(true);
    return true;
}

int BoardBsp::display_width() const { return kLcdWidth; }
int BoardBsp::display_height() const { return kLcdHeight; }

bool BoardBsp::display_flush(int x, int y, int width, int height, const uint16_t* pixels) {
    if (!display_awake_.load() || g_panel == nullptr || pixels == nullptr || width <= 0 ||
        height <= 0) {
        return false;
    }
    (void)wait_color_idle(0);
    if (esp_lcd_panel_draw_bitmap(g_panel, x, y, x + width, y + height, pixels) != ESP_OK) {
        (void)wait_color_idle(kFlushWaitMs);
        return false;
    }
    return wait_color_idle(kFlushWaitMs);
}

bool BoardBsp::start_capture() {
    if (!open_audio()) return false;
    (void)esp_codec_dev_set_out_vol(g_speaker, 0);
    return true;
}

void BoardBsp::stop_capture() {
    if (g_speaker != nullptr) (void)esp_codec_dev_set_out_vol(g_speaker, 0);
}

bool BoardBsp::queue_capture(int16_t* samples, size_t sample_count) {
    if (!g_audio_open || g_mic == nullptr || samples == nullptr || sample_count == 0 ||
        sample_count > kMaxMonoSamples) {
        return false;
    }
    if (esp_codec_dev_read(g_mic, g_stereo_io, sample_count * 2U * sizeof(int16_t)) !=
        ESP_CODEC_DEV_OK) {
        return false;
    }
    for (size_t i = 0; i < sample_count; ++i) samples[i] = g_stereo_io[i * 2];
    return true;
}

bool BoardBsp::start_playback(uint8_t volume_percent) {
    if (!open_audio()) return false;
    (void)esp_codec_dev_set_out_vol(g_speaker, volume_percent);
    return true;
}

void BoardBsp::stop_playback() {
    if (g_speaker != nullptr) (void)esp_codec_dev_set_out_vol(g_speaker, 0);
}

bool BoardBsp::play(const int16_t* samples, size_t sample_count) {
    if (!g_audio_open || g_speaker == nullptr || samples == nullptr || sample_count == 0 ||
        sample_count > kMaxMonoSamples) {
        return false;
    }
    for (size_t i = 0; i < sample_count; ++i) {
        g_stereo_io[i * 2] = samples[i];
        g_stereo_io[i * 2 + 1] = samples[i];
    }
    return esp_codec_dev_write(g_speaker, g_stereo_io, sample_count * 2U * sizeof(int16_t)) ==
           ESP_CODEC_DEV_OK;
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
        if (!play(buffer, chunk)) return false;
        written += chunk;
    }
    return true;
}

}  // namespace walkie
