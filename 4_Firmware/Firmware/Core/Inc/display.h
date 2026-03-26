/** ***************************************************************************
 * @file
 * @brief See display.c
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
#include "arm_math.h"

#include "menu.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
#define DISP_WIDTH		(BSP_LCD_GetXSize())
#define DISP_HEIGHT		(BSP_LCD_GetYSize()-MENU_HEIGHT)

/******************************************************************************
 * Variables
 *****************************************************************************/

/******************************************************************************
 * Functions
 *****************************************************************************/
void disp_clear_data(void);
void disp_name_value(char name[], float32_t value, uint32_t color, uint32_t position);
void disp_curves(float32_t data[], uint32_t count, float32_t min, float32_t max, uint32_t color);
void disp_bars(float32_t data[], uint32_t count, float32_t min, float32_t max, uint32_t color);
void disp_level(float32_t avg_l, float32_t peak_l, float32_t avg_r, float32_t peak_r);
void disp_info(void);

#endif
