/**
 * @file filters.c
 * @brief Simple biquad IIR filters for float32 sample buffers.
 */

#include "filters.h"
#include "math_constants.h"

#include <math.h>

static void normalize_and_store(biquad_df2t_t *q,
                                float32_t b0, float32_t b1, float32_t b2,
                                float32_t a0, float32_t a1, float32_t a2)
{
    /* Normalize so a0 == 1 */
    const float32_t inv_a0 = 1.0f / a0;

    q->b0 = b0 * inv_a0;
    q->b1 = b1 * inv_a0;
    q->b2 = b2 * inv_a0;

    q->a1 = a1 * inv_a0;
    q->a2 = a2 * inv_a0;
}

void biquad_reset(biquad_df2t_t *q)
{
    if (!q)
        return;
    q->z1 = 0.0f;
    q->z2 = 0.0f;
}

void biquad_config(biquad_df2t_t *q, filter_type_t type, float32_t fs, float32_t f0, float32_t Q)
{
    if (!q)
        return;

    if (type == FILTER_BYPASS)
    {
        q->b0 = 1.0f;
        q->b1 = 0.0f;
        q->b2 = 0.0f;
        q->a1 = 0.0f;
        q->a2 = 0.0f;
        return;
    }

    /* Guard rails for stability */
    if (fs <= 0.0f)
        fs = 48000.0f;
    if (f0 < 1.0f)
        f0 = 1.0f;
    if (f0 > (0.49f * fs))
        f0 = 0.49f * fs;
    if (Q < 0.1f)
        Q = 0.1f;

    const float32_t w0 = PM4_TWO_PI_F * (f0 / fs);
    const float32_t cw = cosf(w0);
    const float32_t sw = sinf(w0);
    const float32_t alpha = sw / (2.0f * Q);

    /* RBJ cookbook: a0 = 1+alpha, a1 = -2cos(w0), a2 = 1-alpha */
    float32_t b0, b1, b2, a0, a1, a2;

    switch (type)
    {
    case FILTER_LOWPASS:
        b0 = (1.0f - cw) * 0.5f;
        b1 = (1.0f - cw);
        b2 = (1.0f - cw) * 0.5f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cw;
        a2 = 1.0f - alpha;
        break;

    case FILTER_HIGHPASS:
        b0 = (1.0f + cw) * 0.5f;
        b1 = -(1.0f + cw);
        b2 = (1.0f + cw) * 0.5f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cw;
        a2 = 1.0f - alpha;
        break;

    case FILTER_BANDPASS:
        /* Constant skirt gain, peak gain = Q */
        b0 = alpha;
        b1 = 0.0f;
        b2 = -alpha;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cw;
        a2 = 1.0f - alpha;
        break;

    case FILTER_NOTCH:
        b0 = 1.0f;
        b1 = -2.0f * cw;
        b2 = 1.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cw;
        a2 = 1.0f - alpha;
        break;

    default:
        /* Fallback to bypass */
        b0 = 1.0f;
        b1 = 0.0f;
        b2 = 0.0f;
        a0 = 1.0f;
        a1 = 0.0f;
        a2 = 0.0f;
        break;
    }

    normalize_and_store(q, b0, b1, b2, a0, a1, a2);
}

void biquad_process_buffer(biquad_df2t_t *q, float32_t *buf, uint32_t n)
{
    if (!q || !buf)
        return;
    for (uint32_t i = 0; i < n; i++)
    {
        buf[i] = biquad_process1(q, buf[i]);
    }
}
