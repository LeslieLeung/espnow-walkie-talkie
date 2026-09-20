#include "walkie/ui.hpp"

#include "walkie/display_policy.hpp"
#include "walkie/soft_keys.hpp"

#include <cstddef>
#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

namespace walkie {
namespace {

#if CONFIG_IDF_TARGET_ESP32C3
// Match the ai-passport BSP: C3 has no PSRAM; 20 DMA rows ≈ 9.6KB.
constexpr int kDrawRows = 20;
#else
constexpr int kDrawRows = 24;
#endif
#if CONFIG_FREERTOS_UNICORE
constexpr BaseType_t kUiCore = 0;
#else
constexpr BaseType_t kUiCore = 1;
#endif
constexpr char kTag[] = "walkie_ui";
#if CONFIG_WALKIE_DIAGNOSTICS
uint32_t g_render_count = 0;
uint32_t g_handler_count = 0;
uint32_t g_flush_count = 0;
uint32_t g_blocked_flush_count = 0;
#endif
BoardBsp* g_bsp = nullptr;
int g_flush_align = 1;
lv_disp_draw_buf_t g_draw_buffer{};
lv_disp_drv_t g_display_driver{};
lv_color_t* g_pixels = nullptr;

lv_obj_t* g_screen = nullptr;
lv_obj_t* g_header = nullptr;
lv_obj_t* g_subheader = nullptr;
lv_obj_t* g_primary = nullptr;
lv_obj_t* g_body = nullptr;
lv_obj_t* g_footer = nullptr;
SoftKeyLayout g_soft_layout{};
bool g_soft_enabled = false;
lv_obj_t* g_btn_talk = nullptr;
lv_obj_t* g_lbl_talk = nullptr;
lv_obj_t* g_btn_channel = nullptr;
lv_obj_t* g_lbl_channel = nullptr;
lv_obj_t* g_btn_menu = nullptr;
lv_obj_t* g_lbl_menu = nullptr;
lv_obj_t* g_btn_back = nullptr;
lv_obj_t* g_lbl_back = nullptr;
lv_obj_t* g_btn_item[4]{};
lv_obj_t* g_lbl_item[4]{};

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
    } else if (bsp.display_width() >= 240 && bsp.display_height() >= 320) {
        layout.inset = 12;
        layout.content_width = bsp.display_width() - (layout.inset * 2);
        layout.header_y = 16;
        layout.subheader_y = 48;
        layout.primary_offset_y = -36;
        layout.body_offset_y = 28;
        layout.footer_offset_y = -16;
        layout.menu_x = layout.inset + 24;
        layout.menu_y = 72;
        layout.devices_x = layout.inset + 12;
        layout.devices_y = 64;
        layout.volume_x = layout.inset + 32;
        layout.volume_y = 64;
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
        const bool flushed = g_bsp->display_flush(area->x1, area->y1, width, height,
                                                  reinterpret_cast<const uint16_t*>(colors));
#if CONFIG_WALKIE_DIAGNOSTICS
        if (flushed) {
            ++g_flush_count;
        } else {
            ++g_blocked_flush_count;
        }
#else
        (void)flushed;
#endif
    }
    lv_disp_flush_ready(driver);
}

void rounder(lv_disp_drv_t*, lv_area_t* area) {
    const int align = g_flush_align > 1 ? g_flush_align : 2;
    const int max_x = g_bsp != nullptr ? g_bsp->display_width() - 1 : area->x2;
    int x1 = area->x1;
    int x2 = area->x2;
    if (x1 < 0) x1 = 0;
    if (x2 < 0) x2 = 0;
    if (x1 > max_x) x1 = max_x;
    if (x2 > max_x) x2 = max_x;
    area->x1 = static_cast<lv_coord_t>((x1 / align) * align);
    x2 = ((x2 + align) / align) * align - 1;
    area->x2 = static_cast<lv_coord_t>(x2 > max_x ? max_x : x2);
}

void style_label(lv_obj_t* label, const lv_color_t color) {
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

void place_rect(lv_obj_t* obj, const Rect& rect) {
    lv_obj_set_pos(obj, rect.x, rect.y);
    lv_obj_set_size(obj, rect.w, rect.h);
}

lv_obj_t* make_soft_button(lv_obj_t* parent, const Rect& rect, lv_obj_t** label_out,
                           const lv_font_t* font) {
    lv_obj_t* button = lv_btn_create(parent);
    place_rect(button, rect);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1A3A44), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_t* label = lv_label_create(button);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE7F4F5), 0);
    lv_obj_set_style_text_font(label, font, 0);
    if (label_out != nullptr) *label_out = label;
    lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    return button;
}

void set_button_hidden(lv_obj_t* button, bool hidden) {
    if (button == nullptr) return;
    if (hidden) lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
}

void set_button_lit(lv_obj_t* button, bool lit) {
    if (button == nullptr) return;
    lv_obj_set_style_bg_color(button, lv_color_hex(lit ? 0x8F1D1D : 0x1A3A44), 0);
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

    if (g_soft_enabled) {
        g_btn_talk = make_soft_button(g_screen, g_soft_layout.talk, &g_lbl_talk, g_layout.body_font);
        lv_label_set_text(g_lbl_talk, "TALK");
        g_btn_channel =
            make_soft_button(g_screen, g_soft_layout.channel, &g_lbl_channel, g_layout.footer_font);
        lv_label_set_text(g_lbl_channel, "CH");
        g_btn_menu = make_soft_button(g_screen, g_soft_layout.menu, &g_lbl_menu, g_layout.footer_font);
        lv_label_set_text(g_lbl_menu, "MENU");
        g_btn_back = make_soft_button(g_screen, g_soft_layout.back, &g_lbl_back, g_layout.body_font);
        lv_label_set_text(g_lbl_back, "BACK");
        for (int i = 0; i < 4; ++i) {
            g_btn_item[i] =
                make_soft_button(g_screen, g_soft_layout.items[i], &g_lbl_item[i], g_layout.body_font);
        }
    }

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

void show_soft_main(bool talking) {
    set_button_hidden(g_btn_talk, false);
    set_button_hidden(g_btn_channel, false);
    set_button_hidden(g_btn_menu, false);
    set_button_hidden(g_btn_back, true);
    for (auto* button : g_btn_item) set_button_hidden(button, true);
    set_button_lit(g_btn_talk, talking);
}

void show_soft_items(const char* const* names, uint8_t selected) {
    set_button_hidden(g_btn_talk, true);
    set_button_hidden(g_btn_channel, true);
    set_button_hidden(g_btn_menu, true);
    set_button_hidden(g_btn_back, false);
    for (int i = 0; i < 4; ++i) {
        set_button_hidden(g_btn_item[i], false);
        if (g_lbl_item[i] != nullptr) lv_label_set_text(g_lbl_item[i], names[i]);
        set_button_lit(g_btn_item[i], i == selected);
    }
}

void show_soft_back_only() {
    set_button_hidden(g_btn_talk, true);
    set_button_hidden(g_btn_channel, true);
    set_button_hidden(g_btn_menu, true);
    set_button_hidden(g_btn_back, false);
    for (auto* button : g_btn_item) set_button_hidden(button, true);
}

void hide_soft_keys() {
    set_button_hidden(g_btn_talk, true);
    set_button_hidden(g_btn_channel, true);
    set_button_hidden(g_btn_menu, true);
    set_button_hidden(g_btn_back, true);
    for (auto* button : g_btn_item) set_button_hidden(button, true);
}

void render_main(const UiSnapshot& snapshot) {
    if (snapshot.battery_percent <= 15) {
        lv_label_set_text_fmt(g_header, "CH%u    LOW BAT %u%%",
                              snapshot.logical_channel, snapshot.battery_percent);
    } else {
        lv_label_set_text_fmt(g_header, "CH%u          %u%%",
                              snapshot.logical_channel, snapshot.battery_percent);
    }
    const char* vox_badge = "";
    if (snapshot.vox_level == 1) vox_badge = "  VOX L";
    else if (snapshot.vox_level == 2) vox_badge = "  VOX M";
    else if (snapshot.vox_level == 3) vox_badge = "  VOX H";
    lv_label_set_text_fmt(g_subheader, "%u ONLINE%s", snapshot.online_count, vox_badge);
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
    if (snapshot.uses_soft_keys) {
        lv_obj_add_flag(g_footer, LV_OBJ_FLAG_HIDDEN);
        show_soft_main(snapshot.talk_held || snapshot.talk_state == TalkState::Talking ||
                       snapshot.talk_state == TalkState::Requesting);
        return;
    }
    lv_label_set_text(g_footer,
                      g_bsp != nullptr && g_bsp->uses_ok_and_direction_keys()
                          ? (snapshot.vox_enabled ? "SPEAK OR HOLD OK\nUP/DN: CHANNEL   HOLD: MENU"
                                                  : "HOLD OK TO TALK\nUP/DN: CHANNEL   HOLD: MENU")
                          : (snapshot.vox_enabled ? "SPEAK OR HOLD A\nB: CHANNEL   HOLD B: MENU"
                                                  : "HOLD A TO TALK\nB: CHANNEL   HOLD B: MENU"));
}

void render_menu(const UiSnapshot& snapshot) {
    lv_label_set_text(g_header, "MENU");
    lv_obj_add_flag(g_subheader, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_primary, LV_OBJ_FLAG_HIDDEN);
    if (snapshot.uses_soft_keys) {
        lv_obj_add_flag(g_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_footer, LV_OBJ_FLAG_HIDDEN);
        constexpr const char* names[] = {"DEVICES", "SETTINGS", "VOX", "EXIT"};
        show_soft_items(names, snapshot.menu_index);
        return;
    }
    const char* marker0 = snapshot.menu_index == 0 ? ">" : " ";
    const char* marker1 = snapshot.menu_index == 1 ? ">" : " ";
    const char* marker2 = snapshot.menu_index == 2 ? ">" : " ";
    const char* marker3 = snapshot.menu_index == 3 ? ">" : " ";
    lv_label_set_text_fmt(g_body, "%s DEVICES\n%s SETTINGS\n%s VOX\n%s EXIT",
                          marker0, marker1, marker2, marker3);
    lv_obj_set_style_text_align(g_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(g_body, LV_ALIGN_TOP_LEFT, g_layout.menu_x, g_layout.menu_y);
    lv_label_set_text(g_footer,
                      g_bsp != nullptr && g_bsp->uses_ok_and_direction_keys()
                          ? "UP/DN: NEXT   OK: OPEN\nHOLD: BACK"
                          : "B: NEXT   A: OPEN\nHOLD B: BACK");
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
    if (snapshot.uses_soft_keys) {
        lv_obj_add_flag(g_footer, LV_OBJ_FLAG_HIDDEN);
        show_soft_back_only();
        return;
    }
    lv_label_set_text(g_footer,
                      g_bsp != nullptr && g_bsp->uses_ok_and_direction_keys()
                          ? "UP/DN: SCROLL   HOLD: BACK"
                          : "B: SCROLL   HOLD B: BACK");
}

void render_volume(const UiSnapshot& snapshot) {
    lv_label_set_text(g_header, "VOLUME");
    lv_obj_add_flag(g_subheader, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_primary, LV_OBJ_FLAG_HIDDEN);
    constexpr const char* names[] = {"MUTE", "25%", "50%", "75%"};
    if (snapshot.uses_soft_keys) {
        lv_obj_add_flag(g_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_footer, LV_OBJ_FLAG_HIDDEN);
        show_soft_items(names, snapshot.volume_index);
        return;
    }
    lv_label_set_text_fmt(g_body, "%s %s\n\n%s %s\n\n%s %s\n\n%s %s",
                          snapshot.volume_index == 0 ? ">" : " ", names[0],
                          snapshot.volume_index == 1 ? ">" : " ", names[1],
                          snapshot.volume_index == 2 ? ">" : " ", names[2],
                          snapshot.volume_index == 3 ? ">" : " ", names[3]);
    lv_obj_set_style_text_align(g_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(g_body, LV_ALIGN_TOP_LEFT, g_layout.volume_x, g_layout.volume_y);
    lv_label_set_text(g_footer,
                      g_bsp != nullptr && g_bsp->uses_ok_and_direction_keys()
                          ? "UP/DN: NEXT   OK: SAVE\nHOLD: BACK"
                          : "B: NEXT   A: SAVE\nHOLD B: BACK");
}

void render_vox(const UiSnapshot& snapshot) {
    lv_label_set_text(g_header, "VOX");
    lv_obj_add_flag(g_subheader, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_primary, LV_OBJ_FLAG_HIDDEN);
    constexpr const char* names[] = {"OFF", "LOW", "MED", "HIGH"};
    if (snapshot.uses_soft_keys) {
        lv_obj_add_flag(g_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_footer, LV_OBJ_FLAG_HIDDEN);
        show_soft_items(names, snapshot.vox_index);
        return;
    }
    lv_label_set_text_fmt(g_body, "%s %s\n\n%s %s\n\n%s %s\n\n%s %s",
                          snapshot.vox_index == 0 ? ">" : " ", names[0],
                          snapshot.vox_index == 1 ? ">" : " ", names[1],
                          snapshot.vox_index == 2 ? ">" : " ", names[2],
                          snapshot.vox_index == 3 ? ">" : " ", names[3]);
    lv_obj_set_style_text_align(g_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(g_body, LV_ALIGN_TOP_LEFT, g_layout.volume_x, g_layout.volume_y);
    lv_label_set_text(g_footer,
                      g_bsp != nullptr && g_bsp->uses_ok_and_direction_keys()
                          ? "UP/DN: NEXT   OK: SAVE\nHOLD: BACK"
                          : "B: NEXT   A: SAVE\nHOLD B: BACK");
}

void render(const UiSnapshot& snapshot) {
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(screen_background_rgb(snapshot.talk_state)), 0);
    reset_layout();
    if (!snapshot.uses_soft_keys) hide_soft_keys();
    switch (snapshot.page) {
        case UiPage::Main: render_main(snapshot); break;
        case UiPage::Menu: render_menu(snapshot); break;
        case UiPage::Devices: render_devices(snapshot); break;
        case UiPage::Volume: render_volume(snapshot); break;
        case UiPage::Vox: render_vox(snapshot); break;
    }
}

bool alloc_draw_buffer(int width) {
    const size_t bytes = static_cast<size_t>(width * kDrawRows) * sizeof(lv_color_t);
    g_pixels = static_cast<lv_color_t*>(
        heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (g_pixels == nullptr) {
        g_pixels = static_cast<lv_color_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    }
    return g_pixels != nullptr;
}

}  // namespace

bool Ui::start() {
    model_mutex_ = xSemaphoreCreateMutex();
    if (model_mutex_ == nullptr) return false;
    if (!alloc_draw_buffer(bsp_.display_width())) {
        ESP_LOGE(kTag, "LVGL draw buffer alloc failed (%d x %d)", bsp_.display_width(), kDrawRows);
        vSemaphoreDelete(model_mutex_);
        model_mutex_ = nullptr;
        return false;
    }
    if (xTaskCreatePinnedToCore(task_entry, "walkie_ui", 8192, this, 2, &task_handle_,
                                kUiCore) != pdPASS) {
        heap_caps_free(g_pixels);
        g_pixels = nullptr;
        vSemaphoreDelete(model_mutex_);
        model_mutex_ = nullptr;
        task_handle_ = nullptr;
        return false;
    }
    return true;
}

bool Ui::publish(const UiSnapshot& snapshot) {
    if (model_mutex_ == nullptr || task_handle_ == nullptr) return false;
    if (xSemaphoreTake(model_mutex_, portMAX_DELAY) != pdTRUE) return false;
    const bool notify = delivery_.publish(snapshot);
    xSemaphoreGive(model_mutex_);
    if (notify) xTaskNotifyGive(task_handle_);
    return true;
}

void Ui::task_entry(void* context) {
    static_cast<Ui*>(context)->run();
}

void Ui::run() {
    g_bsp = &bsp_;
    g_layout = make_layout(bsp_);
    g_soft_enabled = bsp_.uses_soft_keys();
    g_soft_layout = g_soft_enabled
                        ? make_soft_key_layout(bsp_.display_width(), bsp_.display_height(),
                                               bsp_.round_display(), bsp_.content_inset())
                        : SoftKeyLayout{};
    lv_init();
    const int width = bsp_.display_width();
    const int height = bsp_.display_height();
    if (g_pixels == nullptr) {
        ESP_LOGE(kTag, "LVGL draw buffer missing (%d x %d)", width, kDrawRows);
        task_handle_ = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    lv_disp_draw_buf_init(&g_draw_buffer, g_pixels, nullptr, width * kDrawRows);
    lv_disp_drv_init(&g_display_driver);
    g_display_driver.hor_res = width;
    g_display_driver.ver_res = height;
    g_display_driver.flush_cb = flush;
    g_display_driver.draw_buf = &g_draw_buffer;
    g_flush_align = bsp_.flush_align();
    if (g_flush_align > 1) g_display_driver.rounder_cb = rounder;
    lv_disp_t* display = lv_disp_drv_register(&g_display_driver);
    create_screen(width, height);

    UiSnapshot snapshot{};
    uint32_t last_tick_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    TickType_t wait_ticks = portMAX_DELAY;
    DisplayPowerPolicy display_power;
#if CONFIG_WALKIE_DIAGNOSTICS
    uint32_t sleep_handler_count = 0;
    uint32_t sleep_flush_count = 0;
    uint32_t sleep_blocked_flush_count = 0;
#endif
    for (;;) {
        const bool notified = ulTaskNotifyTake(pdTRUE, wait_ticks) != 0;
        const uint32_t tick_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        lv_tick_inc(tick_ms - last_tick_ms);
        last_tick_ms = tick_ms;

        if (notified) {
            xSemaphoreTake(model_mutex_, portMAX_DELAY);
            if (delivery_.has_snapshot()) snapshot = delivery_.latest();
            xSemaphoreGive(model_mutex_);

            if (!snapshot.backlight_on) {
                if (display_power.set_awake(false) == DisplayTransition::Sleep) {
                    bsp_.display_sleep();
#if CONFIG_WALKIE_DIAGNOSTICS
                    sleep_handler_count = g_handler_count;
                    sleep_flush_count = g_flush_count;
                    sleep_blocked_flush_count = g_blocked_flush_count;
                    ESP_LOGI(kTag, "Panel sleep: renders=%u handlers=%u flushes=%u blocked_flushes=%u",
                             static_cast<unsigned>(g_render_count),
                             static_cast<unsigned>(g_handler_count),
                             static_cast<unsigned>(g_flush_count),
                             static_cast<unsigned>(g_blocked_flush_count));
#endif
                }
                wait_ticks = portMAX_DELAY;
                continue;
            }

            if (display_power.set_awake(true) == DisplayTransition::Wake) {
                // The controller must be awake before LVGL can issue the first flush.
                bsp_.display_wakeup();
#if CONFIG_WALKIE_DIAGNOSTICS
                ESP_LOGI(kTag, "Panel wake: during_sleep handlers=%u flushes=%u blocked_flushes=%u",
                         static_cast<unsigned>(g_handler_count - sleep_handler_count),
                         static_cast<unsigned>(g_flush_count - sleep_flush_count),
                         static_cast<unsigned>(g_blocked_flush_count - sleep_blocked_flush_count));
#endif
            }
            render(snapshot);
#if CONFIG_WALKIE_DIAGNOSTICS
            ++g_render_count;
#endif
            lv_obj_invalidate(g_screen);
            lv_refr_now(display);
        }

        const uint32_t next_timer_ms = lv_timer_handler();
#if CONFIG_WALKIE_DIAGNOSTICS
        ++g_handler_count;
#endif
        wait_ticks = next_timer_ms == LV_NO_TIMER_READY
                         ? portMAX_DELAY
                         : pdMS_TO_TICKS(next_timer_ms == 0 ? 1 : next_timer_ms);
    }
}

}  // namespace walkie
