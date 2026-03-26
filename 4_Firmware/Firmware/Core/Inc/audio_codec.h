/**
 * @file    audio_codec.h
 * @author  Patrick Rennhard (renn@zhaw.ch)
 * @date    2025-09-24
 * @version 1.0
 * @brief   API for audio codec initialization, SAI/DMA handling, and audio buffer management.
 *
 * DUAL-MODE AUDIO INPUT ARCHITECTURE:
 * ------------------------------------
 * This driver supports audio codec input modes with the following features:
 * - Audio input from external codec via SAI1 Block A (PE3/PE4/PE5)
 * - Codec provides I2S clock (slave mode)
 * - Supports mono/stereo detection via ADC on PF8
 * - DMA2 Stream1 handles reception
 * - Output to codec via SAI1 Block B (PE6) and DMA2 Stream5
 * - Use ping-pong buffer architecture
 * - Process audio as float32_t arrays
 * - Support 24-bit audio in 32-bit I2S frames
 */

#ifndef AUDIO_CODEC_H_
#define AUDIO_CODEC_H_

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "stm32f4xx.h"
#include "arm_math.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
#define AUDIO_FRAME_SIZE 2048
#define AUDIO_CHANNEL_SIZE  (AUDIO_FRAME_SIZE/2) //AUDIO_FRAME_SIZE / 2 = number of samples per left or right channels

#define CODEC_ADC_RES 12
#define CODEC_ADC_REF_VOLTAGE 3.3f

/******************************************************************************
 * Functions
 *****************************************************************************/

/**
 * @brief Initializes the codec and prepares the audio buffers for streaming.
 *
 * This function initializes the GPIOs, including their alternate functions
 * for the SAI interface, and enables the corresponding clock sources for
 * the ports. It also links the provided left and right channel buffers
 * for audio input/output.
 *
 * @param left_channel_buffer Pointer to the buffer for the left audio channel.
 * @param right_channel_buffer Pointer to the buffer for the right audio channel.
 * @param size Number of samples in each channel buffer.
 *
 * @return HAL status code indicating the result of the initialization.
 *         - HAL_OK: Initialization successful.
 *         - HAL_ERROR: Initialization failed.
 *
 * @note Must be called before `codec_start()`.
 */
HAL_StatusTypeDef codec_init(float32_t *left_channel_buffer,
                             float32_t *right_channel_buffer, uint32_t size);

/**
 * @brief Initializes and starts the SAI interface including DMA.
 *
 * This function configures the Serial Audio Interface (SAI) and the
 * associated DMA channels, then starts the audio data transfer.
 * It also enables the DMA interrupt for data reception, ensuring that
 * incoming audio data is handled properly.
 *
 * The DMA for both reception and transmission is implemented as a
 * ping-pong buffer and operates in endless (cyclic) mode.
 */
void codec_start(void);

/**
 * @brief Checks if new audio data is available from the codec.
 *
 * This function should be called to determine whether the codec has
 * completed a data transfer and new audio samples are ready for
 * processing or copying.
 *
 * @return `true` if new audio data is available, `false` otherwise.
 *
 * @note Typically called immediately after the DMA interrupt or in
 *       the audio processing loop to ensure timely handling of data.
 */
uint8_t codec_data_ready(void);

/**
 * @brief Clears the data ready flag of the codec.
 *
 * This function should be called after polling `codec_data_ready()` and
 * processing the available audio data. It resets the internal flag,
 * allowing the codec and DMA to signal the next set of audio samples.
 *
 * @note Typically used in the audio processing loop immediately after
 *       handling the data.
 */
void codec_clear_data_ready(void);

#endif
