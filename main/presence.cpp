#include "walkie/presence.hpp"

#include <algorithm>
#include <cstdio>

namespace walkie {

Peer* PresenceManager::find_mutable(const protocol::DeviceId& id) {
    for (auto& peer : peers_) {
        if (peer.occupied && peer.id == id) return &peer;
    }
    return nullptr;
}

const Peer* PresenceManager::find(const protocol::DeviceId& id) const {
    for (const auto& peer : peers_) {
        if (peer.occupied && peer.id == id) return &peer;
    }
    return nullptr;
}

Peer* PresenceManager::insertion_slot(uint32_t now_ms) {
    for (auto& peer : peers_) {
        if (!peer.occupied) return &peer;
    }
    return &*std::max_element(peers_.begin(), peers_.end(), [now_ms](const Peer& lhs, const Peer& rhs) {
        return static_cast<uint32_t>(now_ms - lhs.last_seen_ms) <
               static_cast<uint32_t>(now_ms - rhs.last_seen_ms);
    });
}

void PresenceManager::observe(const protocol::DeviceId& id, const protocol::Heartbeat& heartbeat,
                              uint32_t now_ms, int8_t rssi) {
    Peer* peer = find_mutable(id);
    if (peer == nullptr) peer = insertion_slot(now_ms);
    *peer = Peer{id, heartbeat, now_ms, rssi, true};
}

void PresenceManager::observe_activity(const protocol::DeviceId& id, protocol::DeviceState state,
                                       uint32_t now_ms, int8_t rssi) {
    if (Peer* peer = find_mutable(id); peer != nullptr) {
        peer->last_seen_ms = now_ms;
        peer->rssi = rssi;
        peer->heartbeat.state = state;
        return;
    }
    protocol::Heartbeat heartbeat{};
    std::snprintf(heartbeat.name.data(), heartbeat.name.size(), "S3-%02X%02X", id[4], id[5]);
    heartbeat.state = state;
    heartbeat.board = protocol::BoardType::StickS3;
    observe(id, heartbeat, now_ms, rssi);
}

size_t PresenceManager::expire(uint32_t now_ms) {
    size_t expired = 0;
    for (auto& peer : peers_) {
        if (peer.occupied && static_cast<uint32_t>(now_ms - peer.last_seen_ms) >= kExpiryMs) {
            peer = {};
            ++expired;
        }
    }
    return expired;
}

size_t PresenceManager::count() const {
    return static_cast<size_t>(std::count_if(peers_.begin(), peers_.end(),
                                            [](const Peer& peer) { return peer.occupied; }));
}

}  // namespace walkie
