#pragma once

#include "walkie/navigation.hpp"
#include "walkie/presence.hpp"
#include "walkie/talk_controller.hpp"

#include <array>
#include <cstdint>

namespace walkie {

struct UiPeer {
    std::array<char, 9> name{};
    uint8_t battery_percent{0};
    protocol::DeviceState state{protocol::DeviceState::Idle};
    int8_t rssi{0};
    bool occupied{false};
};

struct UiSnapshot {
    UiPage page{UiPage::Main};
    uint8_t logical_channel{1};
    uint8_t battery_percent{0};
    uint8_t online_count{0};
    TalkState talk_state{TalkState::Idle};
    uint8_t remaining_seconds{30};
    std::array<char, 9> speaker_name{};
    bool backlight_on{true};
    bool weak_signal{false};
    uint8_t menu_index{0};
    uint8_t volume_index{2};
    uint8_t volume_percent{50};
    uint8_t vox_index{0};
    uint8_t vox_level{0};
    bool vox_enabled{false};
    uint8_t peer_count{0};
    uint8_t device_offset{0};
    std::array<UiPeer, PresenceManager::kMaxPeers> peers{};
};

bool ui_snapshots_equal(const UiSnapshot& lhs, const UiSnapshot& rhs);

// Keeps the newest model while the display is asleep. Callers only need to
// wake the UI task for visible updates or display power transitions.
class UiDeliveryPolicy {
public:
    bool publish(const UiSnapshot& snapshot);
    bool has_snapshot() const { return has_snapshot_; }
    const UiSnapshot& latest() const { return latest_; }

private:
    UiSnapshot latest_{};
    bool has_snapshot_{false};
};

enum class DisplayTransition : uint8_t {
    None,
    Sleep,
    Wake,
};

class DisplayPowerPolicy {
public:
    DisplayTransition set_awake(bool awake);
    bool awake() const { return awake_; }

private:
    bool awake_{true};
};

class BatterySamplePolicy {
public:
    static constexpr uint32_t kScreenOnPeriodMs = 10000;
    static constexpr uint32_t kScreenOffPeriodMs = 60000;

    bool due(uint32_t now_ms, bool screen_on, TalkState talk_state) const;
    void sampled(uint32_t now_ms) {
        last_sample_ms_ = now_ms;
        has_sample_ = true;
    }

private:
    uint32_t last_sample_ms_{0};
    bool has_sample_{false};
};

bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms);

}  // namespace walkie
