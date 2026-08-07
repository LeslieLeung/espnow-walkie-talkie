#pragma once

#include "walkie/bsp.hpp"
#include "walkie/navigation.hpp"
#include "walkie/presence.hpp"
#include "walkie/talk_controller.hpp"

#include <array>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace walkie {

struct UiPeer {
    std::array<char, 9> name{};
    uint8_t battery_percent{0};
    protocol::DeviceState state{protocol::DeviceState::Idle};
    int8_t rssi{0};
    bool occupied{false};
};

struct UiSnapshot {
    UiPage page{UiPage::Main};
    uint8_t logical_channel{1};
    uint8_t battery_percent{0};
    uint8_t online_count{0};
    TalkState talk_state{TalkState::Idle};
    uint8_t remaining_seconds{30};
    std::array<char, 9> speaker_name{};
    bool backlight_on{true};
    bool weak_signal{false};
    uint8_t menu_index{0};
    uint8_t volume_index{2};
    uint8_t volume_percent{50};
    uint8_t peer_count{0};
    uint8_t device_offset{0};
    std::array<UiPeer, PresenceManager::kMaxPeers> peers{};
};

class Ui {
public:
    explicit Ui(BoardBsp& bsp) : bsp_(bsp) {}
    bool start();
    bool publish(const UiSnapshot& snapshot);

private:
    static void task_entry(void* context);
    void run();

    BoardBsp& bsp_;
    QueueHandle_t queue_{nullptr};
};

}  // namespace walkie
