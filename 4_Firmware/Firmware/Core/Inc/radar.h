/**
 * @file    radar.h
 * @author  Patrick Rennhard (renn@zhaw.ch)
 * @date    2025-09-24
 * @version 1.0
 * @brief   Radar I/Q acquisition API using ADC dual-mode and DMA.
 *
 * This module captures radar I/Q samples from:
 * - PC1 (I) via ADC1
 * - PC3 (Q) via ADC2
 * using timer-triggered simultaneous sampling and DMA ping-pong buffers.
 */

#ifndef RADAR_H_
#define RADAR_H_

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "stm32f4xx.h"
#include "arm_math.h"

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

#define RADAR_ADC_RES 12
#define RADAR_ADC_REF_VOLTAGE 3.3f

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

#endif
