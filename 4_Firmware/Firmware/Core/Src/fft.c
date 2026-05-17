/**
 * @file fft.c
 * @brief Calculate centered complex I/Q frequency spectrum
 * @author Hanspeter Hochreutener, hhrt@zhaw.ch
 * @author Patrick Rennhard, renn@zhaw.ch
 * @date 2025-09-03
 */

/******************************************************************************
 * Includes
 *****************************************************************************/
#include <math.h>

#include "radar.h"
#include "fft.h"
#include "math_constants.h"
#include "arm_const_structs.h"

/******************************************************************************
 * Variables
 *****************************************************************************/
static float32_t fft_window[RADAR_CHANNEL_SAMPLES];
static float32_t fft_complex_buffer[2U * RADAR_CHANNEL_SAMPLES];
static float32_t fft_magnitude_buffer[RADAR_CHANNEL_SAMPLES];
static float32_t fft_abs_scale = 1.0f;
static const arm_cfft_instance_f32 *fft_instance = 0;

/******************************************************************************
 * Functions
 *****************************************************************************/
HAL_StatusTypeDef fft_init(void)
{
    float32_t window_sum = 0.0f;

#if (RADAR_CHANNEL_SAMPLES == 16U)
    fft_instance = &arm_cfft_sR_f32_len16;
#elif (RADAR_CHANNEL_SAMPLES == 32U)
    fft_instance = &arm_cfft_sR_f32_len32;
#elif (RADAR_CHANNEL_SAMPLES == 64U)
    fft_instance = &arm_cfft_sR_f32_len64;
#elif (RADAR_CHANNEL_SAMPLES == 128U)
    fft_instance = &arm_cfft_sR_f32_len128;
#elif (RADAR_CHANNEL_SAMPLES == 256U)
    fft_instance = &arm_cfft_sR_f32_len256;
#elif (RADAR_CHANNEL_SAMPLES == 512U)
    fft_instance = &arm_cfft_sR_f32_len512;
#elif (RADAR_CHANNEL_SAMPLES == 1024U)
    fft_instance = &arm_cfft_sR_f32_len1024;
#elif (RADAR_CHANNEL_SAMPLES == 2048U)
    fft_instance = &arm_cfft_sR_f32_len2048;
#elif (RADAR_CHANNEL_SAMPLES == 4096U)
    fft_instance = &arm_cfft_sR_f32_len4096;
#else
    return HAL_ERROR;
#endif

    for (uint32_t i = 0; i < RADAR_CHANNEL_SAMPLES; i++)
    {
        float32_t phase = (PM4_TWO_PI_F * (float32_t)i)
                / (float32_t)(RADAR_CHANNEL_SAMPLES - 1U);
        fft_window[i] = 0.5f - 0.5f * cosf(phase);
        window_sum += fft_window[i];
    }

    if (window_sum <= 0.0f)
    {
        window_sum = 1.0f;
    }

    /* Convert ADC counts to volts and compensate the Hann coherent gain. */
    fft_abs_scale = RADAR_ADC_REF_VOLTAGE / (float32_t)(1U << RADAR_ADC_RES);
    fft_abs_scale /= window_sum;

    return HAL_OK;
}

HAL_StatusTypeDef fft_iq_centered(const float32_t i_in[], const float32_t q_in[],
                                  float32_t out[])
{
    float32_t mean_i = 0.0f;
    float32_t mean_q = 0.0f;
    uint32_t half_size = RADAR_CHANNEL_SAMPLES / 2U;

    if ((i_in == 0) || (q_in == 0) || (out == 0))
    {
        return HAL_ERROR;
    }
    if (fft_instance == 0)
    {
        return HAL_ERROR;
    }

    for (uint32_t i = 0; i < RADAR_CHANNEL_SAMPLES; i++)
    {
        mean_i += i_in[i];
        mean_q += q_in[i];
    }
    mean_i /= (float32_t)RADAR_CHANNEL_SAMPLES;
    mean_q /= (float32_t)RADAR_CHANNEL_SAMPLES;

    for (uint32_t i = 0; i < RADAR_CHANNEL_SAMPLES; i++)
    {
        float32_t windowed_i = (i_in[i] - mean_i) * fft_window[i];
        float32_t windowed_q = (q_in[i] - mean_q) * fft_window[i];

        fft_complex_buffer[2U * i] = windowed_i;
        fft_complex_buffer[(2U * i) + 1U] = windowed_q;
    }

    arm_cfft_f32(fft_instance, fft_complex_buffer, 0U, 1U);
    arm_cmplx_mag_f32(fft_complex_buffer, fft_magnitude_buffer, RADAR_CHANNEL_SAMPLES);
    arm_scale_f32(fft_magnitude_buffer, fft_abs_scale, fft_magnitude_buffer,
                  RADAR_CHANNEL_SAMPLES);

    for (uint32_t i = 0; i < half_size; i++)
    {
        out[i] = fft_magnitude_buffer[i + half_size];
        out[i + half_size] = fft_magnitude_buffer[i];
    }

    return HAL_OK;
}

HAL_StatusTypeDef fft_real_centered(const float32_t x_in[], float32_t out[])
{
    float32_t mean_x = 0.0f;
    float32_t real_abs_scale;
    uint32_t half_size = RADAR_CHANNEL_SAMPLES / 2U;

    if ((x_in == 0) || (out == 0))
    {
        return HAL_ERROR;
    }
    if (fft_instance == 0)
    {
        return HAL_ERROR;
    }

    for (uint32_t i = 0; i < RADAR_CHANNEL_SAMPLES; i++)
    {
        mean_x += x_in[i];
    }
    mean_x /= (float32_t)RADAR_CHANNEL_SAMPLES;

    for (uint32_t i = 0; i < RADAR_CHANNEL_SAMPLES; i++)
    {
        fft_complex_buffer[2U * i] = (x_in[i] - mean_x) * fft_window[i];
        fft_complex_buffer[(2U * i) + 1U] = 0.0f;
    }

    arm_cfft_f32(fft_instance, fft_complex_buffer, 0U, 1U);
    arm_cmplx_mag_f32(fft_complex_buffer, fft_magnitude_buffer, RADAR_CHANNEL_SAMPLES);

    real_abs_scale = 1.0f;
    if (fft_abs_scale > 0.0f)
    {
        real_abs_scale = fft_abs_scale *
                ((float32_t)(1U << RADAR_ADC_RES) / RADAR_ADC_REF_VOLTAGE);
    }

    arm_scale_f32(fft_magnitude_buffer, real_abs_scale, fft_magnitude_buffer,
                  RADAR_CHANNEL_SAMPLES);

    for (uint32_t i = 0; i < half_size; i++)
    {
        out[i] = fft_magnitude_buffer[i + half_size];
        out[i + half_size] = fft_magnitude_buffer[i];
    }

    return HAL_OK;
}
