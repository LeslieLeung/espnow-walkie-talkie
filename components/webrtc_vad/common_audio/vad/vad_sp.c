#include "common_audio/vad/vad_sp.h"

#include "common_audio/signal_processing/include/signal_processing_library.h"
#include "common_audio/vad/vad_core.h"
#include "rtc_base/checks.h"

static const int16_t kAllPassCoefsQ13[2] = {5243, 1392};
static const int16_t kSmoothingDown = 6553;
static const int16_t kSmoothingUp = 32439;

void WebRtcVad_Downsampling(const int16_t* signal_in,
                            int16_t* signal_out,
                            int32_t* filter_state,
                            size_t in_length) {
    int16_t tmp16_1 = 0, tmp16_2 = 0;
    int32_t tmp32_1 = filter_state[0];
    int32_t tmp32_2 = filter_state[1];
    const size_t half_length = in_length >> 1;

    for (size_t n = 0; n < half_length; n++) {
        tmp16_1 = (int16_t)((tmp32_1 >> 1) + ((kAllPassCoefsQ13[0] * *signal_in) >> 14));
        *signal_out = tmp16_1;
        tmp32_1 = (int32_t)(*signal_in++) - ((kAllPassCoefsQ13[0] * tmp16_1) >> 12);

        tmp16_2 = (int16_t)((tmp32_2 >> 1) + ((kAllPassCoefsQ13[1] * *signal_in) >> 14));
        *signal_out++ += tmp16_2;
        tmp32_2 = (int32_t)(*signal_in++) - ((kAllPassCoefsQ13[1] * tmp16_2) >> 12);
    }
    filter_state[0] = tmp32_1;
    filter_state[1] = tmp32_2;
}

int16_t WebRtcVad_FindMinimum(VadInstT* self, int16_t feature_value, int channel) {
    int position = -1;
    const int offset = (channel << 4);
    int16_t current_median = 1600;
    int16_t alpha = 0;
    int16_t* age = &self->index_vector[offset];
    int16_t* smallest_values = &self->low_value_vector[offset];

    RTC_DCHECK_LT(channel, kNumChannels);

    for (int i = 0; i < 16; i++) {
        if (age[i] != 100) {
            age[i]++;
        } else {
            for (int j = i; j < 15; j++) {
                smallest_values[j] = smallest_values[j + 1];
                age[j] = age[j + 1];
            }
            age[15] = 101;
            smallest_values[15] = 10000;
        }
    }

    if (feature_value < smallest_values[7]) {
        if (feature_value < smallest_values[3]) {
            if (feature_value < smallest_values[1]) {
                position = feature_value < smallest_values[0] ? 0 : 1;
            } else {
                position = feature_value < smallest_values[2] ? 2 : 3;
            }
        } else if (feature_value < smallest_values[5]) {
            position = feature_value < smallest_values[4] ? 4 : 5;
        } else {
            position = feature_value < smallest_values[6] ? 6 : 7;
        }
    } else if (feature_value < smallest_values[15]) {
        if (feature_value < smallest_values[11]) {
            if (feature_value < smallest_values[9]) {
                position = feature_value < smallest_values[8] ? 8 : 9;
            } else {
                position = feature_value < smallest_values[10] ? 10 : 11;
            }
        } else if (feature_value < smallest_values[13]) {
            position = feature_value < smallest_values[12] ? 12 : 13;
        } else {
            position = feature_value < smallest_values[14] ? 14 : 15;
        }
    }

    if (position > -1) {
        for (int i = 15; i > position; i--) {
            smallest_values[i] = smallest_values[i - 1];
            age[i] = age[i - 1];
        }
        smallest_values[position] = feature_value;
        age[position] = 1;
    }

    if (self->frame_counter > 2) {
        current_median = smallest_values[2];
    } else if (self->frame_counter > 0) {
        current_median = smallest_values[0];
    }

    if (self->frame_counter > 0) {
        alpha = current_median < self->mean_value[channel] ? kSmoothingDown : kSmoothingUp;
    }
    int32_t tmp32 = (alpha + 1) * self->mean_value[channel];
    tmp32 += (WEBRTC_SPL_WORD16_MAX - alpha) * current_median;
    tmp32 += 16384;
    self->mean_value[channel] = (int16_t)(tmp32 >> 15);
    return self->mean_value[channel];
}
