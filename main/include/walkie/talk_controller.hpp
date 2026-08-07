#pragma once

#include "walkie/protocol.hpp"

#include <cstdint>

namespace walkie {

enum class TalkState : uint8_t {
    Idle,
    Requesting,
    Talking,
    Receiving,
    Busy,
};

enum Action : uint16_t {
    NoAction = 0,
    SendClaim = 1U << 0,
    SendStart = 1U << 1,
    SendEnd = 1U << 2,
    StartCapture = 1U << 3,
    StopCapture = 1U << 4,
    StartPlayback = 1U << 5,
    StopPlayback = 1U << 6,
    TalkTimedOut = 1U << 7,
    PlayRequestCue = 1U << 8,
    PlayTimeoutCue = 1U << 9,
};

using Actions = uint16_t;

struct TalkSnapshot {
    TalkState state{TalkState::Idle};
    uint32_t session_id{0};
    protocol::DeviceId speaker_id{};
    uint32_t arbitration_value{0};
    uint32_t talk_started_ms{0};
};

class TalkController {
public:
    static constexpr uint32_t kContentionWindowMs = 100;
    static constexpr uint32_t kMaximumTalkMs = 30000;
    static constexpr uint32_t kRemoteFloorTimeoutMs = 800;

    explicit TalkController(protocol::DeviceId local_id) : local_id_(local_id) {}

    Actions ptt_pressed(uint32_t now_ms, uint32_t random_value, uint32_t session_id);
    Actions ptt_released(uint32_t now_ms);
    Actions tick(uint32_t now_ms);
    Actions receive_claim(const protocol::Header& header, const protocol::Claim& claim,
                          uint32_t now_ms);
    Actions receive_start(const protocol::Header& header, uint32_t now_ms);
    Actions receive_audio(const protocol::Header& header, uint32_t now_ms);
    Actions receive_end(const protocol::Header& header, uint32_t now_ms);
    void reset();

    const TalkSnapshot& snapshot() const { return snapshot_; }
    uint32_t local_session_id() const { return local_session_id_; }
    uint32_t action_session_id() const { return action_session_id_; }

private:
    bool local_candidate_wins(uint32_t value, const protocol::DeviceId& id) const;
    bool is_active_remote_session(const protocol::Header& header) const;

    protocol::DeviceId local_id_{};
    TalkSnapshot snapshot_{};
    protocol::DeviceId best_candidate_{};
    uint32_t best_value_{0};
    uint32_t local_session_id_{0};
    uint32_t action_session_id_{0};
    uint32_t claim_due_ms_{0};
    uint32_t resolve_due_ms_{0};
    uint32_t remote_activity_ms_{0};
    bool claim_sent_{false};
    bool ptt_held_{false};
};

}  // namespace walkie
