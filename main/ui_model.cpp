#include "walkie/ui_model.hpp"

#include <algorithm>

namespace walkie {
namespace {

bool peers_equal(const UiPeer& lhs, const UiPeer& rhs) {
    return lhs.name == rhs.name &&
           lhs.battery_percent == rhs.battery_percent &&
           lhs.state == rhs.state &&
           lhs.occupied == rhs.occupied;
}

bool active_talk_state(TalkState state) {
    return state == TalkState::Talking ||
           state == TalkState::Receiving ||
           state == TalkState::Busy;
}

}  // namespace

bool ui_snapshots_equal(const UiSnapshot& lhs, const UiSnapshot& rhs) {
    if (lhs.page != rhs.page ||
        lhs.logical_channel != rhs.logical_channel ||
        lhs.battery_percent != rhs.battery_percent ||
        lhs.online_count != rhs.online_count ||
        lhs.talk_state != rhs.talk_state ||
        lhs.remaining_seconds != rhs.remaining_seconds ||
        lhs.speaker_name != rhs.speaker_name ||
        lhs.backlight_on != rhs.backlight_on ||
        lhs.weak_signal != rhs.weak_signal ||
        lhs.menu_index != rhs.menu_index ||
        lhs.volume_index != rhs.volume_index ||
        lhs.volume_percent != rhs.volume_percent) {
        return false;
    }

    if (lhs.page != UiPage::Devices) return true;
    if (lhs.peer_count != rhs.peer_count || lhs.device_offset != rhs.device_offset) return false;
    const size_t peer_count = std::min<size_t>(lhs.peer_count, lhs.peers.size());
    for (size_t index = 0; index < peer_count; ++index) {
        if (!peers_equal(lhs.peers[index], rhs.peers[index])) return false;
    }
    return true;
}

bool UiDeliveryPolicy::publish(const UiSnapshot& snapshot) {
    const bool notify = !has_snapshot_ || snapshot.backlight_on ||
                        latest_.backlight_on != snapshot.backlight_on;
    latest_ = snapshot;
    has_snapshot_ = true;
    return notify;
}

DisplayTransition DisplayPowerPolicy::set_awake(bool awake) {
    if (awake_ == awake) return DisplayTransition::None;
    awake_ = awake;
    return awake ? DisplayTransition::Wake : DisplayTransition::Sleep;
}

bool BatterySamplePolicy::due(uint32_t now_ms, bool screen_on, TalkState talk_state) const {
    if (active_talk_state(talk_state)) return false;
    if (!has_sample_) return true;
    const uint32_t period = screen_on ? kScreenOnPeriodMs : kScreenOffPeriodMs;
    return static_cast<uint32_t>(now_ms - last_sample_ms_) >= period;
}

bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

}  // namespace walkie
