#pragma once

#include "walkie/protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace walkie {

struct Peer {
    protocol::DeviceId id{};
    protocol::Heartbeat heartbeat{};
    uint32_t last_seen_ms{0};
    int8_t rssi{0};
    bool occupied{false};
};

class PresenceManager {
public:
    static constexpr size_t kMaxPeers = 8;
    static constexpr uint32_t kExpiryMs = 6000;

    void observe(const protocol::DeviceId& id, const protocol::Heartbeat& heartbeat,
                 uint32_t now_ms, int8_t rssi);
    void observe_activity(const protocol::DeviceId& id, protocol::DeviceState state,
                          uint32_t now_ms, int8_t rssi);
    size_t expire(uint32_t now_ms);
    size_t count() const;
    const Peer* find(const protocol::DeviceId& id) const;
    const std::array<Peer, kMaxPeers>& peers() const { return peers_; }

private:
    Peer* find_mutable(const protocol::DeviceId& id);
    Peer* insertion_slot(uint32_t now_ms);

    std::array<Peer, kMaxPeers> peers_{};
};

}  // namespace walkie
