#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define WEBRTC_SPL_WORD16_MAX 32767
#define WEBRTC_SPL_MUL(a, b) ((int32_t)((int32_t)(a) * (int32_t)(b)))
#define WEBRTC_SPL_LSHIFT_W32(x, n) ((int32_t)((x) << (n)))

static inline int WebRtcSpl_CountLeadingZeros32(uint32_t n) {
    return n == 0 ? 32 : __builtin_clz(n);
}

static inline int16_t WebRtcSpl_GetSizeInBits(uint32_t n) {
    return (int16_t)(32 - WebRtcSpl_CountLeadingZeros32(n));
}

static inline int16_t WebRtcSpl_NormW32(int32_t a) {
    return a == 0 ? 0 : (int16_t)(WebRtcSpl_CountLeadingZeros32(a < 0 ? (uint32_t)~a : (uint32_t)a) - 1);
}

static inline int16_t WebRtcSpl_NormU32(uint32_t a) {
    return a == 0 ? 0 : (int16_t)WebRtcSpl_CountLeadingZeros32(a);
}

int16_t WebRtcSpl_GetScalingSquare(int16_t* in_vector, size_t in_vector_length, size_t times);
int32_t WebRtcSpl_Energy(int16_t* vector, size_t vector_length, int* scale_factor);
int32_t WebRtcSpl_DivW32W16(int32_t num, int16_t den);

typedef struct {
    int32_t unused;
} WebRtcSpl_State48khzTo8khz;

static inline void WebRtcSpl_ResetResample48khzTo8khz(WebRtcSpl_State48khzTo8khz* state) {
    (void)state;
}

static inline void WebRtcSpl_Resample48khzTo8khz(const int16_t* in,
                                                 int16_t* out,
                                                 WebRtcSpl_State48khzTo8khz* state,
                                                 int32_t* tmp_mem) {
    (void)in;
    (void)out;
    (void)state;
    (void)tmp_mem;
}
