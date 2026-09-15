#include "walkie/adpcm.hpp"
#include "walkie/audio_jitter.hpp"
#include "walkie/display_policy.hpp"
#include "walkie/navigation.hpp"
#include "walkie/button_ladder.hpp"
#include "walkie/presence.hpp"
#include "walkie/protocol.hpp"
#include "walkie/talk_controller.hpp"
#include "walkie/ui_model.hpp"
#include "walkie/vox.hpp"

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
    assert(decoded_heartbeat.board == wp::BoardType::StickS3);

    heartbeat.board = wp::BoardType::StopWatch;
    assert(wp::encode_heartbeat(heartbeat, payload, sizeof(payload), payload_size));
    assert(wp::encode(header, payload, payload_size, wire.data(), wire.size(), wire_size));
    assert(wp::decode(wire.data(), wire_size, decoded) == wp::DecodeError::None);
    assert(wp::decode_heartbeat(decoded, decoded_heartbeat));
    assert(decoded_heartbeat.board == wp::BoardType::StopWatch);

    heartbeat.board = wp::BoardType::AiPassport;
    assert(wp::encode_heartbeat(heartbeat, payload, sizeof(payload), payload_size));
    assert(wp::encode(header, payload, payload_size, wire.data(), wire.size(), wire_size));
    assert(wp::decode(wire.data(), wire_size, decoded) == wp::DecodeError::None);
    assert(wp::decode_heartbeat(decoded, decoded_heartbeat));
    assert(decoded_heartbeat.board == wp::BoardType::AiPassport);

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

void test_ui_snapshot_policy() {
    UiSnapshot first{};
    UiSnapshot same = first;
    assert(ui_snapshots_equal(first, same));

    same.online_count = 1;
    assert(!ui_snapshots_equal(first, same));
    same = first;
    same.remaining_seconds = 29;
    assert(!ui_snapshots_equal(first, same));

    // A talking snapshot is only dirty when its integer-second value changes.
    first.talk_state = TalkState::Talking;
    first.remaining_seconds = 17;
    same = first;
    assert(ui_snapshots_equal(first, same));
    same.remaining_seconds = 16;
    assert(!ui_snapshots_equal(first, same));

    first = {};
    first.peer_count = 1;
    first.peers[0].occupied = true;
    first.peers[0].name = {'S', '3', '-', '0', '0', '0', '1', '\0', '\0'};
    same = first;
    assert(ui_snapshots_equal(first, same));
    same.peers[0].battery_percent = 80;
    // Peer rows are not visible on the main page.
    assert(ui_snapshots_equal(first, same));

    first.page = UiPage::Devices;
    same = first;
    same.peers[0].battery_percent = 80;
    assert(!ui_snapshots_equal(first, same));
    // RSSI is represented separately by weak_signal and is not printed in a row.
    same = first;
    same.peers[0].rssi = -80;
    assert(ui_snapshots_equal(first, same));

    // Changes in unused peer slots do not cause a redraw.
    same = first;
    same.peers[1].battery_percent = 99;
    assert(ui_snapshots_equal(first, same));
}

void test_ui_sleep_coalescing() {
    UiDeliveryPolicy delivery;
    UiSnapshot snapshot{};
    assert(delivery.publish(snapshot));

    snapshot.backlight_on = false;
    snapshot.online_count = 1;
    assert(delivery.publish(snapshot));
    snapshot.online_count = 2;
    assert(!delivery.publish(snapshot));
    snapshot.online_count = 3;
    snapshot.battery_percent = 42;
    assert(!delivery.publish(snapshot));

    snapshot.backlight_on = true;
    assert(delivery.publish(snapshot));
    assert(delivery.latest().online_count == 3);
    assert(delivery.latest().battery_percent == 42);

    DisplayPowerPolicy display;
    assert(display.set_awake(true) == DisplayTransition::None);
    assert(display.set_awake(false) == DisplayTransition::Sleep);
    assert(display.set_awake(false) == DisplayTransition::None);
    assert(display.set_awake(true) == DisplayTransition::Wake);
    assert(display.set_awake(true) == DisplayTransition::None);
}

void test_ladder_buttons() {
    LadderButtonState state{};
    ButtonEvents events = poll_ladder_buttons(state, kLadderKeyOk, 0);
    assert(!events.a_pressed);
    events = poll_ladder_buttons(state, kLadderKeyOk, 40);
    assert(!events.a_pressed);
    events = poll_ladder_buttons(state, kLadderKeyOk, 50);
    assert(events.a_pressed);
    assert(!events.a_released);

    events = poll_ladder_buttons(state, kLadderInvalidSample, 60);
    assert(!events.a_released);
    events = poll_ladder_buttons(state, kLadderInvalidSample, 130);
    assert(!events.a_released);
    events = poll_ladder_buttons(state, kLadderKeyOk, 140);
    assert(!events.a_pressed);
    assert(!events.a_released);

    events = poll_ladder_buttons(state, kLadderNoKey, 150);
    assert(!events.a_released);
    events = poll_ladder_buttons(state, kLadderKeyOk, 160);
    assert(!events.a_released);

    events = poll_ladder_buttons(state, kLadderNoKey, 200);
    assert(!events.a_released);
    events = poll_ladder_buttons(state, kLadderNoKey, 250);
    assert(events.a_released);

    LadderButtonState click{};
    poll_ladder_buttons(click, kLadderKeyUp, 0);
    poll_ladder_buttons(click, kLadderKeyUp, 50);
    poll_ladder_buttons(click, kLadderNoKey, 100);
    events = poll_ladder_buttons(click, kLadderNoKey, 150);
    assert(events.b_clicked);
    assert(!events.b_held);

    LadderButtonState hold{};
    poll_ladder_buttons(hold, kLadderKeyDown, 0);
    poll_ladder_buttons(hold, kLadderKeyDown, 50);
    events = poll_ladder_buttons(hold, kLadderKeyDown, 449);
    assert(!events.b_held);
    events = poll_ladder_buttons(hold, kLadderKeyDown, 450);
    assert(events.b_held);
    events = poll_ladder_buttons(hold, kLadderKeyDown, 500);
    assert(!events.b_held);
    poll_ladder_buttons(hold, kLadderNoKey, 510);
    events = poll_ladder_buttons(hold, kLadderNoKey, 560);
    assert(!events.b_clicked);
}

void test_battery_sample_policy() {
    BatterySamplePolicy battery;
    assert(battery.due(100, true, TalkState::Idle));
    battery.sampled(100);
    assert(!battery.due(10099, true, TalkState::Idle));
    assert(battery.due(10100, true, TalkState::Idle));
    assert(!battery.due(60100, false, TalkState::Talking));
    assert(battery.due(60100, false, TalkState::Idle));

    BatterySamplePolicy switched;
    switched.sampled(1000);
    assert(!switched.due(12000, false, TalkState::Idle));
    assert(switched.due(12000, true, TalkState::Idle));

    BatterySamplePolicy wrapped;
    wrapped.sampled(0xFFFFFF00U);
    assert(!wrapped.due(0x00000100U, true, TalkState::Idle));
    assert(wrapped.due(0x00003000U, true, TalkState::Idle));
    assert(deadline_reached(0x00000010U, 0xFFFFFFF0U));
    assert(!deadline_reached(0xFFFFFFF0U, 0x00000010U));
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
    assert(classify_channel_button(true, false, false) == ChannelButtonAction::OpenMenu);
    assert(classify_channel_button(true, true, false) == ChannelButtonAction::OpenMenu);
    assert(classify_channel_button(false, true, true) == ChannelButtonAction::CycleChannel);
    assert(classify_channel_button(false, true, false) == ChannelButtonAction::None);
    assert(classify_channel_button(false, false, true) == ChannelButtonAction::None);

    NavigationController navigation;
    assert(!navigation.active());
    navigation.open(100, 50, 0);
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

    navigation.open(20000, 255, 0);
    assert(navigation.volume_percent() == 75);
    navigation.long_b(20001);
    assert(navigation.page() == UiPage::Main);

    navigation.open(30000, 50, 2);
    navigation.short_b(30010, 0);
    navigation.short_b(30020, 0);
    assert(navigation.menu_index() == 2);
    navigation.short_a(30030);
    assert(navigation.page() == UiPage::Vox);
    assert(navigation.vox_level() == 2);
    assert(navigation.vox_enabled());
    navigation.short_b(30040, 0);
    assert(navigation.vox_level() == 3);
    navigation.short_b(30041, 0);
    assert(navigation.vox_level() == 0);
    assert(!navigation.vox_enabled());
    navigation.short_b(30042, 0);
    assert(navigation.vox_level() == 1);
    assert(navigation.short_a(30050) == NavigationAction::SaveVox);
    assert(navigation.page() == UiPage::Menu);

    navigation.open(31000, 50, 9);
    navigation.short_b(31010, 0);
    navigation.short_b(31020, 0);
    navigation.short_a(31030);
    assert(navigation.vox_level() == 0);
}

void test_menu_interrupts_talk() {
    TalkController talk(id(1));
    talk.ptt_pressed(1000, 1, 11);
    talk.tick(1015);
    assert((talk.tick(1100) & StartCapture) != 0);
    assert(talk.snapshot().state == TalkState::Talking);
    assert(classify_channel_button(true, false, talk.snapshot().state == TalkState::Idle) ==
           ChannelButtonAction::OpenMenu);

    const Actions ended = talk.ptt_released(1500);
    assert((ended & SendEnd) != 0);
    assert((ended & StopCapture) != 0);
    assert(talk.snapshot().state == TalkState::Idle);

    NavigationController navigation;
    navigation.open(1500, 50, 2);
    assert(navigation.page() == UiPage::Menu);
    assert(navigation.vox_enabled());
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

void test_vox() {
    assert(normalize_vox_level(0) == VoxLevel::Off);
    assert(normalize_vox_level(3) == VoxLevel::High);
    assert(normalize_vox_level(9) == VoxLevel::Off);
    assert(!vox_enabled(VoxLevel::Off));
    assert(vox_enabled(VoxLevel::Med));
    assert(vox_profile(VoxLevel::High).vad_mode == audio::VoiceActivityDetector::kMode);
    assert(vox_profile(VoxLevel::High).min_mean_abs == audio::VoiceActivityDetector::kMinMeanAbs);
    assert(vox_profile(VoxLevel::High).attack_frames == audio::VoxGate::kAttackFrames);
    assert(vox_profile(VoxLevel::Low).min_mean_abs > vox_profile(VoxLevel::Med).min_mean_abs);
    assert(vox_profile(VoxLevel::Med).min_mean_abs > vox_profile(VoxLevel::High).min_mean_abs);
    assert(vox_profile(VoxLevel::Low).noise_margin > vox_profile(VoxLevel::Med).noise_margin);
    assert(vox_profile(VoxLevel::High).noise_margin == 0);
    assert(vox_profile(VoxLevel::Low).attack_frames > vox_profile(VoxLevel::High).attack_frames);
    assert(vox_profile(VoxLevel::Low).learn_frames > 0);
    assert(vox_active(2));
    assert(!vox_active(0));
    assert(!vox_active(9));
    assert(kVoxTimeoutCooldownMs >= 1000);
    assert(vox_cooling_down(0, kVoxTimeoutCooldownMs));
    assert(!vox_cooling_down(kVoxTimeoutCooldownMs, kVoxTimeoutCooldownMs));
    assert(!vox_cooling_down(kVoxTimeoutCooldownMs + 1, kVoxTimeoutCooldownMs));

    audio::VoxGate gate;
    assert(gate.observe(true, false) == audio::VoxDecision::None);
    assert(gate.observe(true, false) == audio::VoxDecision::None);
    assert(gate.observe(true, false) == audio::VoxDecision::Press);
    assert(gate.latched());
    for (int i = 0; i < 40; ++i) {
        assert(gate.observe(false, false) == audio::VoxDecision::None);
    }
    assert(gate.latched());
    for (uint8_t i = 1; i < audio::VoxGate::kHangFrames; ++i) {
        assert(gate.observe(false, true) == audio::VoxDecision::None);
    }
    assert(gate.observe(false, true) == audio::VoxDecision::Release);
    assert(!gate.latched());

    audio::VoxGate slow;
    slow.set_attack_frames(vox_profile(VoxLevel::Low).attack_frames);
    for (uint8_t i = 1; i < vox_profile(VoxLevel::Low).attack_frames; ++i) {
        assert(slow.observe(true, false) == audio::VoxDecision::None);
    }
    assert(slow.observe(true, false) == audio::VoxDecision::Press);

    audio::PcmPreroll preroll;
    int16_t first[wp::kAudioSamples]{};
    int16_t second[wp::kAudioSamples]{};
    first[0] = 11;
    second[0] = 22;
    preroll.push(first);
    preroll.push(second);
    assert(preroll.size() == 2);
    assert(preroll.frame(0) != nullptr && preroll.frame(0)[0] == 11);
    assert(preroll.frame(1) != nullptr && preroll.frame(1)[0] == 22);
    for (size_t i = 0; i < audio::PcmPreroll::kFrames + 3; ++i) {
        preroll.push(second);
    }
    assert(preroll.size() == audio::PcmPreroll::kFrames);
    assert(preroll.frame(audio::PcmPreroll::kFrames - 1)[0] == 22);
    preroll.clear();
    assert(preroll.size() == 0);
    assert(preroll.frame(0) == nullptr);

    audio::VoiceActivityDetector vad;
    assert(vad.start());
    int16_t silence[wp::kAudioSamples]{};
    assert(!vad.is_speech(silence, wp::kAudioSamples));
    assert(vad.apply_profile(VoxLevel::Low));
    int16_t quiet[wp::kAudioSamples];
    for (size_t i = 0; i < wp::kAudioSamples; ++i) quiet[i] = 200;
    assert(!vad.is_speech(quiet, wp::kAudioSamples));

    int16_t office[wp::kAudioSamples];
    for (size_t i = 0; i < wp::kAudioSamples; ++i) office[i] = 2500;
    const uint8_t settle = static_cast<uint8_t>(vox_profile(VoxLevel::Low).learn_frames + 8);
    for (uint8_t i = 0; i < settle; ++i) {
        assert(!vad.is_speech(office, wp::kAudioSamples, true));
    }
    assert(vad.energy_threshold() >= vox_profile(VoxLevel::Low).min_mean_abs);

    int16_t loud_office[wp::kAudioSamples];
    for (size_t i = 0; i < wp::kAudioSamples; ++i) loud_office[i] = 9000;
    assert(vad.apply_profile(VoxLevel::Low));
    for (uint8_t i = 0; i < settle; ++i) {
        assert(!vad.is_speech(loud_office, wp::kAudioSamples, true));
    }
    assert(vad.energy_threshold() > 9000);
    const int32_t after_learn = vad.energy_threshold();
    for (int i = 0; i < 20; ++i) {
        assert(!vad.is_speech(loud_office, wp::kAudioSamples, false));
    }
    assert(vad.energy_threshold() == after_learn);
    assert(!vad.is_speech(loud_office, wp::kAudioSamples, true));

    assert(vad.apply_profile(VoxLevel::Low));
    for (uint8_t i = 0; i < settle; ++i) {
        assert(!vad.is_speech(silence, wp::kAudioSamples, true));
    }
    const int32_t quiet_floor = vad.energy_threshold();
    int16_t tone[wp::kAudioSamples];
    for (size_t i = 0; i < wp::kAudioSamples; ++i) {
        tone[i] = static_cast<int16_t>(
            22000.0 * std::sin(2.0 * 3.141592653589793 * 1000.0 *
                               static_cast<double>(i) / 16000.0));
    }
    bool heard = false;
    for (int i = 0; i < 12; ++i) {
        if (vad.is_speech(tone, wp::kAudioSamples, true)) heard = true;
    }
    if (heard) assert(vad.energy_threshold() == quiet_floor);
    vad.stop();
}

}  // namespace

int main() {
    test_protocol_round_trip();
    test_adpcm();
    test_presence();
    test_display_policy();
    test_ui_snapshot_policy();
    test_ui_sleep_coalescing();
    test_ladder_buttons();
    test_battery_sample_policy();
    test_arbitration_and_timeout();
    test_start_conflict_resolution();
    test_navigation();
    test_menu_interrupts_talk();
    test_jitter_buffer();
    test_eight_device_simulation();
    test_vox();
    std::cout << "walkie host tests: PASS\n";
    return 0;
}
