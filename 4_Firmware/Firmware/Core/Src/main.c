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
#include "ui_layout.h"

#include "dac_output.h"
#include "fft.h"
#include "display.h"
#include "radar.h"
#include "filters.h"
#include "ekg.h"
#include "radar_heartrate.h"
#include "openlog_uart.h"

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

static bool disp_refresh;			///< Display should be refreshed

static uint8_t effect_active = 0;

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
static bool log_touch_was_detected = false;

static radar_hr_state_t  radar_hr_state;
static radar_hr_output_t radar_hr_output = {0};

/******************************************************************************
 * Functions
 *****************************************************************************/
static void SystemClock_Config(void);	///< System Clock Configuration
static void gyro_disable(void);			///< Disable the onboard gyroscope
static void error_handling(HAL_StatusTypeDef error);
static void menu_request_refresh(MENU_item_t menu_item, bool immediate);
static void touch_get_adjusted_state(TS_StateTypeDef *touch_state);
static bool touch_is_inside_rect(uint16_t x, uint16_t y, uint16_t rect_x,
		uint16_t rect_y, uint16_t rect_width, uint16_t rect_height);
static void dac_output_step(float delta_voltage);
static bool dac_output_handle_touch(void);
static bool openlog_handle_touch(void);

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

	BSP_LED_Init(LED3);					// Available as general status LED
	BSP_LED_Init(LED4);					// Toggled in radar DMA IRQ (new frame ready)

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
	
	// Start in bypass mode because current_filter_index defaults to FILTER_BYPASS.
	effect_active = (current_filter_index != FILTER_BYPASS);

	ret_val = fft_init();
	error_handling(ret_val);

	ekg_init(NULL);  // AD8232 on PF6 (ADC3_IN4), interrupt-driven sampling

	radar_hr_init(&radar_hr_state, NULL);  // Radar HR estimator with defaults

	openlog_init();  // TX-only OpenLog on USART6 / PG14 (default: OFF)

	/* Infinite while loop */
	while (1) {							// Infinitely loop in main function

		/* Comment next line if touchscreen interrupt is enabled */
		MENU_check_transition();
		MENU_item_t menu_transition = MENU_get_transition();
		MENU_item_t active_menu;
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
			menu_request_refresh(menu_transition, true);
			break;
		default:	// Should never occur
			break;
		}

		active_menu = MENU_get_active();

		if (PB_pressed()) {				// Check if user pushbutton was pressed
			// Cycle through filter types
			current_filter_index = (current_filter_index + 1) % 5;

			// Update effect active flag (false only for BYPASS)
			effect_active = (current_filter_index != FILTER_BYPASS);

			if (radar_window_fill_samples >= RADAR_CHANNEL_SAMPLES) {
				radar_prepare_processing_window(radar_i_samples, radar_q_samples,
						radar_i_history, radar_q_history,
						effect_active, fxL, fxR, current_filter_index);
				ret_val = fft_iq_centered(radar_i_samples, radar_q_samples, spectrum_shifted);
				error_handling(ret_val);
				radar_update_peak_readout(spectrum_shifted,
						SPECTRUM_DISPLAY_HZ,
						RADAR_SPECTRUM_PEAK_VALID_THRESHOLD,
						RADAR_SPECTRUM_PEAK_DOMINANCE_RATIO,
						&spectrum_pos_peak_hz,
						&spectrum_neg_peak_hz,
						&spectrum_pos_peak_valid,
						&spectrum_neg_peak_valid);
			}

			// Show current filter on LCD
			menu_request_refresh(MENU_THREE, true);
		}

		if (ekg_process_if_ready(&ekg_latest)) {
			if (ekg_latest.r_peak) {
				ekg_last_peak_tick = HAL_GetTick();
			}
			if (active_menu == MENU_NINE) {
				menu_request_refresh(MENU_NINE, false);
			}
		}

		if ((active_menu == MENU_TEN) && dac_output_handle_touch()) {
			menu_request_refresh(MENU_TEN, true);
		}

		if ((active_menu == MENU_SEVEN) && openlog_handle_touch()) {
			menu_request_refresh(MENU_SEVEN, true);
		}

		if (radar_frame_ready()) {
			bool window_ready;
			uint32_t primask = __get_PRIMASK();
			__disable_irq();
			radar_clear_frame_ready();
			window_ready = radar_append_latest_chunk(radar_i_history, radar_q_history,
					radar_i_acquired, radar_q_acquired, &radar_window_fill_samples);
			if (primask == 0U) {
				__enable_irq();
			}

			if (window_ready) {
				radar_prepare_processing_window(radar_i_samples, radar_q_samples,
						radar_i_history, radar_q_history,
						effect_active, fxL, fxR, current_filter_index);

				// Use the rolling 50%-overlapped I/Q window for calculations.
				ret_val = fft_iq_centered(radar_i_samples, radar_q_samples, spectrum_shifted);
				error_handling(ret_val);
				radar_update_peak_readout(spectrum_shifted,
						SPECTRUM_DISPLAY_HZ,
						RADAR_SPECTRUM_PEAK_VALID_THRESHOLD,
						RADAR_SPECTRUM_PEAK_DOMINANCE_RATIO,
						&spectrum_pos_peak_hz,
						&spectrum_neg_peak_hz,
						&spectrum_pos_peak_valid,
						&spectrum_neg_peak_valid);

				radar_hr_process_frame(&radar_hr_state, spectrum_shifted, &radar_hr_output);

				/* Log one CSV row per processed radar frame (best-effort). */
				openlog_write_row(HAL_GetTick(),
						radar_hr_output.bpm,
						radar_hr_output.valid,
						radar_hr_output.state);

				disp_refresh = true;      // Tell the display about the new data
			}
		}

		if (disp_refresh) {
			disp_menu_data_t menu_data;
			disp_refresh = false;

			menu_data.radar_i_samples = radar_i_samples;
			menu_data.radar_q_samples = radar_q_samples;
			menu_data.spectrum_shifted = spectrum_shifted;
			menu_data.current_filter_index = current_filter_index;
			menu_data.filter_names = filter_names;
			menu_data.ekg_latest = ekg_latest;
			menu_data.ekg_last_peak_tick = ekg_last_peak_tick;
			menu_data.spectrum_pos_peak_hz = spectrum_pos_peak_hz;
			menu_data.spectrum_neg_peak_hz = spectrum_neg_peak_hz;
			menu_data.spectrum_pos_peak_valid = spectrum_pos_peak_valid;
			menu_data.spectrum_neg_peak_valid = spectrum_neg_peak_valid;
			menu_data.radar_hr_bpm   = radar_hr_output.bpm;
			menu_data.radar_hr_valid = radar_hr_output.valid;
			menu_data.radar_hr_state = radar_hr_output.state;
			menu_data.logging_enabled    = openlog_is_enabled();
			menu_data.logging_drop_count = openlog_get_drop_count();

			disp_menu_render(active_menu, &menu_data);
		}

	}
}

/** ***************************************************************************
 * @brief Request a display update.
 * @param menu_item Menu entry to force immediately when requested.
 * @param immediate true = force renderer counter to update now, false = normal pacing.
 *****************************************************************************/
static void menu_request_refresh(MENU_item_t menu_item, bool immediate) {
	disp_refresh = true;

	if (immediate) {
		disp_menu_force_refresh(menu_item);
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

/** ***************************************************************************
 * @brief Handle touch events on the MENU_SEVEN logger toggle button.
 *
 * Uses edge detection (tap) consistent with the DAC touch pattern.
 *
 * @return true if logging state changed (display should refresh).
 *****************************************************************************/
static bool openlog_handle_touch(void)
{
	TS_StateTypeDef touch_state;
	bool touch_just_pressed;

	touch_get_adjusted_state(&touch_state);
	touch_just_pressed = (!log_touch_was_detected && touch_state.TouchDetected);
	log_touch_was_detected = touch_state.TouchDetected;

	if (!touch_just_pressed) {
		return false;
	}

	/* Ignore touches inside the menu bar. */
	if (touch_state.Y >= DISP_HEIGHT) {
		return false;
	}

	if (touch_is_inside_rect(touch_state.X, touch_state.Y,
			LOG_TOGGLE_X, LOG_TOGGLE_Y, LOG_TOGGLE_WIDTH, LOG_TOGGLE_HEIGHT)) {
		openlog_set_enabled(!openlog_is_enabled());
		return true;
	}

	return false;
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
