/** ***************************************************************************
 * @file
 * @brief Board-level configuration constants shared across firmware modules.
 *
 * Contains orientation flags and hardware reference constants that were
 * previously scattered across module headers.
 *****************************************************************************/

#ifndef BOARD_CONFIG_H_
#define BOARD_CONFIG_H_

/******************************************************************************
 * Board Variant
 *****************************************************************************/
/**
 * Evalboard revision E (blue PCB) has inverted y-axis touch coordinates.
 * Comment this define if you are not using revision E.
 */
#define EVAL_REV_E

/**
 * Flip LCD orientation by 180 degrees.
 * Comment this define to keep default orientation.
 */
//#define FLIPPED_LCD

/******************************************************************************
 * Shared ADC/DAC Hardware References
 *****************************************************************************/
#define RADAR_ADC_RES             12
#define RADAR_ADC_REF_VOLTAGE     3.3f

#define EKG_ADC_MAX_COUNT         4095U
#define EKG_ADC_REF_VOLTAGE       3.3f

#define DAC_OUTPUT_MAX_VOLTAGE    3.3f
#define DAC_OUTPUT_MAX_CODE       4095U

/******************************************************************************
 * Shared Timer Base (APB1 timers)
 *****************************************************************************/
/* 84 MHz / (83 + 1) = 1 MHz timer tick. */
#define BOARD_TIM_APB1_PSC_1MHZ   83U
#define BOARD_TIM_APB1_TICK_HZ    1000000UL

#endif
