#include "common_audio/vad/vad_gmm.h"

#include "common_audio/signal_processing/include/signal_processing_library.h"

static const int32_t kCompVar = 22005;
static const int16_t kLog2Exp = 5909;

int32_t WebRtcVad_GaussianProbability(int16_t input, int16_t mean, int16_t std, int16_t* delta) {
    int16_t tmp16, inv_std, inv_std2, exp_value = 0;
    int32_t tmp32;

    tmp32 = (int32_t)131072 + (int32_t)(std >> 1);
    inv_std = (int16_t)WebRtcSpl_DivW32W16(tmp32, std);

    tmp16 = (inv_std >> 2);
    inv_std2 = (int16_t)((tmp16 * tmp16) >> 2);

    tmp16 = (int16_t)((input << 3) - mean);
    *delta = (int16_t)((inv_std2 * tmp16) >> 10);

    tmp32 = (*delta * tmp16) >> 9;

    if (tmp32 < kCompVar) {
        tmp16 = (int16_t)((kLog2Exp * tmp32) >> 12);
        tmp16 = (int16_t)(-tmp16);
        exp_value = (int16_t)(0x0400 | (tmp16 & 0x03FF));
        tmp16 ^= 0xFFFF;
        tmp16 >>= 10;
        tmp16 = (int16_t)(tmp16 + 1);
        exp_value >>= tmp16;
    }

    return inv_std * exp_value;
}
