/**
 * @file calc.h
 * @brief Calculate frequency spectrum
 * @author Hanspeter Hochreutener, hhrt@zhaw.ch
 * @author Patrick Rennhard, renn@zhaw.ch
 * @date 2025-09-03
 */

#ifndef CALC_H_
#define CALC_H_

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "stm32f4xx.h"
#include "arm_math.h"

/******************************************************************************
 * Defines
 *****************************************************************************/

/******************************************************************************
 * Functions
 *****************************************************************************/

/**
 * @brief Initializes the calculation module (FFT and FIR filter).
 *
 * This function sets up the fast real FFT (RFFT) instance using CMSIS-DSP
 * for the configured radar channel size and computes the scaling factor
 * required for converting FFT magnitudes into physical units.

 *
 * @return HAL status code:
 *         - HAL_OK if FFT initialization succeeded.
 *         - HAL_ERROR if FFT initialization failed.
 *
 * @note Must be called once during system initialization before using
 *       any FFT signal processing functions.
 */
HAL_StatusTypeDef calc_init(void);

/**
 * @brief Computes the frequency spectrum of an input signal using FFT.
 *
 * This function applies a window to the input signal to reduce
 * spectral leakage, performs a fast real FFT (RFFT) using CMSIS-DSP, and
 * calculates the magnitude spectrum. The magnitudes are then scaled with
 * the previously computed FFT scaling factor.
 *
 * @param in  Pointer to the input buffer containing time-domain samples
 *            (size: RADAR_CHANNEL_SAMPLES).
 * @param out Pointer to the output buffer where the scaled magnitude
 *            spectrum is stored (size: RADAR_CHANNEL_SAMPLES / 2).
 *
 * @return HAL status code:
 *         - HAL_OK: Frequency spectrum calculation succeeded.
 *
 * @note The FFT instance (`fft_instance`) and scaling factor (`fft_abs_scale`)
 *       must be initialized beforehand by calling `calc_init()`.
 *       The output contains only the positive frequency bins.
 */
HAL_StatusTypeDef calc_freq(const float32_t in[], float32_t out[]);

#endif
