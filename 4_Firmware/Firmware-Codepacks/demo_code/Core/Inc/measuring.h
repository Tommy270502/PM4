/** ***************************************************************************
 * @file
 * @brief See measuring.c
 *
 * Prefixes MEAS, ADC, DAC
 *
 *****************************************************************************/

#ifndef MEAS_H_
#define MEAS_H_


/******************************************************************************
 * Includes
 *****************************************************************************/
#include <stdbool.h>
#include <stdint.h>


/******************************************************************************
 * Milestone-1 constants
 *****************************************************************************/
#define MEAS_FRAME_LEN        1024U
#define MEAS_SAMPLE_RATE_HZ   100U


/******************************************************************************
 * Types
 *****************************************************************************/
typedef struct {
	uint16_t raw_i[MEAS_FRAME_LEN];
	uint16_t raw_q[MEAS_FRAME_LEN];
	uint16_t sample_count;
	uint16_t sample_rate_hz;
	uint32_t frame_id;
	bool clip_i;
	bool clip_q;
	bool dma_overrun;
} MEAS_RadarFrame_t;


/******************************************************************************
 * Defines
 *****************************************************************************/
extern bool MEAS_data_ready;
extern uint32_t MEAS_input_count;
extern bool DAC_active;


/******************************************************************************
 * Functions
 *****************************************************************************/
void MEAS_GPIO_analog_init(void);
void MEAS_timer_init(void);
void DAC_reset(void);
void DAC_init(void);
void DAC_increment(void);
void ADC_reset(void);
void ADC3_IN4_single_init(void);
void ADC3_IN4_single_read(void);
void ADC3_IN4_timer_init(void);
void ADC3_IN4_timer_start(void);
void ADC3_IN4_DMA_init(void);
void ADC3_IN4_DMA_start(void);
void ADC1_IN13_ADC2_IN11_dual_init(void);
void ADC1_IN13_ADC2_IN11_dual_start(void);
void ADC2_IN13_IN5_scan_init(void);
void ADC2_IN13_IN5_scan_start(void);
void ADC3_IN13_IN4_scan_init(void);
void ADC3_IN13_IN4_scan_start(void);

void MEAS_show_data(void);

bool MEAS_start_radar_single(void);
bool MEAS_is_frame_ready(void);
bool MEAS_consume_radar_frame(MEAS_RadarFrame_t *out_frame);


#endif
