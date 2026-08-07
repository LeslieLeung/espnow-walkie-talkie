#include "walkie/settings.hpp"

#include <algorithm>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace walkie {
namespace {
constexpr char kNamespace[] = "walkie";
constexpr char kChannelKey[] = "channel";
constexpr char kVolumeKey[] = "volume";

uint8_t normalize_volume(uint8_t volume) {
    constexpr uint8_t values[] = {0, 25, 50, 75};
    uint8_t best = values[0];
    for (uint8_t value : values) {
        if (volume >= value) best = value;
    }
    return best;
}

bool valid_volume(uint8_t volume) {
    return volume == 0 || volume == 25 || volume == 50 || volume == 75;
}
}  // namespace

bool SettingsStore::initialize() {
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        error = nvs_flash_erase();
        if (error == ESP_OK) error = nvs_flash_init();
    }
    ready_ = error == ESP_OK;
    return ready_;
}

Settings SettingsStore::load() const {
    Settings settings{};
    if (!ready_) return settings;
    nvs_handle_t handle{};
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return settings;
    uint8_t channel = settings.logical_channel;
    uint8_t volume = settings.volume_percent;
    if (nvs_get_u8(handle, kChannelKey, &channel) == ESP_OK && channel >= 1 && channel <= 4) {
        settings.logical_channel = channel;
    }
    if (nvs_get_u8(handle, kVolumeKey, &volume) == ESP_OK) {
        settings.volume_percent = normalize_volume(volume);
    }
    nvs_close(handle);
    return settings;
}

bool SettingsStore::save(const Settings& settings) const {
    if (!ready_ || settings.logical_channel < 1 || settings.logical_channel > 4 ||
        !valid_volume(settings.volume_percent)) return false;
    nvs_handle_t handle{};
    esp_err_t error = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (error == ESP_OK) error = nvs_set_u8(handle, kChannelKey, settings.logical_channel);
    if (error == ESP_OK) error = nvs_set_u8(handle, kVolumeKey, normalize_volume(settings.volume_percent));
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return error == ESP_OK;
}

}  // namespace walkie
