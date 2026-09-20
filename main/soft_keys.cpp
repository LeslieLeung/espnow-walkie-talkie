#include "walkie/soft_keys.hpp"

namespace walkie {
namespace {

int scale(int value, int width, int base) {
    return (value * width + base / 2) / base;
}

}  // namespace

SoftKeyLayout make_soft_key_layout(int width, int height, bool round, int inset) {
    SoftKeyLayout layout{};
    layout.enabled = width > 0 && height > 0;
    layout.width = width;
    layout.height = height;
    const int base = round ? 466 : (width > 0 ? width : 240);
    const int gap = scale(12, width, base);
    const int talk_h = scale(56, width, base);
    layout.talk = {inset, height - inset - talk_h, width - inset * 2, talk_h};

    const int side_w = scale(110, width, base);
    const int side_h = scale(48, width, base);
    const int side_y = layout.talk.y - gap - side_h;
    layout.channel = {inset, side_y, side_w, side_h};
    layout.menu = {width - inset - side_w, side_y, side_w, side_h};
    layout.back = layout.talk;

    const int item_x = inset + scale(24, width, base);
    const int item_w = width - item_x * 2;
    const int item_h = scale(40, width, base);
    int item_y = round ? scale(130, width, base) : scale(64, width, base);
    for (int i = 0; i < 4; ++i) {
        layout.items[i] = {item_x, item_y + i * (item_h + gap), item_w, item_h};
    }
    layout.list = {item_x, item_y, item_w, item_h * 4 + gap * 3};
    return layout;
}

SoftHit hit_soft_key(const SoftKeyLayout& layout, UiPage page, int x, int y, bool screen_on) {
    if (!layout.enabled) return SoftHit::None;
    if (!screen_on) {
        return layout.talk.contains(x, y) ? SoftHit::Talk : SoftHit::None;
    }
    switch (page) {
        case UiPage::Main:
            if (layout.talk.contains(x, y)) return SoftHit::Talk;
            if (layout.channel.contains(x, y)) return SoftHit::Channel;
            if (layout.menu.contains(x, y)) return SoftHit::Menu;
            break;
        case UiPage::Menu:
        case UiPage::Volume:
        case UiPage::Vox:
            if (layout.back.contains(x, y)) return SoftHit::Back;
            for (uint8_t i = 0; i < 4; ++i) {
                if (layout.items[i].contains(x, y)) {
                    return static_cast<SoftHit>(static_cast<uint8_t>(SoftHit::Item0) + i);
                }
            }
            break;
        case UiPage::Devices:
            if (layout.back.contains(x, y)) return SoftHit::Back;
            if (layout.list.contains(x, y)) return SoftHit::List;
            break;
    }
    return SoftHit::None;
}

SoftKeyEvents SoftKeyRouter::feed(const PointerSample& pointer, UiPage page, bool screen_on,
                                  bool radio_idle) {
    SoftKeyEvents events{};
    if (!layout_.enabled || !pointer.valid) {
        if (talk_held_ && !pointer.pressed) {
            talk_held_ = false;
            events.talk_released = true;
        }
        was_pressed_ = false;
        press_hit_ = SoftHit::None;
        return events;
    }

    const bool pressed = pointer.pressed;
    const SoftHit hit =
        hit_soft_key(layout_, page, pointer.x, pointer.y, screen_on);

    if (pressed && !was_pressed_) {
        started_off_ = !screen_on;
        press_hit_ = hit;
        if (hit == SoftHit::Talk) {
            talk_held_ = true;
            events.talk_pressed = true;
        }
    } else if (pressed && was_pressed_) {
        if (talk_held_ && hit != SoftHit::Talk) {
            talk_held_ = false;
            events.talk_released = true;
        }
    } else if (!pressed && was_pressed_) {
        if (talk_held_) {
            talk_held_ = false;
            events.talk_released = true;
        } else if (screen_on && !started_off_) {
            switch (press_hit_) {
                case SoftHit::Channel:
                    if (radio_idle) events.channel_clicked = true;
                    break;
                case SoftHit::Menu:
                    events.menu_clicked = true;
                    break;
                case SoftHit::Back:
                    events.back_clicked = true;
                    break;
                case SoftHit::List:
                    events.list_clicked = true;
                    break;
                case SoftHit::Item0:
                    events.item_clicked = 0;
                    break;
                case SoftHit::Item1:
                    events.item_clicked = 1;
                    break;
                case SoftHit::Item2:
                    events.item_clicked = 2;
                    break;
                case SoftHit::Item3:
                    events.item_clicked = 3;
                    break;
                default:
                    break;
            }
        }
        press_hit_ = SoftHit::None;
        started_off_ = false;
    }

    was_pressed_ = pressed;
    return events;
}

}  // namespace walkie
