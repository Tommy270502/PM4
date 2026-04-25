/**
 * @file ekg.h
 * @brief AD8232 acquisition and ECG signal processing on STM32F429
 * @author Thomas Perri, perritho@students.zhaw.ch
 * @date 2026-03-26
 *
 * Acquisition architecture (non-blocking):
 * 1) TIM3 periodic interrupt defines sampling frequency
 * 2) TIM3 ISR triggers ADC3 conversion on PF6 (ADC3_IN4)
 * 3) ADC ISR stores newest raw sample and raises a sample-ready flag
 * 4) Main loop calls ekg_process_if_ready() to run signal processing
 *
 * This module helps with real-time ECG feature extraction, but it is not a
 * certified medical device implementation.
 */

#ifndef EKG_H_
#define EKG_H_

#include <stdint.h>
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EKG_DEFAULT_FS_HZ        (250U)
#define EKG_DEFAULT_HP_HZ        (0.5f)
#define EKG_DEFAULT_LP_HZ        (40.0f)
#define EKG_DEFAULT_ENV_HZ       (8.0f)

typedef struct
{
    uint32_t sample_rate_hz;
    float highpass_hz;
    float lowpass_hz;
    float envelope_hz;
    float refractory_s;
    float min_rr_s;
    float max_rr_s;
} ekg_config_t;

typedef struct
{
    uint16_t raw;
    float voltage;
    float bandpassed;
    float envelope;
    float threshold;
    uint8_t r_peak;
    float bpm;
    uint8_t bpm_valid;
} ekg_output_t;

extern const ekg_config_t EKG_CONFIG_DEFAULT;

/**
 * @brief Initialize PF6 + ADC3 + TIM3 interrupt sampling and ECG processing state.
 *
 * Pass NULL for default configuration. Acquisition starts immediately.
 */
void ekg_init(const ekg_config_t *config);

/**
 * @brief Start interrupt-driven sampling.
 */
void ekg_start(void);

/**
 * @brief Stop interrupt-driven sampling.
 */
void ekg_stop(void);

/**
 * @brief Reconfigure processing parameters (sampling/filter/detection).
 *
 * ADC pin/channel remains PF6/ADC3_IN4.
 */
void ekg_set_config(const ekg_config_t *config);

/**
 * @brief Reset all dynamic filter and detection state.
 */
void ekg_reset_processing(void);

/**
 * @brief Read one raw ADC sample from PF6 (ADC3_IN4).
 *
 * @note This is a blocking fallback helper. Prefer ekg_process_if_ready()
 *       in normal operation.
 */
uint16_t ekg_read_raw_blocking(void);

/**
 * @brief Convert raw ADC code to volts.
 */
float ekg_raw_to_voltage(uint16_t raw);

/**
 * @brief Process one raw sample and update ECG features.
 */
void ekg_process_raw(uint16_t raw, ekg_output_t *out);

/**
 * @brief Convenience: read ADC and process in one call.
 *
 * @note This is blocking and mainly kept for debug/backward compatibility.
 */
void ekg_read_and_process(ekg_output_t *out);

/**
 * @brief Returns 1 if a new interrupt-acquired sample is waiting, else 0.
 */
uint8_t ekg_sample_ready(void);

/**
 * @brief Fetch and clear the newest interrupt-acquired raw sample.
 *
 * @param raw Output pointer for the sample.
 * @return 1 on success, 0 if no sample is available.
 */
uint8_t ekg_get_latest_raw_sample(uint16_t *raw);

/**
 * @brief Process one pending interrupt-acquired sample if available.
 *
 * @param out Output structure for all processed values.
 * @return 1 if a sample was processed, 0 if no new sample was available.
 */
uint8_t ekg_process_if_ready(ekg_output_t *out);

/**
 * @brief Number of overwritten samples since startup/reset.
 */
uint32_t ekg_get_overrun_count(void);

/**
 * @brief Latest estimated BPM. Returns 0.0f if not yet valid.
 */
float ekg_get_latest_bpm(void);

/**
 * @brief Returns 1 when BPM estimate is valid, else 0.
 */
uint8_t ekg_has_valid_bpm(void);

#ifdef __cplusplus
}
#endif

#endif /* EKG_H_ */
