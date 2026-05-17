/**
 * @file    radar.h
 * @author  Patrick Rennhard (renn@zhaw.ch)
 * @date    2025-09-24
 * @version 1.0
 * @brief   Radar acquisition and processing helper API.
 *
 * This module captures radar I/Q samples from:
 * - PC1 (I) via ADC1
 * - PC3 (Q) via ADC2
 * using timer-triggered simultaneous sampling and DMA ping-pong buffers.
 *
 * It also provides model-side helper functions for rolling-window updates,
 * preprocessing, and display-oriented scaling/peak extraction.
 */

#ifndef RADAR_H_
#define RADAR_H_

/******************************************************************************
 * Includes
 *****************************************************************************/
#include <stdbool.h>

#include "stm32f4xx.h"
#include "arm_math.h"
#include "board_config.h"
#include "filters.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
#define RADAR_FRAME_SIZE 1024U
#define RADAR_CHANNEL_SAMPLES (RADAR_FRAME_SIZE / 2U) /* One packed DMA word contains one I/Q sample pair. */
#define RADAR_FRAME_ADVANCE_SAMPLES (RADAR_CHANNEL_SAMPLES / 2U) /* 50% overlap: advance the analysis window by half its length. */

#if ((RADAR_CHANNEL_SAMPLES % 2U) != 0U)
#error "RADAR_CHANNEL_SAMPLES must be even for 50% overlap."
#endif

/*
 * Low-frequency vital-sign radar:
 * - fs = 100 Hz comfortably covers content up to 5 Hz
 * - 512 samples/channel give a 5.12 s FFT window
 * - 256 new samples/channel advance that window every 2.56 s (50% overlap)
 * - FFT bin spacing is 100 / 512 = 0.1953125 Hz
 */
#define RADAR_SAMPLE_RATE_HZ 100U

/* Default spectrum peak readout tuning. */
#define RADAR_SPECTRUM_PEAK_VALID_THRESHOLD 0.01f
#define RADAR_SPECTRUM_PEAK_DOMINANCE_RATIO 3.0f

/******************************************************************************
 * Types
 *****************************************************************************/

#define RADAR_PHASE_FLAG_VALID       (1UL << 0)
#define RADAR_PHASE_FLAG_CLIPPING    (1UL << 1)
#define RADAR_PHASE_FLAG_LOW_SIGNAL  (1UL << 2)

/**
 * @brief Configuration for I/Q phase unwrapping and displacement conversion.
 */
typedef struct {
    float32_t adc_center_count;          /**< Nominal ADC mid-scale count. */
    float32_t wavelength_m;              /**< Radar wavelength in metres. */
    float32_t offset_alpha;              /**< Per-chunk I/Q DC offset update rate. */
    float32_t gain_alpha;                /**< Per-chunk I/Q RMS update rate. */
    float32_t min_radius_counts;         /**< Minimum corrected I/Q vector radius. */
    float32_t min_gain_rms_counts;       /**< Minimum RMS before gain correction is trusted. */
    float32_t max_low_signal_ratio;      /**< Max low-radius sample ratio for valid chunk. */
    uint16_t clip_low_count;             /**< ADC count at/below this is treated as clipping. */
    uint16_t clip_high_count;            /**< ADC count at/above this is treated as clipping. */
} radar_phase_config_t;

/**
 * @brief Per-chunk phase/displacement quality and calibration snapshot.
 */
typedef struct {
    uint32_t sample_count;
    uint32_t valid_sample_count;
    uint32_t clipped_sample_count;
    uint32_t low_signal_sample_count;
    uint32_t flags;
    float32_t mean_radius_counts;
    float32_t i_offset_counts;
    float32_t q_offset_counts;
    float32_t i_gain;
    float32_t q_gain;
} radar_phase_quality_t;

/**
 * @brief Stateful phase unwrap and displacement conversion context.
 */
typedef struct {
    radar_phase_config_t cfg;
    float32_t i_offset_counts;
    float32_t q_offset_counts;
    float32_t i_rms_counts;
    float32_t q_rms_counts;
    float32_t i_gain;
    float32_t q_gain;
    float32_t previous_phase_rad;
    float32_t unwrapped_phase_rad;
    float32_t reference_phase_rad;
    uint8_t phase_initialized;
} radar_phase_state_t;

extern const radar_phase_config_t RADAR_PHASE_CONFIG_DEFAULT;

/******************************************************************************
 * Functions
 *****************************************************************************/

/**
 * @brief Initializes radar input and binds I/Q output buffers.
 *
 * This function initializes GPIO/ADC/DMA dependencies and links the
 * provided I and Q channel sample buffers.
 *
 * @param i_channel_buffer Pointer to the I-channel sample buffer.
 * @param q_channel_buffer Pointer to the Q-channel sample buffer.
 * @param size Number of samples per channel buffer.
 *             Must be at least `RADAR_FRAME_ADVANCE_SAMPLES`.
 *
 * @return HAL status code indicating the result of the initialization.
 *         - HAL_OK: Initialization successful.
 *         - HAL_ERROR: Initialization failed.
 *
 * @note Must be called before `radar_start()`.
 */
HAL_StatusTypeDef radar_init(float32_t *i_channel_buffer,
                                   float32_t *q_channel_buffer, uint32_t size);

/**
 * @brief Starts timer-triggered radar acquisition.
 *
 * This function enables ADCs and starts the timer trigger source.
 * DMA runs in circular double-buffer mode and continuously fills
 * overlapped I/Q sample chunks.
 */
void radar_start(void);

/**
 * @brief Checks if a new radar I/Q acquisition chunk is available.
 *
 * This function should be polled from the main loop to determine whether
 * DMA completed a new I/Q chunk transfer.
 *
 * @return `true` if a new radar acquisition chunk is available, `false` otherwise.
 *
 * @note Typically called immediately after the DMA interrupt or in
 *       the radar processing loop to ensure timely handling of the rolling
 *       analysis window.
 */
uint8_t radar_frame_ready(void);

/**
 * @brief Clears the radar frame-ready flag.
 *
 * This function should be called after polling `radar_frame_ready()`
 * and consuming the available I/Q acquisition chunk.
 *
 * @note Typically used in the radar processing loop immediately after
 *       handling the data.
 */
void radar_clear_frame_ready(void);

/**
 * @brief Claims and unpacks the latest DMA-completed radar chunk.
 *
 * This function performs only a short critical section to claim the completed
 * ping/pong buffer. The I/Q unpacking is done in foreground after interrupts
 * are restored.
 *
 * @return 1U when a chunk was copied into the buffers passed to radar_init(),
 *         0U when no completed chunk is pending.
 */
uint8_t radar_get_latest_chunk(void);

/**
 * @brief Returns the number of DMA-completed chunks since radar_start().
 */
uint32_t radar_get_dma_sequence(void);

/**
 * @brief Returns the number of completed radar chunks overwritten before use.
 */
uint32_t radar_get_overrun_count(void);

/**
 * @brief Returns the number of DMA error flags observed by the radar ISR.
 */
uint32_t radar_get_dma_error_count(void);

/**
 * @brief Initializes phase unwrap and displacement conversion state.
 *
 * Pass NULL for the built-in RFbeam K-LC5 defaults.
 */
void radar_phase_init(radar_phase_state_t *state, const radar_phase_config_t *config);

/**
 * @brief Resets phase unwrap state while keeping the active configuration.
 */
void radar_phase_reset(radar_phase_state_t *state);

/**
 * @brief Converts one I/Q ADC chunk into relative chest displacement samples.
 *
 * ADC counts are offset-corrected, gain-balanced, converted to atan2 phase,
 * unwrapped sample-by-sample, and scaled as displacement = lambda * phase / 4pi.
 *
 * @return true when the input was processed, false on invalid arguments.
 */
bool radar_phase_process_chunk(radar_phase_state_t *state,
                               const float32_t *i_counts,
                               const float32_t *q_counts,
                               float32_t *displacement_m,
                               uint32_t count,
                               radar_phase_quality_t *quality);

/**
 * @brief Shift-in the latest displacement chunk into a rolling history buffer.
 */
bool radar_append_displacement_chunk(float32_t *displacement_history,
                                     const float32_t *displacement_acquired,
                                     uint32_t *window_fill_samples);

/**
 * @brief Copy rolling displacement history to a processing buffer.
 */
void radar_prepare_displacement_window(float32_t *displacement_samples,
                                       const float32_t *displacement_history);

/**
 * @brief Shift-in the latest acquired chunk into rolling history buffers.
 *
 * The history buffers keep the last full analysis window and are advanced by
 * RADAR_FRAME_ADVANCE_SAMPLES on each call.
 *
 * @param i_history Rolling I-history buffer of size RADAR_CHANNEL_SAMPLES.
 * @param q_history Rolling Q-history buffer of size RADAR_CHANNEL_SAMPLES.
 * @param i_acquired Latest acquired I chunk of size RADAR_FRAME_ADVANCE_SAMPLES.
 * @param q_acquired Latest acquired Q chunk of size RADAR_FRAME_ADVANCE_SAMPLES.
 * @param window_fill_samples In/out fill level of the rolling window.
 *
 * @return true when a full RADAR_CHANNEL_SAMPLES analysis window is available.
 */
bool radar_append_latest_chunk(float32_t *i_history, float32_t *q_history,
                               const float32_t *i_acquired, const float32_t *q_acquired,
                               uint32_t *window_fill_samples);

/**
 * @brief Copy rolling history to processing buffers and apply optional filter.
 *
 * @param i_samples Output processing buffer for I, size RADAR_CHANNEL_SAMPLES.
 * @param q_samples Output processing buffer for Q, size RADAR_CHANNEL_SAMPLES.
 * @param i_history Input rolling I history, size RADAR_CHANNEL_SAMPLES.
 * @param q_history Input rolling Q history, size RADAR_CHANNEL_SAMPLES.
 * @param effect_active Apply filter when true.
 * @param filter_l_bank Filter bank for I channel (index matches filter type).
 * @param filter_r_bank Filter bank for Q channel (index matches filter type).
 * @param filter_index Active filter index in both filter banks.
 */
void radar_prepare_processing_window(float32_t *i_samples, float32_t *q_samples,
                                     const float32_t *i_history, const float32_t *q_history,
                                     bool effect_active,
                                     const biquad_df2t_t *filter_l_bank,
                                     const biquad_df2t_t *filter_r_bank,
                                     uint8_t filter_index);

/**
 * @brief Returns start index for the newest @p count samples in a radar buffer.
 */
uint32_t radar_get_recent_start_index(uint32_t count);

/**
 * @brief Compute display scale for recent time-domain I/Q samples.
 */
void radar_get_time_scale(const float32_t *i_samples, const float32_t *q_samples,
                          uint32_t start_index, uint32_t count,
                          float32_t min_span, float32_t headroom_ratio,
                          float32_t *min_value, float32_t *max_value);

/**
 * @brief Compute centered spectrum display window and y-axis max value.
 */
void radar_get_spectrum_window(const float32_t *spectrum_shifted,
                               float32_t display_hz,
                               float32_t min_display_max,
                               float32_t headroom_ratio,
                               uint32_t *start_index,
                               uint32_t *count,
                               float32_t *max_value);

/**
 * @brief Compute dominant positive/negative frequency peaks around DC.
 */
void radar_update_peak_readout(const float32_t *spectrum_shifted,
                               float32_t display_hz,
                               float32_t valid_threshold,
                               float32_t dominance_ratio,
                               float32_t *pos_peak_hz,
                               float32_t *neg_peak_hz,
                               bool *pos_peak_valid,
                               bool *neg_peak_valid);

#endif
