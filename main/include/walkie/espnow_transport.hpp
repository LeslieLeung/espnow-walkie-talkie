#pragma once

#include "walkie/protocol.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace walkie {

struct ReceivedFrame {
    std::array<uint8_t, protocol::kMaxWireSize> data{};
    uint16_t size{0};
    int8_t rssi{0};
};

class EspNowTransport {
public:
    EspNowTransport();
    ~EspNowTransport();

    esp_err_t initialize(uint8_t radio_channel);
    bool receive(ReceivedFrame& frame, TickType_t wait_ticks = 0);
    esp_err_t send(const uint8_t* data, size_t size, TickType_t wait_ticks = pdMS_TO_TICKS(25));
    const protocol::DeviceId& device_id() const { return device_id_; }
    uint32_t dropped_rx_frames() const { return dropped_rx_frames_.load(); }

private:
    static void receive_callback(const esp_now_recv_info_t* info, const uint8_t* data, int size);
    static void send_callback(const esp_now_send_info_t* tx_info, esp_now_send_status_t status);

    QueueHandle_t receive_queue_{nullptr};
    SemaphoreHandle_t send_ready_{nullptr};
    SemaphoreHandle_t send_mutex_{nullptr};
    protocol::DeviceId device_id_{};
    std::atomic<uint32_t> dropped_rx_frames_{0};
    bool initialized_{false};
    static EspNowTransport* instance_;
};

}  // namespace walkie
