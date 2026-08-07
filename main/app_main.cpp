#include "walkie/adpcm.hpp"
#include "walkie/audio_jitter.hpp"
#include "walkie/bsp.hpp"
#include "walkie/display_policy.hpp"
#include "walkie/espnow_transport.hpp"
#include "walkie/navigation.hpp"
#include "walkie/presence.hpp"
#include "walkie/protocol.hpp"
#include "walkie/settings.hpp"
#include "walkie/talk_controller.hpp"
#include "walkie/ui.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

using namespace walkie;
namespace wp = walkie::protocol;

constexpr char kTag[] = "walkie";
constexpr uint8_t kRadioChannel = CONFIG_WALKIE_RADIO_CHANNEL;
constexpr EventBits_t kCaptureBit = 1U << 0;
constexpr EventBits_t kPlaybackBit = 1U << 1;
constexpr uint32_t kAudioTaskStackBytes = 6144;
constexpr uint32_t kNotificationTaskStackBytes = 4096;
constexpr uint32_t kBspRetryMagic = 0x42535052U;  // "BSPR"
constexpr uint32_t kMaximumBspRetries = 2;

RTC_NOINIT_ATTR uint32_t g_bsp_retry_magic;
RTC_NOINIT_ATTR uint32_t g_bsp_retry_count;

StickS3Bsp g_bsp;
EspNowTransport g_transport;
EventGroupHandle_t g_audio_events = nullptr;
QueueHandle_t g_playback_queue = nullptr;
QueueHandle_t g_notification_queue = nullptr;
SemaphoreHandle_t g_audio_owner = nullptr;
TaskHandle_t g_capture_task_handle = nullptr;
TaskHandle_t g_playback_task_handle = nullptr;
TaskHandle_t g_notification_task_handle = nullptr;
std::atomic<uint8_t> g_logical_channel{1};
std::atomic<uint8_t> g_volume_percent{50};
std::atomic<uint32_t> g_tx_session{0};
std::atomic<uint16_t> g_tx_sequence{0};

struct ToneRequest {
    uint16_t frequency_hz{0};
    uint16_t duration_ms{0};
    uint32_t expires_at_ms{0};
};

// These buffers are long-lived and several kilobytes each. Keeping them in internal
// BSS avoids consuming the FreeRTOS task stacks (and keeps DMA-facing PCM out of PSRAM).
struct CaptureTaskBuffers {
    std::array<std::array<int16_t, wp::kAudioSamples>, 3> pcm{};
    std::array<uint8_t, wp::kAudioPayloadSize> payload{};
};

struct PlaybackTaskBuffers {
    std::array<std::array<int16_t, wp::kAudioSamples>, 3> pcm{};
    std::array<int16_t, wp::kAudioSamples> silence{};
    audio::AudioJitterBuffer jitter{};
    audio::EncodedAudioFrame incoming{};
    audio::EncodedAudioFrame frame{};
};

CaptureTaskBuffers g_capture_buffers;
PlaybackTaskBuffers g_playback_buffers;

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

wp::Header make_header(wp::MessageType type, uint32_t session_id, uint16_t sequence) {
    wp::Header header{};
    header.type = type;
    header.logical_channel = g_logical_channel.load();
    header.sender_id = g_transport.device_id();
    header.session_id = session_id;
    header.sequence = sequence;
    header.timestamp_ms = now_ms();
    return header;
}

bool send_packet(const wp::Header& header, const uint8_t* payload, size_t payload_size) {
    std::array<uint8_t, wp::kMaxWireSize> wire{};
    size_t wire_size = 0;
    if (!wp::encode(header, payload, payload_size, wire.data(), wire.size(), wire_size)) return false;
    return g_transport.send(wire.data(), wire_size) == ESP_OK;
}

void send_control_repeated(wp::MessageType type, uint32_t session_id,
                           const uint8_t* payload = nullptr, size_t payload_size = 0) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        send_packet(make_header(type, session_id, static_cast<uint16_t>(attempt)), payload, payload_size);
        if (attempt != 2) vTaskDelay(pdMS_TO_TICKS(8));
    }
}

void capture_task(void*) {
    audio::AdpcmState encoder_state{};
    size_t record_index = 0;
    size_t queued = 0;
    for (;;) {
        xEventGroupWaitBits(g_audio_events, kCaptureBit, pdFALSE, pdTRUE, portMAX_DELAY);
        xSemaphoreTake(g_audio_owner, portMAX_DELAY);
        encoder_state = {};
        record_index = 0;
        queued = 0;
        if (!g_bsp.start_capture()) {
            ESP_LOGE(kTag, "Microphone start failed");
            xEventGroupClearBits(g_audio_events, kCaptureBit);
        }
        while ((xEventGroupGetBits(g_audio_events) & kCaptureBit) != 0) {
            if (!g_bsp.queue_capture(g_capture_buffers.pcm[record_index].data(),
                                     g_capture_buffers.pcm[record_index].size())) {
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            ++queued;
            if (queued >= 3) {
                const size_t ready = (record_index + 1) % g_capture_buffers.pcm.size();
                auto& payload = g_capture_buffers.payload;
                const audio::AdpcmState initial = encoder_state;
                payload[0] = static_cast<uint8_t>(static_cast<uint16_t>(initial.predictor) >> 8);
                payload[1] = static_cast<uint8_t>(initial.predictor);
                payload[2] = initial.step_index;
                payload[3] = 0;
                if (audio::encode_ima_adpcm(g_capture_buffers.pcm[ready].data(),
                                            g_capture_buffers.pcm[ready].size(),
                                            payload.data() + 4, wp::kAdpcmBytes, encoder_state)) {
                    const uint16_t sequence = g_tx_sequence.fetch_add(1);
                    send_packet(make_header(wp::MessageType::Audio, g_tx_session.load(), sequence),
                                payload.data(), payload.size());
                }
            }
            record_index = (record_index + 1) % g_capture_buffers.pcm.size();
        }
        g_bsp.stop_capture();
        xSemaphoreGive(g_audio_owner);
    }
}

void playback_task(void*) {
    size_t buffer_index = 0;
    auto& buffers = g_playback_buffers;
    for (;;) {
        xEventGroupWaitBits(g_audio_events, kPlaybackBit, pdFALSE, pdTRUE, portMAX_DELAY);
        xSemaphoreTake(g_audio_owner, portMAX_DELAY);
        buffers.jitter.reset();
        if (!g_bsp.start_playback(g_volume_percent.load())) {
            ESP_LOGE(kTag, "Speaker start failed");
            xEventGroupClearBits(g_audio_events, kPlaybackBit);
        }
        for (;;) {
            if ((xEventGroupGetBits(g_audio_events) & kPlaybackBit) == 0) break;
            const TickType_t wait = buffers.jitter.started() ? 0 : pdMS_TO_TICKS(30);
            if (xQueueReceive(g_playback_queue, &buffers.incoming, wait) == pdTRUE) {
                buffers.jitter.push(buffers.incoming);
                while (xQueueReceive(g_playback_queue, &buffers.incoming, 0) == pdTRUE) {
                    buffers.jitter.push(buffers.incoming);
                }
            }
            const audio::JitterPopResult result = buffers.jitter.pop(buffers.frame);
            if (result == audio::JitterPopResult::Wait) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }
            if (result == audio::JitterPopResult::Missing) {
                g_bsp.play(buffers.silence.data(), buffers.silence.size());
            } else {
                audio::AdpcmState state = buffers.frame.initial_state;
                if (audio::decode_ima_adpcm(buffers.frame.data.data(), buffers.frame.data.size(),
                                            buffers.pcm[buffer_index].data(),
                                            buffers.pcm[buffer_index].size(), state)) {
                    while (!g_bsp.play(buffers.pcm[buffer_index].data(),
                                       buffers.pcm[buffer_index].size()) &&
                           (xEventGroupGetBits(g_audio_events) & kPlaybackBit) != 0) {
                        vTaskDelay(pdMS_TO_TICKS(2));
                    }
                    buffer_index = (buffer_index + 1) % buffers.pcm.size();
                }
            }
        }
        g_bsp.stop_playback();
        xQueueReset(g_playback_queue);
        xSemaphoreGive(g_audio_owner);
    }
}

void notification_task(void*) {
    ToneRequest request{};
    for (;;) {
        if (xQueueReceive(g_notification_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        if (g_volume_percent.load() == 0 ||
            static_cast<int32_t>(now_ms() - request.expires_at_ms) > 0) continue;
        xSemaphoreTake(g_audio_owner, portMAX_DELAY);
        if (g_volume_percent.load() == 0 ||
            static_cast<int32_t>(now_ms() - request.expires_at_ms) > 0 ||
            (xEventGroupGetBits(g_audio_events) & (kCaptureBit | kPlaybackBit)) != 0) {
            xSemaphoreGive(g_audio_owner);
            continue;
        }
        if (g_bsp.start_playback(g_volume_percent.load())) {
            g_bsp.tone(request.frequency_hz, request.duration_ms);
            vTaskDelay(pdMS_TO_TICKS(request.duration_ms));
            g_bsp.stop_playback();
        }
        xSemaphoreGive(g_audio_owner);
    }
}

void apply_actions(Actions actions, TalkController& controller) {
    const uint32_t session = controller.action_session_id();
    if ((actions & SendClaim) != 0) {
        uint8_t payload[4]{};
        size_t size = 0;
        const wp::Claim claim{controller.snapshot().arbitration_value};
        if (wp::encode_claim(claim, payload, sizeof(payload), size)) {
            send_control_repeated(wp::MessageType::TalkClaim, session, payload, size);
        }
    }
    if ((actions & SendStart) != 0) {
        g_tx_session.store(session);
        g_tx_sequence.store(0);
        send_control_repeated(wp::MessageType::TalkStart, session);
    }
    if ((actions & StartCapture) != 0) {
        xEventGroupClearBits(g_audio_events, kPlaybackBit);
        xEventGroupSetBits(g_audio_events, kCaptureBit);
    }
    if ((actions & StopCapture) != 0) xEventGroupClearBits(g_audio_events, kCaptureBit);
    if ((actions & StartPlayback) != 0) {
        xEventGroupClearBits(g_audio_events, kCaptureBit);
        xEventGroupSetBits(g_audio_events, kPlaybackBit);
    }
    if ((actions & StopPlayback) != 0) xEventGroupClearBits(g_audio_events, kPlaybackBit);
    if ((actions & SendEnd) != 0) send_control_repeated(wp::MessageType::TalkEnd, session);
    if ((actions & TalkTimedOut) != 0) ESP_LOGW(kTag, "Maximum talk time reached");
    if ((actions & PlayRequestCue) != 0) {
        const ToneRequest request{880, 55, now_ms() + 150};
        xQueueSend(g_notification_queue, &request, 0);
    }
    if ((actions & PlayTimeoutCue) != 0) {
        const ToneRequest request{440, 180, now_ms() + 1000};
        xQueueSend(g_notification_queue, &request, 0);
    }
}

void format_device_name(const wp::DeviceId& id, std::array<char, wp::kDeviceNameSize>& name) {
    std::snprintf(name.data(), name.size(), "S3-%02X%02X", id[4], id[5]);
}

protocol::DeviceState wire_state(TalkState state) {
    return static_cast<protocol::DeviceState>(static_cast<uint8_t>(state));
}

}  // namespace

extern "C" void app_main() {
    SettingsStore settings_store;
    settings_store.initialize();
    Settings settings = settings_store.load();
    g_logical_channel.store(settings.logical_channel);
    g_volume_percent.store(settings.volume_percent);

    if (!g_bsp.initialize()) {
        if (g_bsp_retry_magic != kBspRetryMagic) {
            g_bsp_retry_magic = kBspRetryMagic;
            g_bsp_retry_count = 0;
        }
        if (g_bsp_retry_count < kMaximumBspRetries) {
            ++g_bsp_retry_count;
            ESP_LOGW(kTag, "StickS3 BSP initialization failed; retrying boot (%u/%u)",
                     static_cast<unsigned>(g_bsp_retry_count),
                     static_cast<unsigned>(kMaximumBspRetries));
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
        ESP_LOGE(kTag, "StickS3 BSP initialization failed after retries");
        return;
    }
    g_bsp_retry_magic = kBspRetryMagic;
    g_bsp_retry_count = 0;
    if (g_transport.initialize(kRadioChannel) != ESP_OK) {
        ESP_LOGE(kTag, "ESP-NOW initialization failed");
        return;
    }

    g_audio_events = xEventGroupCreate();
    g_playback_queue = xQueueCreate(8, sizeof(audio::EncodedAudioFrame));
    g_notification_queue = xQueueCreate(2, sizeof(ToneRequest));
    g_audio_owner = xSemaphoreCreateMutex();
    if (g_audio_events == nullptr || g_playback_queue == nullptr ||
        g_notification_queue == nullptr || g_audio_owner == nullptr) {
        ESP_LOGE(kTag, "Audio task resources unavailable");
        return;
    }
    const BaseType_t capture_created = xTaskCreatePinnedToCore(
        capture_task, "audio_capture", kAudioTaskStackBytes, nullptr, 5,
        &g_capture_task_handle, 0);
    const BaseType_t playback_created = xTaskCreatePinnedToCore(
        playback_task, "audio_playback", kAudioTaskStackBytes, nullptr, 5,
        &g_playback_task_handle, 0);
    const BaseType_t notification_created = xTaskCreatePinnedToCore(
        notification_task, "audio_notify", kNotificationTaskStackBytes, nullptr, 4,
        &g_notification_task_handle, 0);
    if (capture_created != pdPASS || playback_created != pdPASS ||
        notification_created != pdPASS) {
        ESP_LOGE(kTag, "Audio task creation failed");
        return;
    }

    Ui ui(g_bsp);
    if (!ui.start()) {
        ESP_LOGE(kTag, "UI task start failed");
        return;
    }

    const wp::DeviceId local_id = g_transport.device_id();
    TalkController controller(local_id);
    PresenceManager presence;
    NavigationController navigation;
    std::array<char, wp::kDeviceNameSize> local_name{};
    format_device_name(local_id, local_name);
    uint32_t last_input_ms = now_ms();
    uint32_t next_heartbeat_ms = now_ms();
    uint32_t next_ui_ms = 0;
    uint32_t weak_signal_until_ms = 0;
    uint32_t observed_dropped_rx_frames = 0;
    uint32_t next_stack_report_ms = now_ms() + 5000;
    bool backlight_on = true;

    ESP_LOGI(kTag, "Ready as %s on logical CH%u / radio CH%u",
             local_name.data(), settings.logical_channel, kRadioChannel);

    for (;;) {
        const uint32_t now = now_ms();
        const ButtonEvents buttons = g_bsp.poll_buttons();
        if (buttons.a_pressed || buttons.a_released || buttons.b_clicked || buttons.b_held) {
            const bool was_off = !backlight_on;
            last_input_ms = now;
            if (was_off) {
                backlight_on = true;
                g_bsp.set_backlight(true);
            }
            if (!was_off) {
                if (navigation.active()) {
                    if (buttons.a_released) {
                        if (navigation.short_a(now) == NavigationAction::SaveVolume) {
                            settings.volume_percent = navigation.volume_percent();
                            g_volume_percent.store(settings.volume_percent);
                            settings_store.save(settings);
                        }
                    }
                    if (buttons.b_held) {
                        navigation.long_b(now);
                    } else if (buttons.b_clicked) {
                        navigation.short_b(now, presence.count());
                    }
                } else {
                    if (buttons.a_pressed) {
                        apply_actions(controller.ptt_pressed(now, esp_random(), esp_random()), controller);
                    }
                    if (buttons.a_released) apply_actions(controller.ptt_released(now), controller);
                    if (buttons.b_held && controller.snapshot().state == TalkState::Idle) {
                        navigation.open(now, settings.volume_percent);
                    } else if (buttons.b_clicked && controller.snapshot().state == TalkState::Idle) {
                        settings.logical_channel = static_cast<uint8_t>((settings.logical_channel % 4) + 1);
                        g_logical_channel.store(settings.logical_channel);
                        settings_store.save(settings);
                        presence = {};
                    }
                }
            } else if (buttons.a_pressed) {
                apply_actions(controller.ptt_pressed(now, esp_random(), esp_random()), controller);
            }
        }
        navigation.tick(now);

        ReceivedFrame received{};
        while (g_transport.receive(received)) {
            wp::PacketView packet{};
            if (wp::decode(received.data.data(), received.size, packet) != wp::DecodeError::None ||
                packet.header.logical_channel != settings.logical_channel ||
                packet.header.sender_id == local_id) continue;
            if (packet.header.type == wp::MessageType::Heartbeat) {
                wp::Heartbeat heartbeat{};
                if (wp::decode_heartbeat(packet, heartbeat)) {
                    presence.observe(packet.header.sender_id, heartbeat, now, received.rssi);
                }
            } else {
                wp::DeviceState activity_state = wp::DeviceState::Idle;
                if (packet.header.type == wp::MessageType::TalkClaim) {
                    activity_state = wp::DeviceState::Requesting;
                } else if (packet.header.type == wp::MessageType::TalkStart ||
                           packet.header.type == wp::MessageType::Audio) {
                    activity_state = wp::DeviceState::Talking;
                }
                presence.observe_activity(packet.header.sender_id, activity_state, now, received.rssi);
            }

            Actions actions = NoAction;
            switch (packet.header.type) {
                case wp::MessageType::TalkClaim: {
                    wp::Claim claim{};
                    if (wp::decode_claim(packet, claim)) actions = controller.receive_claim(packet.header, claim, now);
                    break;
                }
                case wp::MessageType::TalkStart:
                    actions = controller.receive_start(packet.header, now);
                    break;
                case wp::MessageType::Audio:
                    actions = controller.receive_audio(packet.header, now);
                    if (packet.header.payload_length == wp::kAudioPayloadSize &&
                        (controller.snapshot().state == TalkState::Receiving ||
                         controller.snapshot().state == TalkState::Busy) &&
                        packet.header.session_id == controller.snapshot().session_id &&
                        packet.header.sender_id == controller.snapshot().speaker_id) {
                        audio::EncodedAudioFrame audio_frame{};
                        audio_frame.session_id = packet.header.session_id;
                        audio_frame.sequence = packet.header.sequence;
                        audio_frame.initial_state.predictor = static_cast<int16_t>(
                            (static_cast<uint16_t>(packet.payload[0]) << 8) | packet.payload[1]);
                        audio_frame.initial_state.step_index = packet.payload[2];
                        if (audio_frame.initial_state.step_index <= 88 && packet.payload[3] == 0) {
                            std::memcpy(audio_frame.data.data(), packet.payload + 4, audio_frame.data.size());
                            if (xQueueSend(g_playback_queue, &audio_frame, 0) != pdTRUE) {
                                weak_signal_until_ms = now + 1500;
                            }
                        }
                    }
                    break;
                case wp::MessageType::TalkEnd:
                    actions = controller.receive_end(packet.header, now);
                    break;
                case wp::MessageType::Heartbeat:
                    break;
            }
            apply_actions(actions, controller);
        }

        apply_actions(controller.tick(now), controller);
        if (talk_activity_keeps_screen_awake(controller.snapshot().state)) {
            last_input_ms = now;
            if (!backlight_on) {
                backlight_on = true;
                g_bsp.set_backlight(true);
                next_ui_ms = now;
            }
        } else if (backlight_on && static_cast<uint32_t>(now - last_input_ms) >= 30000) {
            backlight_on = false;
            g_bsp.set_backlight(false);
        }
        presence.expire(now);
        const uint32_t dropped_rx_frames = g_transport.dropped_rx_frames();
        if (dropped_rx_frames != observed_dropped_rx_frames) {
            observed_dropped_rx_frames = dropped_rx_frames;
            weak_signal_until_ms = now + 1500;
        }
        if (static_cast<int32_t>(now - next_stack_report_ms) >= 0) {
            ESP_LOGI(kTag, "Audio stack minimum free: capture=%u playback=%u notify=%u bytes",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(g_capture_task_handle)),
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(g_playback_task_handle)),
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(g_notification_task_handle)));
            next_stack_report_ms = now + 30000;
        }

        if (static_cast<int32_t>(now - next_heartbeat_ms) >= 0) {
            wp::Heartbeat heartbeat{};
            heartbeat.name = local_name;
            heartbeat.battery_percent = static_cast<uint8_t>(g_bsp.battery_percent());
            heartbeat.state = wire_state(controller.snapshot().state);
            heartbeat.board = wp::BoardType::StickS3;
            uint8_t payload[16]{};
            size_t payload_size = 0;
            if (wp::encode_heartbeat(heartbeat, payload, sizeof(payload), payload_size)) {
                send_packet(make_header(wp::MessageType::Heartbeat, 0, 0), payload, payload_size);
            }
            next_heartbeat_ms = now + 1800 + (esp_random() % 401);
        }

        if (static_cast<int32_t>(now - next_ui_ms) >= 0) {
            UiSnapshot snapshot{};
            snapshot.page = navigation.page();
            snapshot.logical_channel = settings.logical_channel;
            snapshot.battery_percent = static_cast<uint8_t>(g_bsp.battery_percent());
            snapshot.online_count = static_cast<uint8_t>(presence.count());
            snapshot.talk_state = controller.snapshot().state;
            snapshot.backlight_on = backlight_on;
            snapshot.menu_index = navigation.menu_index();
            snapshot.volume_index = navigation.volume_index();
            snapshot.volume_percent = settings.volume_percent;
            snapshot.device_offset = static_cast<uint8_t>(navigation.device_offset());
            if (snapshot.talk_state == TalkState::Talking) {
                const uint32_t elapsed_ms = now - controller.snapshot().talk_started_ms;
                snapshot.remaining_seconds = static_cast<uint8_t>(30 - std::min<uint32_t>(30, elapsed_ms / 1000));
            }
            if (const Peer* peer = presence.find(controller.snapshot().speaker_id); peer != nullptr) {
                std::memcpy(snapshot.speaker_name.data(), peer->heartbeat.name.data(), wp::kDeviceNameSize);
                snapshot.weak_signal = peer->rssi <= -78;
            }
            if (static_cast<int32_t>(weak_signal_until_ms - now) > 0) {
                snapshot.weak_signal = true;
            }
            for (const Peer& peer : presence.peers()) {
                if (!peer.occupied || snapshot.peer_count >= snapshot.peers.size()) continue;
                UiPeer& ui_peer = snapshot.peers[snapshot.peer_count++];
                std::memcpy(ui_peer.name.data(), peer.heartbeat.name.data(), wp::kDeviceNameSize);
                ui_peer.battery_percent = peer.heartbeat.battery_percent;
                ui_peer.state = peer.heartbeat.state;
                ui_peer.rssi = peer.rssi;
                ui_peer.occupied = true;
            }
            ui.publish(snapshot);
            next_ui_ms = now + 100;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
