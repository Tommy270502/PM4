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
#define RADAR_FRAME_SIZE 512U
#define RADAR_CHANNEL_SAMPLES (RADAR_FRAME_SIZE / 2U) /* One packed DMA word contains one I/Q sample pair. */

/*
 * Low-frequency vital-sign radar:
 * - fs = 100 Hz comfortably covers content up to 5 Hz
 * - 256 samples/channel give a fresh frame every 2.56 s
 * - FFT bin spacing is 100 / 256 = 0.390625 Hz
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
 * I/Q sample frames.
 */
void radar_start(void);

/**
 * @brief Checks if a new radar I/Q frame is available.
 *
 * This function should be polled from the main loop to determine whether
 * DMA completed a full I/Q frame transfer.
 *
 * @return `true` if a new radar frame is available, `false` otherwise.
 *
 * @note Typically called immediately after the DMA interrupt or in
 *       the radar processing loop to ensure timely handling of data.
 */
uint8_t radar_frame_ready(void);

/**
 * @brief Clears the radar frame-ready flag.
 *
 * This function should be called after polling `radar_frame_ready()`
 * and consuming the available I/Q frame.
 *
 * @note Typically used in the radar processing loop immediately after
 *       handling the data.
 */
void radar_clear_frame_ready(void);

#endif
