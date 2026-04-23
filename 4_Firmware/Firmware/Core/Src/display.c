/** ***************************************************************************
 * @file
 * @brief Display primitives and menu-page renderer.
 *
 * @warning Drawing outside of the display crashes the system!
 *
 * ----------------------------------------------------------------------------
 * @author Hanspeter Hochreutener, hhrt@zhaw.ch
 * @author Patrick Rennhard, renn@zhaw.ch
 * @date 03.09.2025
 *****************************************************************************/

/******************************************************************************
 * Includes
 *****************************************************************************/
#include <stdio.h>

#include "stm32f4xx.h"
#include "stm32f429i_discovery.h"
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_ts.h"
#include "math.h"

#include "display.h"
#include "main.h"
#include "radar.h"
#include "dac_output.h"
#include "ekg.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
#define DISP_LOOP_M0 	10	// Info screen
#define DISP_LOOP_M1 	0   // Time signal (refresh on every new I/Q block)
#define DISP_LOOP_M2 	0	// Spectrum analyzer (refresh on every new I/Q block)
#define DISP_LOOP_M3 	4	// Effect Menu (Filter Selection)
#define DISP_LOOP_M4 	4	// Frequency peak readout
#define DISP_LOOP_M5 	4	// Level meter
#define DISP_LOOP_M6 	4	// ...
#define DISP_LOOP_M7 	4	// ...
#define DISP_LOOP_M8 	4	// ...
#define DISP_LOOP_M9 	4	// EKG BPM
#define DISP_LOOP_M10 	4	// DAC output

#define MAX_TIME_SIGNAL_POINTS 240
#define TIME_SIGNAL_POINTS (RADAR_CHANNEL_SAMPLES > MAX_TIME_SIGNAL_POINTS ? MAX_TIME_SIGNAL_POINTS : RADAR_CHANNEL_SAMPLES)

#define TIME_SIGNAL_MIN_SPAN     4.0f
#define TIME_SIGNAL_HEADROOM     0.10f
#define SPECTRUM_DISPLAY_HZ      4.0f
#define SPECTRUM_MIN_DISPLAY_MAX 0.001f
#define SPECTRUM_HEADROOM        1.15f

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
static uint32_t disp_menu_loop_count[MENU_TOTAL_ENTRIES] = {0};

/******************************************************************************
 * Functions
 *****************************************************************************/
static uint32_t disp_menu_get_refresh_limit(MENU_item_t menu_item);
static void disp_peak_frequencies(const disp_menu_data_t *data);
static void disp_format_frequency_hz(char text[], size_t text_size, float32_t frequency_hz);
static void disp_dac_output(void);

/** ***************************************************************************
 * @brief Clear the data display
 *
 *****************************************************************************/
void disp_clear_data(void)
{
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
    BSP_LCD_FillRect(0, 0, DISP_WIDTH, DISP_HEIGHT + 1);
    HAL_Delay(2);						// Wait a little bit
}

/** ***************************************************************************
 * @brief Display name and value of data item
 *
 * @param[in] name to display
 * @param[in] value to display
 * @param[in] color for drawing
 * @param[in] position of text
 *****************************************************************************/
void disp_name_value(char name[], float32_t value, uint32_t color,
                     uint32_t position)
{
    BSP_LCD_SetFont(&Font24);
    BSP_LCD_SetTextColor(color);
    char text[16];
    snprintf(text, 15, "%s %4d", name, (int) value);
    BSP_LCD_DisplayStringAt(2, position, (uint8_t*) text, LEFT_MODE);
}

/** ***************************************************************************
 * @brief Draw data as curve
 *
 * @param[in] data to be displayed
 * @param[in] count of datapoints
 * @param[in] max = value for top of display
 * @param[in] min = value for bottom of display
 * @param[in] color of the curve
 *****************************************************************************/
void disp_curves(float32_t data[], uint32_t count, float32_t min, float32_t max,
                 uint32_t color)
{
    int32_t pos0, pos1;
    int32_t value0, value1;
    uint32_t d_width = DISP_WIDTH;
    uint32_t d_height = DISP_HEIGHT;
    uint32_t position_divisor;

    if ((data == 0) || (count == 0U) || (max <= min))
    {
        return;
    }

    position_divisor = (count > 1U) ? (count - 1U) : 1U;
    // First datapoint
    pos0 = 0;
    value0 = (uint32_t) ((data[0] - min) / (max - min) * (d_height - 1));
    ;
    if (value0 < 0)
    {
        value0 = 0;
    }
    if (value0 > (d_height - 1))
    {
        value0 = (d_height - 1);
    }
    // Draw the data as a curve
    BSP_LCD_SetTextColor(color);
    for (uint32_t i = 1; i < count; i++)
    {
        pos1 = (d_width - 1) * i / position_divisor;
        value1 = (int32_t) ((data[i] - min) / (max - min) * (d_height - 1));
        if (value1 < 0)
        {
            value1 = 0;
        }
        if (value1 > (d_height - 1))
        {
            value1 = (d_height - 1);
        }
        BSP_LCD_DrawLine(pos0, d_height - value0, pos1, d_height - value1);
        pos0 = pos1;
        value0 = value1;
    }
}

/** ***************************************************************************
 * @brief Draw data as bar diagram
 *
 * @param[in] data to be displayed
 * @param[in] count of datapoints
 * @param[in] max = value for top of display
 * @param[in] min = value for bottom of display
 * @param[in] color of the curve
 *****************************************************************************/
void disp_bars(float32_t data[], uint32_t count, float32_t min, float32_t max,
               uint32_t color)
{
    int32_t posx, posy;
    uint32_t d_width = DISP_WIDTH;
    uint32_t d_height = DISP_HEIGHT;
    // Draw the data as a bar diagram
    BSP_LCD_SetTextColor(color);
    for (uint32_t i = 0; i < count; i++)
    {
        posx = (d_width - 1) * i / count;
        posy = (int32_t) ((data[i] - min) / (max - min) * (d_height - 1));
        if (posy < 0)
        {
            posy = 0;
        }
        if (posy > (d_height - 1))
        {
            posy = (d_height - 1);
        }
        BSP_LCD_FillRect(posx, d_height - posy, ((d_width - 1) / count - 1),
                         posy);
    }
}

/** ***************************************************************************
 * @brief Display channel level meter
 *
 * @param[in] avg_l  average I channel
 * @param[in] peak_l peak I channel
 * @param[in] avg_r  average Q channel
 * @param[in] peak_r peak Q channel
 *****************************************************************************/
void disp_level(float32_t avg_l, float32_t peak_l, float32_t avg_r,
             float32_t peak_r)
{
    uint32_t d_width = DISP_WIDTH;
    uint32_t d_height = DISP_HEIGHT;
    int32_t posx, posy;
    int32_t posxdelta = (d_width - 1) / 5;
    int32_t peaky = 10;
    int32_t min = -40;
    int32_t max = 0;
    float avg, peak;
    for (uint32_t i = 0; i < 2; i++)
    {
        posx = posxdelta * (2 * i + 1);
        // Draw the averages
        switch (i)
        {
            case 0:
                avg = avg_l;
                peak = peak_l;
                break;
            case 1:
                avg = avg_r;
                peak = peak_r;
                break;
        }
        if (avg > -3)
        {
            BSP_LCD_SetTextColor(LCD_COLOR_LIGHTRED);
        } else if (avg > -9)
        {
            BSP_LCD_SetTextColor(LCD_COLOR_LIGHTYELLOW);
        } else
        {
            BSP_LCD_SetTextColor(LCD_COLOR_LIGHTGREEN);
        }
        if (avg < min)
        {
            avg = min;
        }
        posy = (int32_t) ((avg - min) / (max - min) * (d_height - 1));
        if (posy < 0)
        {
            posy = 0;
        }
        if (posy > (d_height - 1))
        {
            posy = (d_height - 1);
        }
        BSP_LCD_FillRect(posx, d_height - posy, (posxdelta - 1), posy);

        // Draw the peaks
        if (peak > -3)
        {
            BSP_LCD_SetTextColor(LCD_COLOR_RED);
        } else if (peak > -9)
        {
            BSP_LCD_SetTextColor(LCD_COLOR_YELLOW);
        } else
        {
            BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
        }
        if (peak < min)
        {
            peak = min;
        }
        posy = (int32_t) ((peak - min) / (max - min) * (d_height - 1));
        if (posy > (d_height))
        {
            posy = (d_height);
        }
        if (posy < peaky)
        {
            posy = peaky;
        }
        BSP_LCD_FillRect(posx, d_height - posy, (posxdelta - 1), peaky);

    }
}

/** ***************************************************************************
 * @brief Display info screen
 *
 *****************************************************************************/
void disp_info(void)
{
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(5, 10, (uint8_t*) "Radar Processing", LEFT_MODE);
    BSP_LCD_DisplayStringAt(5, 30, (uint8_t*) "Heart-Rate Monitor", LEFT_MODE);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(5, 60, (uint8_t*) "--------------------", LEFT_MODE);
    BSP_LCD_DisplayStringAt(5, 80, (uint8_t*) "Authors:", LEFT_MODE);
    BSP_LCD_DisplayStringAt(5, 110, (uint8_t*) "Bogdans Grebnevs", LEFT_MODE);
    BSP_LCD_DisplayStringAt(5, 130, (uint8_t*) "Thomas Perri", LEFT_MODE);

    BSP_LCD_DisplayStringAt(5, 160, (uint8_t*) "--------------------", LEFT_MODE);
    BSP_LCD_DisplayStringAt(5, 200, (uint8_t*) "Version 26.03.2026", LEFT_MODE);
}

/** ***************************************************************************
 * @brief Force immediate refresh for one menu page.
 *
 * Sets the internal per-menu loop counter to its refresh limit so the next
 * call to disp_menu_render() redraws that page immediately.
 *****************************************************************************/
void disp_menu_force_refresh(MENU_item_t menu_item)
{
    if ((menu_item >= MENU_ZERO) && (menu_item <= MENU_TEN))
    {
        disp_menu_loop_count[menu_item] = disp_menu_get_refresh_limit(menu_item);
    }
}

/** ***************************************************************************
 * @brief Render currently active menu page with internal refresh throttling.
 *
 * The function owns per-menu pacing and draws only when the corresponding
 * loop counter reaches its configured limit.
 *****************************************************************************/
void disp_menu_render(MENU_item_t active_menu, const disp_menu_data_t *data)
{
    if (data == 0)
    {
        return;
    }

    switch (active_menu)
    {
    case MENU_NONE:
        break;
    case MENU_ZERO:
        if (disp_menu_loop_count[MENU_ZERO]++ >= DISP_LOOP_M0)
        {
            disp_menu_loop_count[MENU_ZERO] = 0;
            disp_clear_data();
            disp_info();
        }
        break;
    case MENU_ONE:
        if (disp_menu_loop_count[MENU_ONE]++ >= DISP_LOOP_M1)
        {
            uint32_t signal_start;
            float32_t signal_min;
            float32_t signal_max;

            disp_menu_loop_count[MENU_ONE] = 0;
            signal_start = radar_get_recent_start_index(TIME_SIGNAL_POINTS);
            radar_get_time_scale(data->radar_i_samples, data->radar_q_samples,
                        signal_start, TIME_SIGNAL_POINTS,
                        TIME_SIGNAL_MIN_SPAN, TIME_SIGNAL_HEADROOM,
                        &signal_min, &signal_max);
            disp_clear_data();
            disp_curves((float32_t*) &data->radar_i_samples[signal_start], TIME_SIGNAL_POINTS,
                    signal_min, signal_max, LCD_COLOR_RED);
            disp_curves((float32_t*) &data->radar_q_samples[signal_start], TIME_SIGNAL_POINTS,
                    signal_min, signal_max, LCD_COLOR_BLUE);
        }
        break;
    case MENU_TWO:
        if (disp_menu_loop_count[MENU_TWO]++ >= DISP_LOOP_M2)
        {
            uint32_t spectrum_start;
            uint32_t spectrum_count;
            float32_t spectrum_max;

            disp_menu_loop_count[MENU_TWO] = 0;
            radar_get_spectrum_window(data->spectrum_shifted,
                        SPECTRUM_DISPLAY_HZ,
                        SPECTRUM_MIN_DISPLAY_MAX,
                        SPECTRUM_HEADROOM,
                        &spectrum_start,
                        &spectrum_count,
                        &spectrum_max);
            disp_clear_data();
            BSP_LCD_SetTextColor(LCD_COLOR_LIGHTGRAY);
            BSP_LCD_DrawLine(DISP_WIDTH / 2U, 0U, DISP_WIDTH / 2U, DISP_HEIGHT - 1U);
            disp_curves((float32_t*) &data->spectrum_shifted[spectrum_start], spectrum_count,
                    0.0f, spectrum_max, LCD_COLOR_BLUE);
            BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
            BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
            BSP_LCD_SetFont(&Font12);
            BSP_LCD_DisplayStringAt(2, 2, (uint8_t*) "-4 Hz", LEFT_MODE);
            BSP_LCD_DisplayStringAt(0, 2, (uint8_t*) "0 Hz", CENTER_MODE);
            BSP_LCD_DisplayStringAt(DISP_WIDTH - 36U, 2, (uint8_t*) "+4 Hz", LEFT_MODE);
        }
        break;
    case MENU_THREE:
        if (disp_menu_loop_count[MENU_THREE]++ >= DISP_LOOP_M3)
        {
            const uint32_t start_y = 28U;
            const uint32_t spacing = 56U;

            disp_menu_loop_count[MENU_THREE] = 0;
            disp_clear_data();
            BSP_LCD_SetFont(&Font24);

            if (data->filter_names != 0)
            {
                for (uint8_t i = 0; i < 5U; i++)
                {
                    uint32_t y_pos = start_y + ((uint32_t) i * spacing);

                    if (i == data->current_filter_index)
                    {
                        BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
                    }
                    else
                    {
                        BSP_LCD_SetTextColor(LCD_COLOR_LIGHTGRAY);
                    }

                    BSP_LCD_DisplayStringAt(0, y_pos, (uint8_t*) data->filter_names[i], CENTER_MODE);
                }
            }
        }
        break;
    case MENU_FOUR:
        if (disp_menu_loop_count[MENU_FOUR]++ >= DISP_LOOP_M4)
        {
            disp_menu_loop_count[MENU_FOUR] = 0;
            disp_peak_frequencies(data);
        }
        break;
    case MENU_FIVE:
        if (disp_menu_loop_count[MENU_FIVE]++ >= DISP_LOOP_M5)
        {
            disp_menu_loop_count[MENU_FIVE] = 0;
            disp_clear_data();
            disp_level(-10, -5, -12, -6);
        }
        break;
    case MENU_SIX:
        if (disp_menu_loop_count[MENU_SIX]++ >= DISP_LOOP_M6)
        {
            char text[32];

            disp_menu_loop_count[MENU_SIX] = 0;
            disp_clear_data();

            BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
            BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
            BSP_LCD_SetFont(&Font20);
            BSP_LCD_DisplayStringAt(0, 10, (uint8_t*) "Radar BPM", CENTER_MODE);

            BSP_LCD_SetFont(&Font24);
            if (data->radar_hr_valid)
            {
                snprintf(text, sizeof(text), "BPM: %3d", (int)(data->radar_hr_bpm + 0.5f));
                BSP_LCD_SetTextColor(LCD_COLOR_RED);
            }
            else
            {
                snprintf(text, sizeof(text), "BPM: ---");
                BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
            }
            BSP_LCD_DisplayStringAt(0, 70, (uint8_t*) text, CENTER_MODE);

            BSP_LCD_SetFont(&Font16);
            BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
            if (data->radar_hr_state == RADAR_HR_LOCKED)
            {
                BSP_LCD_DisplayStringAt(0, 130, (uint8_t*) "LOCKED", CENTER_MODE);
            }
            else
            {
                BSP_LCD_DisplayStringAt(0, 130, (uint8_t*) "SEARCH", CENTER_MODE);
            }
        }
        break;
    case MENU_SEVEN:
    case MENU_EIGHT:
        break;
    case MENU_NINE:
        if (disp_menu_loop_count[MENU_NINE]++ >= DISP_LOOP_M9)
        {
            char text[32];
            uint32_t now = HAL_GetTick();

            disp_menu_loop_count[MENU_NINE] = 0;
            disp_clear_data();

            BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
            BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
            BSP_LCD_SetFont(&Font20);
            BSP_LCD_DisplayStringAt(0, 10, (uint8_t*) "EKG Monitor", CENTER_MODE);

            BSP_LCD_SetFont(&Font24);
            if (data->ekg_latest.bpm_valid)
            {
                snprintf(text, sizeof(text), "BPM: %3d", (int) (data->ekg_latest.bpm + 0.5f));
                BSP_LCD_SetTextColor(LCD_COLOR_RED);
            }
            else
            {
                snprintf(text, sizeof(text), "BPM: ---");
                BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
            }
            BSP_LCD_DisplayStringAt(0, 70, (uint8_t*) text, CENTER_MODE);

            BSP_LCD_SetFont(&Font16);
            BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
            snprintf(text, sizeof(text), "ADC: %4u", (unsigned int) data->ekg_latest.raw);
            BSP_LCD_DisplayStringAt(0, 130, (uint8_t*) text, CENTER_MODE);

            if ((now - data->ekg_last_peak_tick) < 140U)
            {
                BSP_LCD_SetTextColor(LCD_COLOR_RED);
                BSP_LCD_DisplayStringAt(0, 165, (uint8_t*) "R-PEAK", CENTER_MODE);
            }
            else
            {
                BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
                BSP_LCD_DisplayStringAt(0, 165, (uint8_t*) "      ", CENTER_MODE);
            }

            BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
            snprintf(text, sizeof(text), "Overrun: %lu",
                    (unsigned long) ekg_get_overrun_count());
            BSP_LCD_DisplayStringAt(0, 210, (uint8_t*) text, CENTER_MODE);
        }
        break;
    case MENU_TEN:
        if (disp_menu_loop_count[MENU_TEN]++ >= DISP_LOOP_M10)
        {
            disp_menu_loop_count[MENU_TEN] = 0;
            disp_clear_data();
            disp_dac_output();
        }
        break;
    default:
        break;
    }
}

static uint32_t disp_menu_get_refresh_limit(MENU_item_t menu_item)
{
    switch (menu_item)
    {
    case MENU_ZERO:
        return DISP_LOOP_M0;
    case MENU_ONE:
        return DISP_LOOP_M1;
    case MENU_TWO:
        return DISP_LOOP_M2;
    case MENU_THREE:
        return DISP_LOOP_M3;
    case MENU_FOUR:
        return DISP_LOOP_M4;
    case MENU_FIVE:
        return DISP_LOOP_M5;
    case MENU_SIX:
        return DISP_LOOP_M6;
    case MENU_SEVEN:
        return DISP_LOOP_M7;
    case MENU_EIGHT:
        return DISP_LOOP_M8;
    case MENU_NINE:
        return DISP_LOOP_M9;
    case MENU_TEN:
        return DISP_LOOP_M10;
    default:
        return 0U;
    }
}

static void disp_peak_frequencies(const disp_menu_data_t *data)
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
    if (data->spectrum_pos_peak_valid)
    {
        disp_format_frequency_hz(text, sizeof(text), data->spectrum_pos_peak_hz);
        BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
    }
    else
    {
        snprintf(text, sizeof(text), "--- Hz");
        BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
    }
    BSP_LCD_DisplayStringAt(0, 84, (uint8_t*) text, CENTER_MODE);

    BSP_LCD_SetFont(&Font16);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_DisplayStringAt(0, 158, (uint8_t*) "Negative peak", CENTER_MODE);

    BSP_LCD_SetFont(&Font24);
    if (data->spectrum_neg_peak_valid)
    {
        disp_format_frequency_hz(text, sizeof(text), data->spectrum_neg_peak_hz);
        BSP_LCD_SetTextColor(LCD_COLOR_RED);
    }
    else
    {
        snprintf(text, sizeof(text), "--- Hz");
        BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
    }
    BSP_LCD_DisplayStringAt(0, 184, (uint8_t*) text, CENTER_MODE);
}

static void disp_format_frequency_hz(char text[], size_t text_size, float32_t frequency_hz)
{
    float32_t abs_frequency_hz;
    uint32_t milli_hz;
    uint32_t whole_hz;
    uint32_t fractional_milli_hz;
    char sign;

    if ((text == 0) || (text_size == 0U))
    {
        return;
    }

    sign = (frequency_hz < 0.0f) ? '-' : '+';
    abs_frequency_hz = (frequency_hz < 0.0f) ? -frequency_hz : frequency_hz;

    /* Avoid `%f` so formatting works even when printf-float is not linked. */
    milli_hz = (uint32_t) ((abs_frequency_hz * 1000.0f) + 0.5f);
    whole_hz = milli_hz / 1000U;
    fractional_milli_hz = milli_hz % 1000U;

    snprintf(text, text_size, "%c%lu.%03lu Hz",
        sign,
        (unsigned long) whole_hz,
        (unsigned long) fractional_milli_hz);
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
    if (fill_width > 0U)
    {
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
    BSP_LCD_DisplayStringAt(DAC_MINUS_X + 26U, DAC_BUTTON_Y + 8U, (uint8_t*) "-", LEFT_MODE);
    BSP_LCD_DisplayStringAt(DAC_PLUS_X + 28U, DAC_BUTTON_Y + 8U, (uint8_t*) "+", LEFT_MODE);

    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(0, 244, (uint8_t*) "Tap bar or +/- 0.1 V", CENTER_MODE);
}

