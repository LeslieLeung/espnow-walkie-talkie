#include "walkie/ui.hpp"

#include "walkie/display_policy.hpp"

#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"

namespace walkie {
namespace {

constexpr int kDrawRows = 24;
BoardBsp* g_bsp = nullptr;
lv_disp_draw_buf_t g_draw_buffer{};
lv_disp_drv_t g_display_driver{};
lv_color_t* g_pixels = nullptr;

lv_obj_t* g_screen = nullptr;
lv_obj_t* g_header = nullptr;
lv_obj_t* g_subheader = nullptr;
lv_obj_t* g_primary = nullptr;
lv_obj_t* g_body = nullptr;
lv_obj_t* g_footer = nullptr;

struct Layout {
    int inset{6};
    int content_width{123};
    int header_y{8};
    int subheader_y{30};
    int primary_offset_y{-20};
    int body_offset_y{18};
    int footer_offset_y{-8};
    int menu_x{20};
    int menu_y{48};
    int devices_x{5};
    int devices_y{42};
    int volume_x{28};
    int volume_y{43};
    const lv_font_t* header_font{&lv_font_montserrat_14};
    const lv_font_t* primary_font{&lv_font_montserrat_20};
    const lv_font_t* body_font{&lv_font_montserrat_14};
    const lv_font_t* list_font{&lv_font_montserrat_12};
    const lv_font_t* footer_font{&lv_font_montserrat_12};
};

Layout g_layout{};

Layout make_layout(const BoardBsp& bsp) {
    Layout layout{};
    layout.inset = bsp.content_inset();
    layout.content_width = bsp.display_width() - (layout.inset * 2);
    if (bsp.round_display()) {
        layout.header_y = 72;
        layout.subheader_y = 108;
        layout.primary_offset_y = -28;
        layout.body_offset_y = 36;
        layout.footer_offset_y = -72;
        layout.menu_x = layout.inset + 36;
        layout.menu_y = 140;
        layout.devices_x = layout.inset + 24;
        layout.devices_y = 130;
        layout.volume_x = layout.inset + 48;
        layout.volume_y = 130;
        layout.header_font = &lv_font_montserrat_20;
        layout.primary_font = &lv_font_montserrat_28;
        layout.body_font = &lv_font_montserrat_20;
        layout.list_font = &lv_font_montserrat_14;
        layout.footer_font = &lv_font_montserrat_14;
    }
    return layout;
}

const char* state_text(TalkState state) {
    switch (state) {
        case TalkState::Idle: return "IDLE";
        case TalkState::Requesting: return "REQUESTING";
        case TalkState::Talking: return "TALKING";
        case TalkState::Receiving: return "RECEIVING";
        case TalkState::Busy: return "BUSY";
    }
    return "IDLE";
}

const char* peer_state_text(protocol::DeviceState state) {
    switch (state) {
        case protocol::DeviceState::Idle: return "IDLE";
        case protocol::DeviceState::Requesting: return "REQ";
        case protocol::DeviceState::Talking: return "TALK";
        case protocol::DeviceState::Receiving: return "RX";
        case protocol::DeviceState::Busy: return "BUSY";
    }
    return "?";
}

void flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* colors) {
    if (g_bsp != nullptr) {
        const int width = area->x2 - area->x1 + 1;
        const int height = area->y2 - area->y1 + 1;
        g_bsp->display_flush(area->x1, area->y1, width, height,
                             reinterpret_cast<const uint16_t*>(colors));
    }
    lv_disp_flush_ready(driver);
}

void style_label(lv_obj_t* label, const lv_color_t color) {
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

void create_screen(int width, int height) {
    (void)height;
    g_screen = lv_scr_act();
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(kIdleScreenRgb), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);

    g_header = lv_label_create(g_screen);
    lv_obj_set_width(g_header, g_layout.content_width);
    lv_obj_align(g_header, LV_ALIGN_TOP_MID, 0, g_layout.header_y);
    lv_obj_set_style_text_font(g_header, g_layout.header_font, 0);
    style_label(g_header, lv_color_hex(0xE7F4F5));

    g_subheader = lv_label_create(g_screen);
    lv_obj_set_width(g_subheader, g_layout.content_width);
    lv_obj_align(g_subheader, LV_ALIGN_TOP_MID, 0, g_layout.subheader_y);
    lv_obj_set_style_text_font(g_subheader, g_layout.body_font, 0);
    style_label(g_subheader, lv_color_hex(0x7FC9C8));

    g_primary = lv_label_create(g_screen);
    lv_obj_set_width(g_primary, g_layout.content_width);
    lv_obj_align(g_primary, LV_ALIGN_CENTER, 0, g_layout.primary_offset_y);
    lv_obj_set_style_text_font(g_primary, g_layout.primary_font, 0);
    style_label(g_primary, lv_color_hex(0xFFFFFF));

    g_body = lv_label_create(g_screen);
    lv_obj_set_width(g_body, g_layout.content_width);
    lv_label_set_long_mode(g_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(g_body, LV_ALIGN_CENTER, 0, g_layout.body_offset_y);
    lv_obj_set_style_text_font(g_body, g_layout.body_font, 0);
    style_label(g_body, lv_color_hex(0xA7B7BD));

    g_footer = lv_label_create(g_screen);
    lv_obj_set_width(g_footer, g_layout.content_width);
    lv_obj_align(g_footer, LV_ALIGN_BOTTOM_MID, 0, g_layout.footer_offset_y);
    lv_obj_set_style_text_font(g_footer, g_layout.footer_font, 0);
    style_label(g_footer, lv_color_hex(0x789096));

    (void)width;
}

void reset_layout() {
    lv_obj_clear_flag(g_subheader, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_primary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_footer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(g_body, g_layout.content_width);
    lv_obj_set_style_text_font(g_body, g_layout.body_font, 0);
    lv_obj_set_style_text_align(g_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_body, LV_ALIGN_CENTER, 0, g_layout.body_offset_y);
}

void render_main(const UiSnapshot& snapshot) {
    if (snapshot.battery_percent <= 15) {
        lv_label_set_text_fmt(g_header, "CH%u    LOW BAT %u%%",
                              snapshot.logical_channel, snapshot.battery_percent);
    } else {
        lv_label_set_text_fmt(g_header, "CH%u          %u%%",
                              snapshot.logical_channel, snapshot.battery_percent);
    }
    lv_label_set_text_fmt(g_subheader, "%u ONLINE", snapshot.online_count);
    lv_label_set_text(g_primary, snapshot.weak_signal ? "SIGNAL WEAK" : state_text(snapshot.talk_state));
    if (snapshot.talk_state == TalkState::Talking) {
        lv_label_set_text_fmt(g_body, "%u SEC LEFT", snapshot.remaining_seconds);
    } else if (snapshot.weak_signal) {
        lv_label_set_text(g_body, "AUDIO MAY BE\nINTERMITTENT");
    } else if (snapshot.talk_state == TalkState::Receiving || snapshot.talk_state == TalkState::Busy) {
        lv_label_set_text_fmt(g_body, "%s", snapshot.speaker_name.data());
    } else if (snapshot.online_count == 0) {
        lv_label_set_text(g_body, "NO DEVICES");
    } else {
        lv_label_set_text(g_body, "READY");
    }
    lv_label_set_text(g_footer, "HOLD A TO TALK\nB: CHANNEL   HOLD B: MENU");
}

void render_menu(const UiSnapshot& snapshot) {
    lv_label_set_text(g_header, "MENU");
    lv_obj_add_flag(g_subheader, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_primary, LV_OBJ_FLAG_HIDDEN);
    const char* marker0 = snapshot.menu_index == 0 ? ">" : " ";
    const char* marker1 = snapshot.menu_index == 1 ? ">" : " ";
    const char* marker2 = snapshot.menu_index == 2 ? ">" : " ";
    lv_label_set_text_fmt(g_body, "%s DEVICES\n\n%s SETTINGS\n\n%s EXIT", marker0, marker1, marker2);
    lv_obj_set_style_text_align(g_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(g_body, LV_ALIGN_TOP_LEFT, g_layout.menu_x, g_layout.menu_y);
    lv_label_set_text(g_footer, "B: NEXT   A: OPEN\nHOLD B: BACK");
}

void render_devices(const UiSnapshot& snapshot) {
    lv_label_set_text_fmt(g_header, "DEVICES . CH%u", snapshot.logical_channel);
    lv_obj_add_flag(g_subheader, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_primary, LV_OBJ_FLAG_HIDDEN);
    char lines[256]{};
    size_t used = 0;
    if (snapshot.peer_count == 0) {
        std::snprintf(lines, sizeof(lines), "NO DEVICES");
    } else {
        const size_t max_rows = g_bsp != nullptr && g_bsp->round_display() ? 8 : 6;
        const size_t shown = snapshot.peer_count < max_rows ? snapshot.peer_count : max_rows;
        for (size_t row = 0; row < shown; ++row) {
            const size_t index = (snapshot.device_offset + row) % snapshot.peer_count;
            const UiPeer& peer = snapshot.peers[index];
            const int written = std::snprintf(lines + used, sizeof(lines) - used, "%s %3u%% %s%s",
                                              peer.name.data(), peer.battery_percent,
                                              peer_state_text(peer.state), row + 1 == shown ? "" : "\n");
            if (written < 0 || static_cast<size_t>(written) >= sizeof(lines) - used) break;
            used += static_cast<size_t>(written);
        }
    }
    lv_label_set_text(g_body, lines);
    lv_obj_set_style_text_font(g_body, g_layout.list_font, 0);
    lv_obj_set_style_text_align(g_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(g_body, LV_ALIGN_TOP_LEFT, g_layout.devices_x, g_layout.devices_y);
    lv_label_set_text(g_footer, "B: SCROLL   HOLD B: BACK");
}

void render_volume(const UiSnapshot& snapshot) {
    lv_label_set_text(g_header, "VOLUME");
    lv_obj_add_flag(g_subheader, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_primary, LV_OBJ_FLAG_HIDDEN);
    constexpr const char* names[] = {"MUTE", "25%", "50%", "75%"};
    lv_label_set_text_fmt(g_body, "%s %s\n\n%s %s\n\n%s %s\n\n%s %s",
                          snapshot.volume_index == 0 ? ">" : " ", names[0],
                          snapshot.volume_index == 1 ? ">" : " ", names[1],
                          snapshot.volume_index == 2 ? ">" : " ", names[2],
                          snapshot.volume_index == 3 ? ">" : " ", names[3]);
    lv_obj_set_style_text_align(g_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(g_body, LV_ALIGN_TOP_LEFT, g_layout.volume_x, g_layout.volume_y);
    lv_label_set_text(g_footer, "B: NEXT   A: SAVE\nHOLD B: BACK");
}

void render(const UiSnapshot& snapshot) {
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(screen_background_rgb(snapshot.talk_state)), 0);
    reset_layout();
    switch (snapshot.page) {
        case UiPage::Main: render_main(snapshot); break;
        case UiPage::Menu: render_menu(snapshot); break;
        case UiPage::Devices: render_devices(snapshot); break;
        case UiPage::Volume: render_volume(snapshot); break;
    }
}

}  // namespace

bool Ui::start() {
    queue_ = xQueueCreate(1, sizeof(UiSnapshot));
    if (queue_ == nullptr) return false;
    return xTaskCreatePinnedToCore(task_entry, "walkie_ui", 6144, this, 2, nullptr, 1) == pdPASS;
}

bool Ui::publish(const UiSnapshot& snapshot) {
    return queue_ != nullptr && xQueueOverwrite(queue_, &snapshot) == pdTRUE;
}

void Ui::task_entry(void* context) {
    static_cast<Ui*>(context)->run();
}

void Ui::run() {
    g_bsp = &bsp_;
    g_layout = make_layout(bsp_);
    lv_init();
    const int width = bsp_.display_width();
    const int height = bsp_.display_height();
    // Prefer internal DMA RAM; fall back to PSRAM for the larger StopWatch panel.
    g_pixels = static_cast<lv_color_t*>(heap_caps_malloc(
        static_cast<size_t>(width * kDrawRows) * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (g_pixels == nullptr) {
        g_pixels = static_cast<lv_color_t*>(heap_caps_malloc(
            static_cast<size_t>(width * kDrawRows) * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
    }
    if (g_pixels == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    lv_disp_draw_buf_init(&g_draw_buffer, g_pixels, nullptr, width * kDrawRows);
    lv_disp_drv_init(&g_display_driver);
    g_display_driver.hor_res = width;
    g_display_driver.ver_res = height;
    g_display_driver.flush_cb = flush;
    g_display_driver.draw_buf = &g_draw_buffer;
    lv_disp_drv_register(&g_display_driver);
    create_screen(width, height);

    UiSnapshot snapshot{};
    uint32_t last_tick_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    for (;;) {
        if (xQueueReceive(queue_, &snapshot, pdMS_TO_TICKS(10)) == pdTRUE) render(snapshot);
        const uint32_t tick_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        lv_tick_inc(tick_ms - last_tick_ms);
        last_tick_ms = tick_ms;
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}  // namespace walkie
