/**
 * @file ekg.h
 * @brief AD8232 acquisition and ECG signal processing on STM32F429
 * @author Thomas Perri, perritho@students.zhaw.ch
 * @date 2026-03-26
 *
 * Acquisition architecture (non-blocking):
 * 1) TIM3 periodic interrupt defines sampling frequency
 * 2) TIM3 ISR triggers ADC3 conversion on PF6 (ADC3_IN4)
 * 3) ADC ISR pushes raw samples into a small ring buffer
 * 4) Main loop calls ekg_process_if_ready() to process queued samples
 *
 * The AD8232 cardiac-monitor analog network is the ECG waveform-shaping
 * filter: nominal 0.5 Hz high-pass and 40 Hz low-pass. The default digital
 * path therefore preserves the ADC waveform and only derives an envelope for
 * R-peak/BPM estimation. Optional digital cleanup filters can be enabled by
 * setting highpass_hz and/or lowpass_hz to nonzero values.
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
#define EKG_AD8232_AFE_HP_HZ     (0.5f)
#define EKG_AD8232_AFE_LP_HZ     (40.0f)
#define EKG_DEFAULT_HP_HZ        (0.0f)
#define EKG_DEFAULT_LP_HZ        (0.0f)
#define EKG_DEFAULT_ENV_HZ       (8.0f)
#define EKG_DISPLAY_HISTORY_SAMPLES (240U)

typedef struct
{
    uint32_t sample_rate_hz;
    float highpass_hz;     /**< Optional digital high-pass cleanup; 0 disables it. */
    float lowpass_hz;      /**< Optional digital low-pass cleanup; 0 disables it. */
    float envelope_hz;     /**< R-peak detector envelope smoothing cutoff. */
    float refractory_s;
    float min_rr_s;
    float max_rr_s;
} ekg_config_t;

typedef struct
{
    uint16_t raw;
    float voltage;         /**< Raw ADC voltage from AD8232 OUT. */
    float bandpassed;      /**< AC-centered ECG waveform after optional digital cleanup. */
    float envelope;        /**< Squared-derivative detector envelope. */
    float threshold;       /**< Adaptive detector threshold. */
    uint8_t r_peak;
    uint32_t sample_index;        /**< Processing sample index for this output. */
    uint32_t r_peak_sample_index; /**< Candidate sample index when r_peak is set. */
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
 * @brief Fetch and clear the oldest queued interrupt-acquired raw sample.
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
 * @brief Clear display-side EKG waveform and peak-marker history.
 */
void ekg_display_history_clear(float *signal_history,
                               uint8_t *peak_history,
                               uint32_t *history_fill_samples);

/**
 * @brief Append one processed EKG sample to display-side rolling history.
 *
 * Stores ekg_output_t.bandpassed and aligns the marker to r_peak_sample_index.
 *
 * @return 1 when a sample was appended, 0 on invalid arguments.
 */
uint8_t ekg_display_history_append(float *signal_history,
                                   uint8_t *peak_history,
                                   uint32_t *history_fill_samples,
                                   const ekg_output_t *sample);

/**
 * @brief Compute display scale for one recent EKG waveform history window.
 */
void ekg_get_display_scale(const float *samples,
                           uint32_t start_index,
                           uint32_t count,
                           float min_span,
                           float headroom_ratio,
                           float *min_value,
                           float *max_value);

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
