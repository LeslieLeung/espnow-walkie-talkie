#include "walkie/adpcm.hpp"

#include <algorithm>

namespace walkie::audio {
namespace {

constexpr int kStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};
constexpr int8_t kIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

uint8_t encode_sample(int16_t sample, AdpcmState& state) {
    const int step = kStepTable[state.step_index];
    int diff = static_cast<int>(sample) - state.predictor;
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    int delta = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; delta += step; }
    if (diff >= (step >> 1)) { code |= 2; diff -= step >> 1; delta += step >> 1; }
    if (diff >= (step >> 2)) { code |= 1; delta += step >> 2; }
    const int prediction = state.predictor + ((code & 8) ? -delta : delta);
    state.predictor = static_cast<int16_t>(std::clamp(prediction, -32768, 32767));
    state.step_index = static_cast<uint8_t>(std::clamp<int>(state.step_index + kIndexTable[code], 0, 88));
    return code;
}

int16_t decode_sample(uint8_t code, AdpcmState& state) {
    code &= 0x0F;
    const int step = kStepTable[state.step_index];
    int delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    const int prediction = state.predictor + ((code & 8) ? -delta : delta);
    state.predictor = static_cast<int16_t>(std::clamp(prediction, -32768, 32767));
    state.step_index = static_cast<uint8_t>(std::clamp<int>(state.step_index + kIndexTable[code], 0, 88));
    return state.predictor;
}

}  // namespace

bool encode_ima_adpcm(const int16_t* pcm, size_t sample_count, uint8_t* encoded,
                      size_t encoded_capacity, AdpcmState& state) {
    if (pcm == nullptr || encoded == nullptr || (sample_count & 1U) != 0 ||
        encoded_capacity < sample_count / 2 || state.step_index > 88) return false;
    for (size_t i = 0; i < sample_count; i += 2) {
        const uint8_t low = encode_sample(pcm[i], state);
        const uint8_t high = encode_sample(pcm[i + 1], state);
        encoded[i / 2] = static_cast<uint8_t>(low | (high << 4));
    }
    return true;
}

bool decode_ima_adpcm(const uint8_t* encoded, size_t encoded_size, int16_t* pcm,
                      size_t sample_capacity, AdpcmState& state) {
    if (encoded == nullptr || pcm == nullptr || encoded_size > sample_capacity / 2 ||
        state.step_index > 88) return false;
    for (size_t i = 0; i < encoded_size; ++i) {
        pcm[i * 2] = decode_sample(encoded[i], state);
        pcm[i * 2 + 1] = decode_sample(encoded[i] >> 4, state);
    }
    return true;
}

}  // namespace walkie::audio
