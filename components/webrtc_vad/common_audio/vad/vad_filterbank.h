#ifndef COMMON_AUDIO_VAD_VAD_FILTERBANK_H_
#define COMMON_AUDIO_VAD_VAD_FILTERBANK_H_

#include <stddef.h>
#include <stdint.h>

#include "common_audio/vad/vad_core.h"

int16_t WebRtcVad_CalculateFeatures(VadInstT* self,
                                    const int16_t* data_in,
                                    size_t data_length,
                                    int16_t* features);

#endif
