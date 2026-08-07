#include "walkie/audio_jitter.hpp"

namespace walkie::audio {
namespace {

bool is_before(uint16_t lhs, uint16_t rhs) {
    const uint16_t distance = static_cast<uint16_t>(rhs - lhs);
    return distance != 0 && distance < 0x8000U;
}

}  // namespace

void AudioJitterBuffer::reset() {
    slots_ = {};
    count_ = 0;
    session_id_ = 0;
    expected_sequence_ = 0;
    have_session_ = false;
    started_ = false;
}

void AudioJitterBuffer::reset_for_session(uint32_t session_id, uint16_t first_sequence) {
    reset();
    session_id_ = session_id;
    expected_sequence_ = first_sequence;
    have_session_ = true;
}

AudioJitterBuffer::Slot* AudioJitterBuffer::find(uint16_t sequence) {
    for (auto& slot : slots_) {
        if (slot.occupied && slot.frame.sequence == sequence) return &slot;
    }
    return nullptr;
}

JitterPushResult AudioJitterBuffer::push(const EncodedAudioFrame& frame) {
    if (!have_session_ || frame.session_id != session_id_) {
        reset_for_session(frame.session_id, frame.sequence);
    }
    if (find(frame.sequence) != nullptr) return JitterPushResult::Duplicate;
    if (started_ && is_before(frame.sequence, expected_sequence_)) return JitterPushResult::TooOld;
    if (count_ >= slots_.size()) return JitterPushResult::Full;
    if (!started_ && is_before(frame.sequence, expected_sequence_)) {
        expected_sequence_ = frame.sequence;
    }
    for (auto& slot : slots_) {
        if (!slot.occupied) {
            slot.frame = frame;
            slot.occupied = true;
            ++count_;
            return JitterPushResult::Accepted;
        }
    }
    return JitterPushResult::Full;
}

JitterPopResult AudioJitterBuffer::pop(EncodedAudioFrame& frame) {
    if (!have_session_ || (!started_ && count_ < kPrefillFrames)) return JitterPopResult::Wait;
    started_ = true;
    if (Slot* slot = find(expected_sequence_); slot != nullptr) {
        frame = slot->frame;
        slot->occupied = false;
        --count_;
        ++expected_sequence_;
        return JitterPopResult::Frame;
    }
    for (const auto& slot : slots_) {
        if (!slot.occupied) continue;
        const uint16_t distance = static_cast<uint16_t>(slot.frame.sequence - expected_sequence_);
        if (distance != 0 && distance < 0x8000U) {
            ++expected_sequence_;
            return JitterPopResult::Missing;
        }
    }
    return JitterPopResult::Wait;
}

}  // namespace walkie::audio
