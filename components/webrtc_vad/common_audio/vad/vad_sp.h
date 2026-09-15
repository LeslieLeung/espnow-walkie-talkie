#ifndef COMMON_AUDIO_VAD_VAD_SP_H_
#define COMMON_AUDIO_VAD_VAD_SP_H_

#include <stddef.h>
#include <stdint.h>

#include "common_audio/vad/vad_core.h"

void WebRtcVad_Downsampling(const int16_t* signal_in,
                            int16_t* signal_out,
                            int32_t* filter_state,
                            size_t in_length);

int16_t WebRtcVad_FindMinimum(VadInstT* handle, int16_t feature_value, int channel);

#endif
