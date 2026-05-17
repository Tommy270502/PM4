/**
 * @file    radar_heartrate.h
 * @brief   Folded-spectrum radar heart-rate estimator.
 *
 * Implements the 6-step HR algorithm described in heart_rate_algorith.md.
 * The module is stateful: call radar_hr_init() once, then
 * radar_hr_process_frame() for each new FFT frame.
 */

#ifndef RADAR_HEARTRATE_H_
#define RADAR_HEARTRATE_H_

/******************************************************************************
 * Includes
 *****************************************************************************/
#include <stdbool.h>
#include "stm32f4xx.h"
#include "arm_math.h"
#include "radar.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
#define RADAR_HR_MAX_CANDIDATES   3U   /**< Top-K candidates per frame.     */
#define RADAR_HR_MEDIAN_BUF_SIZE  3U   /**< Circular median buffer depth.   */

/** Maximum number of (i,j) index pairs that can fall into the HR band. */
#define RADAR_HR_MAX_BAND_BINS    (RADAR_CHANNEL_SAMPLES / 2U)

/******************************************************************************
 * Types
 *****************************************************************************/

/** Configuration constants (tuneable at init or run-time). */
typedef struct {
    float32_t f_min;              /**< Lower HR search frequency [Hz].       */
    float32_t f_max;              /**< Upper HR search frequency [Hz].       */
    uint32_t  K;                  /**< Number of top candidates to keep.     */
    uint32_t  W_mask;             /**< Noise-floor exclusion half-width [bins]. */
    float32_t PNR_min;            /**< PNR gate [dB].                        */
    float32_t prominence_min;     /**< Prominence gate (linear ratio).       */
    float32_t jump_limit;         /**< Max BPM change per frame [bpm].       */
    uint32_t  N_lock;             /**< Frames to enter LOCKED.               */
    uint32_t  N_reset;            /**< Invalid frames to exit LOCKED.        */
    float32_t alpha_high;         /**< IIR alpha for PNR >= 12 dB.           */
    float32_t alpha_mid;          /**< IIR alpha for 9 <= PNR < 12.          */
    float32_t alpha_low;          /**< IIR alpha for 7 <= PNR < 9.           */
} radar_hr_config_t;

/** Per-candidate debug/output data. */
typedef struct {
    uint32_t  bin;                /**< Peak bin index in shifted spectrum.    */
    float32_t f_interp_hz;        /**< Interpolated frequency [Hz].          */
    float32_t bpm_raw;            /**< 60 * f_interp [bpm].                  */
    float32_t PNR_dB;             /**< Peak-to-noise ratio [dB].             */
    float32_t prominence;         /**< Linear prominence.                    */
    bool      valid_conf;         /**< Passes PNR + prominence gates.        */
} radar_hr_candidate_t;

/** State-machine states. */
typedef enum {
    RADAR_HR_UNLOCKED = 0,
    RADAR_HR_LOCKED   = 1
} radar_hr_sm_state_t;

/** Full algorithm state (opaque to caller). */
typedef struct {
    /* Configuration */
    radar_hr_config_t cfg;

    /* Precomputed band index pairs */
    uint32_t  band_i[RADAR_HR_MAX_BAND_BINS];  /**< Positive-freq bin index. */
    uint32_t  band_j[RADAR_HR_MAX_BAND_BINS];  /**< Mirror (neg-freq) index. */
    uint32_t  band_len;                         /**< Number of valid pairs.   */

    /* Folded spectrum scratch (only band_len entries used). */
    float32_t S[RADAR_HR_MAX_BAND_BINS];
    float32_t scratch[RADAR_HR_MAX_BAND_BINS];

    /* Candidate scratch */
    radar_hr_candidate_t candidates[RADAR_HR_MAX_CANDIDATES];
    uint32_t  top_k_idx[RADAR_HR_MAX_CANDIDATES];
    uint32_t  num_candidates;

    /* State machine */
    radar_hr_sm_state_t sm_state;
    uint32_t  consecutive_valid;
    uint32_t  consecutive_invalid;

    /* Median buffer (circular) */
    float32_t median_buf[RADAR_HR_MEDIAN_BUF_SIZE];
    uint32_t  median_count;
    uint32_t  median_idx;

    /* Smoothing / reference */
    float32_t ref_bpm;
    float32_t smoothed_bpm;

    /* Cached constants */
    float32_t delta_f;            /**< Bin spacing = Fs / N.                 */
    uint32_t  N;                  /**< = RADAR_CHANNEL_SAMPLES.              */
    float32_t fs;                 /**< = RADAR_SAMPLE_RATE_HZ.               */
} radar_hr_state_t;

/** Output produced by each call to radar_hr_process_frame(). */
typedef struct {
    float32_t bpm;                /**< Smoothed BPM output.                  */
    bool      valid;              /**< true when LOCKED and accepted.        */
    radar_hr_sm_state_t state;    /**< Current state machine state.          */
} radar_hr_output_t;

/******************************************************************************
 * Functions
 *****************************************************************************/

/**
 * @brief  One-time initialisation of the radar HR estimator.
 *
 * Precomputes (i,j) index pairs for the configured HR band and resets all
 * state-machine counters to their initial (UNLOCKED) values.
 *
 * @param[out] st   State structure to initialise (caller-allocated).
 * @param[in]  cfg  Configuration parameters.  Pass NULL for built-in defaults.
 */
void radar_hr_init(radar_hr_state_t *st, const radar_hr_config_t *cfg);

/**
 * @brief  Process one FFT frame through the full HR pipeline.
 *
 * Steps 1-6 of the algorithm are executed in sequence.  The caller supplies
 * a shifted magnitude spectrum (size = RADAR_CHANNEL_SAMPLES, DC at index
 * N/2). The preferred input is the displacement spectrum produced by
 * fft_real_centered().
 *
 * @param[in,out] st               Algorithm state.
 * @param[in]     spectrum_shifted  Shifted magnitude spectrum (size N).
 * @param[out]    out               Result for this frame.
 */
void radar_hr_process_frame(radar_hr_state_t *st,
                            const float32_t *spectrum_shifted,
                            radar_hr_output_t *out);

/**
 * @brief  Advance the HR state machine with an explicitly invalid frame.
 *
 * Use this when upstream radar phase/displacement quality is invalid, so the
 * state machine can age out stale locks instead of processing bad spectra.
 */
void radar_hr_process_invalid_frame(radar_hr_state_t *st,
                                    radar_hr_output_t *out);

/**
 * @brief  Return current state enum for debug display.
 */
radar_hr_sm_state_t radar_hr_get_state(const radar_hr_state_t *st);

#endif /* RADAR_HEARTRATE_H_ */
