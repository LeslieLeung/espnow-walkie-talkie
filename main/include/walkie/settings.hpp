#pragma once

#include <cstdint>

namespace walkie {

struct Settings {
    uint8_t logical_channel{1};
    uint8_t volume_percent{50};
};

class SettingsStore {
public:
    bool initialize();
    Settings load() const;
    bool save(const Settings& settings) const;

private:
    bool ready_{false};
};

}  // namespace walkie
