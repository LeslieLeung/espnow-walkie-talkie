#include "walkie/vox.hpp"

#include "webrtc_vad.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

static_assert(walkie::vox_profile(walkie::VoxLevel::High).vad_mode ==
              walkie::audio::VoiceActivityDetector::kMode);
static_assert(walkie::vox_profile(walkie::VoxLevel::High).min_mean_abs ==
              walkie::audio::VoiceActivityDetector::kMinMeanAbs);
static_assert(walkie::vox_profile(walkie::VoxLevel::High).attack_frames ==
              walkie::audio::VoxGate::kAttackFrames);
static_assert(walkie::vox_profile(walkie::VoxLevel::High).noise_margin == 0);
static_assert(walkie::vox_profile(walkie::VoxLevel::High).learn_frames == 0);

namespace walkie::audio {

void VoxGate::reset() {
    speech_run_ = 0;
    silence_run_ = 0;
    latched_ = false;
}

void VoxGate::set_attack_frames(uint8_t frames) {
    attack_frames_ = frames == 0 ? 1 : frames;
}

VoxDecision VoxGate::observe(bool speech, bool transmitting) {
    if (speech) {
        if (speech_run_ < 255) ++speech_run_;
        silence_run_ = 0;
        if (!latched_ && speech_run_ >= attack_frames_) {
            latched_ = true;
            return VoxDecision::Press;
        }
        return VoxDecision::None;
    }
    speech_run_ = 0;
    if (!latched_ || !transmitting) {
        silence_run_ = 0;
        return VoxDecision::None;
    }
    if (silence_run_ < 255) ++silence_run_;
    if (silence_run_ >= kHangFrames) {
        latched_ = false;
        silence_run_ = 0;
        return VoxDecision::Release;
    }
    return VoxDecision::None;
}

void PcmPreroll::clear() {
    head_ = 0;
    count_ = 0;
}

void PcmPreroll::push(const int16_t* samples) {
    std::memcpy(frames_[head_], samples, protocol::kAudioSamples * sizeof(int16_t));
    head_ = (head_ + 1) % kFrames;
    if (count_ < kFrames) ++count_;
}

const int16_t* PcmPreroll::frame(size_t oldest_index) const {
    if (oldest_index >= count_) return nullptr;
    const size_t start = (head_ + kFrames - count_) % kFrames;
    return frames_[(start + oldest_index) % kFrames];
}

VoiceActivityDetector::~VoiceActivityDetector() { stop(); }

bool VoiceActivityDetector::start() {
    if (inst_ != nullptr) return true;
    VadInst* vad = WebRtcVad_Create();
    if (vad == nullptr) return false;
    if (WebRtcVad_Init(vad) != 0 || WebRtcVad_set_mode(vad, kMode) != 0) {
        WebRtcVad_Free(vad);
        return false;
    }
    inst_ = vad;
    if (!apply_profile(VoxLevel::High)) {
        stop();
        return false;
    }
    return true;
}

void VoiceActivityDetector::stop() {
    if (inst_ == nullptr) return;
    WebRtcVad_Free(static_cast<VadInst*>(inst_));
    inst_ = nullptr;
}

bool VoiceActivityDetector::apply_profile(VoxLevel level) {
    const VoxProfile profile = vox_profile(level);
    min_mean_abs_ = profile.min_mean_abs;
    noise_margin_ = profile.noise_margin;
    learn_frames_left_ = profile.learn_frames;
    have_noise_ = false;
    noise_mean_abs_ = 0;
    if (inst_ == nullptr) return false;
    return WebRtcVad_set_mode(static_cast<VadInst*>(inst_), profile.vad_mode) == 0;
}

void VoiceActivityDetector::update_noise(int32_t mean_abs) {
    if (!have_noise_) {
        noise_mean_abs_ = mean_abs;
        have_noise_ = true;
        return;
    }
    if (mean_abs < noise_mean_abs_) {
        noise_mean_abs_ -= (noise_mean_abs_ - mean_abs) / 4;
    } else {
        noise_mean_abs_ += (mean_abs - noise_mean_abs_) / 32;
    }
}

int32_t VoiceActivityDetector::energy_threshold() const {
    if (noise_margin_ <= 0 || !have_noise_) return min_mean_abs_;
    const int32_t adaptive = noise_mean_abs_ > kMaxEnergyThreshold - noise_margin_
                                 ? kMaxEnergyThreshold
                                 : noise_mean_abs_ + noise_margin_;
    return std::min(kMaxEnergyThreshold, std::max(min_mean_abs_, adaptive));
}

bool VoiceActivityDetector::is_speech(const int16_t* samples, size_t count, bool learn_noise) {
    if (inst_ == nullptr || samples == nullptr || count == 0) return false;
    int32_t abs_sum = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t sample = samples[i];
        abs_sum += sample >= 0 ? sample : -sample;
    }
    const int32_t mean_abs = abs_sum / static_cast<int32_t>(count);
    if (learn_noise && learn_frames_left_ > 0) {
        if (noise_margin_ > 0) update_noise(mean_abs);
        --learn_frames_left_;
        return false;
    }
    if (mean_abs < energy_threshold()) {
        if (learn_noise && noise_margin_ > 0) update_noise(mean_abs);
        return false;
    }
    const bool speech =
        WebRtcVad_Process(static_cast<VadInst*>(inst_), kSampleRateHz, samples, count) == 1;
    if (learn_noise && noise_margin_ > 0 && !speech) update_noise(mean_abs);
    return speech;
}

}  // namespace walkie::audio
