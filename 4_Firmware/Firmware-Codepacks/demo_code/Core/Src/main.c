/** ***************************************************************************
 * @file
 * @brief Sets up the microcontroller, the clock system and the peripherals.
 *
 * Initialization is done for the system, the blue user button, the user LEDs,
 * and the LCD display with the touchscreen.
 * @n Then the code enters an infinite while-loop, where it checks for
 * user input and starts the requested demonstration.
 *
 * @author  Hanspeter Hochreutener, hhrt@zhaw.ch
 * @date	17.06.2021
 *****************************************************************************/


/******************************************************************************
 * Includes
 *****************************************************************************/
#include <stdio.h>

#include "stm32f4xx.h"
#include "stm32f429i_discovery.h"
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_ts.h"

#include "main.h"
#include "pushbutton.h"
#include "menu.h"
#include "measuring.h"
#include "hr.h"


/******************************************************************************
 * Defines
 *****************************************************************************/
#define DEBUG_SELFTEST_FAILED_INDEX_NONE 0xFFFFU


/******************************************************************************
 * Variables
 *****************************************************************************/
typedef struct {
	bool has_result;
	bool last_overrun;
	HR_Status_t last_hr_status;
	HR_Result_t last_result;
	HR_SelfTestResult_t self_test;
} APP_DebugState_t;


/******************************************************************************
 * Functions
 *****************************************************************************/
static void SystemClock_Config(void);	///< System Clock Configuration
static void gyro_disable(void);			///< Disable the onboard gyroscope
static void APP_draw_debug_screen(const APP_DebugState_t *state);
static void APP_show_debug_screen_blocking(const APP_DebugState_t *state);


/** ***************************************************************************
 * @brief  Main function
 * @return not used because main ends in an infinite loop
 *
 * Initialization and infinite while loop
 *****************************************************************************/
int main(void) {
	MEAS_RadarFrame_t radar_frame;
	HR_Result_t radar_result;
	HR_SelfTestResult_t self_test_result;
	APP_DebugState_t debug_state = {0};
	bool hr_ready = false;
	bool measuring_active = false;

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
	// BSP_TS_ITConfig();					// Enable Touchscreen interrupt

	PB_init();							// Initialize the user pushbutton
	PB_enableIRQ();						// Enable interrupt on user pushbutton

	BSP_LED_Init(LED3);					// Toggles in while loop
	BSP_LED_Init(LED4);					// Is toggled by user button

	MENU_draw();						// Draw the menu
	MENU_hint();						// Show hint at startup
	MENU_set_entry(MENU_FIVE, (MENU_entry_t){"Debug", "view", LCD_COLOR_BLACK, LCD_COLOR_YELLOW});
	MENU_draw();

	gyro_disable();						// Disable gyro, use those analog inputs

	MEAS_GPIO_analog_init();			// Configure GPIOs in analog mode
	MEAS_timer_init();					// Configure the timer
	HR_init();
	debug_state.self_test.passed_all = false;
	debug_state.self_test.total_tests = 0U;
	debug_state.self_test.passed_tests = 0U;
	debug_state.self_test.first_failed_test = DEBUG_SELFTEST_FAILED_INDEX_NONE;
	debug_state.self_test.expected_bin = 0U;
	debug_state.self_test.detected_bin = 0U;
	debug_state.self_test.expected_peak_hz = 0.0f;
	debug_state.self_test.detected_peak_hz = 0.0f;
	debug_state.self_test.last_status = HR_STATUS_INVALID_ARG;
	if (HR_run_self_test(&self_test_result) == HR_STATUS_OK) {
		debug_state.self_test = self_test_result;
	} else {
		debug_state.self_test = self_test_result;
	}


	/* Infinite while loop */
	while (1) {							// Infinitely loop in main function
		if (MEAS_is_frame_ready()) {
			if (MEAS_consume_radar_frame(&radar_frame)) {
				debug_state.last_overrun = radar_frame.dma_overrun;
				debug_state.last_hr_status = HR_process_radar_frame(&radar_frame, &radar_result);
				if (debug_state.last_hr_status == HR_STATUS_OK) {
					debug_state.last_result = radar_result;
					debug_state.has_result = true;
					hr_ready = true;
				}
				measuring_active = false;
				BSP_LED_Off(LED4);
			}
		}

		if (MEAS_data_ready) {			// Show data if new data available
			if (!MEAS_is_frame_ready()) {
				MEAS_data_ready = false;
				MEAS_show_data();
			}
		}

		if (PB_pressed()) {				// Check if user pushbutton was pressed
			if (!measuring_active) {
				if (MEAS_start_radar_single()) {
					measuring_active = true;
					hr_ready = false;
					BSP_LED_On(LED4);
				}
			}
		}

		/* Comment next line if touchscreen interrupt is enabled */
		MENU_check_transition();

		switch (MENU_get_transition()) {	// Handle user menu choice
		case MENU_NONE:					// No transition => do nothing
			break;
		case MENU_ZERO:
			ADC3_IN4_single_init();
			ADC3_IN4_single_read();
			break;
		case MENU_ONE:
			ADC3_IN4_timer_init();
			ADC3_IN4_timer_start();
			break;
		case MENU_TWO:
			ADC3_IN4_DMA_init();
			ADC3_IN4_DMA_start();
			break;
		case MENU_THREE:
			if (!measuring_active) {
				if (MEAS_start_radar_single()) {
					measuring_active = true;
					hr_ready = false;
					BSP_LED_On(LED4);
				}
			}
			break;
		case MENU_FOUR:
			ADC2_IN13_IN5_scan_init();
			ADC2_IN13_IN5_scan_start();
			break;
		case MENU_FIVE:
			APP_show_debug_screen_blocking(&debug_state);
			MENU_draw();
			break;
		default:						// Should never occur
			break;
		}

		if (hr_ready) {
			BSP_LED_Toggle(LED3);
			hr_ready = false;
		}
	}
}


/** ***************************************************************************
 * @brief Draw one minimal text-only debug screen.
 *****************************************************************************/
static void APP_draw_debug_screen(const APP_DebugState_t *state)
{
	char line[64];

	BSP_LCD_Clear(LCD_COLOR_WHITE);
	BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
	BSP_LCD_SetFont(&Font16);

	BSP_LCD_DisplayStringAt(5, 8, (uint8_t *)"Radar Debug View", LEFT_MODE);

	if (state->self_test.total_tests > 0U) {
		snprintf(line, sizeof(line), "SelfTest: %s %u/%u", state->self_test.passed_all ? "PASS" : "FAIL", (unsigned int)state->self_test.passed_tests, (unsigned int)state->self_test.total_tests);
		BSP_LCD_DisplayStringAt(5, 36, (uint8_t *)line, LEFT_MODE);
		if (!state->self_test.passed_all) {
			snprintf(line, sizeof(line), "Fail idx=%u exp=%u got=%u", (unsigned int)state->self_test.first_failed_test, (unsigned int)state->self_test.expected_bin, (unsigned int)state->self_test.detected_bin);
			BSP_LCD_DisplayStringAt(5, 56, (uint8_t *)line, LEFT_MODE);
		}
	}

	snprintf(line, sizeof(line), "HR status: %d", (int)state->last_hr_status);
	BSP_LCD_DisplayStringAt(5, 88, (uint8_t *)line, LEFT_MODE);

	if (!state->has_result) {
		BSP_LCD_DisplayStringAt(5, 112, (uint8_t *)"No result yet", LEFT_MODE);
	} else {
		snprintf(line, sizeof(line), "Frame: %lu", (unsigned long)state->last_result.frame_id);
		BSP_LCD_DisplayStringAt(5, 112, (uint8_t *)line, LEFT_MODE);
		snprintf(line, sizeof(line), "BPM: %.1f", state->last_result.bpm);
		BSP_LCD_DisplayStringAt(5, 132, (uint8_t *)line, LEFT_MODE);
		snprintf(line, sizeof(line), "Peak Hz: %.3f", state->last_result.peak_hz);
		BSP_LCD_DisplayStringAt(5, 152, (uint8_t *)line, LEFT_MODE);
		snprintf(line, sizeof(line), "Valid:%u Clip:%u Ovr:%u", state->last_result.valid ? 1U : 0U, state->last_result.clipped ? 1U : 0U, state->last_overrun ? 1U : 0U);
		BSP_LCD_DisplayStringAt(5, 172, (uint8_t *)line, LEFT_MODE);
	}

	BSP_LCD_DisplayStringAt(5, 220, (uint8_t *)"Touch to return", LEFT_MODE);
}


/** ***************************************************************************
 * @brief Show debug screen and return after one touch.
 *****************************************************************************/
static void APP_show_debug_screen_blocking(const APP_DebugState_t *state)
{
	TS_StateTypeDef ts_state;

	APP_draw_debug_screen(state);

	do {
		BSP_TS_GetState(&ts_state);
	} while (ts_state.TouchDetected);

	while (1) {
		BSP_TS_GetState(&ts_state);
		if (ts_state.TouchDetected) {
			do {
				BSP_TS_GetState(&ts_state);
			} while (ts_state.TouchDetected);
			break;
		}
	}
}


/** ***************************************************************************
 * @brief System Clock Configuration
 *
 *****************************************************************************/
static void SystemClock_Config(void){
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
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
static void gyro_disable(void)
{
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


// Default function implementations required to prevent build errors.
__attribute__((weak)) void _close(void){}
__attribute__((weak)) void _lseek(void){}
__attribute__((weak)) void _read(void){}
__attribute__((weak)) void _write(void){}

