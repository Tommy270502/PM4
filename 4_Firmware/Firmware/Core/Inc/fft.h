/**
 * @file fft.h
 * @brief Calculate centered complex I/Q frequency spectrum
 * @author Hanspeter Hochreutener, hhrt@zhaw.ch
 * @author Patrick Rennhard, renn@zhaw.ch
 * @date 2025-09-03
 */

#ifndef FFT_H_
#define FFT_H_

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
 * This function sets up the complex FFT instance using CMSIS-DSP for the
 * configured radar channel size, precomputes the Hann window, and computes
 * the scaling factor required for converting FFT magnitudes into physical
 * units.

 *
 * @return HAL status code:
 *         - HAL_OK if FFT initialization succeeded.
 *         - HAL_ERROR if FFT initialization failed.
 *
 * @note Must be called once during system initialization before using
 *       any FFT signal processing functions.
 */
HAL_StatusTypeDef fft_init(void);

/**
 * @brief Computes a centered complex spectrum from I/Q radar samples.
 *
 * The input channels are treated as a complex baseband signal:
 *   x[n] = I[n] + j * Q[n]
 * Before the FFT, the mean of each channel is removed and the same Hann
 * window is applied to both channels to reduce spectral leakage.
 *
 * The output is fft-shifted so that negative frequencies occupy the first
 * half of the buffer, DC is centered at index RADAR_CHANNEL_SAMPLES / 2,
 * and positive frequencies occupy the second half.
 *
 * @param i_in Pointer to the I-channel time-domain samples
 *             (size: RADAR_CHANNEL_SAMPLES).
 * @param q_in Pointer to the Q-channel time-domain samples
 *             (size: RADAR_CHANNEL_SAMPLES).
 * @param out  Pointer to the output buffer where the shifted magnitude
 *             spectrum is stored (size: RADAR_CHANNEL_SAMPLES).
 *
 * @return HAL status code:
 *         - HAL_OK: Frequency spectrum calculation succeeded.
 *
 * @note The FFT instance, window, and scaling factor must be initialized
 *       beforehand by calling `fft_init()`.
 */
HAL_StatusTypeDef fft_iq_centered(const float32_t i_in[], const float32_t q_in[],
                                  float32_t out[]);

#endif
