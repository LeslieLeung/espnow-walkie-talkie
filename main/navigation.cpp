#include "walkie/navigation.hpp"

#include <algorithm>

namespace walkie {
namespace {
constexpr uint8_t kVolumes[] = {0, 25, 50, 75};
}

void NavigationController::open(uint32_t now_ms, uint8_t current_volume) {
    page_ = UiPage::Menu;
    menu_index_ = 0;
    device_offset_ = 0;
    const uint8_t normalized = static_cast<uint8_t>(std::min<uint8_t>(current_volume, 75) / 25);
    volume_index_ = std::min<uint8_t>(normalized, 3);
    touch(now_ms);
}

void NavigationController::short_b(uint32_t now_ms, size_t peer_count) {
    if (!active()) return;
    touch(now_ms);
    switch (page_) {
        case UiPage::Menu:
            menu_index_ = static_cast<uint8_t>((menu_index_ + 1) % 3);
            break;
        case UiPage::Devices:
            device_offset_ = peer_count == 0 ? 0 : (device_offset_ + 1) % peer_count;
            break;
        case UiPage::Volume:
            volume_index_ = static_cast<uint8_t>((volume_index_ + 1) % 4);
            break;
        case UiPage::Main:
            break;
    }
}

NavigationAction NavigationController::short_a(uint32_t now_ms) {
    if (!active()) return NavigationAction::None;
    touch(now_ms);
    if (page_ == UiPage::Menu) {
        if (menu_index_ == 0) {
            page_ = UiPage::Devices;
            device_offset_ = 0;
        } else if (menu_index_ == 1) {
            page_ = UiPage::Volume;
        } else {
            close();
        }
    } else if (page_ == UiPage::Volume) {
        page_ = UiPage::Menu;
        return NavigationAction::SaveVolume;
    }
    return NavigationAction::None;
}

void NavigationController::long_b(uint32_t now_ms) {
    if (!active()) return;
    touch(now_ms);
    if (page_ == UiPage::Menu) {
        close();
    } else {
        page_ = UiPage::Menu;
    }
}

bool NavigationController::tick(uint32_t now_ms) {
    if (active() && static_cast<uint32_t>(now_ms - last_input_ms_) >= kTimeoutMs) {
        close();
        return true;
    }
    return false;
}

void NavigationController::close() {
    page_ = UiPage::Main;
    device_offset_ = 0;
}

uint8_t NavigationController::volume_percent() const {
    return kVolumes[volume_index_];
}

}  // namespace walkie
