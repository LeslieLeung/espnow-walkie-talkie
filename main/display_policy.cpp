#include "walkie/display_policy.hpp"

namespace walkie {

uint32_t screen_background_rgb(TalkState state) {
    switch (state) {
        case TalkState::Talking:
            return kTransmittingScreenRgb;
        case TalkState::Receiving:
        case TalkState::Busy:
            return kReceivingScreenRgb;
        case TalkState::Idle:
        case TalkState::Requesting:
            return kIdleScreenRgb;
    }
    return kIdleScreenRgb;
}

bool talk_activity_keeps_screen_awake(TalkState state) {
    return state == TalkState::Talking || state == TalkState::Receiving || state == TalkState::Busy;
}

}  // namespace walkie
