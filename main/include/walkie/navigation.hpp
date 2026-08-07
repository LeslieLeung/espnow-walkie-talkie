#pragma once

#include <cstddef>
#include <cstdint>

namespace walkie {

enum class UiPage : uint8_t {
    Main,
    Menu,
    Devices,
    Volume,
};

enum class NavigationAction : uint8_t {
    None,
    SaveVolume,
};

class NavigationController {
public:
    static constexpr uint32_t kTimeoutMs = 10000;

    void open(uint32_t now_ms, uint8_t current_volume);
    void short_b(uint32_t now_ms, size_t peer_count);
    NavigationAction short_a(uint32_t now_ms);
    void long_b(uint32_t now_ms);
    bool tick(uint32_t now_ms);
    void close();

    UiPage page() const { return page_; }
    uint8_t menu_index() const { return menu_index_; }
    uint8_t volume_index() const { return volume_index_; }
    uint8_t volume_percent() const;
    size_t device_offset() const { return device_offset_; }
    bool active() const { return page_ != UiPage::Main; }

private:
    void touch(uint32_t now_ms) { last_input_ms_ = now_ms; }

    UiPage page_{UiPage::Main};
    uint8_t menu_index_{0};
    uint8_t volume_index_{2};
    size_t device_offset_{0};
    uint32_t last_input_ms_{0};
};

}  // namespace walkie
