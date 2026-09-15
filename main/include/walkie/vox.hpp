#pragma once

#include "walkie/protocol.hpp"

#include <cstddef>
#include <cstdint>

namespace walkie {

enum class VoxLevel : uint8_t { Off = 0, Low = 1, Med = 2, High = 3 };

inline constexpr uint8_t kVoxLevelCount = 4;

struct VoxProfile {
    int vad_mode;
    int32_t min_mean_abs;
    int32_t noise_margin;
    uint8_t attack_frames;
    uint8_t learn_frames;
};

inline constexpr bool vox_enabled(VoxLevel level) {
    return level != VoxLevel::Off;
}

inline constexpr VoxLevel normalize_vox_level(uint8_t raw) {
    return raw < kVoxLevelCount ? static_cast<VoxLevel>(raw) : VoxLevel::Off;
}

inline constexpr bool vox_active(uint8_t raw) {
    return vox_enabled(normalize_vox_level(raw));
}

inline constexpr uint32_t kVoxTimeoutCooldownMs = 1500;

inline constexpr bool vox_cooling_down(uint32_t now_ms, uint32_t until_ms) {
    return static_cast<int32_t>(now_ms - until_ms) < 0;
}

// HIGH is the original hair-trigger VAD. MED/LOW add a close-talk energy floor
// and a margin above the tracked noise so office chatter does not key up.
inline constexpr VoxProfile vox_profile(VoxLevel level) {
    switch (level) {
        case VoxLevel::Low: return VoxProfile{3, 8000, 3000, 12, 20};
        case VoxLevel::Med: return VoxProfile{3, 3500, 1600, 8, 12};
        case VoxLevel::High: return VoxProfile{2, 180, 0, 3, 0};
        case VoxLevel::Off: break;
    }
    return VoxProfile{2, 180, 0, 3, 0};
}

namespace audio {

enum class VoxDecision : uint8_t { None, Press, Release };

// Debounces WebRTC VAD frames into virtual PTT edges.
class VoxGate {
public:
    static constexpr uint8_t kAttackFrames = 3;
    static constexpr uint8_t kHangFrames = 30;

    void reset();
    void set_attack_frames(uint8_t frames);
    VoxDecision observe(bool speech, bool transmitting);
    bool latched() const { return latched_; }

private:
    uint8_t attack_frames_{kAttackFrames};
    uint8_t speech_run_{0};
    uint8_t silence_run_{0};
    bool latched_{false};
};

// ~7.6 KiB of PCM. Keep instances in BSS/heap, never on a FreeRTOS task stack.
class PcmPreroll {
public:
    static constexpr size_t kFrames = 12;

    void clear();
    void push(const int16_t* samples);
    size_t size() const { return count_; }
    const int16_t* frame(size_t oldest_index) const;

private:
    int16_t frames_[kFrames][protocol::kAudioSamples]{};
    size_t head_{0};
    size_t count_{0};
};

class VoiceActivityDetector {
public:
    static constexpr int kSampleRateHz = 16000;
    static constexpr int kMode = 2;
    static constexpr int32_t kMinMeanAbs = 180;
    static constexpr int32_t kMaxEnergyThreshold = 14000;

    VoiceActivityDetector() = default;
    VoiceActivityDetector(const VoiceActivityDetector&) = delete;
    VoiceActivityDetector& operator=(const VoiceActivityDetector&) = delete;
    ~VoiceActivityDetector();

    bool start();
    void stop();
    bool apply_profile(VoxLevel level);
    bool is_speech(const int16_t* samples, size_t count, bool learn_noise = true);
    int32_t energy_threshold() const;

private:
    void update_noise(int32_t mean_abs);

    void* inst_{nullptr};
    int32_t min_mean_abs_{kMinMeanAbs};
    int32_t noise_margin_{0};
    int32_t noise_mean_abs_{0};
    uint8_t learn_frames_left_{0};
    bool have_noise_{false};
};

}  // namespace audio
}  // namespace walkie
