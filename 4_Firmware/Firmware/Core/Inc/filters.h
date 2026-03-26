/**
 * @file filters.h
 * @brief Simple biquad IIR filters for float32 sample buffers.
 *
 * Filter coefficient formulas are from the RBJ "Audio EQ Cookbook".
 * This implementation is stable, low-CPU, and easy to parameterize
 * for embedded signal processing.
 */

#ifndef FILTERS_H_
#define FILTERS_H_

#include <stdint.h>

/* CMSIS-DSP uses float32_t; fall back to float if not available. */
#ifndef __F32_DEFINED
typedef float float32_t;
#endif

typedef enum
{
    FILTER_BYPASS = 0,
    FILTER_LOWPASS,
    FILTER_HIGHPASS,
    FILTER_BANDPASS,
    FILTER_NOTCH
} filter_type_t;

typedef struct
{
    /* Normalized coefficients for DF-II Transposed:
     * y = b0*x + z1
     * z1 = b1*x - a1*y + z2
     * z2 = b2*x - a2*y
     */
    float32_t b0, b1, b2;
    float32_t a1, a2;

    /* State */
    float32_t z1, z2;
} biquad_df2t_t;

void biquad_reset(biquad_df2t_t *q);

/**
 * @brief Configure a biquad for a standard filter type.
 *
 * @param q     Filter instance
 * @param type  FILTER_LOWPASS / FILTER_HIGHPASS / FILTER_BANDPASS / FILTER_NOTCH
 * @param fs    Sample rate [Hz]
 * @param f0    Cutoff/center frequency [Hz]
 * @param Q     Quality factor (typical: 0.707 for Butterworth)
 */
void biquad_config(biquad_df2t_t *q, filter_type_t type, float32_t fs, float32_t f0, float32_t Q);

/** Process one sample */
static inline float32_t biquad_process1(biquad_df2t_t *q, float32_t x)
{
    float32_t y = (q->b0 * x) + q->z1;
    q->z1 = (q->b1 * x) - (q->a1 * y) + q->z2;
    q->z2 = (q->b2 * x) - (q->a2 * y);
    return y;
}

/** Process a buffer in-place */
void biquad_process_buffer(biquad_df2t_t *q, float32_t *buf, uint32_t n);

#endif /* FILTERS_H_ */
