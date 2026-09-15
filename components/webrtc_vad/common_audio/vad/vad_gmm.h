#ifndef COMMON_AUDIO_VAD_VAD_GMM_H_
#define COMMON_AUDIO_VAD_VAD_GMM_H_

#include <stdint.h>

int32_t WebRtcVad_GaussianProbability(int16_t input, int16_t mean, int16_t std, int16_t* delta);

#endif
