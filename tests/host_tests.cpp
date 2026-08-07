#include "walkie/adpcm.hpp"
#include "walkie/audio_jitter.hpp"
#include "walkie/display_policy.hpp"
#include "walkie/navigation.hpp"
#include "walkie/presence.hpp"
#include "walkie/protocol.hpp"
#include "walkie/talk_controller.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using namespace walkie;
namespace wp = walkie::protocol;

wp::DeviceId id(uint8_t suffix) {
    return wp::DeviceId{0x02, 0, 0, 0, 0, suffix};
}

void test_protocol_round_trip() {
    wp::Heartbeat heartbeat{};
    heartbeat.name = {'S', '3', '-', 'A', '1', 'B', '2', '\0'};
    heartbeat.battery_percent = 72;
    heartbeat.state = wp::DeviceState::Idle;
    heartbeat.board = wp::BoardType::StickS3;
    uint8_t payload[16]{};
    size_t payload_size = 0;
    assert(wp::encode_heartbeat(heartbeat, payload, sizeof(payload), payload_size));

    wp::Header header{};
    header.type = wp::MessageType::Heartbeat;
    header.logical_channel = 3;
    header.sender_id = id(0x42);
    header.session_id = 0x10203040;
    header.sequence = 0x5060;
    header.timestamp_ms = 0x708090A0;
    std::array<uint8_t, wp::kMaxWireSize> wire{};
    size_t wire_size = 0;
    assert(wp::encode(header, payload, payload_size, wire.data(), wire.size(), wire_size));
    assert(wire_size == wp::kHeaderSize + payload_size);

    wp::PacketView decoded{};
    assert(wp::decode(wire.data(), wire_size, decoded) == wp::DecodeError::None);
    assert(decoded.header.logical_channel == 3);
    assert(decoded.header.sender_id == header.sender_id);
    assert(decoded.header.session_id == header.session_id);
    assert(decoded.header.sequence == header.sequence);
    wp::Heartbeat decoded_heartbeat{};
    assert(wp::decode_heartbeat(decoded, decoded_heartbeat));
    assert(decoded_heartbeat.name == heartbeat.name);
    assert(decoded_heartbeat.battery_percent == 72);

    wire[0] ^= 0x01;
    assert(wp::decode(wire.data(), wire_size, decoded) == wp::DecodeError::BadMagic);
    wire[0] ^= 0x01;
    wire[24] = 0;
    wire[25] = 0;
    assert(wp::decode(wire.data(), wire_size, decoded) == wp::DecodeError::LengthMismatch);
    assert(wp::decode(wire.data(), wp::kHeaderSize - 1, decoded) == wp::DecodeError::TooShort);

    std::array<uint8_t, wp::kMaxWireSize + 1> oversized{};
    assert(wp::decode(oversized.data(), oversized.size(), decoded) == wp::DecodeError::TooLarge);
    std::array<uint8_t, wp::kMaxPayloadSize + 1> large_payload{};
    assert(!wp::encode(header, large_payload.data(), large_payload.size(), wire.data(), wire.size(), wire_size));
    assert(!wp::encode(header, payload, static_cast<size_t>(-1), wire.data(), wire.size(), wire_size));

    assert(wp::encode(header, payload, payload_size, wire.data(), wire.size(), wire_size));
    wire[4] = wp::kVersion + 1;
    assert(wp::decode(wire.data(), wire_size, decoded) == wp::DecodeError::BadVersion);
    wire[4] = wp::kVersion;
    wire[5] = 0xFF;
    assert(wp::decode(wire.data(), wire_size, decoded) == wp::DecodeError::BadType);
    wire[5] = static_cast<uint8_t>(wp::MessageType::Heartbeat);
    wire[7] = 5;
    assert(wp::decode(wire.data(), wire_size, decoded) == wp::DecodeError::BadChannel);

    wp::Header start = header;
    start.type = wp::MessageType::TalkStart;
    assert(!wp::encode(start, payload, 1, wire.data(), wire.size(), wire_size));
    start.type = wp::MessageType::Audio;
    assert(!wp::encode(start, payload, payload_size, wire.data(), wire.size(), wire_size));

    assert(wp::encode(header, payload, payload_size, wire.data(), wire.size(), wire_size));
    wire[5] = static_cast<uint8_t>(wp::MessageType::TalkStart);
    assert(wp::decode(wire.data(), wire_size, decoded) == wp::DecodeError::BadPayloadSize);
}

void test_adpcm() {
    std::array<int16_t, wp::kAudioSamples> input{};
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<int16_t>(std::sin(static_cast<double>(i) * 0.11) * 12000.0);
    }
    std::array<uint8_t, wp::kAdpcmBytes> encoded{};
    std::array<int16_t, wp::kAudioSamples> output{};
    audio::AdpcmState encoder{};
    const audio::AdpcmState initial = encoder;
    assert(audio::encode_ima_adpcm(input.data(), input.size(), encoded.data(), encoded.size(), encoder));
    audio::AdpcmState decoder = initial;
    assert(audio::decode_ima_adpcm(encoded.data(), encoded.size(), output.data(), output.size(), decoder));
    int64_t absolute_error = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        absolute_error += std::abs(static_cast<int>(input[i]) - output[i]);
    }
    assert(absolute_error / static_cast<int64_t>(input.size()) < 1800);
    assert(encoder.predictor == decoder.predictor);
    assert(encoder.step_index == decoder.step_index);
    decoder.step_index = 89;
    assert(!audio::decode_ima_adpcm(encoded.data(), encoded.size(), output.data(), output.size(), decoder));
    decoder = {};
    assert(!audio::decode_ima_adpcm(encoded.data(), static_cast<size_t>(-1),
                                    output.data(), output.size(), decoder));
}

void test_presence() {
    PresenceManager presence;
    wp::Heartbeat heartbeat{};
    for (uint8_t i = 1; i <= PresenceManager::kMaxPeers; ++i) {
        heartbeat.battery_percent = static_cast<uint8_t>(50 + i);
        presence.observe(id(i), heartbeat, 1000 + i, -40);
    }
    assert(presence.count() == PresenceManager::kMaxPeers);
    presence.observe(id(9), heartbeat, 2000, -60);
    assert(presence.count() == PresenceManager::kMaxPeers);
    assert(presence.find(id(1)) == nullptr);
    assert(presence.find(id(9)) != nullptr);
    presence.observe_activity(id(10), wp::DeviceState::Talking, 3000, -55);
    const Peer* activity_only = presence.find(id(10));
    assert(activity_only != nullptr);
    assert(activity_only->heartbeat.state == wp::DeviceState::Talking);
    assert(presence.expire(9001) == PresenceManager::kMaxPeers);
    assert(presence.count() == 0);

    PresenceManager wrapped;
    wrapped.observe(id(1), heartbeat, 0xFFFFFF00U, -40);
    assert(wrapped.expire(0x00000080U) == 0);
    assert(wrapped.expire(0x00002000U) == 1);
}

void test_display_policy() {
    assert(screen_background_rgb(TalkState::Idle) == kIdleScreenRgb);
    assert(screen_background_rgb(TalkState::Requesting) == kIdleScreenRgb);
    assert(screen_background_rgb(TalkState::Talking) == kTransmittingScreenRgb);
    assert(screen_background_rgb(TalkState::Receiving) == kReceivingScreenRgb);
    assert(screen_background_rgb(TalkState::Busy) == kReceivingScreenRgb);

    assert(!talk_activity_keeps_screen_awake(TalkState::Idle));
    assert(!talk_activity_keeps_screen_awake(TalkState::Requesting));
    assert(talk_activity_keeps_screen_awake(TalkState::Talking));
    assert(talk_activity_keeps_screen_awake(TalkState::Receiving));
    assert(talk_activity_keeps_screen_awake(TalkState::Busy));
}

void test_start_conflict_resolution() {
    TalkController lower(id(1));
    TalkController higher(id(2));
    lower.ptt_pressed(1000, 10, 11);
    higher.ptt_pressed(1000, 20, 22);
    lower.tick(1050);
    higher.tick(1050);
    assert((lower.tick(1100) & StartCapture) != 0);
    assert((higher.tick(1100) & StartCapture) != 0);

    wp::Header from_lower{};
    from_lower.type = wp::MessageType::TalkStart;
    from_lower.sender_id = id(1);
    from_lower.session_id = 11;
    wp::Header from_higher = from_lower;
    from_higher.sender_id = id(2);
    from_higher.session_id = 22;

    assert(lower.receive_start(from_higher, 1101) == NoAction);
    assert(lower.snapshot().state == TalkState::Talking);
    const Actions higher_yields = higher.receive_start(from_lower, 1101);
    assert((higher_yields & StopCapture) != 0);
    assert((higher_yields & StartPlayback) != 0);
    assert(higher.snapshot().state == TalkState::Busy);

    // A repeated start refreshes the active session without restarting playback.
    assert(higher.receive_start(from_lower, 1200) == NoAction);
    wp::Header intruder = from_lower;
    intruder.sender_id = id(3);
    intruder.session_id = 33;
    assert(higher.receive_start(intruder, 1201) == NoAction);
    assert(higher.snapshot().speaker_id == id(1));

    TalkController observer(id(9));
    wp::Header first = from_lower;
    first.sender_id = id(3);
    first.session_id = 30;
    assert((observer.receive_start(first, 1300) & StartPlayback) != 0);
    assert(observer.snapshot().speaker_id == id(3));
    assert(observer.receive_start(from_lower, 1301) == NoAction);
    assert(observer.snapshot().speaker_id == id(1));
}

void test_arbitration_and_timeout() {
    TalkController a(id(1));
    TalkController b(id(2));
    assert((a.ptt_pressed(1000, 100, 11) & PlayRequestCue) != 0);
    assert((b.ptt_pressed(1000, 50, 22) & PlayRequestCue) != 0);
    assert((a.tick(1050) & SendClaim) != 0);
    assert((b.tick(1050) & SendClaim) != 0);

    wp::Header from_a{};
    from_a.type = wp::MessageType::TalkClaim;
    from_a.sender_id = id(1);
    from_a.session_id = 11;
    wp::Header from_b = from_a;
    from_b.sender_id = id(2);
    from_b.session_id = 22;
    a.receive_claim(from_b, wp::Claim{50}, 1050);
    b.receive_claim(from_a, wp::Claim{100}, 1050);
    assert(a.tick(1100) == NoAction);
    const Actions b_wins = b.tick(1100);
    assert((b_wins & SendStart) != 0);
    assert((b_wins & StartCapture) != 0);
    assert(b.snapshot().state == TalkState::Talking);

    from_b.type = wp::MessageType::TalkStart;
    const Actions a_receives = a.receive_start(from_b, 1101);
    assert((a_receives & StartPlayback) != 0);
    assert(a.snapshot().state == TalkState::Busy);
    assert((a.tick(1901) & StopPlayback) != 0);
    assert(a.snapshot().state == TalkState::Idle);

    const Actions timeout = b.tick(31100);
    assert((timeout & SendEnd) != 0);
    assert((timeout & StopCapture) != 0);
    assert((timeout & TalkTimedOut) != 0);
    assert((timeout & PlayTimeoutCue) != 0);
    assert(b.action_session_id() == 22);
}

void test_navigation() {
    NavigationController navigation;
    assert(!navigation.active());
    navigation.open(100, 50);
    assert(navigation.page() == UiPage::Menu);
    assert(navigation.menu_index() == 0);
    navigation.short_a(110);
    assert(navigation.page() == UiPage::Devices);
    navigation.short_b(120, 8);
    assert(navigation.device_offset() == 1);
    navigation.long_b(130);
    assert(navigation.page() == UiPage::Menu);

    navigation.short_b(140, 8);
    assert(navigation.menu_index() == 1);
    navigation.short_a(150);
    assert(navigation.page() == UiPage::Volume);
    assert(navigation.volume_percent() == 50);
    navigation.short_b(160, 8);
    assert(navigation.volume_percent() == 75);
    assert(navigation.short_a(170) == NavigationAction::SaveVolume);
    assert(navigation.page() == UiPage::Menu);
    assert(navigation.tick(10169) == false);
    assert(navigation.tick(10170) == true);
    assert(navigation.page() == UiPage::Main);

    navigation.open(20000, 255);
    assert(navigation.volume_percent() == 75);
    navigation.long_b(20001);
    assert(navigation.page() == UiPage::Main);
}

audio::EncodedAudioFrame audio_frame(uint32_t session, uint16_t sequence) {
    audio::EncodedAudioFrame frame{};
    frame.session_id = session;
    frame.sequence = sequence;
    frame.data[0] = static_cast<uint8_t>(sequence);
    return frame;
}

void test_jitter_buffer() {
    audio::AudioJitterBuffer jitter;
    assert(jitter.push(audio_frame(7, 10)) == audio::JitterPushResult::Accepted);
    assert(jitter.push(audio_frame(7, 12)) == audio::JitterPushResult::Accepted);
    assert(jitter.push(audio_frame(7, 12)) == audio::JitterPushResult::Duplicate);
    audio::EncodedAudioFrame output{};
    assert(jitter.pop(output) == audio::JitterPopResult::Wait);
    assert(jitter.push(audio_frame(7, 11)) == audio::JitterPushResult::Accepted);
    assert(jitter.pop(output) == audio::JitterPopResult::Frame && output.sequence == 10);
    assert(jitter.pop(output) == audio::JitterPopResult::Frame && output.sequence == 11);
    assert(jitter.pop(output) == audio::JitterPopResult::Frame && output.sequence == 12);
    assert(jitter.push(audio_frame(7, 11)) == audio::JitterPushResult::TooOld);

    jitter.reset();
    jitter.push(audio_frame(8, 20));
    jitter.push(audio_frame(8, 22));
    jitter.push(audio_frame(8, 23));
    assert(jitter.pop(output) == audio::JitterPopResult::Frame && output.sequence == 20);
    assert(jitter.pop(output) == audio::JitterPopResult::Missing);
    assert(jitter.expected_sequence() == 22);
    assert(jitter.pop(output) == audio::JitterPopResult::Frame && output.sequence == 22);
    assert(jitter.pop(output) == audio::JitterPopResult::Frame && output.sequence == 23);

    assert(jitter.push(audio_frame(9, 65535)) == audio::JitterPushResult::Accepted);
    assert(jitter.session_id() == 9);
    jitter.push(audio_frame(9, 0));
    jitter.push(audio_frame(9, 1));
    assert(jitter.pop(output) == audio::JitterPopResult::Frame && output.sequence == 65535);
    assert(jitter.pop(output) == audio::JitterPopResult::Frame && output.sequence == 0);
}

void test_eight_device_simulation() {
    constexpr size_t kDevices = 8;
    const uint32_t priorities[kDevices] = {100, 50, 50, 200, 75, 90, 300, 80};
    std::vector<TalkController> controllers;
    controllers.reserve(kDevices);
    for (uint8_t index = 0; index < kDevices; ++index) {
        controllers.emplace_back(id(static_cast<uint8_t>(index + 1)));
        controllers.back().ptt_pressed(1000, priorities[index], 100 + index);
    }
    for (auto& controller : controllers) {
        assert((controller.tick(1050) & SendClaim) != 0);
    }
    for (size_t receiver = 0; receiver < kDevices; ++receiver) {
        for (size_t sender = 0; sender < kDevices; ++sender) {
            if (receiver == sender) continue;
            wp::Header header{};
            header.type = wp::MessageType::TalkClaim;
            header.sender_id = id(static_cast<uint8_t>(sender + 1));
            header.session_id = static_cast<uint32_t>(100 + sender);
            controllers[receiver].receive_claim(header, wp::Claim{priorities[sender]}, 1050);
        }
    }

    size_t winners = 0;
    size_t winner_index = 0;
    for (size_t index = 0; index < kDevices; ++index) {
        const Actions actions = controllers[index].tick(1100);
        if ((actions & SendStart) != 0) {
            ++winners;
            winner_index = index;
        }
    }
    assert(winners == 1);
    assert(winner_index == 1);  // priority tie resolved by the lower device ID

    wp::Header start{};
    start.type = wp::MessageType::TalkStart;
    start.sender_id = id(static_cast<uint8_t>(winner_index + 1));
    start.session_id = static_cast<uint32_t>(100 + winner_index);
    for (size_t index = 0; index < kDevices; ++index) {
        if (index == winner_index) continue;
        assert((controllers[index].receive_start(start, 1101) & StartPlayback) != 0);
        assert(controllers[index].snapshot().state == TalkState::Busy);
    }
    assert((controllers[winner_index].ptt_released(1200) & SendEnd) != 0);
    start.type = wp::MessageType::TalkEnd;
    for (size_t index = 0; index < kDevices; ++index) {
        if (index == winner_index) continue;
        assert((controllers[index].receive_end(start, 1201) & StopPlayback) != 0);
        assert(controllers[index].snapshot().state == TalkState::Idle);
    }

    std::array<PresenceManager, kDevices> presence{};
    wp::Heartbeat heartbeat{};
    for (size_t receiver = 0; receiver < kDevices; ++receiver) {
        for (size_t sender = 0; sender < kDevices; ++sender) {
            if (receiver == sender) continue;
            presence[receiver].observe(id(static_cast<uint8_t>(sender + 1)), heartbeat, 2000, -45);
        }
        assert(presence[receiver].count() == kDevices - 1);
    }
}

}  // namespace

int main() {
    test_protocol_round_trip();
    test_adpcm();
    test_presence();
    test_display_policy();
    test_arbitration_and_timeout();
    test_start_conflict_resolution();
    test_navigation();
    test_jitter_buffer();
    test_eight_device_simulation();
    std::cout << "walkie host tests: PASS\n";
    return 0;
}
