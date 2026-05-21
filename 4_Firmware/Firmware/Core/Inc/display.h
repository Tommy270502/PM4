/** ***************************************************************************
 * @file
 * @brief Display primitives and menu rendering API.
 *
 * Prefixes DISP
 *
 *****************************************************************************/

#ifndef DISP_H_
#define DISP_H_

/******************************************************************************
 * Includes
 *****************************************************************************/
#include <stdbool.h>
#include "stm32f4xx.h"
#include "arm_math.h"

#include "menu.h"
#include "ui_layout.h"
#include "ekg.h"
#include "radar.h"
#include "radar_heartrate.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
#define DISP_WIDTH		(BSP_LCD_GetXSize())
#define DISP_HEIGHT		(BSP_LCD_GetYSize()-MENU_HEIGHT)

/* Shared radar-spectrum span used by menu rendering and peak extraction. */
#define SPECTRUM_DISPLAY_HZ      4.0f

/******************************************************************************
 * Variables
 *****************************************************************************/
/**
 * @brief Snapshot of model/controller data required to render menu pages.
 */
typedef struct {
	const float32_t *radar_i_samples;
	const float32_t *radar_q_samples;
	const float32_t *radar_displacement_samples;
	const float32_t *spectrum_shifted;
	uint8_t current_filter_index;
	const char * const *filter_names;
	ekg_output_t ekg_latest;
	uint32_t ekg_last_peak_tick;
	const float32_t *ekg_signal_samples;
	const uint8_t *ekg_peak_markers;
	uint32_t ekg_signal_count;
	float32_t spectrum_pos_peak_hz;
	float32_t spectrum_neg_peak_hz;
	bool spectrum_pos_peak_valid;
	bool spectrum_neg_peak_valid;
	/* Radar heart-rate module output */
	float32_t radar_hr_bpm;
	bool      radar_hr_valid;
	radar_hr_sm_state_t radar_hr_state;
	radar_phase_quality_t radar_phase_quality;
	uint32_t  radar_dma_overrun_count;
	uint32_t  radar_dma_error_count;
	/* OpenLog logger status (MENU_SIX) */
	bool      logging_enabled;
	uint32_t  logging_drop_count;
} disp_menu_data_t;

/******************************************************************************
 * Functions
 *****************************************************************************/
void disp_clear_data(void);
void disp_name_value(char name[], float32_t value, uint32_t color, uint32_t position);
void disp_curves(float32_t data[], uint32_t count, float32_t min, float32_t max, uint32_t color);
void disp_bars(float32_t data[], uint32_t count, float32_t min, float32_t max, uint32_t color);
void disp_info(void);
/**
 * @brief Force one menu page to redraw on next disp_menu_render call.
 */
void disp_menu_force_refresh(MENU_item_t menu_item);
/**
 * @brief Render active menu page with internal refresh throttling.
 */
void disp_menu_render(MENU_item_t active_menu, const disp_menu_data_t *data);

#endif
