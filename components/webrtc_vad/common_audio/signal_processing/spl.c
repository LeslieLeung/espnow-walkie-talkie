#include "common_audio/signal_processing/include/signal_processing_library.h"

int16_t WebRtcSpl_GetScalingSquare(int16_t* in_vector, size_t in_vector_length, size_t times) {
    const int16_t nbits = WebRtcSpl_GetSizeInBits((uint32_t)times);
    int16_t smax = -1;
    int16_t* sptr = in_vector;
    for (size_t i = in_vector_length; i > 0; --i) {
        const int16_t sabs = (*sptr > 0 ? *sptr++ : (int16_t)(-*sptr++));
        if (sabs > smax) smax = sabs;
    }
    if (smax == 0) return 0;
    const int16_t t = WebRtcSpl_NormW32(WEBRTC_SPL_MUL(smax, smax));
    return (t > nbits) ? 0 : (int16_t)(nbits - t);
}

int32_t WebRtcSpl_Energy(int16_t* vector, size_t vector_length, int* scale_factor) {
    int32_t en = 0;
    const int scaling = WebRtcSpl_GetScalingSquare(vector, vector_length, vector_length);
    for (size_t i = 0; i < vector_length; ++i) {
        en += (vector[i] * vector[i]) >> scaling;
    }
    *scale_factor = scaling;
    return en;
}

int32_t WebRtcSpl_DivW32W16(int32_t num, int16_t den) {
    if (den != 0) return num / den;
    return 0x7FFFFFFF;
}
