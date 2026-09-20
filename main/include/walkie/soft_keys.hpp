#pragma once

#include "walkie/bsp.hpp"
#include "walkie/navigation.hpp"

#include <cstdint>

namespace walkie {

struct Rect {
    int x{0};
    int y{0};
    int w{0};
    int h{0};

    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

enum class SoftHit : uint8_t {
    None = 0,
    Talk,
    Channel,
    Menu,
    Back,
    Item0,
    Item1,
    Item2,
    Item3,
    List,
};

struct SoftKeyLayout {
    bool enabled{false};
    int width{0};
    int height{0};
    Rect talk{};
    Rect channel{};
    Rect menu{};
    Rect back{};
    Rect items[4]{};
    Rect list{};
};

SoftKeyLayout make_soft_key_layout(int width, int height, bool round, int inset);

SoftHit hit_soft_key(const SoftKeyLayout& layout, UiPage page, int x, int y, bool screen_on);

struct SoftKeyEvents {
    bool talk_pressed{false};
    bool talk_released{false};
    bool channel_clicked{false};
    bool menu_clicked{false};
    bool back_clicked{false};
    bool list_clicked{false};
    int8_t item_clicked{-1};

    bool any() const {
        return talk_pressed || talk_released || channel_clicked || menu_clicked ||
               back_clicked || list_clicked || item_clicked >= 0;
    }
};

class SoftKeyRouter {
public:
    void set_layout(const SoftKeyLayout& layout) { layout_ = layout; }
    SoftKeyEvents feed(const PointerSample& pointer, UiPage page, bool screen_on, bool radio_idle);
    bool talk_held() const { return talk_held_; }

private:
    SoftKeyLayout layout_{};
    bool was_pressed_{false};
    bool started_off_{false};
    bool talk_held_{false};
    SoftHit press_hit_{SoftHit::None};
};

}  // namespace walkie
