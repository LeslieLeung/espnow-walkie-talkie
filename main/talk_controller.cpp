#include "walkie/talk_controller.hpp"

namespace walkie {
namespace {

bool elapsed(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace

bool TalkController::local_candidate_wins(uint32_t value, const protocol::DeviceId& id) const {
    return value < best_value_ ||
           (value == best_value_ && protocol::compare_device_id(id, best_candidate_) < 0);
}

Actions TalkController::ptt_pressed(uint32_t now_ms, uint32_t random_value, uint32_t session_id) {
    ptt_held_ = true;
    if (snapshot_.state == TalkState::Receiving) {
        snapshot_.state = TalkState::Busy;
        return NoAction;
    }
    if (snapshot_.state != TalkState::Idle) return NoAction;

    local_session_id_ = session_id == 0 ? 1 : session_id;
    action_session_id_ = local_session_id_;
    best_value_ = random_value;
    best_candidate_ = local_id_;
    snapshot_.state = TalkState::Requesting;
    snapshot_.session_id = local_session_id_;
    snapshot_.arbitration_value = random_value;
    snapshot_.speaker_id = {};
    claim_due_ms_ = now_ms + 10 + (random_value % 31);
    resolve_due_ms_ = now_ms + kContentionWindowMs;
    claim_sent_ = false;
    return PlayRequestCue;
}

Actions TalkController::ptt_released(uint32_t) {
    ptt_held_ = false;
    if (snapshot_.state == TalkState::Talking) {
        action_session_id_ = local_session_id_;
        reset();
        return SendEnd | StopCapture;
    }
    if (snapshot_.state == TalkState::Requesting) {
        reset();
    } else if (snapshot_.state == TalkState::Busy) {
        snapshot_.state = TalkState::Receiving;
    }
    return NoAction;
}

Actions TalkController::tick(uint32_t now_ms) {
    Actions actions = NoAction;
    if (snapshot_.state == TalkState::Requesting) {
        if (!claim_sent_ && elapsed(now_ms, claim_due_ms_)) {
            claim_sent_ = true;
            actions |= SendClaim;
        }
        if (elapsed(now_ms, resolve_due_ms_)) {
            if (best_candidate_ == local_id_ && ptt_held_) {
                action_session_id_ = local_session_id_;
                snapshot_.state = TalkState::Talking;
                snapshot_.speaker_id = local_id_;
                snapshot_.talk_started_ms = now_ms;
                actions |= SendStart | StartCapture;
            } else {
                reset();
            }
        }
    } else if (snapshot_.state == TalkState::Talking &&
               static_cast<uint32_t>(now_ms - snapshot_.talk_started_ms) >= kMaximumTalkMs) {
        action_session_id_ = local_session_id_;
        reset();
        ptt_held_ = false;
        actions |= SendEnd | StopCapture | TalkTimedOut | PlayTimeoutCue;
    } else if ((snapshot_.state == TalkState::Receiving || snapshot_.state == TalkState::Busy) &&
               static_cast<uint32_t>(now_ms - remote_activity_ms_) >= kRemoteFloorTimeoutMs) {
        reset();
        actions |= StopPlayback;
    }
    return actions;
}

Actions TalkController::receive_claim(const protocol::Header& header, const protocol::Claim& claim,
                                      uint32_t) {
    if (header.sender_id == local_id_ || snapshot_.state != TalkState::Requesting) return NoAction;
    if (local_candidate_wins(claim.arbitration_value, header.sender_id)) {
        best_value_ = claim.arbitration_value;
        best_candidate_ = header.sender_id;
    }
    return NoAction;
}

Actions TalkController::receive_start(const protocol::Header& header, uint32_t now_ms) {
    if (header.sender_id == local_id_) return NoAction;
    if (is_active_remote_session(header)) {
        remote_activity_ms_ = now_ms;
        return NoAction;
    }
    if (snapshot_.state == TalkState::Receiving || snapshot_.state == TalkState::Busy) {
        if (protocol::compare_device_id(header.sender_id, snapshot_.speaker_id) < 0) {
            snapshot_.session_id = header.session_id;
            snapshot_.speaker_id = header.sender_id;
            snapshot_.talk_started_ms = now_ms;
            remote_activity_ms_ = now_ms;
        }
        return NoAction;
    }
    // If claims crossed or were lost, both contenders can briefly believe they won.
    // Device ID provides a deterministic final fallback so exactly one keeps sending.
    if (snapshot_.state == TalkState::Talking &&
        protocol::compare_device_id(local_id_, header.sender_id) < 0) {
        return NoAction;
    }
    Actions actions = NoAction;
    if (snapshot_.state == TalkState::Talking) actions |= StopCapture;
    snapshot_.state = ptt_held_ ? TalkState::Busy : TalkState::Receiving;
    snapshot_.session_id = header.session_id;
    snapshot_.speaker_id = header.sender_id;
    snapshot_.talk_started_ms = now_ms;
    remote_activity_ms_ = now_ms;
    return actions | StartPlayback;
}

bool TalkController::is_active_remote_session(const protocol::Header& header) const {
    return (snapshot_.state == TalkState::Receiving || snapshot_.state == TalkState::Busy) &&
           header.session_id == snapshot_.session_id && header.sender_id == snapshot_.speaker_id;
}

Actions TalkController::receive_audio(const protocol::Header& header, uint32_t now_ms) {
    if (!is_active_remote_session(header)) return NoAction;
    remote_activity_ms_ = now_ms;
    return NoAction;
}

Actions TalkController::receive_end(const protocol::Header& header, uint32_t) {
    if (!is_active_remote_session(header)) return NoAction;
    reset();
    return StopPlayback;
}

void TalkController::reset() {
    snapshot_ = {};
    best_candidate_ = {};
    best_value_ = 0;
    local_session_id_ = 0;
    claim_due_ms_ = 0;
    resolve_due_ms_ = 0;
    remote_activity_ms_ = 0;
    claim_sent_ = false;
}

}  // namespace walkie
