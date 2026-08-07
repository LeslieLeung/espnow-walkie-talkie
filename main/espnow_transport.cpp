#include "walkie/espnow_transport.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"

namespace walkie {

EspNowTransport* EspNowTransport::instance_ = nullptr;

EspNowTransport::EspNowTransport() {
    receive_queue_ = xQueueCreate(12, sizeof(ReceivedFrame));
    send_ready_ = xSemaphoreCreateBinary();
    send_mutex_ = xSemaphoreCreateMutex();
    if (send_ready_ != nullptr) xSemaphoreGive(send_ready_);
}

EspNowTransport::~EspNowTransport() {
    if (initialized_) esp_now_deinit();
    if (receive_queue_ != nullptr) vQueueDelete(receive_queue_);
    if (send_ready_ != nullptr) vSemaphoreDelete(send_ready_);
    if (send_mutex_ != nullptr) vSemaphoreDelete(send_mutex_);
    if (instance_ == this) instance_ = nullptr;
}

esp_err_t EspNowTransport::initialize(uint8_t radio_channel) {
    if (receive_queue_ == nullptr || send_ready_ == nullptr || send_mutex_ == nullptr ||
        radio_channel < 1 || radio_channel > 13) return ESP_ERR_INVALID_STATE;
    instance_ = this;
    esp_err_t error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    if ((error = esp_wifi_init(&wifi_config)) != ESP_OK) return error;
    if ((error = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return error;
    if ((error = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return error;
    if ((error = esp_wifi_start()) != ESP_OK) return error;
    if ((error = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK) return error;
    if ((error = esp_wifi_set_channel(radio_channel, WIFI_SECOND_CHAN_NONE)) != ESP_OK) return error;
    if ((error = esp_read_mac(device_id_.data(), ESP_MAC_WIFI_STA)) != ESP_OK) return error;
    if ((error = esp_now_init()) != ESP_OK) return error;
    if ((error = esp_now_register_recv_cb(receive_callback)) != ESP_OK) return error;
    if ((error = esp_now_register_send_cb(send_callback)) != ESP_OK) return error;

    esp_now_peer_info_t peer{};
    std::memset(peer.peer_addr, 0xFF, ESP_NOW_ETH_ALEN);
    peer.channel = radio_channel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if ((error = esp_now_add_peer(&peer)) != ESP_OK && error != ESP_ERR_ESPNOW_EXIST) return error;
    initialized_ = true;
    return ESP_OK;
}

bool EspNowTransport::receive(ReceivedFrame& frame, TickType_t wait_ticks) {
    return receive_queue_ != nullptr && xQueueReceive(receive_queue_, &frame, wait_ticks) == pdTRUE;
}

esp_err_t EspNowTransport::send(const uint8_t* data, size_t size, TickType_t wait_ticks) {
    if (!initialized_ || data == nullptr || size == 0 || size > protocol::kMaxWireSize) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(send_mutex_, wait_ticks) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t error = ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(send_ready_, wait_ticks) == pdTRUE) {
        static constexpr uint8_t broadcast[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        error = esp_now_send(broadcast, data, size);
        if (error != ESP_OK) xSemaphoreGive(send_ready_);
    }
    xSemaphoreGive(send_mutex_);
    return error;
}

void EspNowTransport::receive_callback(const esp_now_recv_info_t* info, const uint8_t* data, int size) {
    if (instance_ == nullptr || info == nullptr || data == nullptr || size <= 0 ||
        size > static_cast<int>(protocol::kMaxWireSize)) return;
    ReceivedFrame frame{};
    frame.size = static_cast<uint16_t>(size);
    frame.rssi = info->rx_ctrl == nullptr ? 0 : info->rx_ctrl->rssi;
    std::memcpy(frame.data.data(), data, frame.size);
    if (xQueueSend(instance_->receive_queue_, &frame, 0) != pdTRUE) {
        instance_->dropped_rx_frames_.fetch_add(1, std::memory_order_relaxed);
    }
}

void EspNowTransport::send_callback(const uint8_t*, esp_now_send_status_t) {
    if (instance_ != nullptr && instance_->send_ready_ != nullptr) xSemaphoreGive(instance_->send_ready_);
}

}  // namespace walkie
