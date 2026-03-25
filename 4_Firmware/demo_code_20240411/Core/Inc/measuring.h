/** ***************************************************************************
 * @file
 * @brief Radar I/Q signal acquisition
 *
 * Configures ADC1 + ADC2 in dual simultaneous mode, timer-triggered,
 * with DMA transfer of packed 32-bit dual-ADC samples.
 * Unpacks into a MEAS_RadarFrame_t structure with quality flags.
 *
 * Prefix: MEAS
 *
 ****************************************************************************/

#ifndef MEAS_H_
#define MEAS_H_


/******************************************************************************
 * Includes
 *****************************************************************************/
#include <stdbool.h>
#include <stdint.h>


/******************************************************************************
 * Defines
 *****************************************************************************/
#define MEAS_FRAME_LEN      1024    ///< Samples per channel
#define MEAS_SAMPLE_RATE    100     ///< Sampling frequency in Hz


/******************************************************************************
 * Types
 *****************************************************************************/

/** Radar frame structure holding unpacked I/Q data and quality metadata */
typedef struct {
    uint16_t raw_i[MEAS_FRAME_LEN]; ///< I channel (ADC1, PC1)
    uint16_t raw_q[MEAS_FRAME_LEN]; ///< Q channel (ADC2, PC3)
    uint16_t sample_count;          ///< Valid samples per channel
    uint16_t sample_rate_hz;        ///< e.g. 100
    uint32_t frame_id;              ///< Incrementing frame counter
    bool     clip_i;                ///< true if I channel clipped
    bool     clip_q;                ///< true if Q channel clipped
    bool     dma_overrun;           ///< true if previous frame not consumed
} MEAS_RadarFrame_t;


/******************************************************************************
 * Variables
 *****************************************************************************/
extern bool MEAS_data_ready;        ///< New frame is ready for processing


/******************************************************************************
 * Functions
 *****************************************************************************/

/** Configure GPIOs in analog mode for radar inputs */
void MEAS_GPIO_analog_init(void);

/** Configure TIM2 to trigger ADC at MEAS_SAMPLE_RATE */
void MEAS_timer_init(void);

/** Initialize ADC1+ADC2 dual mode and DMA for radar acquisition */
void MEAS_radar_dual_init(void);

/** Start a single-shot radar acquisition (1024 samples) */
void MEAS_start_radar_single(void);

/** Get pointer to the most recent frame (valid after MEAS_data_ready) */
const MEAS_RadarFrame_t* MEAS_get_frame(void);

/** Mark the current frame as consumed */
void MEAS_consume_frame(void);

/** Reset the ADCs and the timer */
void ADC_reset(void);


#endif
