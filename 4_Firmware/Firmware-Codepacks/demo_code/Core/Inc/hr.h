/** ***************************************************************************
 * @file
 * @brief Heart-rate extraction from radar I/Q frames.
 *****************************************************************************/

#ifndef HR_H_
#define HR_H_

#include <stdbool.h>
#include <stdint.h>

#include "fft.h"
#include "measuring.h"

#define HR_BAND_MIN_HZ 0.8f
#define HR_BAND_MAX_HZ 3.0f

typedef struct {
    float bpm;
    float peak_hz;
    uint32_t frame_id;
    bool valid;
    bool clipped;
} HR_Result_t;

typedef enum {
    HR_STATUS_OK = 0,
    HR_STATUS_INVALID_ARG,
    HR_STATUS_FFT_ERROR,
    HR_STATUS_NO_PEAK
} HR_Status_t;

typedef struct {
    bool passed_all;
    uint16_t total_tests;
    uint16_t passed_tests;
    uint16_t first_failed_test;
    uint16_t expected_bin;
    uint16_t detected_bin;
    float expected_peak_hz;
    float detected_peak_hz;
    HR_Status_t last_status;
} HR_SelfTestResult_t;

HR_Status_t HR_init(void);
HR_Status_t HR_process_radar_frame(const MEAS_RadarFrame_t *frame, HR_Result_t *result);
HR_Status_t HR_run_self_test(HR_SelfTestResult_t *self_test_result);

#endif
