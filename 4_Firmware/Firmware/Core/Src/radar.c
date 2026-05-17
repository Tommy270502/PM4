/**
 * @file    radar.c
 * @brief   Radar acquisition backend and processing helper implementation.
 */

#include "radar.h"

#include <math.h>
#include <stdint.h>

#include "math_constants.h"
#include "stm32f4xx.h"
#include "stm32f429i_discovery.h"

#define RADAR_ADC_MAX_COUNT             4095U
#define RADAR_DEFAULT_ADC_CENTER_COUNT  2048.0f
#define RADAR_KLC5_WAVELENGTH_M         0.0124266f
#define RADAR_DEFAULT_OFFSET_ALPHA      0.05f
#define RADAR_DEFAULT_GAIN_ALPHA        0.05f
#define RADAR_DEFAULT_MIN_RADIUS_COUNT  20.0f
#define RADAR_DEFAULT_MIN_GAIN_RMS      2.0f
#define RADAR_DEFAULT_LOW_SIGNAL_RATIO  0.20f
#define RADAR_DEFAULT_CLIP_LOW_COUNT    8U
#define RADAR_DEFAULT_CLIP_HIGH_COUNT   4087U
#define RADAR_PHASE_MIN_GAIN            0.25f
#define RADAR_PHASE_MAX_GAIN            4.0f

const radar_phase_config_t RADAR_PHASE_CONFIG_DEFAULT = {
    .adc_center_count = RADAR_DEFAULT_ADC_CENTER_COUNT,
    .wavelength_m = RADAR_KLC5_WAVELENGTH_M,
    .offset_alpha = RADAR_DEFAULT_OFFSET_ALPHA,
    .gain_alpha = RADAR_DEFAULT_GAIN_ALPHA,
    .min_radius_counts = RADAR_DEFAULT_MIN_RADIUS_COUNT,
    .min_gain_rms_counts = RADAR_DEFAULT_MIN_GAIN_RMS,
    .max_low_signal_ratio = RADAR_DEFAULT_LOW_SIGNAL_RATIO,
    .clip_low_count = RADAR_DEFAULT_CLIP_LOW_COUNT,
    .clip_high_count = RADAR_DEFAULT_CLIP_HIGH_COUNT
};

static uint32_t radar_iq_buffer_ping[RADAR_FRAME_ADVANCE_SAMPLES];
static uint32_t radar_iq_buffer_pong[RADAR_FRAME_ADVANCE_SAMPLES];

static float32_t *radar_i_buffer_pointer = 0;
static float32_t *radar_q_buffer_pointer = 0;

static volatile uint8_t radar_data_ready = 0U;
static volatile uint8_t radar_completed_buffer_index = 0U;
static volatile uint32_t radar_dma_sequence = 0U;
static volatile uint32_t radar_overrun_count = 0U;
static volatile uint32_t radar_dma_error_count = 0U;

static void timer2_init_sample_rate(void);
static void adc_dual_dma_init(void);
static void unpack_iq_samples(const uint32_t *packed_buffer);
static float32_t radar_clampf(float32_t value, float32_t lo, float32_t hi);
static void radar_phase_sanitize_config(radar_phase_config_t *config);
static bool radar_get_spectrum_bin_window(float32_t display_hz,
                                          uint32_t *center_bin,
                                          uint32_t *start_bin,
                                          uint32_t *end_bin);

void DMA2_Stream0_IRQHandler(void);
void DMA2_Stream1_IRQHandler(void);
void DMA2_Stream5_IRQHandler(void);

HAL_StatusTypeDef radar_init(float32_t *i_channel_buffer,
                                   float32_t *q_channel_buffer, uint32_t size)
{
    GPIO_InitTypeDef gpio_init = {0};

    if ((i_channel_buffer == 0) || (q_channel_buffer == 0) || (size < RADAR_FRAME_ADVANCE_SAMPLES))
    {
        return HAL_ERROR;
    }

    radar_i_buffer_pointer = i_channel_buffer;
    radar_q_buffer_pointer = q_channel_buffer;

    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PC1 = ADC123_IN11 (I), PC3 = ADC123_IN13 (Q). */
    gpio_init.Pin = GPIO_PIN_1 | GPIO_PIN_3;
    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    timer2_init_sample_rate();
    adc_dual_dma_init();

    return HAL_OK;
}

void radar_start(void)
{
    /* Enable ADC2 first, then ADC1 (master in multimode). */
    ADC2->CR2 |= ADC_CR2_ADON;
    ADC1->CR2 |= ADC_CR2_ADON;

    /* Clear stale flags and start timer-triggered conversions. */
    radar_data_ready = 0;
    radar_completed_buffer_index = 0U;
    radar_dma_sequence = 0U;
    radar_overrun_count = 0U;
    radar_dma_error_count = 0U;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 |= TIM_CR1_CEN;
}

uint8_t radar_frame_ready(void)
{
    return radar_data_ready;
}

void radar_clear_frame_ready(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    radar_data_ready = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

uint8_t radar_get_latest_chunk(void)
{
    uint8_t completed_buffer_index;
    const uint32_t *completed_buffer;
    uint32_t primask;

    if ((radar_i_buffer_pointer == 0) || (radar_q_buffer_pointer == 0))
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if (radar_data_ready == 0U)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return 0U;
    }

    completed_buffer_index = radar_completed_buffer_index;
    radar_data_ready = 0U;

    if (primask == 0U)
    {
        __enable_irq();
    }

    completed_buffer = (completed_buffer_index == 0U) ?
            radar_iq_buffer_ping : radar_iq_buffer_pong;

    unpack_iq_samples(completed_buffer);

    return 1U;
}

uint32_t radar_get_dma_sequence(void)
{
    return radar_dma_sequence;
}

uint32_t radar_get_overrun_count(void)
{
    return radar_overrun_count;
}

uint32_t radar_get_dma_error_count(void)
{
    return radar_dma_error_count;
}

void radar_phase_init(radar_phase_state_t *state, const radar_phase_config_t *config)
{
    if (state == 0)
    {
        return;
    }

    if (config != 0)
    {
        state->cfg = *config;
    }
    else
    {
        state->cfg = RADAR_PHASE_CONFIG_DEFAULT;
    }

    radar_phase_sanitize_config(&state->cfg);
    radar_phase_reset(state);
}

void radar_phase_reset(radar_phase_state_t *state)
{
    if (state == 0)
    {
        return;
    }

    state->i_offset_counts = state->cfg.adc_center_count;
    state->q_offset_counts = state->cfg.adc_center_count;
    state->i_rms_counts = 0.0f;
    state->q_rms_counts = 0.0f;
    state->i_gain = 1.0f;
    state->q_gain = 1.0f;
    state->previous_phase_rad = 0.0f;
    state->unwrapped_phase_rad = 0.0f;
    state->reference_phase_rad = 0.0f;
    state->phase_initialized = 0U;
}

bool radar_phase_process_chunk(radar_phase_state_t *state,
                               const float32_t *i_counts,
                               const float32_t *q_counts,
                               float32_t *displacement_m,
                               uint32_t count,
                               radar_phase_quality_t *quality)
{
    radar_phase_quality_t local_quality = {0};
    float32_t i_sum = 0.0f;
    float32_t q_sum = 0.0f;
    float32_t i_square_sum = 0.0f;
    float32_t q_square_sum = 0.0f;
    float32_t radius_sum = 0.0f;
    float32_t phase_to_displacement;
    float32_t low_signal_limit;

    if ((state == 0) || (i_counts == 0) || (q_counts == 0) ||
            (displacement_m == 0) || (count == 0U))
    {
        return false;
    }

    local_quality.sample_count = count;

    for (uint32_t i = 0; i < count; i++)
    {
        float32_t i_raw = i_counts[i];
        float32_t q_raw = q_counts[i];

        i_sum += i_raw;
        q_sum += q_raw;

        if ((i_raw <= (float32_t)state->cfg.clip_low_count) ||
                (q_raw <= (float32_t)state->cfg.clip_low_count) ||
                (i_raw >= (float32_t)state->cfg.clip_high_count) ||
                (q_raw >= (float32_t)state->cfg.clip_high_count))
        {
            local_quality.clipped_sample_count++;
        }
    }

    {
        float32_t inv_count = 1.0f / (float32_t)count;
        float32_t i_mean = i_sum * inv_count;
        float32_t q_mean = q_sum * inv_count;

        state->i_offset_counts += state->cfg.offset_alpha *
                (i_mean - state->i_offset_counts);
        state->q_offset_counts += state->cfg.offset_alpha *
                (q_mean - state->q_offset_counts);
    }

    for (uint32_t i = 0; i < count; i++)
    {
        float32_t i_centered = i_counts[i] - state->i_offset_counts;
        float32_t q_centered = q_counts[i] - state->q_offset_counts;

        i_square_sum += i_centered * i_centered;
        q_square_sum += q_centered * q_centered;
    }

    {
        float32_t inv_count = 1.0f / (float32_t)count;
        float32_t i_rms = sqrtf(i_square_sum * inv_count);
        float32_t q_rms = sqrtf(q_square_sum * inv_count);

        if ((i_rms >= state->cfg.min_gain_rms_counts) &&
                (q_rms >= state->cfg.min_gain_rms_counts))
        {
            if ((state->i_rms_counts <= 0.0f) || (state->q_rms_counts <= 0.0f))
            {
                state->i_rms_counts = i_rms;
                state->q_rms_counts = q_rms;
            }
            else
            {
                state->i_rms_counts += state->cfg.gain_alpha *
                        (i_rms - state->i_rms_counts);
                state->q_rms_counts += state->cfg.gain_alpha *
                        (q_rms - state->q_rms_counts);
            }
        }
    }

    if ((state->i_rms_counts >= state->cfg.min_gain_rms_counts) &&
            (state->q_rms_counts >= state->cfg.min_gain_rms_counts))
    {
        float32_t mean_rms = 0.5f * (state->i_rms_counts + state->q_rms_counts);

        state->i_gain = radar_clampf(mean_rms / state->i_rms_counts,
                RADAR_PHASE_MIN_GAIN, RADAR_PHASE_MAX_GAIN);
        state->q_gain = radar_clampf(mean_rms / state->q_rms_counts,
                RADAR_PHASE_MIN_GAIN, RADAR_PHASE_MAX_GAIN);
    }
    else
    {
        state->i_gain = 1.0f;
        state->q_gain = 1.0f;
    }

    phase_to_displacement = state->cfg.wavelength_m / (4.0f * PM4_PI_F);

    for (uint32_t i = 0; i < count; i++)
    {
        float32_t i_corr = (i_counts[i] - state->i_offset_counts) * state->i_gain;
        float32_t q_corr = (q_counts[i] - state->q_offset_counts) * state->q_gain;
        float32_t radius = sqrtf((i_corr * i_corr) + (q_corr * q_corr));

        radius_sum += radius;

        if (radius < state->cfg.min_radius_counts)
        {
            local_quality.low_signal_sample_count++;
            displacement_m[i] = (state->unwrapped_phase_rad - state->reference_phase_rad) *
                    phase_to_displacement;
            continue;
        }

        {
            float32_t phase = atan2f(q_corr, i_corr);

            if (state->phase_initialized == 0U)
            {
                state->previous_phase_rad = phase;
                state->unwrapped_phase_rad = phase;
                state->reference_phase_rad = phase;
                state->phase_initialized = 1U;
            }
            else
            {
                float32_t delta = phase - state->previous_phase_rad;

                while (delta > PM4_PI_F)
                {
                    delta -= PM4_TWO_PI_F;
                }
                while (delta < -PM4_PI_F)
                {
                    delta += PM4_TWO_PI_F;
                }

                state->unwrapped_phase_rad += delta;
                state->previous_phase_rad = phase;
            }

            displacement_m[i] = (state->unwrapped_phase_rad - state->reference_phase_rad) *
                    phase_to_displacement;
            local_quality.valid_sample_count++;
        }
    }

    local_quality.mean_radius_counts = radius_sum / (float32_t)count;
    local_quality.i_offset_counts = state->i_offset_counts;
    local_quality.q_offset_counts = state->q_offset_counts;
    local_quality.i_gain = state->i_gain;
    local_quality.q_gain = state->q_gain;

    if (local_quality.clipped_sample_count != 0U)
    {
        local_quality.flags |= RADAR_PHASE_FLAG_CLIPPING;
    }

    low_signal_limit = state->cfg.max_low_signal_ratio * (float32_t)count;
    if ((float32_t)local_quality.low_signal_sample_count > low_signal_limit)
    {
        local_quality.flags |= RADAR_PHASE_FLAG_LOW_SIGNAL;
    }

    if ((local_quality.flags & (RADAR_PHASE_FLAG_CLIPPING | RADAR_PHASE_FLAG_LOW_SIGNAL)) == 0U)
    {
        local_quality.flags |= RADAR_PHASE_FLAG_VALID;
    }

    if (quality != 0)
    {
        *quality = local_quality;
    }

    return true;
}

bool radar_append_displacement_chunk(float32_t *displacement_history,
                                     const float32_t *displacement_acquired,
                                     uint32_t *window_fill_samples)
{
    uint32_t history_keep;

    if ((displacement_history == 0) || (displacement_acquired == 0) ||
            (window_fill_samples == 0))
    {
        return false;
    }

    history_keep = RADAR_CHANNEL_SAMPLES - RADAR_FRAME_ADVANCE_SAMPLES;

    for (uint32_t i = 0; i < history_keep; i++)
    {
        displacement_history[i] = displacement_history[i + RADAR_FRAME_ADVANCE_SAMPLES];
    }

    for (uint32_t i = 0; i < RADAR_FRAME_ADVANCE_SAMPLES; i++)
    {
        displacement_history[history_keep + i] = displacement_acquired[i];
    }

    if (*window_fill_samples < RADAR_CHANNEL_SAMPLES)
    {
        *window_fill_samples += RADAR_FRAME_ADVANCE_SAMPLES;
        if (*window_fill_samples > RADAR_CHANNEL_SAMPLES)
        {
            *window_fill_samples = RADAR_CHANNEL_SAMPLES;
        }
    }

    return (*window_fill_samples >= RADAR_CHANNEL_SAMPLES);
}

void radar_prepare_displacement_window(float32_t *displacement_samples,
                                       const float32_t *displacement_history)
{
    if ((displacement_samples == 0) || (displacement_history == 0))
    {
        return;
    }

    for (uint32_t i = 0; i < RADAR_CHANNEL_SAMPLES; i++)
    {
        displacement_samples[i] = displacement_history[i];
    }
}

bool radar_append_latest_chunk(float32_t *i_history, float32_t *q_history,
                               const float32_t *i_acquired, const float32_t *q_acquired,
                               uint32_t *window_fill_samples)
{
    /* Shift rolling history by half-window and append newest chunk at the end. */
    uint32_t history_keep;

    if ((i_history == 0) || (q_history == 0) || (i_acquired == 0) || (q_acquired == 0)
            || (window_fill_samples == 0))
    {
        return false;
    }

    history_keep = RADAR_CHANNEL_SAMPLES - RADAR_FRAME_ADVANCE_SAMPLES;

    for (uint32_t i = 0; i < history_keep; i++)
    {
        i_history[i] = i_history[i + RADAR_FRAME_ADVANCE_SAMPLES];
        q_history[i] = q_history[i + RADAR_FRAME_ADVANCE_SAMPLES];
    }

    for (uint32_t i = 0; i < RADAR_FRAME_ADVANCE_SAMPLES; i++)
    {
        i_history[history_keep + i] = i_acquired[i];
        q_history[history_keep + i] = q_acquired[i];
    }

    if (*window_fill_samples < RADAR_CHANNEL_SAMPLES)
    {
        *window_fill_samples += RADAR_FRAME_ADVANCE_SAMPLES;
        if (*window_fill_samples > RADAR_CHANNEL_SAMPLES)
        {
            *window_fill_samples = RADAR_CHANNEL_SAMPLES;
        }
    }

    return (*window_fill_samples >= RADAR_CHANNEL_SAMPLES);
}

void radar_prepare_processing_window(float32_t *i_samples, float32_t *q_samples,
                                     const float32_t *i_history, const float32_t *q_history,
                                     bool effect_active,
                                     const biquad_df2t_t *filter_l_bank,
                                     const biquad_df2t_t *filter_r_bank,
                                     uint8_t filter_index)
{
    /* Copy history to processing buffers, then optionally apply current filter. */
    if ((i_samples == 0) || (q_samples == 0) || (i_history == 0) || (q_history == 0))
    {
        return;
    }

    for (uint32_t i = 0; i < RADAR_CHANNEL_SAMPLES; i++)
    {
        i_samples[i] = i_history[i];
        q_samples[i] = q_history[i];
    }

    if (effect_active)
    {
        biquad_df2t_t filter_l;
        biquad_df2t_t filter_r;

        if ((filter_l_bank == 0) || (filter_r_bank == 0))
        {
            return;
        }

        filter_l = filter_l_bank[filter_index];
        filter_r = filter_r_bank[filter_index];

        biquad_reset(&filter_l);
        biquad_reset(&filter_r);

        biquad_process_buffer(&filter_l, i_samples, RADAR_CHANNEL_SAMPLES);
        biquad_process_buffer(&filter_r, q_samples, RADAR_CHANNEL_SAMPLES);
    }
}

uint32_t radar_get_recent_start_index(uint32_t count)
{
    if (count >= RADAR_CHANNEL_SAMPLES)
    {
        return 0U;
    }

    return RADAR_CHANNEL_SAMPLES - count;
}

void radar_get_time_scale(const float32_t *i_samples, const float32_t *q_samples,
                          uint32_t start_index, uint32_t count,
                          float32_t min_span, float32_t headroom_ratio,
                          float32_t *min_value, float32_t *max_value)
{
    float32_t min_sample;
    float32_t max_sample;
    float32_t span;
    float32_t center;
    float32_t headroom;

    if ((i_samples == 0) || (q_samples == 0) || (min_value == 0) || (max_value == 0)
            || (count == 0U))
    {
        return;
    }

    if (start_index >= RADAR_CHANNEL_SAMPLES)
    {
        return;
    }

    if (count > (RADAR_CHANNEL_SAMPLES - start_index))
    {
        count = RADAR_CHANNEL_SAMPLES - start_index;
    }

    min_sample = i_samples[start_index];
    max_sample = i_samples[start_index];

    for (uint32_t i = start_index; i < (start_index + count); i++)
    {
        if (i_samples[i] < min_sample)
        {
            min_sample = i_samples[i];
        }
        if (i_samples[i] > max_sample)
        {
            max_sample = i_samples[i];
        }
        if (q_samples[i] < min_sample)
        {
            min_sample = q_samples[i];
        }
        if (q_samples[i] > max_sample)
        {
            max_sample = q_samples[i];
        }
    }

    span = max_sample - min_sample;
    if (span < min_span)
    {
        span = min_span;
    }

    center = 0.5f * (max_sample + min_sample);
    headroom = span * headroom_ratio;

    *min_value = center - (0.5f * span) - headroom;
    *max_value = center + (0.5f * span) + headroom;
}

void radar_get_spectrum_window(const float32_t *spectrum_shifted,
                               float32_t display_hz,
                               float32_t min_display_max,
                               float32_t headroom_ratio,
                               uint32_t *start_index,
                               uint32_t *count,
                               float32_t *max_value)
{
    uint32_t local_start;
    uint32_t local_end;
    uint32_t local_count;
    float32_t peak = 0.0f;

    if ((spectrum_shifted == 0) || (start_index == 0) || (count == 0) || (max_value == 0))
    {
        return;
    }

    if (!radar_get_spectrum_bin_window(display_hz, 0, &local_start, &local_end))
    {
        return;
    }

    local_count = local_end - local_start;

    for (uint32_t i = 0; i < local_count; i++)
    {
        float32_t value = spectrum_shifted[local_start + i];
        if (value > peak)
        {
            peak = value;
        }
    }

    if (peak < min_display_max)
    {
        peak = min_display_max;
    }

    *start_index = local_start;
    *count = local_count;
    *max_value = peak * headroom_ratio;
}

void radar_update_peak_readout(const float32_t *spectrum_shifted,
                               float32_t display_hz,
                               float32_t valid_threshold,
                               float32_t dominance_ratio,
                               float32_t *pos_peak_hz,
                               float32_t *neg_peak_hz,
                               bool *pos_peak_valid,
                               bool *neg_peak_valid)
{
    uint32_t center_bin = 0U;
    uint32_t neg_start;
    uint32_t pos_end;
    uint32_t neg_peak_bin = 0U;
    uint32_t pos_peak_bin = 0U;
    float32_t neg_peak_mag = 0.0f;
    float32_t pos_peak_mag = 0.0f;
    float32_t bin_hz;

    if ((spectrum_shifted == 0) || (pos_peak_hz == 0) || (neg_peak_hz == 0)
            || (pos_peak_valid == 0) || (neg_peak_valid == 0))
    {
        return;
    }

    if (!radar_get_spectrum_bin_window(display_hz, &center_bin, &neg_start, &pos_end))
    {
        *neg_peak_valid = false;
        *pos_peak_valid = false;
        *neg_peak_hz = 0.0f;
        *pos_peak_hz = 0.0f;
        return;
    }

    neg_peak_bin = center_bin;
    pos_peak_bin = center_bin;

    bin_hz = (float32_t) RADAR_SAMPLE_RATE_HZ / (float32_t) RADAR_CHANNEL_SAMPLES;

    for (uint32_t i = neg_start; i < center_bin; i++)
    {
        float32_t value = spectrum_shifted[i];
        if (value > neg_peak_mag)
        {
            neg_peak_mag = value;
            neg_peak_bin = i;
        }
    }

    for (uint32_t i = center_bin + 1U; i < pos_end; i++)
    {
        float32_t value = spectrum_shifted[i];
        if (value > pos_peak_mag)
        {
            pos_peak_mag = value;
            pos_peak_bin = i;
        }
    }

    *neg_peak_valid = (neg_peak_mag > valid_threshold);
    *pos_peak_valid = (pos_peak_mag > valid_threshold);

    if (*neg_peak_valid)
    {
        *neg_peak_hz = ((float32_t) neg_peak_bin - (float32_t) center_bin) * bin_hz;
    }
    else
    {
        *neg_peak_hz = 0.0f;
    }

    if (*pos_peak_valid)
    {
        *pos_peak_hz = ((float32_t) pos_peak_bin - (float32_t) center_bin) * bin_hz;
    }
    else
    {
        *pos_peak_hz = 0.0f;
    }

    if (*neg_peak_valid && *pos_peak_valid)
    {
        if (neg_peak_mag >= (dominance_ratio * pos_peak_mag))
        {
            *pos_peak_valid = false;
            *pos_peak_hz = 0.0f;
        }
        else if (pos_peak_mag >= (dominance_ratio * neg_peak_mag))
        {
            *neg_peak_valid = false;
            *neg_peak_hz = 0.0f;
        }
    }
}

static float32_t radar_clampf(float32_t value, float32_t lo, float32_t hi)
{
    if (value < lo)
    {
        return lo;
    }
    if (value > hi)
    {
        return hi;
    }
    return value;
}

static void radar_phase_sanitize_config(radar_phase_config_t *config)
{
    if (config == 0)
    {
        return;
    }

    if ((config->adc_center_count < 0.0f) ||
            (config->adc_center_count > (float32_t)RADAR_ADC_MAX_COUNT))
    {
        config->adc_center_count = RADAR_DEFAULT_ADC_CENTER_COUNT;
    }
    if (config->wavelength_m <= 0.0f)
    {
        config->wavelength_m = RADAR_KLC5_WAVELENGTH_M;
    }

    config->offset_alpha = radar_clampf(config->offset_alpha, 0.0f, 1.0f);
    config->gain_alpha = radar_clampf(config->gain_alpha, 0.0f, 1.0f);

    if (config->min_radius_counts < 0.0f)
    {
        config->min_radius_counts = RADAR_DEFAULT_MIN_RADIUS_COUNT;
    }
    if (config->min_gain_rms_counts <= 0.0f)
    {
        config->min_gain_rms_counts = RADAR_DEFAULT_MIN_GAIN_RMS;
    }

    config->max_low_signal_ratio = radar_clampf(config->max_low_signal_ratio, 0.0f, 1.0f);

    if ((config->clip_high_count > RADAR_ADC_MAX_COUNT) ||
            (config->clip_low_count >= config->clip_high_count))
    {
        config->clip_low_count = RADAR_DEFAULT_CLIP_LOW_COUNT;
        config->clip_high_count = RADAR_DEFAULT_CLIP_HIGH_COUNT;
    }
}

static bool radar_get_spectrum_bin_window(float32_t display_hz,
                                          uint32_t *center_bin,
                                          uint32_t *start_bin,
                                          uint32_t *end_bin)
{
    /* Build a symmetric frequency window around DC (shifted spectrum center). */
    uint32_t local_center = RADAR_CHANNEL_SAMPLES / 2U;
    uint32_t half_bins;
    float32_t bin_hz;

    if ((start_bin == 0) || (end_bin == 0) || (local_center == 0U))
    {
        return false;
    }

    bin_hz = (float32_t) RADAR_SAMPLE_RATE_HZ / (float32_t) RADAR_CHANNEL_SAMPLES;
    half_bins = (uint32_t)(display_hz / bin_hz);
    if (((float32_t)half_bins * bin_hz) < display_hz)
    {
        half_bins++;
    }
    if (half_bins >= local_center)
    {
        half_bins = local_center - 1U;
    }

    if (center_bin != 0)
    {
        *center_bin = local_center;
    }
    *start_bin = local_center - half_bins;
    *end_bin = local_center + half_bins + 1U;

    return true;
}

static void timer2_init_sample_rate(void)
{
    uint32_t ticks_per_sample;

    __HAL_RCC_TIM2_CLK_ENABLE();

    TIM2->CR1 = 0;
    TIM2->CR2 = 0;
    TIM2->PSC = BOARD_TIM_APB1_PSC_1MHZ;

    ticks_per_sample = ((uint32_t)BOARD_TIM_APB1_TICK_HZ + (RADAR_SAMPLE_RATE_HZ / 2U))
            / RADAR_SAMPLE_RATE_HZ;
    if (ticks_per_sample == 0U)
    {
        ticks_per_sample = 1U;
    }
    TIM2->ARR = ticks_per_sample - 1U;

    TIM2->CNT = 0;

    /* TRGO on update event. */
    TIM2->CR2 |= TIM_CR2_MMS_1;
    TIM2->EGR = TIM_EGR_UG;
}

static void adc_dual_dma_init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC2_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* Disable ADCs before configuration. */
    ADC1->CR2 &= ~ADC_CR2_ADON;
    ADC2->CR2 &= ~ADC_CR2_ADON;

    /* Disable DMA stream before reconfiguration. */
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream0->CR & DMA_SxCR_EN)
    {
    }

    DMA2->LIFCR = DMA_LIFCR_CFEIF0 | DMA_LIFCR_CDMEIF0 |
                  DMA_LIFCR_CTEIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTCIF0;

    /* ADC common: dual regular simultaneous mode, DMA access mode 2. */
    ADC->CCR = 0;
    ADC->CCR |= ADC_CCR_ADCPRE_0 | ADC_CCR_ADCPRE_1; /* PCLK2/8 */
    ADC->CCR |= ADC_CCR_MULTI_0;                     /* Regular simultaneous mode */
    ADC->CCR |= ADC_CCR_DMA_1;                       /* DMA mode 2 */
    ADC->CCR |= ADC_CCR_DDS;                         /* DMA requests issued continuously */

    /* ADC1 = I channel (PC1 / IN11). */
    ADC1->CR1 = 0;
    ADC1->CR2 = 0;
    ADC1->SMPR1 &= ~ADC_SMPR1_SMP11_Msk;
    ADC1->SMPR1 |= (5UL << ADC_SMPR1_SMP11_Pos);     /* 84 cycles sample time */
    ADC1->SQR1 = 0;
    ADC1->SQR3 = 11U;
    ADC1->CR2 |= ADC_CR2_EXTEN_0;                    /* Trigger on rising edge */
    ADC1->CR2 |= ADC_CR2_EXTSEL_1 | ADC_CR2_EXTSEL_2; /* TIM2_TRGO */
    ADC1->CR2 |= ADC_CR2_DMA;

    /* ADC2 = Q channel (PC3 / IN13). */
    ADC2->CR1 = 0;
    ADC2->CR2 = 0;
    ADC2->SMPR1 &= ~ADC_SMPR1_SMP13_Msk;
    ADC2->SMPR1 |= (5UL << ADC_SMPR1_SMP13_Pos);
    ADC2->SQR1 = 0;
    ADC2->SQR3 = 13U;
    ADC2->CR2 |= ADC_CR2_EXTEN_0;
    ADC2->CR2 |= ADC_CR2_EXTSEL_1 | ADC_CR2_EXTSEL_2;

    /* DMA2 Stream0 Channel0 reads packed ADC_CDR values. */
    DMA2_Stream0->CR = 0;
    DMA2_Stream0->CR |= (0UL << DMA_SxCR_CHSEL_Pos); /* Channel 0 */
    DMA2_Stream0->CR |= DMA_SxCR_PL_1;               /* High priority */
    DMA2_Stream0->CR |= DMA_SxCR_MSIZE_1;            /* Memory 32-bit */
    DMA2_Stream0->CR |= DMA_SxCR_PSIZE_1;            /* Peripheral 32-bit */
    DMA2_Stream0->CR |= DMA_SxCR_MINC;
    DMA2_Stream0->CR |= DMA_SxCR_CIRC;
    DMA2_Stream0->CR |= DMA_SxCR_DBM;
    DMA2_Stream0->CR |= DMA_SxCR_TCIE;

    DMA2_Stream0->NDTR = RADAR_FRAME_ADVANCE_SAMPLES;
    DMA2_Stream0->PAR = (uint32_t)&(ADC->CDR);
    DMA2_Stream0->M0AR = (uint32_t)radar_iq_buffer_ping;
    DMA2_Stream0->M1AR = (uint32_t)radar_iq_buffer_pong;

    DMA2_Stream0->CR |= DMA_SxCR_EN;

    NVIC_SetPriority(DMA2_Stream0_IRQn, 1);
    NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

static void unpack_iq_samples(const uint32_t *packed_buffer)
{
    if ((packed_buffer == 0) || (radar_i_buffer_pointer == 0) || (radar_q_buffer_pointer == 0))
    {
        return;
    }

    for (uint32_t i = 0; i < RADAR_FRAME_ADVANCE_SAMPLES; i++)
    {
        uint32_t pair = packed_buffer[i];

        /* CDR layout in dual regular mode: [31:16]=ADC2 (Q), [15:0]=ADC1 (I). */
        radar_i_buffer_pointer[i] = (float32_t)(pair & 0xFFFFU);
        radar_q_buffer_pointer[i] = (float32_t)((pair >> 16) & 0xFFFFU);
    }
}

void DMA2_Stream0_IRQHandler(void)
{
    uint32_t lisr = DMA2->LISR;

    if ((lisr & (DMA_LISR_FEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_TEIF0)) != 0U)
    {
        radar_dma_error_count++;
    }

    DMA2->LIFCR = DMA_LIFCR_CFEIF0 | DMA_LIFCR_CDMEIF0 |
                  DMA_LIFCR_CTEIF0 | DMA_LIFCR_CHTIF0;

    if ((lisr & DMA_LISR_TCIF0) != 0U)
    {
        uint8_t completed_buffer_index;

        DMA2->LIFCR = DMA_LIFCR_CTCIF0;

        if ((DMA2_Stream0->CR & DMA_SxCR_CT) == 0U)
        {
            completed_buffer_index = 1U;
        }
        else
        {
            completed_buffer_index = 0U;
        }

        if (radar_data_ready != 0U)
        {
            radar_overrun_count++;
        }

        radar_completed_buffer_index = completed_buffer_index;
        radar_dma_sequence++;
        radar_data_ready = 1U;
    }
}

/* Legacy handlers kept to preserve vector symbol compatibility. */
void DMA2_Stream1_IRQHandler(void)
{
}

void DMA2_Stream5_IRQHandler(void)
{
}
