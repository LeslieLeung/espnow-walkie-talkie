#pragma once

#include <cstddef>
#include <cstdint>

namespace walkie::audio {

struct AdpcmState {
    int16_t predictor{0};
    uint8_t step_index{0};
};

bool encode_ima_adpcm(const int16_t* pcm, size_t sample_count, uint8_t* encoded,
                      size_t encoded_capacity, AdpcmState& state);
bool decode_ima_adpcm(const uint8_t* encoded, size_t encoded_size, int16_t* pcm,
                      size_t sample_capacity, AdpcmState& state);

}  // namespace walkie::audio
