/** ***************************************************************************
 * @file
 * @brief Sets up the microcontroller, the clock system and the peripherals.
 *
 * Initialization is done for the system, the blue user button, the user LEDs,
 * and the LCD display with the touchscreen.
 * @n All the peripherals needed for measuring are initialized.
 * @n Then the code enters an infinite while-loop, where it checks for
 * user input or newly available data and refreshes the display accordingly.
 *
 * @author  Hanspeter Hochreutener, hhrt@zhaw.ch
 * @date	19.11.2024
 * @modified_by Patrick Rennhard, renn@zhaw.ch
 * @modified_date 25.08.2025
 *****************************************************************************/

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "stm32f4xx.h"
#include "stm32f429i_discovery.h"
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_ts.h"

#include "core_cm4.h"
#include <stdio.h>

#include "main.h"
#include "pushbutton.h"
#include "menu.h"

#include "dac_output.h"
#include "fft.h"
#include "display.h"
#include "radar.h"
#include "filters.h"
#include "ekg.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
// Display refresh loop limits for each menu (to prevent flicker)
#define DISP_LOOP_M0 	10	// Info screen
#define DISP_LOOP_M1 	0   // Time signal (refresh on every new I/Q block)
#define DISP_LOOP_M2 	0	// Spectrum analyzer (refresh on every new I/Q block)
#define DISP_LOOP_M3 	4	// Effect Menu (Filter Selection)
#define DISP_LOOP_M4 	4	// Level meter
#define DISP_LOOP_M5 	4	// ...
#define DISP_LOOP_M6 	4	// ...
#define DISP_LOOP_M7 	4	// ...
#define DISP_LOOP_M8 	4	// ...
#define DISP_LOOP_M9 	4	// EKG BPM
#define DISP_LOOP_M10 	4	// DAC output

// Define the maximum number of points for the time signal (Display 240 x 320 pixels)
#define MAX_TIME_SIGNAL_POINTS 240

// Define time_signal_points based on RADAR_CHANNEL_SAMPLES, ensuring it's capped at MAX_TIME_SIGNAL_POINTS
#define TIME_SIGNAL_POINTS (RADAR_CHANNEL_SAMPLES > MAX_TIME_SIGNAL_POINTS ? MAX_TIME_SIGNAL_POINTS : RADAR_CHANNEL_SAMPLES)

#define TIME_SIGNAL_MIN_SPAN     4.0f
#define TIME_SIGNAL_HEADROOM     0.10f
#define SPECTRUM_DISPLAY_HZ      4.0f
#define SPECTRUM_MIN_DISPLAY_MAX 0.001f
#define SPECTRUM_HEADROOM        1.15f
#define SPECTRUM_PEAK_VALID_THRESHOLD 0.01f
#define SPECTRUM_PEAK_DOMINANCE_RATIO 3.0f
#define DAC_TOUCH_STEP_VOLTAGE   0.1f
#define DAC_SLIDER_X             20U
#define DAC_SLIDER_Y             120U
#define DAC_SLIDER_WIDTH         200U
#define DAC_SLIDER_HEIGHT        24U
#define DAC_BUTTON_Y             185U
#define DAC_BUTTON_WIDTH         85U
#define DAC_BUTTON_HEIGHT        44U
#define DAC_MINUS_X              20U
#define DAC_PLUS_X               135U


/******************************************************************************
 * Variables
 *****************************************************************************/

static float32_t radar_i_acquired[RADAR_FRAME_ADVANCE_SAMPLES];
static float32_t radar_q_acquired[RADAR_FRAME_ADVANCE_SAMPLES];

static float32_t radar_i_history[RADAR_CHANNEL_SAMPLES];
static float32_t radar_q_history[RADAR_CHANNEL_SAMPLES];
static float32_t radar_i_samples[RADAR_CHANNEL_SAMPLES];
static float32_t radar_q_samples[RADAR_CHANNEL_SAMPLES];
static float32_t spectrum_shifted[RADAR_CHANNEL_SAMPLES];
static uint32_t radar_window_fill_samples = 0U;
static float32_t spectrum_pos_peak_hz = 0.0f;
static float32_t spectrum_neg_peak_hz = 0.0f;
static bool spectrum_pos_peak_valid = false;
static bool spectrum_neg_peak_valid = false;

static uint32_t disp_loop_count[MENU_TOTAL_ENTRIES] = {0}; // Loop counters for refreshing display menus
static bool disp_refresh;			///< Display should be refreshed

static uint8_t efect_active = 0;

/* Multi-filter system: 5 types per channel (preconfigured) */
static biquad_df2t_t fxL[5];
static biquad_df2t_t fxR[5];
static uint8_t current_filter_index = 0;  // Start with FILTER_BYPASS (no effect)

static const char* filter_names[] = {
	"BYPASS",
	"LOWPASS",
	"HIGHPASS",
	"BANDPASS",
	"NOTCH"
};

static ekg_output_t ekg_latest = {0};
static uint32_t ekg_last_peak_tick = 0;
static bool dac_touch_was_detected = false;

/******************************************************************************
 * Functions
 *****************************************************************************/
static void SystemClock_Config(void);	///< System Clock Configuration
static void gyro_disable(void);			///< Disable the onboard gyroscope
static void error_handling(HAL_StatusTypeDef error);
static bool radar_append_latest_chunk(void);
static void radar_prepare_processing_window(void);
static uint32_t radar_get_recent_start_index(uint32_t count);
static void radar_get_time_scale(uint32_t start_index, uint32_t count, float32_t *min_value,
		float32_t *max_value);
static void radar_get_spectrum_window(uint32_t *start_index, uint32_t *count,
		float32_t *max_value);
static void radar_update_peak_readout(void);
static void touch_get_adjusted_state(TS_StateTypeDef *touch_state);
static bool touch_is_inside_rect(uint16_t x, uint16_t y, uint16_t rect_x,
		uint16_t rect_y, uint16_t rect_width, uint16_t rect_height);
static void dac_output_step(float delta_voltage);
static bool dac_output_handle_touch(void);
static void disp_peak_frequencies(void);
static void disp_dac_output(void);

/** ***************************************************************************
 * @brief  Main function
 * @return not used because main ends in an infinite loop
 *
 * Initialization and infinite while loop
 *****************************************************************************/
int main(void) {
	HAL_StatusTypeDef ret_val;

	HAL_Init();							// Initialize the system

	SystemClock_Config();				// Configure system clocks

#ifdef FLIPPED_LCD
	BSP_LCD_Init_Flipped();				// Initialize the LCD for flipped orientation
#else
	BSP_LCD_Init();						// Initialize the LCD display
#endif
	BSP_LCD_LayerDefaultInit(LCD_FOREGROUND_LAYER, LCD_FRAME_BUFFER);
	BSP_LCD_SelectLayer(LCD_FOREGROUND_LAYER);
	BSP_LCD_DisplayOn();
	BSP_LCD_Clear(LCD_COLOR_WHITE);

	BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize());	// Touchscreen
	/* Uncomment next line to enable touchscreen interrupt */
	//BSP_TS_ITConfig();					// Enable Touchscreen interrupt
	PB_init();							// Initialize the user pushbutton
	PB_enableIRQ();					// Enable interrupt on user pushbutton

	BSP_LED_Init(LED3);					// Toggles in while loop
	BSP_LED_Init(LED4);					// Is toggled by user button

	MENU_draw();						// Draw the menu
	disp_info();						// Show info menu at startup

	gyro_disable();					// Disable gyro, use those analog inputs
	dac_output_init();				// DAC output on PA5 (DAC channel 2)

	ret_val = radar_init(radar_i_acquired, radar_q_acquired, RADAR_FRAME_ADVANCE_SAMPLES);
	error_handling(ret_val);

	radar_start();

	/* --------------------------------------------------------------------
	 * Signal filter configuration (biquad)
	 *
	 * Pre-configure all 5 filter types for instant switching.
	 * Notes:
	 * - fs must match the radar I/Q sampling rate.
	 * - Q controls resonance / bandwidth. Q=0.707 is a good general default.
	 * -------------------------------------------------------------------- */
	const float32_t fs = (float32_t)RADAR_SAMPLE_RATE_HZ; /* radar sampling rate */
	const float32_t f0 = 2.0f;             /* low-frequency heartbeat range */
	const float32_t Q  = 0.707f;            /* Butterworth-ish */
	
	// Initialize all filter types
	for (uint8_t i = 0; i < 5; i++) {
		filter_type_t type = (filter_type_t)i;  // FILTER_BYPASS=0, LOWPASS=1, etc.
		biquad_config(&fxL[i], type, fs, f0, Q);
		biquad_config(&fxR[i], type, fs, f0, Q);
		biquad_reset(&fxL[i]);
		biquad_reset(&fxR[i]);
	}
	
	// Set initial effect state (active for LOWPASS)
	efect_active = (current_filter_index != FILTER_BYPASS);

	ret_val = fft_init();
	error_handling(ret_val);

	ekg_init(NULL);  // AD8232 on PF6 (ADC3_IN4), interrupt-driven sampling

	/* Infinite while loop */
	while (1) {							// Infinitely loop in main function

		/* Comment next line if touchscreen interrupt is enabled */
		MENU_check_transition();
		MENU_item_t menu_transition = MENU_get_transition();
		switch (menu_transition) {	// Handle user menu transitions
		case MENU_NONE:	// No transition => do nothing
			break;
		case MENU_SCROLL_LEFT:	// Scroll menu left
			MENU_scroll_left();
			break;
		case MENU_SCROLL_RIGHT:	// Scroll menu right
			MENU_scroll_right();
			break;
		case MENU_ZERO:
		case MENU_ONE:
		case MENU_TWO:
		case MENU_THREE:
		case MENU_FOUR:
		case MENU_FIVE:
		case MENU_SIX:
		case MENU_SEVEN:
		case MENU_EIGHT:
		case MENU_NINE:
		case MENU_TEN:
			disp_refresh = true;	// Switch to new menu item
			break;
		default:	// Should never occur
			break;
		}
		if (menu_transition == MENU_TEN) {
			disp_loop_count[MENU_TEN] = DISP_LOOP_M10;
		}

		if (PB_pressed()) {				// Check if user pushbutton was pressed
			// Cycle through filter types
			current_filter_index = (current_filter_index + 1) % 5;

			// Update effect active flag (false only for BYPASS)
			efect_active = (current_filter_index != FILTER_BYPASS);

			if (radar_window_fill_samples >= RADAR_CHANNEL_SAMPLES) {
				radar_prepare_processing_window();
				ret_val = fft_iq_centered(radar_i_samples, radar_q_samples, spectrum_shifted);
				error_handling(ret_val);
				radar_update_peak_readout();
			}

			// Show current filter on LCD
			disp_refresh = true;
			// Force immediate display refresh by resetting loop counter
			disp_loop_count[MENU_THREE] = DISP_LOOP_M3;
		}

		if (ekg_process_if_ready(&ekg_latest)) {
			if (ekg_latest.r_peak) {
				ekg_last_peak_tick = HAL_GetTick();
			}
			if (MENU_get_active() == MENU_NINE) {
				disp_refresh = true;
			}
		}

		if ((MENU_get_active() == MENU_TEN) && dac_output_handle_touch()) {
			disp_refresh = true;
			disp_loop_count[MENU_TEN] = DISP_LOOP_M10;
		}

		if (radar_frame_ready()) {
			bool window_ready;
			uint32_t primask = __get_PRIMASK();
			__disable_irq();
			radar_clear_frame_ready();
			window_ready = radar_append_latest_chunk();
			if (primask == 0U) {
				__enable_irq();
			}

			if (window_ready) {
				radar_prepare_processing_window();

				// Use the rolling 50%-overlapped I/Q window for calculations.
				ret_val = fft_iq_centered(radar_i_samples, radar_q_samples, spectrum_shifted);
				error_handling(ret_val);
				radar_update_peak_readout();

				disp_refresh = true;      // Tell the display about the new data
			}
		}

		if (disp_refresh) {
			disp_refresh = false;

			switch (MENU_get_active()) {	// Show data for active user menu
			case MENU_NONE:	// Display help screen
				break;
			case MENU_ZERO:	// Info screen
				if (disp_loop_count[MENU_ZERO]++ >= DISP_LOOP_M0) {
					disp_loop_count[MENU_ZERO] = 0;
					disp_clear_data();
					disp_info();
				}
				break;
			case MENU_ONE:	// Time signal
				if (disp_loop_count[MENU_ONE]++ >= DISP_LOOP_M1) {
					uint32_t signal_start;
					float32_t signal_min;
					float32_t signal_max;
					disp_loop_count[MENU_ONE] = 0;
					signal_start = radar_get_recent_start_index(TIME_SIGNAL_POINTS);
					radar_get_time_scale(signal_start, TIME_SIGNAL_POINTS, &signal_min, &signal_max);
					disp_clear_data();
					disp_curves(&radar_i_samples[signal_start], TIME_SIGNAL_POINTS,
							signal_min,
							signal_max,
							LCD_COLOR_RED);
					disp_curves(&radar_q_samples[signal_start], TIME_SIGNAL_POINTS,
							signal_min,
							signal_max,
							LCD_COLOR_BLUE);
				}
				break;
			case MENU_TWO: // Frequency spectrum
				if (disp_loop_count[MENU_TWO]++ >= DISP_LOOP_M2) {
					uint32_t spectrum_start;
					uint32_t spectrum_count;
					float32_t spectrum_max;
					disp_loop_count[MENU_TWO] = 0;
					radar_get_spectrum_window(&spectrum_start, &spectrum_count, &spectrum_max);
					disp_clear_data();
					BSP_LCD_SetTextColor(LCD_COLOR_LIGHTGRAY);
					BSP_LCD_DrawLine(DISP_WIDTH / 2U, 0U, DISP_WIDTH / 2U, DISP_HEIGHT - 1U);
					disp_curves(&spectrum_shifted[spectrum_start], spectrum_count, 0.0f,
							spectrum_max, LCD_COLOR_BLUE);
					BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
					BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
					BSP_LCD_SetFont(&Font12);
					BSP_LCD_DisplayStringAt(2, 2, (uint8_t*) "-4 Hz", LEFT_MODE);
					BSP_LCD_DisplayStringAt(0, 2, (uint8_t*) "0 Hz", CENTER_MODE);
					BSP_LCD_DisplayStringAt(DISP_WIDTH - 36U, 2, (uint8_t*) "+4 Hz",
							LEFT_MODE);
				}
				break;
			case MENU_THREE:	// Effect Menu (Filter Selection)
				if (disp_loop_count[MENU_THREE]++ >= DISP_LOOP_M3) {
					disp_loop_count[MENU_THREE] = 0;
					disp_clear_data();
					
					// Display full-screen filter list with color highlighting for active filter
					BSP_LCD_SetFont(&Font24);
					
					// Calculate vertical spacing for 5 filters (280px height / 5 = 56px per item)
					const uint32_t start_y = 28;  // Start position (half spacing)
					const uint32_t spacing = 56;   // Vertical spacing between items
					
					// Display all 5 filter types
					for (uint8_t i = 0; i < 5; i++) {
						uint32_t y_pos = start_y + (i * spacing);
						
						// Set color: green for active filter, grey for others
						if (i == current_filter_index) {
							BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
						} else {
							BSP_LCD_SetTextColor(LCD_COLOR_LIGHTGRAY);
						}
						
						// Display filter name centered
						BSP_LCD_DisplayStringAt(0, y_pos, (uint8_t*)filter_names[i], CENTER_MODE);
					}
				}
				break;
			case MENU_FOUR:	// Level meter
				if (disp_loop_count[MENU_FOUR]++ >= DISP_LOOP_M4) {
					disp_loop_count[MENU_FOUR] = 0;
					disp_clear_data();
					disp_level(-10, -5, -12, -6); // TODO
				}
				break;
			case MENU_FIVE: // Frequency peak readout
				if (disp_loop_count[MENU_FIVE]++ >= DISP_LOOP_M5) {
					disp_loop_count[MENU_FIVE] = 0;
					disp_peak_frequencies();
				}
				break;
			case MENU_SIX:
			case MENU_SEVEN:
			case MENU_EIGHT:
				// ToDo ....
				break;
			case MENU_NINE:	// EKG BPM
				if (disp_loop_count[MENU_NINE]++ >= DISP_LOOP_M9) {
					char text[32];
					uint32_t now = HAL_GetTick();
					disp_loop_count[MENU_NINE] = 0;
					disp_clear_data();

					BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
					BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
					BSP_LCD_SetFont(&Font20);
					BSP_LCD_DisplayStringAt(0, 10, (uint8_t*) "EKG Monitor", CENTER_MODE);

					BSP_LCD_SetFont(&Font24);
					if (ekg_latest.bpm_valid) {
						snprintf(text, sizeof(text), "BPM: %3d", (int) (ekg_latest.bpm + 0.5f));
						BSP_LCD_SetTextColor(LCD_COLOR_RED);
					} else {
						snprintf(text, sizeof(text), "BPM: ---");
						BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
					}
					BSP_LCD_DisplayStringAt(0, 70, (uint8_t*) text, CENTER_MODE);

					BSP_LCD_SetFont(&Font16);
					BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
					snprintf(text, sizeof(text), "ADC: %4u", (unsigned int) ekg_latest.raw);
					BSP_LCD_DisplayStringAt(0, 130, (uint8_t*) text, CENTER_MODE);

					if ((now - ekg_last_peak_tick) < 140U) {
						BSP_LCD_SetTextColor(LCD_COLOR_RED);
						BSP_LCD_DisplayStringAt(0, 165, (uint8_t*) "R-PEAK", CENTER_MODE);
					} else {
						BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
						BSP_LCD_DisplayStringAt(0, 165, (uint8_t*) "      ", CENTER_MODE);
					}

					BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
					snprintf(text, sizeof(text), "Overrun: %lu",
							(unsigned long) ekg_get_overrun_count());
					BSP_LCD_DisplayStringAt(0, 210, (uint8_t*) text, CENTER_MODE);
				}
				break;
			case MENU_TEN:	// DAC output on PA5
				if (disp_loop_count[MENU_TEN]++ >= DISP_LOOP_M10) {
					disp_loop_count[MENU_TEN] = 0;
					disp_clear_data();
					disp_dac_output();
				}
				break;
			default:
				// Should never occur
				break;
			}
			//BSP_LED_Off(LED4);

		}

	}
}

static bool radar_append_latest_chunk(void) {
	uint32_t history_keep = RADAR_CHANNEL_SAMPLES - RADAR_FRAME_ADVANCE_SAMPLES;

	for (uint32_t i = 0; i < history_keep; i++) {
		radar_i_history[i] = radar_i_history[i + RADAR_FRAME_ADVANCE_SAMPLES];
		radar_q_history[i] = radar_q_history[i + RADAR_FRAME_ADVANCE_SAMPLES];
	}

	for (uint32_t i = 0; i < RADAR_FRAME_ADVANCE_SAMPLES; i++) {
		radar_i_history[history_keep + i] = radar_i_acquired[i];
		radar_q_history[history_keep + i] = radar_q_acquired[i];
	}

	if (radar_window_fill_samples < RADAR_CHANNEL_SAMPLES) {
		radar_window_fill_samples += RADAR_FRAME_ADVANCE_SAMPLES;
		if (radar_window_fill_samples > RADAR_CHANNEL_SAMPLES) {
			radar_window_fill_samples = RADAR_CHANNEL_SAMPLES;
		}
	}

	return (radar_window_fill_samples >= RADAR_CHANNEL_SAMPLES);
}

static void radar_prepare_processing_window(void) {
	for (uint32_t i = 0; i < RADAR_CHANNEL_SAMPLES; i++) {
		radar_i_samples[i] = radar_i_history[i];
		radar_q_samples[i] = radar_q_history[i];
	}

	if (efect_active) {
		biquad_df2t_t filter_l = fxL[current_filter_index];
		biquad_df2t_t filter_r = fxR[current_filter_index];

		biquad_reset(&filter_l);
		biquad_reset(&filter_r);

		biquad_process_buffer(&filter_l, radar_i_samples, RADAR_CHANNEL_SAMPLES);
		biquad_process_buffer(&filter_r, radar_q_samples, RADAR_CHANNEL_SAMPLES);
	}
}

static uint32_t radar_get_recent_start_index(uint32_t count) {
	if (count >= RADAR_CHANNEL_SAMPLES) {
		return 0U;
	}

	return RADAR_CHANNEL_SAMPLES - count;
}

static void radar_get_time_scale(uint32_t start_index, uint32_t count, float32_t *min_value,
		float32_t *max_value) {
	float32_t min_sample;
	float32_t max_sample;
	float32_t span;
	float32_t center;
	float32_t headroom;

	if ((min_value == 0) || (max_value == 0) || (count == 0U)) {
		return;
	}

	if (start_index >= RADAR_CHANNEL_SAMPLES) {
		return;
	}

	if (count > (RADAR_CHANNEL_SAMPLES - start_index)) {
		count = RADAR_CHANNEL_SAMPLES - start_index;
	}

	min_sample = radar_i_samples[start_index];
	max_sample = radar_i_samples[start_index];

	for (uint32_t i = start_index; i < (start_index + count); i++) {
		if (radar_i_samples[i] < min_sample) {
			min_sample = radar_i_samples[i];
		}
		if (radar_i_samples[i] > max_sample) {
			max_sample = radar_i_samples[i];
		}
		if (radar_q_samples[i] < min_sample) {
			min_sample = radar_q_samples[i];
		}
		if (radar_q_samples[i] > max_sample) {
			max_sample = radar_q_samples[i];
		}
	}

	span = max_sample - min_sample;
	if (span < TIME_SIGNAL_MIN_SPAN) {
		span = TIME_SIGNAL_MIN_SPAN;
	}

	center = 0.5f * (max_sample + min_sample);
	headroom = span * TIME_SIGNAL_HEADROOM;

	*min_value = center - (0.5f * span) - headroom;
	*max_value = center + (0.5f * span) + headroom;
}

static void radar_get_spectrum_window(uint32_t *start_index, uint32_t *count,
		float32_t *max_value) {
	uint32_t half_bins;
	uint32_t local_start;
	uint32_t local_count;
	float32_t bin_hz;
	float32_t peak = 0.0f;

	if ((start_index == 0) || (count == 0) || (max_value == 0)) {
		return;
	}

	bin_hz = (float32_t) RADAR_SAMPLE_RATE_HZ / (float32_t) RADAR_CHANNEL_SAMPLES;
	half_bins = (uint32_t) (SPECTRUM_DISPLAY_HZ / bin_hz);
	if (((float32_t) half_bins * bin_hz) < SPECTRUM_DISPLAY_HZ) {
		half_bins++;
	}
	if (half_bins >= (RADAR_CHANNEL_SAMPLES / 2U)) {
		half_bins = (RADAR_CHANNEL_SAMPLES / 2U) - 1U;
	}

	local_start = (RADAR_CHANNEL_SAMPLES / 2U) - half_bins;
	local_count = (2U * half_bins) + 1U;

	for (uint32_t i = 0; i < local_count; i++) {
		float32_t value = spectrum_shifted[local_start + i];
		if (value > peak) {
			peak = value;
		}
	}

	if (peak < SPECTRUM_MIN_DISPLAY_MAX) {
		peak = SPECTRUM_MIN_DISPLAY_MAX;
	}

	*start_index = local_start;
	*count = local_count;
	*max_value = peak * SPECTRUM_HEADROOM;
}

static void radar_update_peak_readout(void)
{
	uint32_t center_bin = RADAR_CHANNEL_SAMPLES / 2U;
	uint32_t half_bins;
	uint32_t neg_start;
	uint32_t pos_end;
	uint32_t neg_peak_bin = center_bin;
	uint32_t pos_peak_bin = center_bin;
	float32_t neg_peak_mag = 0.0f;
	float32_t pos_peak_mag = 0.0f;
	float32_t bin_hz;

	bin_hz = (float32_t) RADAR_SAMPLE_RATE_HZ / (float32_t) RADAR_CHANNEL_SAMPLES;
	half_bins = (uint32_t) (SPECTRUM_DISPLAY_HZ / bin_hz);
	if (((float32_t) half_bins * bin_hz) < SPECTRUM_DISPLAY_HZ) {
		half_bins++;
	}
	if (half_bins >= center_bin) {
		half_bins = center_bin - 1U;
	}

	neg_start = center_bin - half_bins;
	pos_end = center_bin + half_bins + 1U;

	for (uint32_t i = neg_start; i < center_bin; i++) {
		float32_t value = spectrum_shifted[i];
		if (value > neg_peak_mag) {
			neg_peak_mag = value;
			neg_peak_bin = i;
		}
	}

	for (uint32_t i = center_bin + 1U; i < pos_end; i++) {
		float32_t value = spectrum_shifted[i];
		if (value > pos_peak_mag) {
			pos_peak_mag = value;
			pos_peak_bin = i;
		}
	}

	spectrum_neg_peak_valid = (neg_peak_mag > SPECTRUM_PEAK_VALID_THRESHOLD);
	spectrum_pos_peak_valid = (pos_peak_mag > SPECTRUM_PEAK_VALID_THRESHOLD);

	if (spectrum_neg_peak_valid) {
		spectrum_neg_peak_hz = ((float32_t) neg_peak_bin - (float32_t) center_bin) * bin_hz;
	} else {
		spectrum_neg_peak_hz = 0.0f;
	}

	if (spectrum_pos_peak_valid) {
		spectrum_pos_peak_hz = ((float32_t) pos_peak_bin - (float32_t) center_bin) * bin_hz;
	} else {
		spectrum_pos_peak_hz = 0.0f;
	}

	if (spectrum_neg_peak_valid && spectrum_pos_peak_valid) {
		if (neg_peak_mag >= (SPECTRUM_PEAK_DOMINANCE_RATIO * pos_peak_mag)) {
			spectrum_pos_peak_valid = false;
			spectrum_pos_peak_hz = 0.0f;
		} else if (pos_peak_mag >= (SPECTRUM_PEAK_DOMINANCE_RATIO * neg_peak_mag)) {
			spectrum_neg_peak_valid = false;
			spectrum_neg_peak_hz = 0.0f;
		}
	}
}

static void touch_get_adjusted_state(TS_StateTypeDef *touch_state)
{
	if (touch_state == 0) {
		return;
	}

	BSP_TS_GetState(touch_state);

#ifdef EVAL_REV_E
	touch_state->Y = BSP_LCD_GetYSize() - touch_state->Y;
#endif
#ifdef FLIPPED_LCD
	touch_state->X = BSP_LCD_GetXSize() - touch_state->X;
	touch_state->Y = BSP_LCD_GetYSize() - touch_state->Y;
#endif
}

static bool touch_is_inside_rect(uint16_t x, uint16_t y, uint16_t rect_x,
		uint16_t rect_y, uint16_t rect_width, uint16_t rect_height)
{
	return (x >= rect_x) && (x < (rect_x + rect_width)) && (y >= rect_y)
			&& (y < (rect_y + rect_height));
}

static void dac_output_step(float delta_voltage)
{
	dac_output_set_voltage(dac_output_get_voltage() + delta_voltage);
}

static bool dac_output_handle_touch(void)
{
	TS_StateTypeDef touch_state;
	bool touch_just_pressed;
	uint16_t touch_x;
	uint16_t touch_y;
	uint16_t old_code = dac_output_get_code();

	touch_get_adjusted_state(&touch_state);
	touch_just_pressed = (!dac_touch_was_detected && touch_state.TouchDetected);
	dac_touch_was_detected = touch_state.TouchDetected;

	if (!touch_state.TouchDetected) {
		return false;
	}

	touch_x = touch_state.X;
	touch_y = touch_state.Y;

	if (touch_y >= DISP_HEIGHT) {
		return false;
	}

	if (touch_is_inside_rect(touch_x, touch_y, DAC_SLIDER_X, DAC_SLIDER_Y,
			DAC_SLIDER_WIDTH, DAC_SLIDER_HEIGHT)) {
		float voltage = ((float) (touch_x - DAC_SLIDER_X)
				/ (float) (DAC_SLIDER_WIDTH - 1U)) * DAC_OUTPUT_MAX_VOLTAGE;
		dac_output_set_voltage(voltage);
	} else if (touch_just_pressed
			&& touch_is_inside_rect(touch_x, touch_y, DAC_MINUS_X, DAC_BUTTON_Y,
					DAC_BUTTON_WIDTH, DAC_BUTTON_HEIGHT)) {
		dac_output_step(-DAC_TOUCH_STEP_VOLTAGE);
	} else if (touch_just_pressed
			&& touch_is_inside_rect(touch_x, touch_y, DAC_PLUS_X, DAC_BUTTON_Y,
					DAC_BUTTON_WIDTH, DAC_BUTTON_HEIGHT)) {
		dac_output_step(DAC_TOUCH_STEP_VOLTAGE);
	}

	return (old_code != dac_output_get_code());
}

static void disp_peak_frequencies(void)
{
	char text[32];

	disp_clear_data();

	BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);

	BSP_LCD_SetFont(&Font20);
	BSP_LCD_DisplayStringAt(0, 10, (uint8_t*) "Peak Frequencies", CENTER_MODE);

	BSP_LCD_SetFont(&Font16);
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
	BSP_LCD_DisplayStringAt(0, 58, (uint8_t*) "Positive peak", CENTER_MODE);

	BSP_LCD_SetFont(&Font24);
	if (spectrum_pos_peak_valid) {
		snprintf(text, sizeof(text), "%+.2f Hz", spectrum_pos_peak_hz);
		BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
	} else {
		snprintf(text, sizeof(text), "--- Hz");
		BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
	}
	BSP_LCD_DisplayStringAt(0, 84, (uint8_t*) text, CENTER_MODE);

	BSP_LCD_SetFont(&Font16);
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
	BSP_LCD_DisplayStringAt(0, 158, (uint8_t*) "Negative peak", CENTER_MODE);

	BSP_LCD_SetFont(&Font24);
	if (spectrum_neg_peak_valid) {
		snprintf(text, sizeof(text), "%+.2f Hz", spectrum_neg_peak_hz);
		BSP_LCD_SetTextColor(LCD_COLOR_RED);
	} else {
		snprintf(text, sizeof(text), "--- Hz");
		BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
	}
	BSP_LCD_DisplayStringAt(0, 184, (uint8_t*) text, CENTER_MODE);
}

static void disp_dac_output(void)
{
	char text[32];
	uint32_t millivolts = (uint32_t) ((dac_output_get_voltage() * 1000.0f) + 0.5f);
	uint16_t code = dac_output_get_code();
	uint32_t fill_width = ((uint32_t) code * DAC_SLIDER_WIDTH) / DAC_OUTPUT_MAX_CODE;
	uint32_t marker_x = DAC_SLIDER_X
			+ (((uint32_t) code * (DAC_SLIDER_WIDTH - 1U)) / DAC_OUTPUT_MAX_CODE);

	BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);

	BSP_LCD_SetFont(&Font20);
	BSP_LCD_DisplayStringAt(0, 12, (uint8_t*) "DAC Output PA5", CENTER_MODE);

	BSP_LCD_SetFont(&Font24);
	snprintf(text, sizeof(text), "%lu.%03lu V",
			(unsigned long) (millivolts / 1000U),
			(unsigned long) (millivolts % 1000U));
	BSP_LCD_DisplayStringAt(0, 48, (uint8_t*) text, CENTER_MODE);

	BSP_LCD_SetFont(&Font16);
	snprintf(text, sizeof(text), "Code: %4u / %u", code, DAC_OUTPUT_MAX_CODE);
	BSP_LCD_DisplayStringAt(0, 84, (uint8_t*) text, CENTER_MODE);

	BSP_LCD_SetTextColor(LCD_COLOR_LIGHTGRAY);
	BSP_LCD_FillRect(DAC_SLIDER_X, DAC_SLIDER_Y, DAC_SLIDER_WIDTH, DAC_SLIDER_HEIGHT);
	if (fill_width > 0U) {
		BSP_LCD_SetTextColor(LCD_COLOR_LIGHTGREEN);
		BSP_LCD_FillRect(DAC_SLIDER_X, DAC_SLIDER_Y, fill_width, DAC_SLIDER_HEIGHT);
	}
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
	BSP_LCD_DrawRect(DAC_SLIDER_X, DAC_SLIDER_Y, DAC_SLIDER_WIDTH, DAC_SLIDER_HEIGHT);
	BSP_LCD_DrawVLine((uint16_t) marker_x, DAC_SLIDER_Y - 6U, DAC_SLIDER_HEIGHT + 12U);
	BSP_LCD_SetFont(&Font12);
	BSP_LCD_DisplayStringAt(DAC_SLIDER_X, DAC_SLIDER_Y + DAC_SLIDER_HEIGHT + 10U,
			(uint8_t*) "0.0V", LEFT_MODE);
	BSP_LCD_DisplayStringAt(DAC_SLIDER_X + DAC_SLIDER_WIDTH - 34U,
			DAC_SLIDER_Y + DAC_SLIDER_HEIGHT + 10U, (uint8_t*) "3.3V", LEFT_MODE);

	BSP_LCD_SetTextColor(LCD_COLOR_LIGHTBLUE);
	BSP_LCD_FillRect(DAC_MINUS_X, DAC_BUTTON_Y, DAC_BUTTON_WIDTH, DAC_BUTTON_HEIGHT);
	BSP_LCD_FillRect(DAC_PLUS_X, DAC_BUTTON_Y, DAC_BUTTON_WIDTH, DAC_BUTTON_HEIGHT);
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
	BSP_LCD_DrawRect(DAC_MINUS_X, DAC_BUTTON_Y, DAC_BUTTON_WIDTH, DAC_BUTTON_HEIGHT);
	BSP_LCD_DrawRect(DAC_PLUS_X, DAC_BUTTON_Y, DAC_BUTTON_WIDTH, DAC_BUTTON_HEIGHT);
	BSP_LCD_SetBackColor(LCD_COLOR_LIGHTBLUE);
	BSP_LCD_SetFont(&Font24);
	BSP_LCD_DisplayStringAt(DAC_MINUS_X + 26U, DAC_BUTTON_Y + 8U, (uint8_t*) "-",
			LEFT_MODE);
	BSP_LCD_DisplayStringAt(DAC_PLUS_X + 28U, DAC_BUTTON_Y + 8U, (uint8_t*) "+",
			LEFT_MODE);

	BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
	BSP_LCD_SetFont(&Font16);
	BSP_LCD_DisplayStringAt(0, 244, (uint8_t*) "Tap bar or +/- 0.1 V", CENTER_MODE);
}

/** ***************************************************************************
 * @brief System Clock Configuration
 *
 *****************************************************************************/
static void SystemClock_Config(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };
	RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = { 0 };
	/* Configure the main internal regulator output voltage */
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
	/* Initialize High Speed External Oscillator and PLL circuits */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLM = 8;
	RCC_OscInitStruct.PLL.PLLN = 336;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
	RCC_OscInitStruct.PLL.PLLQ = 7;
	HAL_RCC_OscConfig(&RCC_OscInitStruct);
	/* Initialize gates and clock dividers for CPU, AHB and APB busses */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
	HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
	/* Initialize PLL and clock divider for the LCD */
	PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
	PeriphClkInitStruct.PLLSAI.PLLSAIN = 192;
	PeriphClkInitStruct.PLLSAI.PLLSAIR = 4;
	PeriphClkInitStruct.PLLSAIDivR = RCC_PLLSAIDIVR_8;
	HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
	/* Set clock prescaler for ADCs */
	ADC->CCR |= ADC_CCR_ADCPRE_0;
}

/** ***************************************************************************
 * @brief Disable the GYRO on the microcontroller board.
 *
 * @note MISO of the GYRO is connected to PF8 and CS to PC1.
 * @n Some times the GYRO goes into an undefined mode at startup
 * and pulls the MISO low or high thus blocking the analog input on PF8.
 * @n The simplest solution is to pull the CS of the GYRO low for a short while
 * which is done with the code below.
 * @n PF8 is also reconfigured.
 * @n An other solution would be to remove the GYRO
 * from the microcontroller board by unsoldering it.
 *****************************************************************************/
static void gyro_disable(void) {
	__HAL_RCC_GPIOC_CLK_ENABLE();		// Enable Clock for GPIO port C
	/* Disable PC1 and PF8 first */
	GPIOC->MODER &= ~GPIO_MODER_MODER1_Msk;	// Reset mode for PC1
	GPIOC->MODER |= 1UL << GPIO_MODER_MODER1_Pos;	// Set PC1 as output
	GPIOC->BSRR |= GPIO_BSRR_BR1;		// Set GYRO (CS) to 0 for a short time
	HAL_Delay(10);						// Wait some time
	GPIOC->MODER |= 3UL << GPIO_MODER_MODER1_Pos;	// Analog PC1 = ADC123_IN11
	__HAL_RCC_GPIOF_CLK_ENABLE();		// Enable Clock for GPIO port F
	GPIOF->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED8_Msk;	// Reset speed of PF8
	GPIOF->AFR[1] &= ~GPIO_AFRH_AFSEL8_Msk;	// Reset alternate function of PF8
	GPIOF->PUPDR &= ~GPIO_PUPDR_PUPD8_Msk;	// Reset pulup/down of PF8
	HAL_Delay(10);						// Wait some time
	GPIOF->MODER |= 3UL << GPIO_MODER_MODER8_Pos; // Analog mode PF8 = ADC3_IN4
}

void error_handling(HAL_StatusTypeDef error) {
	if (error != HAL_OK) {
		while (1) {

		}
	}
}

// Write-function for debugging over console
int _write(int file, char *ptr, int len) {
	for (int i = 0; i < len; i++) {
		ITM_SendChar(*ptr++);
	}
	return len;
}

// Default function implementations required to prevent build errors.
__attribute__((weak)) void _close(void) {
}
__attribute__((weak)) void _lseek(void) {
}
__attribute__((weak)) void _read(void) {
}

__attribute__((weak)) void _fstat(void) {
}

__attribute__((weak)) void _isatty(void) {
}
/*__attribute__((weak)) void _write(void)
 {
 }*/
