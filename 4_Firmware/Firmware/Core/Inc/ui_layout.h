/** ***************************************************************************
 * @file
 * @brief Shared UI layout and touch-policy constants for menu pages.
 *****************************************************************************/

#ifndef UI_LAYOUT_H_
#define UI_LAYOUT_H_

/******************************************************************************
 * DAC slider and button layout (MENU_TEN)
 *****************************************************************************/
#define DAC_SLIDER_X             20U
#define DAC_SLIDER_Y             120U
#define DAC_SLIDER_WIDTH         200U
#define DAC_SLIDER_HEIGHT        24U
#define DAC_BUTTON_Y             185U
#define DAC_BUTTON_WIDTH         85U
#define DAC_BUTTON_HEIGHT        44U
#define DAC_MINUS_X              20U
#define DAC_PLUS_X               135U

/* Touch step for +/- buttons in DAC menu. */
#define DAC_TOUCH_STEP_VOLTAGE   0.1f

/******************************************************************************
 * OpenLog logger toggle layout (MENU_SEVEN)
 *****************************************************************************/
#define LOG_TOGGLE_X             30U
#define LOG_TOGGLE_Y             140U
#define LOG_TOGGLE_WIDTH         180U
#define LOG_TOGGLE_HEIGHT        50U

#endif
