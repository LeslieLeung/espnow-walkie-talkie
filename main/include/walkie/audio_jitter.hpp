#pragma once

#include "walkie/adpcm.hpp"
#include "walkie/protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace walkie::audio {

struct EncodedAudioFrame {
    uint32_t session_id{0};
    uint16_t sequence{0};
    AdpcmState initial_state{};
    std::array<uint8_t, protocol::kAdpcmBytes> data{};
};

enum class JitterPushResult : uint8_t {
    Accepted,
    Duplicate,
    TooOld,
    Full,
};

enum class JitterPopResult : uint8_t {
    Wait,
    Frame,
    Missing,
};

class AudioJitterBuffer {
public:
    static constexpr size_t kCapacity = 8;
    static constexpr size_t kPrefillFrames = 3;

    void reset();
    JitterPushResult push(const EncodedAudioFrame& frame);
    JitterPopResult pop(EncodedAudioFrame& frame);

    size_t buffered() const { return count_; }
    bool started() const { return started_; }
    uint32_t session_id() const { return session_id_; }
    uint16_t expected_sequence() const { return expected_sequence_; }

private:
    struct Slot {
        EncodedAudioFrame frame{};
        bool occupied{false};
    };

    void reset_for_session(uint32_t session_id, uint16_t first_sequence);
    Slot* find(uint16_t sequence);

    std::array<Slot, kCapacity> slots_{};
    size_t count_{0};
    uint32_t session_id_{0};
    uint16_t expected_sequence_{0};
    bool have_session_{false};
    bool started_{false};
};

}  // namespace walkie::audio
