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

#include "calc.h"
#include "display.h"
#include "audio_codec.h"
#include "filters.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
// Display refresh loop limits for each menu (to prevent flicker)
#define DISP_LOOP_M0 	10	// Info screen
#define DISP_LOOP_M1 	4	// Light bars
#define DISP_LOOP_M2 	10  // Time signal
#define DISP_LOOP_M3 	4	// Spectrum analyzer
#define DISP_LOOP_M4 	4	// Effect Menu (Filter Selection)
#define DISP_LOOP_M5 	4	// Audio level
#define DISP_LOOP_M6 	4	// ...
#define DISP_LOOP_M7 	4	// ...
#define DISP_LOOP_M8 	4	// ...
#define DISP_LOOP_M9 	4	// ...

// Define the maximum number of points for the time signal (Display 240 x 320 pixels)
#define MAX_TIME_SIGNAL_POINTS 240

// Define time_signal_points based on AUDIO_CHANNEL_SIZE, ensuring it's capped at MAX_TIME_SIGNAL_POINTS
#define TIME_SIGNAL_POINTS (AUDIO_CHANNEL_SIZE > MAX_TIME_SIGNAL_POINTS ? MAX_TIME_SIGNAL_POINTS : AUDIO_CHANNEL_SIZE)


/******************************************************************************
 * Variables
 *****************************************************************************/

static float32_t left_channel_samples[AUDIO_CHANNEL_SIZE];
static float32_t right_channel_samples[AUDIO_CHANNEL_SIZE];

static float32_t spectrum_left[AUDIO_CHANNEL_SIZE / 2];
static float32_t spectrum_right[AUDIO_CHANNEL_SIZE / 2];

static float32_t light_avgs[NUMBER_OF_COLORS];
static float32_t light_peaks[NUMBER_OF_COLORS];

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

/******************************************************************************
 * Functions
 *****************************************************************************/
static void SystemClock_Config(void);	///< System Clock Configuration
static void gyro_disable(void);			///< Disable the onboard gyroscope
static void error_handling(HAL_StatusTypeDef error);

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

	ret_val = codec_init(left_channel_samples, right_channel_samples, AUDIO_CHANNEL_SIZE);	// Audio Codec init
	error_handling(ret_val);

	codec_start();

	/* --------------------------------------------------------------------
	 * Audio effect configuration (biquad)
	 *
	 * Pre-configure all 5 filter types for instant switching.
	 * Notes:
	 * - fs must match the *actual* audio stream sample-rate.
	 *   In CODEC mode (CS4271) this is typically 48 kHz.
	 * - Q controls resonance / bandwidth. Q=0.707 is a good general default.
	 * -------------------------------------------------------------------- */
	const float32_t fs = 100.0f;           /* radar sampling rate */
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

	ret_val = calc_init();
	error_handling(ret_val);

	/* Infinite while loop */
	while (1) {							// Infinitely loop in main function

		/* Comment next line if touchscreen interrupt is enabled */
		MENU_check_transition();
		switch (MENU_get_transition()) {	// Handle user menu transitions
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
			disp_refresh = true;	// Switch to new menu item
			break;
		default:	// Should never occur
			break;
		}

		if (PB_pressed()) {				// Check if user pushbutton was pressed
			// Cycle through filter types
			current_filter_index = (current_filter_index + 1) % 5;
			
			// Reset filter state to avoid artifacts from previous filter
			biquad_reset(&fxL[current_filter_index]);
			biquad_reset(&fxR[current_filter_index]);
			
			// Update effect active flag (false only for BYPASS)
			efect_active = (current_filter_index != FILTER_BYPASS);

			// Show current filter on LCD
			disp_refresh = true;
			// Force immediate display refresh by resetting loop counter
			disp_loop_count[MENU_FOUR] = DISP_LOOP_M4;
		}

		if (codec_data_ready()) {
			codec_clear_data_ready();
			//BSP_LED_On(LED4);

			// Check if right channel is present (codec mode only)
			if (!codec_is_right_channel_present()) {
				// Only left channel detected - mirror it to right for mono output
				codec_mirror_left_channel();
			}

			if (efect_active) {
				/*
				 * Apply the currently selected biquad filter to each channel in-place.
				 * BYPASS mode skips processing entirely via efect_active flag.
				 */
				biquad_process_buffer(&fxL[current_filter_index], left_channel_samples, AUDIO_CHANNEL_SIZE);
				biquad_process_buffer(&fxR[current_filter_index], right_channel_samples, AUDIO_CHANNEL_SIZE);
			}

			// Update codec output buffer with processed audio samples
			codec_update_output_buffer(0, left_channel_samples, AUDIO_CHANNEL_SIZE);
			codec_update_output_buffer(1, right_channel_samples, AUDIO_CHANNEL_SIZE);


			// Use the audio data in left_channel_samples and right_channel_samples for the different calculations.
			ret_val = calc_freq(left_channel_samples, spectrum_left);
			error_handling(ret_val);
			ret_val = calc_freq(right_channel_samples, spectrum_right);
			error_handling(ret_val);
			

			disp_refresh = true;      // Tell the display about the new data
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
			case MENU_ONE:	// Light bars
				if (disp_loop_count[MENU_ONE]++ >= DISP_LOOP_M1) {
					disp_loop_count[MENU_ONE] = 0;
					disp_clear_data();
					disp_light_bars(light_avgs, light_peaks);
				}
				break;
			case MENU_TWO:	// Time signal
				if (disp_loop_count[MENU_TWO]++ >= DISP_LOOP_M2) {
					disp_loop_count[MENU_TWO] = 0;
					disp_clear_data();
					disp_curves(left_channel_samples, TIME_SIGNAL_POINTS,
							-(1 << (CODEC_ADC_RES - 1)) / 2,
							(1 << (CODEC_ADC_RES - 1)) / 2,
							LCD_COLOR_RED);
					disp_curves(right_channel_samples, TIME_SIGNAL_POINTS,
							-(1 << (CODEC_ADC_RES - 1)) / 2,
							(1 << (CODEC_ADC_RES - 1)) / 2,
							LCD_COLOR_BLUE);
				}
				break;
			case MENU_THREE: // Frequency spectrum
				if (disp_loop_count[MENU_THREE]++ >= DISP_LOOP_M3) {
					disp_loop_count[MENU_THREE] = 0;
					disp_clear_data();
					// Note: x axis is in bins, not in Hz
					disp_curves(spectrum_left, AUDIO_CHANNEL_SIZE / 2, 0, 0.05, LCD_COLOR_RED);
					disp_curves(spectrum_right, AUDIO_CHANNEL_SIZE / 2, 0, 0.05, LCD_COLOR_BLUE);
				}
				break;
			case MENU_FOUR:	// Effect Menu (Filter Selection)
				if (disp_loop_count[MENU_FOUR]++ >= DISP_LOOP_M4) {
					disp_loop_count[MENU_FOUR] = 0;
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
			case MENU_FIVE:	// Audio level
				if (disp_loop_count[MENU_FIVE]++ >= DISP_LOOP_M5) {
					disp_loop_count[MENU_FIVE] = 0;
					disp_clear_data();
					disp_level(-10, -5, -12, -6); // TODO
				}
				break;
			case MENU_SIX:
			case MENU_SEVEN:
			case MENU_EIGHT:
			case MENU_NINE:
				// ToDo ....
				break;
			default:
				// Should never occur
				break;
			}
			//BSP_LED_Off(LED4);

		}

	}
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

