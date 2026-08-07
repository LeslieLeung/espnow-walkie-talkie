#pragma once

#include "walkie/talk_controller.hpp"

#include <cstdint>

namespace walkie {

constexpr uint32_t kIdleScreenRgb = 0x081018;
constexpr uint32_t kReceivingScreenRgb = 0x146B3A;
constexpr uint32_t kTransmittingScreenRgb = 0x8F1D1D;

uint32_t screen_background_rgb(TalkState state);
bool talk_activity_keeps_screen_awake(TalkState state);

}  // namespace walkie
