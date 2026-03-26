/** ***************************************************************************
 * @file
 * @brief Heart-rate extraction from radar I/Q frames.
 *****************************************************************************/

#include "hr.h"

static float32_t g_cfft_inout[2U * FFT_SIZE];
static float32_t g_magnitude[FFT_SIZE];
static bool g_hr_initialized = false;

#define HR_SELFTEST_CASES        3U
#define HR_SELFTEST_BIN_TOL      1U
#define HR_SYNTH_DC_OFFSET       2048.0f
#define HR_SYNTH_AMPLITUDE       600.0f
#define HR_TWO_PI                6.28318530718f

static uint16_t HR_frequency_to_bin(float frequency_hz, uint16_t sample_rate_hz, uint16_t fft_size)
{
    float bin_f = (frequency_hz * (float)fft_size) / (float)sample_rate_hz;
    if (bin_f < 0.0f) {
        return 0U;
    }
    if (bin_f > (float)(fft_size - 1U)) {
        return (uint16_t)(fft_size - 1U);
    }
    return (uint16_t)(bin_f + 0.5f);
}

static uint16_t HR_clamp_to_u12(float value)
{
    if (value < 0.0f) {
        return 0U;
    }
    if (value > 4095.0f) {
        return 4095U;
    }
    return (uint16_t)(value + 0.5f);
}

static void HR_fill_synthetic_frame(MEAS_RadarFrame_t *frame, uint16_t tone_bin)
{
    uint32_t n;

    frame->sample_count = FFT_SIZE;
    frame->sample_rate_hz = MEAS_SAMPLE_RATE_HZ;
    frame->clip_i = false;
    frame->clip_q = false;
    frame->dma_overrun = false;

    for (n = 0U; n < FFT_SIZE; n++) {
        float phase = (HR_TWO_PI * (float)tone_bin * (float)n) / (float)FFT_SIZE;
        float i_sample = HR_SYNTH_DC_OFFSET + HR_SYNTH_AMPLITUDE * arm_cos_f32(phase);
        float q_sample = HR_SYNTH_DC_OFFSET + HR_SYNTH_AMPLITUDE * arm_sin_f32(phase);
        frame->raw_i[n] = HR_clamp_to_u12(i_sample);
        frame->raw_q[n] = HR_clamp_to_u12(q_sample);
    }
}

HR_Status_t HR_init(void)
{
    if (FFT_init() != FFT_STATUS_OK) {
        g_hr_initialized = false;
        return HR_STATUS_FFT_ERROR;
    }

    g_hr_initialized = true;
    return HR_STATUS_OK;
}

HR_Status_t HR_process_radar_frame(const MEAS_RadarFrame_t *frame, HR_Result_t *result)
{
    uint32_t i;
    float mean_i = 0.0f;
    float mean_q = 0.0f;
    uint16_t start_bin;
    uint16_t end_bin;
    uint16_t peak_bin;
    float32_t peak_value;
    float bin_hz;

    if ((frame == 0) || (result == 0)) {
        return HR_STATUS_INVALID_ARG;
    }

    result->bpm = 0.0f;
    result->peak_hz = 0.0f;
    result->frame_id = frame->frame_id;
    result->valid = false;
    result->clipped = (bool)(frame->clip_i || frame->clip_q);

    if (!g_hr_initialized) {
        return HR_STATUS_FFT_ERROR;
    }

    if ((frame->sample_count != FFT_SIZE) || (frame->sample_rate_hz == 0U)) {
        return HR_STATUS_INVALID_ARG;
    }

    for (i = 0U; i < frame->sample_count; i++) {
        mean_i += (float)frame->raw_i[i];
        mean_q += (float)frame->raw_q[i];
    }
    mean_i /= (float)frame->sample_count;
    mean_q /= (float)frame->sample_count;

    for (i = 0U; i < frame->sample_count; i++) {
        g_cfft_inout[2U * i] = (float32_t)((float)frame->raw_i[i] - mean_i);
        g_cfft_inout[2U * i + 1U] = (float32_t)((float)frame->raw_q[i] - mean_q);
    }

    if (FFT_run_cfft(g_cfft_inout, FFT_SIZE) != FFT_STATUS_OK) {
        return HR_STATUS_FFT_ERROR;
    }

    if (FFT_compute_magnitude(g_cfft_inout, g_magnitude, FFT_SIZE) != FFT_STATUS_OK) {
        return HR_STATUS_FFT_ERROR;
    }

    start_bin = HR_frequency_to_bin(HR_BAND_MIN_HZ, frame->sample_rate_hz, FFT_SIZE);
    end_bin = HR_frequency_to_bin(HR_BAND_MAX_HZ, frame->sample_rate_hz, FFT_SIZE);

    if (end_bin <= start_bin) {
        return HR_STATUS_NO_PEAK;
    }

    if (FFT_find_peak_in_bin_range(g_magnitude, start_bin, end_bin, &peak_bin, &peak_value) != FFT_STATUS_OK) {
        return HR_STATUS_FFT_ERROR;
    }

    if (peak_value <= 0.0f) {
        return HR_STATUS_NO_PEAK;
    }

    bin_hz = (float)frame->sample_rate_hz / (float)FFT_SIZE;
    result->peak_hz = (float)peak_bin * bin_hz;
    result->bpm = result->peak_hz * 60.0f;
    result->valid = !frame->dma_overrun;

    return HR_STATUS_OK;
}

HR_Status_t HR_run_self_test(HR_SelfTestResult_t *self_test_result)
{
    static const float test_freq_hz[HR_SELFTEST_CASES] = {1.0f, 1.6f, 2.4f};
    MEAS_RadarFrame_t frame;
    HR_Result_t result;
    HR_Status_t status;
    uint16_t i;

    if (self_test_result == 0) {
        return HR_STATUS_INVALID_ARG;
    }

    self_test_result->passed_all = true;
    self_test_result->total_tests = HR_SELFTEST_CASES;
    self_test_result->passed_tests = 0U;
    self_test_result->first_failed_test = 0xFFFFU;
    self_test_result->expected_bin = 0U;
    self_test_result->detected_bin = 0U;
    self_test_result->expected_peak_hz = 0.0f;
    self_test_result->detected_peak_hz = 0.0f;
    self_test_result->last_status = HR_STATUS_OK;

    if (!g_hr_initialized) {
        self_test_result->passed_all = false;
        self_test_result->last_status = HR_STATUS_FFT_ERROR;
        return HR_STATUS_FFT_ERROR;
    }

    for (i = 0U; i < HR_SELFTEST_CASES; i++) {
        uint16_t expected_bin;
        uint16_t detected_bin;
        uint16_t delta;

        expected_bin = HR_frequency_to_bin(test_freq_hz[i], MEAS_SAMPLE_RATE_HZ, FFT_SIZE);
        HR_fill_synthetic_frame(&frame, expected_bin);
        frame.frame_id = (uint32_t)(1000U + i);

        status = HR_process_radar_frame(&frame, &result);
        self_test_result->last_status = status;
        if (status != HR_STATUS_OK) {
            self_test_result->passed_all = false;
            if (self_test_result->first_failed_test == 0xFFFFU) {
                self_test_result->first_failed_test = i;
                self_test_result->expected_bin = expected_bin;
                self_test_result->expected_peak_hz = test_freq_hz[i];
            }
            continue;
        }

        detected_bin = HR_frequency_to_bin(result.peak_hz, MEAS_SAMPLE_RATE_HZ, FFT_SIZE);
        delta = (detected_bin > expected_bin) ? (detected_bin - expected_bin) : (expected_bin - detected_bin);

        if (delta <= HR_SELFTEST_BIN_TOL) {
            self_test_result->passed_tests++;
        } else {
            self_test_result->passed_all = false;
            if (self_test_result->first_failed_test == 0xFFFFU) {
                self_test_result->first_failed_test = i;
                self_test_result->expected_bin = expected_bin;
                self_test_result->detected_bin = detected_bin;
                self_test_result->expected_peak_hz = test_freq_hz[i];
                self_test_result->detected_peak_hz = result.peak_hz;
            }
        }
    }

    if (self_test_result->passed_all) {
        self_test_result->first_failed_test = 0xFFFFU;
    }

    return self_test_result->passed_all ? HR_STATUS_OK : HR_STATUS_NO_PEAK;
}
