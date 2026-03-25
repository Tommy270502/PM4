/** ***************************************************************************
 * @file
 * @brief Main application: heart-rate radar prototype
 *
 * Sets up the microcontroller, clock, and peripherals.
 * Implements a state machine for single-shot radar measurement:
 *   IDLE → MEASURING → PROCESSING → RESULT → IDLE
 *
 * @author  Based on demo code by Hanspeter Hochreutener, hhrt@zhaw.ch
 * @date    Reworked for PM4 radar HR prototype
 ****************************************************************************/


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
#include "fft.h"
#include "hr.h"


/******************************************************************************
 * Defines
 *****************************************************************************/
#define PROCESSING_TIMEOUT_MS  30000    ///< Max time in PROCESSING before ERROR


/******************************************************************************
 * Types
 *****************************************************************************/

/** Application states */
typedef enum {
    APP_BOOT = 0,
    APP_IDLE,
    APP_MEASURING,
    APP_PROCESSING,
    APP_RESULT,
    APP_ERROR
} APP_State_t;


/******************************************************************************
 * Variables
 *****************************************************************************/
static APP_State_t app_state = APP_BOOT;
static HR_Result_t last_result;
static bool last_result_available = false;
static uint32_t processing_start_tick = 0;


/******************************************************************************
 * Functions
 *****************************************************************************/
static void SystemClock_Config(void);   ///< System Clock Configuration
static void gyro_disable(void);         ///< Disable the onboard gyroscope


/* ----- Screen display helpers ----- */

/** ***************************************************************************
 * @brief Clear the main display area (above the menu bar).
 ****************************************************************************/
static void clear_display_area(void)
{
    BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
    BSP_LCD_FillRect(0, 0, BSP_LCD_GetXSize(), BSP_LCD_GetYSize() - 40);
}


/** ***************************************************************************
 * @brief Show the "Measuring..." screen.
 ****************************************************************************/
static void show_measuring_screen(void)
{
    clear_display_area();
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_SetFont(&Font24);
    BSP_LCD_DisplayStringAt(0, 40, (uint8_t *)"Radar measurement", CENTER_MODE);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 100, (uint8_t *)"Measuring...", CENTER_MODE);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(0, 160, (uint8_t *)"Please wait (~10s)", CENTER_MODE);
}


/** ***************************************************************************
 * @brief Show a heart-rate result on the LCD.
 * @param r  Pointer to the HR result to display
 ****************************************************************************/
static void show_result_screen(const HR_Result_t *r)
{
    clear_display_area();
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);

    char text[32];

    BSP_LCD_SetFont(&Font24);
    BSP_LCD_DisplayStringAt(0, 20, (uint8_t *)"Heart Rate", CENTER_MODE);

    if (r->valid) {
        /* Large BPM display */
        BSP_LCD_SetFont(&Font24);
        snprintf(text, sizeof(text), "%.0f BPM", r->bpm);
        BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
        BSP_LCD_DisplayStringAt(0, 70, (uint8_t *)text, CENTER_MODE);

        /* Peak frequency */
        BSP_LCD_SetFont(&Font16);
        BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
        snprintf(text, sizeof(text), "Peak: %.2f Hz", r->peak_hz);
        BSP_LCD_DisplayStringAt(0, 120, (uint8_t *)text, CENTER_MODE);

        /* Validity */
        BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
        BSP_LCD_DisplayStringAt(0, 150, (uint8_t *)"Valid result", CENTER_MODE);
    } else {
        BSP_LCD_SetFont(&Font20);
        BSP_LCD_SetTextColor(LCD_COLOR_RED);
        BSP_LCD_DisplayStringAt(0, 80, (uint8_t *)"No valid result", CENTER_MODE);
    }

    if (r->clipped) {
        BSP_LCD_SetFont(&Font12);
        BSP_LCD_SetTextColor(LCD_COLOR_ORANGE);
        BSP_LCD_DisplayStringAt(0, 180, (uint8_t *)"Warning: signal clipped", CENTER_MODE);
    }

    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
    BSP_LCD_DisplayStringAt(0, 210, (uint8_t *)"Touch to return", CENTER_MODE);
}


/** ***************************************************************************
 * @brief Show "No result yet" screen.
 ****************************************************************************/
static void show_no_result_screen(void)
{
    clear_display_area();
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 80, (uint8_t *)"No result yet", CENTER_MODE);
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
    BSP_LCD_DisplayStringAt(0, 140, (uint8_t *)"Touch to return", CENTER_MODE);
}


/** ***************************************************************************
 * @brief Show a placeholder screen for unimplemented features.
 * @param title  Feature name to display
 ****************************************************************************/
static void show_placeholder_screen(const char *title)
{
    clear_display_area();
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 60, (uint8_t *)title, CENTER_MODE);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
    BSP_LCD_DisplayStringAt(0, 110, (uint8_t *)"Not implemented yet", CENTER_MODE);
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_DisplayStringAt(0, 170, (uint8_t *)"Touch to return", CENTER_MODE);
}


/** ***************************************************************************
 * @brief Show error screen.
 ****************************************************************************/
static void show_error_screen(void)
{
    clear_display_area();
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_RED);
    BSP_LCD_SetFont(&Font24);
    BSP_LCD_DisplayStringAt(0, 60, (uint8_t *)"ERROR", CENTER_MODE);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_DisplayStringAt(0, 110, (uint8_t *)"Processing timeout", CENTER_MODE);
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
    BSP_LCD_DisplayStringAt(0, 170, (uint8_t *)"Touch to return", CENTER_MODE);
}


/** ***************************************************************************
 * @brief Check if any part of the LCD is touched (above the menu bar).
 * @return true if a touch is detected in the main display area
 ****************************************************************************/
static bool touch_detected_in_display(void)
{
    TS_StateTypeDef ts;
    BSP_TS_GetState(&ts);

#ifdef EVAL_REV_E
    ts.Y = BSP_LCD_GetYSize() - ts.Y;
#endif
#ifdef FLIPPED_LCD
    ts.X = BSP_LCD_GetXSize() - ts.X;
    ts.Y = BSP_LCD_GetYSize() - ts.Y;
#endif

    if (ts.TouchDetected) {
        /* Only accept touches above the menu bar */
        if (ts.Y < (BSP_LCD_GetYSize() - 40)) {
            return true;
        }
    }
    return false;
}


/** ***************************************************************************
 * @brief Return to IDLE state and redraw the menu.
 ****************************************************************************/
static void return_to_idle(void)
{
    app_state = APP_IDLE;
    clear_display_area();
    MENU_hint();
    MENU_draw();
}


/** ***************************************************************************
 * @brief  Main function
 * @return not used because main ends in an infinite loop
 *
 * Initialization and infinite state-machine loop.
 ****************************************************************************/
int main(void)
{
    HAL_Init();                             // Initialize the system
    SystemClock_Config();                   // Configure system clocks

#ifdef FLIPPED_LCD
    BSP_LCD_Init_Flipped();
#else
    BSP_LCD_Init();
#endif
    BSP_LCD_LayerDefaultInit(LCD_FOREGROUND_LAYER, LCD_FRAME_BUFFER);
    BSP_LCD_SelectLayer(LCD_FOREGROUND_LAYER);
    BSP_LCD_DisplayOn();
    BSP_LCD_Clear(LCD_COLOR_WHITE);

    BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize());

    PB_init();
    PB_enableIRQ();

    BSP_LED_Init(LED3);
    BSP_LED_Init(LED4);

    MENU_draw();
    MENU_hint();

    gyro_disable();                         // Disable gyro before analog use

    MEAS_GPIO_analog_init();                // Configure GPIOs in analog mode
    MEAS_timer_init();                      // Configure the timer
    FFT_init();                             // Initialize FFT instance

    app_state = APP_IDLE;

    /* Infinite state-machine loop */
    while (1) {
        BSP_LED_Toggle(LED3);               // Visual heartbeat

        switch (app_state) {

        case APP_IDLE:
            /* Handle pushbutton as fallback "Measure" trigger */
            if (PB_pressed()) {
                show_measuring_screen();
                MEAS_start_radar_single();
                app_state = APP_MEASURING;
                break;
            }

            /* Poll touchscreen for menu transitions */
            MENU_check_transition();

            switch (MENU_get_transition()) {
            case MENU_NONE:
                break;
            case MENU_ZERO:     /* Measure */
                show_measuring_screen();
                MEAS_start_radar_single();
                app_state = APP_MEASURING;
                break;
            case MENU_ONE:      /* Result */
                if (last_result_available) {
                    show_result_screen(&last_result);
                } else {
                    show_no_result_screen();
                }
                app_state = APP_RESULT;
                break;
            case MENU_TWO:      /* Continuous (placeholder) */
                show_placeholder_screen("Continuous");
                app_state = APP_RESULT;     /* Re-use RESULT state for return */
                break;
            case MENU_THREE:    /* ECG Ref (placeholder) */
                show_placeholder_screen("ECG Reference");
                app_state = APP_RESULT;
                break;
            case MENU_FOUR:     /* Settings (placeholder) */
                show_placeholder_screen("Settings");
                app_state = APP_RESULT;
                break;
            case MENU_FIVE:     /* About */
                show_placeholder_screen("PM4 Heart-Rate Radar");
                app_state = APP_RESULT;
                break;
            default:
                break;
            }
            break;

        case APP_MEASURING:
            if (MEAS_data_ready) {
                MEAS_data_ready = false;
                app_state = APP_PROCESSING;
                processing_start_tick = HAL_GetTick();
            }
            break;

        case APP_PROCESSING:
            /* Run HR processing to completion (< 1 sec at 1024 samples) */
            last_result = HR_process_radar_frame(MEAS_get_frame());
            MEAS_consume_frame();
            last_result_available = true;
            show_result_screen(&last_result);
            BSP_LED_On(LED4);
            app_state = APP_RESULT;
            break;

        case APP_RESULT:
            /* Wait for touch to return to menu */
            if (touch_detected_in_display() || PB_pressed()) {
                BSP_LED_Off(LED4);
                return_to_idle();
            }
            break;

        case APP_ERROR:
            if (touch_detected_in_display() || PB_pressed()) {
                return_to_idle();
            }
            break;

        default:
            return_to_idle();
            break;
        }

        /* Processing timeout watchdog */
        if (app_state == APP_PROCESSING) {
            if ((HAL_GetTick() - processing_start_tick) > PROCESSING_TIMEOUT_MS) {
                show_error_screen();
                app_state = APP_ERROR;
            }
        }

        HAL_Delay(50);                      // Loop rate limiter
    }
}


/** ***************************************************************************
 * @brief System Clock Configuration
 ****************************************************************************/
static void SystemClock_Config(void)
{
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
 * @n The GYRO can interfere with the analog input on PC1.
 * @n This pulls CS low briefly, then reconfigures PC1 to analog mode.
 ****************************************************************************/
static void gyro_disable(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    /* Disable PC1 and PF8 first */
    GPIOC->MODER &= ~GPIO_MODER_MODER1_Msk;
    GPIOC->MODER |= 1UL << GPIO_MODER_MODER1_Pos;     // Set PC1 as output
    GPIOC->BSRR |= GPIO_BSRR_BR1;                     // Set GYRO CS to 0
    HAL_Delay(10);
    GPIOC->MODER |= 3UL << GPIO_MODER_MODER1_Pos;     // Analog PC1 = ADC123_IN11

    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIOF->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED8_Msk;
    GPIOF->AFR[1] &= ~GPIO_AFRH_AFSEL8_Msk;
    GPIOF->PUPDR &= ~GPIO_PUPDR_PUPD8_Msk;
    HAL_Delay(10);
    GPIOF->MODER |= 3UL << GPIO_MODER_MODER8_Pos;     // Analog PF8 = ADC3_IN4
}


// Default function implementations required to prevent build errors.
__attribute__((weak)) void _close(void){}
__attribute__((weak)) void _lseek(void){}
__attribute__((weak)) void _read(void){}
__attribute__((weak)) void _write(void){}
