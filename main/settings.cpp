#include "walkie/settings.hpp"
#include "walkie/vox.hpp"

#include <algorithm>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace walkie {
namespace {
constexpr char kNamespace[] = "walkie";
constexpr char kChannelKey[] = "channel";
constexpr char kVolumeKey[] = "volume";
constexpr char kVoxKey[] = "vox";
constexpr char kVoxLevelKey[] = "vox_lvl";

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

bool valid_vox_level(uint8_t level) {
    return level < kVoxLevelCount;
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
    uint8_t vox = 0;
    uint8_t vox_level = 0;
    if (nvs_get_u8(handle, kChannelKey, &channel) == ESP_OK && channel >= 1 && channel <= 4) {
        settings.logical_channel = channel;
    }
    if (nvs_get_u8(handle, kVolumeKey, &volume) == ESP_OK) {
        settings.volume_percent = normalize_volume(volume);
    }
    if (nvs_get_u8(handle, kVoxLevelKey, &vox_level) == ESP_OK && valid_vox_level(vox_level)) {
        settings.vox_level = vox_level;
    } else if (nvs_get_u8(handle, kVoxKey, &vox) == ESP_OK && vox != 0) {
        settings.vox_level = 2;  // previous ON/OFF flag → MED
    }
    nvs_close(handle);
    return settings;
}

bool SettingsStore::save(const Settings& settings) const {
    if (!ready_ || settings.logical_channel < 1 || settings.logical_channel > 4 ||
        !valid_volume(settings.volume_percent) || !valid_vox_level(settings.vox_level)) {
        return false;
    }
    nvs_handle_t handle{};
    esp_err_t error = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (error == ESP_OK) error = nvs_set_u8(handle, kChannelKey, settings.logical_channel);
    if (error == ESP_OK) error = nvs_set_u8(handle, kVolumeKey, normalize_volume(settings.volume_percent));
    if (error == ESP_OK) error = nvs_set_u8(handle, kVoxKey, vox_active(settings.vox_level) ? 1 : 0);
    if (error == ESP_OK) error = nvs_set_u8(handle, kVoxLevelKey, settings.vox_level);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return error == ESP_OK;
}

}  // namespace walkie
