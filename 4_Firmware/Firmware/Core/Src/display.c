/** ***************************************************************************
 * @file
 * @brief Displays curves and spectrograms
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

#include "main.h"
#include "menu.h"
#include "display.h"

/******************************************************************************
 * Defines
 *****************************************************************************/

/******************************************************************************
 * Variables
 *****************************************************************************/

/******************************************************************************
 * Functions
 *****************************************************************************/

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

