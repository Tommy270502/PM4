/**
 * @file ekg.c
 * @brief AD8232 acquisition and ECG signal processing on STM32F429
 * @author Thomas Perri, perritho@students.zhaw.ch
 * @date 2026-03-26
 */

#include "ekg.h"
#include "math_constants.h"

#include "stm32f4xx.h"

#define EKG_ADC_TIMEOUT_LOOPS    (1000000U)
#define EKG_WARMUP_SECONDS       (2U)
#define EKG_MIN_THRESHOLD        (1.0e-8f)

const ekg_config_t EKG_CONFIG_DEFAULT = {
    .sample_rate_hz = EKG_DEFAULT_FS_HZ,
    .highpass_hz = EKG_DEFAULT_HP_HZ,
    .lowpass_hz = EKG_DEFAULT_LP_HZ,
    .envelope_hz = EKG_DEFAULT_ENV_HZ,
    .refractory_s = 0.250f,
    .min_rr_s = 0.300f,
    .max_rr_s = 2.000f
};

static ekg_config_t g_cfg = {
    .sample_rate_hz = EKG_DEFAULT_FS_HZ,
    .highpass_hz = EKG_DEFAULT_HP_HZ,
    .lowpass_hz = EKG_DEFAULT_LP_HZ,
    .envelope_hz = EKG_DEFAULT_ENV_HZ,
    .refractory_s = 0.250f,
    .min_rr_s = 0.300f,
    .max_rr_s = 2.000f
};

static uint8_t g_hw_initialized = 0U;
static volatile uint8_t g_sampling_enabled = 0U;

/* Latest interrupt-acquired sample and status flags. */
static volatile uint16_t g_irq_latest_raw = 0U;
static volatile uint8_t g_irq_sample_ready = 0U;
static volatile uint32_t g_irq_overrun_count = 0U;

/* Filter coefficients (first-order HP + LP + envelope LP). */
static float g_alpha_hp = 0.0f;
static float g_alpha_lp = 0.0f;
static float g_alpha_env = 0.0f;

/* Filter state. */
static float g_prev_x = 0.0f;
static float g_hp_y = 0.0f;
static float g_lp_y = 0.0f;
static float g_prev_lp = 0.0f;
static float g_env = 0.0f;

/* Adaptive detection state. */
static float g_noise_level = 1.0e-6f;
static float g_signal_level = 5.0e-6f;
static float g_threshold = 2.0e-6f;
static uint8_t g_prev_above_threshold = 0U;

/* Beat timing state. */
static uint32_t g_sample_index = 0U;
static uint32_t g_last_accepted_peak = 0U;
static uint32_t g_refractory_samples = 1U;
static uint32_t g_min_rr_samples = 1U;
static uint32_t g_max_rr_samples = 1U;
static uint32_t g_warmup_samples = EKG_DEFAULT_FS_HZ * EKG_WARMUP_SECONDS;

static float g_latest_bpm = 0.0f;
static uint8_t g_bpm_valid = 0U;

static float clampf(float value, float lo, float hi)
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

static void ekg_timer3_set_rate(uint32_t sample_rate_hz)
{
    uint32_t ticks_per_sample;

    if (sample_rate_hz == 0U)
    {
        sample_rate_hz = EKG_DEFAULT_FS_HZ;
    }

    ticks_per_sample = (BOARD_TIM_APB1_TICK_HZ + (sample_rate_hz / 2U)) / sample_rate_hz;
    if (ticks_per_sample == 0U)
    {
        ticks_per_sample = 1U;
    }
    if (ticks_per_sample > 65536U)
    {
        ticks_per_sample = 65536U;
    }

    TIM3->PSC = BOARD_TIM_APB1_PSC_1MHZ;
    TIM3->ARR = ticks_per_sample - 1U;
    TIM3->CNT = 0U;
    TIM3->EGR = TIM_EGR_UG;
}

static void ekg_timer3_init(uint32_t sample_rate_hz)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;

    TIM3->CR1 = 0U;
    TIM3->CR2 = 0U;
    TIM3->SMCR = 0U;
    TIM3->DIER = TIM_DIER_UIE;

    ekg_timer3_set_rate(sample_rate_hz);

    TIM3->SR = 0U;
}

static void ekg_adc3_pf6_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN;
    (void)RCC->AHB1ENR;

    /* PF6 -> analog mode, no pull-up/down. */
    GPIOF->MODER &= ~(3UL << GPIO_MODER_MODER6_Pos);
    GPIOF->MODER |= (3UL << GPIO_MODER_MODER6_Pos);
    GPIOF->PUPDR &= ~(3UL << GPIO_PUPDR_PUPD6_Pos);
    GPIOF->OTYPER &= ~GPIO_OTYPER_OT6;
    GPIOF->OSPEEDR &= ~(3UL << GPIO_OSPEEDR_OSPEED6_Pos);

    RCC->APB2ENR |= RCC_APB2ENR_ADC3EN;
    (void)RCC->APB2ENR;

    /* ADC common prescaler PCLK2/8 for robust ADC clock margin. */
    ADC->CCR &= ~ADC_CCR_ADCPRE_Msk;
    ADC->CCR |= ADC_CCR_ADCPRE_0 | ADC_CCR_ADCPRE_1;

    ADC3->CR1 = ADC_CR1_EOCIE | ADC_CR1_OVRIE;
    ADC3->CR2 = 0U;
    ADC3->CR2 |= ADC_CR2_EOCS;

    /* Channel 4 sample time = 84 cycles. */
    ADC3->SMPR2 &= ~ADC_SMPR2_SMP4_Msk;
    ADC3->SMPR2 |= (5UL << ADC_SMPR2_SMP4_Pos);

    /* Regular sequence length 1, first conversion = CH4 (PF6). */
    ADC3->SQR1 = 0U;
    ADC3->SQR2 = 0U;
    ADC3->SQR3 = 4U;

    ADC3->SR = 0U;

    /* Enable ADC3. */
    ADC3->CR2 |= ADC_CR2_ADON;
}

static void ekg_irq_init(void)
{
    NVIC_SetPriority(TIM3_IRQn, 3U);
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(TIM3_IRQn);

    NVIC_SetPriority(ADC_IRQn, 2U);
    NVIC_ClearPendingIRQ(ADC_IRQn);
    NVIC_EnableIRQ(ADC_IRQn);
}

static void ekg_update_coefficients(void)
{
    float fs = (float)g_cfg.sample_rate_hz;
    float dt;
    float tau;

    if (g_cfg.sample_rate_hz == 0U)
    {
        g_cfg.sample_rate_hz = EKG_DEFAULT_FS_HZ;
        fs = (float)g_cfg.sample_rate_hz;
    }

    g_cfg.highpass_hz = clampf(g_cfg.highpass_hz, 0.05f, 5.0f);
    g_cfg.lowpass_hz = clampf(g_cfg.lowpass_hz, 5.0f, 100.0f);
    g_cfg.envelope_hz = clampf(g_cfg.envelope_hz, 1.0f, 30.0f);

    if (g_cfg.lowpass_hz <= g_cfg.highpass_hz)
    {
        g_cfg.lowpass_hz = g_cfg.highpass_hz + 2.0f;
    }

    g_cfg.refractory_s = clampf(g_cfg.refractory_s, 0.12f, 0.60f);
    g_cfg.min_rr_s = clampf(g_cfg.min_rr_s, 0.25f, 1.50f);
    g_cfg.max_rr_s = clampf(g_cfg.max_rr_s, 0.60f, 3.00f);

    if (g_cfg.max_rr_s < g_cfg.min_rr_s)
    {
        float tmp = g_cfg.max_rr_s;
        g_cfg.max_rr_s = g_cfg.min_rr_s;
        g_cfg.min_rr_s = tmp;
    }

    dt = 1.0f / fs;

    /* HP: y[n] = a * (y[n-1] + x[n] - x[n-1]) */
    tau = 1.0f / (PM4_TWO_PI_F * g_cfg.highpass_hz);
    g_alpha_hp = tau / (tau + dt);

    /* LP: y[n] = y[n-1] + a * (x[n] - y[n-1]) */
    tau = 1.0f / (PM4_TWO_PI_F * g_cfg.lowpass_hz);
    g_alpha_lp = dt / (tau + dt);

    /* Envelope LP (smoothed squared derivative). */
    tau = 1.0f / (PM4_TWO_PI_F * g_cfg.envelope_hz);
    g_alpha_env = dt / (tau + dt);

    g_refractory_samples = (uint32_t)(g_cfg.refractory_s * fs + 0.5f);
    g_min_rr_samples = (uint32_t)(g_cfg.min_rr_s * fs + 0.5f);
    g_max_rr_samples = (uint32_t)(g_cfg.max_rr_s * fs + 0.5f);
    g_warmup_samples = g_cfg.sample_rate_hz * EKG_WARMUP_SECONDS;

    if (g_refractory_samples == 0U)
    {
        g_refractory_samples = 1U;
    }
    if (g_min_rr_samples == 0U)
    {
        g_min_rr_samples = 1U;
    }
    if (g_max_rr_samples < g_min_rr_samples)
    {
        g_max_rr_samples = g_min_rr_samples;
    }

    if (g_hw_initialized != 0U)
    {
        ekg_timer3_set_rate(g_cfg.sample_rate_hz);
    }
}

void ekg_reset_processing(void)
{
    g_prev_x = 0.0f;
    g_hp_y = 0.0f;
    g_lp_y = 0.0f;
    g_prev_lp = 0.0f;
    g_env = 0.0f;

    g_noise_level = 1.0e-6f;
    g_signal_level = 5.0e-6f;
    g_threshold = 2.0e-6f;
    g_prev_above_threshold = 0U;

    g_sample_index = 0U;
    g_last_accepted_peak = 0U;

    g_latest_bpm = 0.0f;
    g_bpm_valid = 0U;
}

void ekg_set_config(const ekg_config_t *config)
{
    if (config != 0)
    {
        g_cfg = *config;
    }
    else
    {
        g_cfg = EKG_CONFIG_DEFAULT;
    }

    ekg_update_coefficients();
    ekg_reset_processing();
}

void ekg_start(void)
{
    uint32_t primask = __get_PRIMASK();

    if (g_hw_initialized == 0U)
    {
        return;
    }

    __disable_irq();
    g_irq_sample_ready = 0U;
    g_sampling_enabled = 1U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    ADC3->SR = 0U;
    TIM3->SR = 0U;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->CR1 |= TIM_CR1_CEN;
}

void ekg_stop(void)
{
    TIM3->CR1 &= ~TIM_CR1_CEN;
    g_sampling_enabled = 0U;
}

void ekg_init(const ekg_config_t *config)
{
    if (g_hw_initialized == 0U)
    {
        ekg_adc3_pf6_init();
        ekg_timer3_init(EKG_DEFAULT_FS_HZ);
        ekg_irq_init();
        g_hw_initialized = 1U;
    }

    g_irq_overrun_count = 0U;
    g_irq_sample_ready = 0U;
    g_irq_latest_raw = 0U;

    ekg_set_config(config);
    ekg_start();
}

uint16_t ekg_read_raw_blocking(void)
{
    uint32_t timeout = EKG_ADC_TIMEOUT_LOOPS;

    ADC3->SR &= ~ADC_SR_EOC;
    ADC3->CR2 |= ADC_CR2_SWSTART;

    while ((ADC3->SR & ADC_SR_EOC) == 0U)
    {
        if (timeout == 0U)
        {
            return 0U;
        }
        timeout--;
    }

    return (uint16_t)(ADC3->DR & EKG_ADC_MAX_COUNT);
}

float ekg_raw_to_voltage(uint16_t raw)
{
    return ((float)raw * EKG_ADC_REF_VOLTAGE) / (float)EKG_ADC_MAX_COUNT;
}

void ekg_process_raw(uint16_t raw, ekg_output_t *out)
{
    float x = ekg_raw_to_voltage(raw);
    float bp;
    float diff;
    float sq;
    float fs = (float)g_cfg.sample_rate_hz;
    uint8_t r_peak = 0U;

    /* Band-pass chain = high-pass then low-pass. */
    g_hp_y = g_alpha_hp * (g_hp_y + x - g_prev_x);
    g_prev_x = x;

    g_lp_y = g_lp_y + g_alpha_lp * (g_hp_y - g_lp_y);
    bp = g_lp_y;

    /* QRS emphasis. */
    diff = bp - g_prev_lp;
    g_prev_lp = bp;
    sq = diff * diff;
    g_env = g_env + g_alpha_env * (sq - g_env);

    if (g_sample_index < g_warmup_samples)
    {
        /* Startup: learn baseline noise to stabilize threshold. */
        g_noise_level = 0.99f * g_noise_level + 0.01f * g_env;
        g_threshold = g_noise_level * 4.0f;
    }
    else
    {
        uint8_t above = (g_env > g_threshold) ? 1U : 0U;

        if ((above != 0U) && (g_prev_above_threshold == 0U))
        {
            uint32_t samples_since_peak = g_sample_index - g_last_accepted_peak;

            if ((g_last_accepted_peak == 0U) || (samples_since_peak > g_refractory_samples))
            {
                if ((g_last_accepted_peak == 0U) ||
                    ((samples_since_peak >= g_min_rr_samples) && (samples_since_peak <= g_max_rr_samples)))
                {
                    r_peak = 1U;

                    if (g_last_accepted_peak != 0U)
                    {
                        float inst_bpm = PM4_BPM_PER_HZ * fs / (float)samples_since_peak;

                        if (g_bpm_valid != 0U)
                        {
                            g_latest_bpm = 0.80f * g_latest_bpm + 0.20f * inst_bpm;
                        }
                        else
                        {
                            g_latest_bpm = inst_bpm;
                            g_bpm_valid = 1U;
                        }
                    }

                    g_last_accepted_peak = g_sample_index;
                    g_signal_level = 0.875f * g_signal_level + 0.125f * g_env;
                }
                else
                {
                    g_noise_level = 0.875f * g_noise_level + 0.125f * g_env;
                }
            }
            else
            {
                g_noise_level = 0.875f * g_noise_level + 0.125f * g_env;
            }
        }
        else
        {
            g_noise_level = 0.95f * g_noise_level + 0.05f * g_env;
        }

        g_threshold = g_noise_level + 0.25f * (g_signal_level - g_noise_level);
        if (g_threshold < EKG_MIN_THRESHOLD)
        {
            g_threshold = EKG_MIN_THRESHOLD;
        }

        g_prev_above_threshold = above;
    }

    g_sample_index++;

    if (out != 0)
    {
        out->raw = raw;
        out->voltage = x;
        out->bandpassed = bp;
        out->envelope = g_env;
        out->threshold = g_threshold;
        out->r_peak = r_peak;
        out->bpm = (g_bpm_valid != 0U) ? g_latest_bpm : 0.0f;
        out->bpm_valid = g_bpm_valid;
    }
}

void ekg_read_and_process(ekg_output_t *out)
{
    uint16_t raw = ekg_read_raw_blocking();
    ekg_process_raw(raw, out);
}

uint8_t ekg_sample_ready(void)
{
    return g_irq_sample_ready;
}

uint8_t ekg_get_latest_raw_sample(uint16_t *raw)
{
    uint32_t primask;

    if (raw == 0)
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if (g_irq_sample_ready == 0U)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return 0U;
    }

    *raw = g_irq_latest_raw;
    g_irq_sample_ready = 0U;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return 1U;
}

uint8_t ekg_process_if_ready(ekg_output_t *out)
{
    uint16_t raw;

    if (ekg_get_latest_raw_sample(&raw) == 0U)
    {
        return 0U;
    }

    ekg_process_raw(raw, out);
    return 1U;
}

uint32_t ekg_get_overrun_count(void)
{
    return g_irq_overrun_count;
}

float ekg_get_latest_bpm(void)
{
    if (g_bpm_valid != 0U)
    {
        return g_latest_bpm;
    }
    return 0.0f;
}

uint8_t ekg_has_valid_bpm(void)
{
    return g_bpm_valid;
}

void TIM3_IRQHandler(void)
{
    if ((TIM3->SR & TIM_SR_UIF) != 0U)
    {
        TIM3->SR &= ~TIM_SR_UIF;

        if (g_sampling_enabled != 0U)
        {
            ADC3->CR2 |= ADC_CR2_SWSTART;
        }
    }
}

void ADC_IRQHandler(void)
{
    uint32_t sr = ADC3->SR;

    if ((sr & ADC_SR_EOC) != 0U)
    {
        uint16_t sample = (uint16_t)(ADC3->DR & EKG_ADC_MAX_COUNT);

        if (g_irq_sample_ready != 0U)
        {
            g_irq_overrun_count++;
        }

        g_irq_latest_raw = sample;
        g_irq_sample_ready = 1U;
    }
    else if ((sr & ADC_SR_OVR) != 0U)
    {
        (void)ADC3->DR;
        g_irq_overrun_count++;
    }
}
