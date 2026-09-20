#pragma once

#include <cstddef>
#include <cstdint>

namespace walkie {

enum class UiPage : uint8_t {
    Main,
    Menu,
    Devices,
    Volume,
    Vox,
};

enum class NavigationAction : uint8_t {
    None,
    SaveVolume,
    SaveVox,
};

enum class ChannelButtonAction : uint8_t {
    None,
    OpenMenu,
    CycleChannel,
};

// Long-press B always opens the menu, including while this unit is talking.
// Short-press B only cycles the logical channel when the radio is idle.
inline ChannelButtonAction classify_channel_button(bool held, bool clicked, bool idle) {
    if (held) return ChannelButtonAction::OpenMenu;
    if (clicked && idle) return ChannelButtonAction::CycleChannel;
    return ChannelButtonAction::None;
}

class NavigationController {
public:
    static constexpr uint32_t kTimeoutMs = 10000;

    void open(uint32_t now_ms, uint8_t current_volume, uint8_t vox_level);
    void short_b(uint32_t now_ms, size_t peer_count);
    NavigationAction short_a(uint32_t now_ms);
    void long_b(uint32_t now_ms);
    // Touch boards: tap a row instead of moving a cursor. Volume/VOX save in place.
    NavigationAction activate_index(uint32_t now_ms, uint8_t index);
    bool tick(uint32_t now_ms);
    void close();

    UiPage page() const { return page_; }
    uint8_t menu_index() const { return menu_index_; }
    uint8_t volume_index() const { return volume_index_; }
    uint8_t volume_percent() const;
    uint8_t vox_index() const { return vox_index_; }
    uint8_t vox_level() const { return vox_index_; }
    bool vox_enabled() const { return vox_index_ != 0; }
    size_t device_offset() const { return device_offset_; }
    bool active() const { return page_ != UiPage::Main; }

private:
    void touch(uint32_t now_ms) { last_input_ms_ = now_ms; }

    UiPage page_{UiPage::Main};
    uint8_t menu_index_{0};
    uint8_t volume_index_{2};
    uint8_t vox_index_{0};
    size_t device_offset_{0};
    uint32_t last_input_ms_{0};
};

}  // namespace walkie
