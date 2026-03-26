/** ***************************************************************************
 * @file
 * @brief CMSIS FFT wrapper for milestone-1 radar processing.
 *****************************************************************************/

#include "fft.h"
#include "arm_const_structs.h"

static const arm_cfft_instance_f32 *g_cfft_instance = 0;
static bool g_fft_initialized = false;

FFT_Status_t FFT_init(void)
{
    g_cfft_instance = &arm_cfft_sR_f32_len1024;
    g_fft_initialized = true;
    return FFT_STATUS_OK;
}

FFT_Status_t FFT_run_cfft(float32_t *cfft_inout, uint16_t fft_size)
{
    if (cfft_inout == 0) {
        return FFT_STATUS_INVALID_ARG;
    }

    if (!g_fft_initialized) {
        return FFT_STATUS_NOT_INITIALIZED;
    }

    if (fft_size != FFT_SIZE) {
        return FFT_STATUS_INVALID_ARG;
    }

    arm_cfft_f32(g_cfft_instance, cfft_inout, 0, 1);
    return FFT_STATUS_OK;
}

FFT_Status_t FFT_compute_magnitude(const float32_t *cfft_out, float32_t *magnitude, uint16_t fft_size)
{
    if ((cfft_out == 0) || (magnitude == 0) || (fft_size == 0U)) {
        return FFT_STATUS_INVALID_ARG;
    }

    arm_cmplx_mag_f32(cfft_out, magnitude, fft_size);
    return FFT_STATUS_OK;
}

FFT_Status_t FFT_find_peak_in_bin_range(const float32_t *magnitude, uint16_t start_bin, uint16_t end_bin, uint16_t *peak_bin, float32_t *peak_value)
{
    uint16_t i;
    float32_t best;
    uint16_t best_index;

    if ((magnitude == 0) || (peak_bin == 0) || (peak_value == 0)) {
        return FFT_STATUS_INVALID_ARG;
    }

    if ((start_bin >= FFT_SIZE) || (end_bin >= FFT_SIZE) || (start_bin > end_bin)) {
        return FFT_STATUS_INVALID_ARG;
    }

    best = magnitude[start_bin];
    best_index = start_bin;

    for (i = (uint16_t)(start_bin + 1U); i <= end_bin; i++) {
        if (magnitude[i] > best) {
            best = magnitude[i];
            best_index = i;
        }
    }

    *peak_bin = best_index;
    *peak_value = best;
    return FFT_STATUS_OK;
}
