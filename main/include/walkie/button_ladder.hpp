#pragma once

#include "walkie/bsp.hpp"

#include <cstdint>

namespace walkie {

constexpr int kLadderNoKey = -1;
constexpr int kLadderInvalidSample = -2;
constexpr int kLadderKeyUp = 0;
constexpr int kLadderKeyDown = 1;
constexpr int kLadderKeyOk = 2;
constexpr uint32_t kLadderDebounceMs = 50;
constexpr uint32_t kLadderHoldMs = 400;

struct LadderButtonState {
    int active_key{kLadderNoKey};
    int pending_key{kLadderNoKey};
    uint32_t pending_since_ms{0};
    uint32_t pressed_at_ms{0};
    bool hold_fired{false};
    bool edge_a_pressed{false};
    bool edge_a_released{false};
    bool edge_b_clicked{false};
};

inline void ladder_commit_key(LadderButtonState& state, int next_key, uint32_t now_ms) {
    if (state.active_key >= 0) {
        if (state.active_key == kLadderKeyOk) {
            state.edge_a_released = true;
        } else if (!state.hold_fired &&
                   static_cast<uint32_t>(now_ms - state.pressed_at_ms) < kLadderHoldMs) {
            state.edge_b_clicked = true;
        }
    }
    if (next_key >= 0) {
        state.pressed_at_ms = now_ms;
        state.hold_fired = false;
        if (next_key == kLadderKeyOk) state.edge_a_pressed = true;
    }
    state.active_key = next_key;
}

// `detected` is a key index, kLadderNoKey, or kLadderInvalidSample (keep last key).
// Invalid / noisy samples stretch debounce so ESP-NOW TX does not false-release PTT.
inline ButtonEvents poll_ladder_buttons(LadderButtonState& state, int detected, uint32_t now_ms) {
    ButtonEvents events{};
    if (detected == kLadderInvalidSample) {
        state.pending_since_ms = now_ms;
    } else if (detected != state.pending_key) {
        state.pending_key = detected;
        state.pending_since_ms = now_ms;
    } else if (detected != state.active_key &&
               static_cast<uint32_t>(now_ms - state.pending_since_ms) >= kLadderDebounceMs) {
        ladder_commit_key(state, detected, now_ms);
    }

    if (state.active_key >= 0 && state.active_key != kLadderKeyOk && !state.hold_fired &&
        static_cast<uint32_t>(now_ms - state.pressed_at_ms) >= kLadderHoldMs) {
        state.hold_fired = true;
        events.b_held = true;
    }

    events.a_pressed = state.edge_a_pressed;
    events.a_released = state.edge_a_released;
    events.b_clicked = state.edge_b_clicked;
    state.edge_a_pressed = false;
    state.edge_a_released = false;
    state.edge_b_clicked = false;
    return events;
}

}  // namespace walkie
