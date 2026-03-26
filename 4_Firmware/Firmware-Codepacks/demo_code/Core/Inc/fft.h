/** ***************************************************************************
 * @file
 * @brief CMSIS FFT wrapper for milestone-1 radar processing.
 *****************************************************************************/

#ifndef FFT_H_
#define FFT_H_

#include <stdbool.h>
#include <stdint.h>
#include "arm_math.h"

#define FFT_SIZE 1024U

typedef enum {
    FFT_STATUS_OK = 0,
    FFT_STATUS_INVALID_ARG,
    FFT_STATUS_NOT_INITIALIZED,
    FFT_STATUS_INIT_FAILED
} FFT_Status_t;

FFT_Status_t FFT_init(void);
FFT_Status_t FFT_run_cfft(float32_t *cfft_inout, uint16_t fft_size);
FFT_Status_t FFT_compute_magnitude(const float32_t *cfft_out, float32_t *magnitude, uint16_t fft_size);
FFT_Status_t FFT_find_peak_in_bin_range(const float32_t *magnitude, uint16_t start_bin, uint16_t end_bin, uint16_t *peak_bin, float32_t *peak_value);

#endif
