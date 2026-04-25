/** ***************************************************************************
 * @file
 * @brief The menu
 *
 * Initializes and displays the menu.
 * @n Provides the function MENU_check_transition() for polling user actions.
 * The variable MENU_transition is set to the touched menu item.
 * If no touch has occurred the variable MENU_transition is set to MENU_NONE
 * @n If the interrupt handler is enabled by calling BSP_TS_ITConfig();
 * the variable MENU_transition is set to the touched menu entry as above.
 * @n Either call once BSP_TS_ITConfig() to enable the interrupt
 * or MENU_check_transition() in the main while loop for polling.
 * @n The function MENU_get_transition() returns the new menu item.
 *
 * @author  Hanspeter Hochreutener, hhrt@zhaw.ch
 * @date	30.04.2020
 * @modified_by Patrick Rennhard, renn@zhaw.ch
 * @modified_date 03.09.2025
 *****************************************************************************/

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "stm32f4xx.h"
#include "stm32f429i_discovery.h"
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_ts.h"

#include "board_config.h"

#include "menu.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
#define MENU_FONT				&Font12	///< Possible font sizes: 8 12 16 20 24

/******************************************************************************
 * Variables
 *****************************************************************************/
static MENU_item_t MENU_transition = MENU_NONE;	///< Transition to this menu
static MENU_item_t MENU_active = MENU_NONE;		///< Currently active menu
static uint32_t MENU_scroll_offset = 0;			///< Current scroll offset

// Touch detection state variables
static bool touch_was_detected = false;			///< Previous touch state for edge detection
static uint32_t last_scroll_time = 0;				///< Time of last scroll action (ms)

/******************************************************************************
 * Menu Appearance Configuration
 *****************************************************************************/
// Arrow button appearance
static const char* MENU_arrow_left_text = " < ";					///< Left arrow text
static const char* MENU_arrow_right_text = " > ";					///< Right arrow text
static const uint32_t MENU_arrow_active_back_color = LCD_COLOR_WHITE;		///< Arrow background when active
static const uint32_t MENU_arrow_active_text_color = LCD_COLOR_BLACK;		///< Arrow text when active
static const uint32_t MENU_arrow_disabled_back_color = LCD_COLOR_LIGHTGRAY;	///< Arrow background when disabled
static const uint32_t MENU_arrow_disabled_text_color = LCD_COLOR_DARKGRAY;	///< Arrow text when disabled

// Empty slot appearance
static const char* MENU_empty_slot_text = "---";				///< Text for empty slots
static const uint32_t MENU_empty_slot_back_color = LCD_COLOR_LIGHTGRAY;	///< Empty slot background
static const uint32_t MENU_empty_slot_text_color = LCD_COLOR_DARKGRAY;	///< Empty slot text color

static MENU_entry_t MENU_entry[MENU_TOTAL_ENTRIES] =
{
{ "Info", "Screen",
LCD_COLOR_BLACK, LCD_COLOR_LIGHTYELLOW },
{ "Time", "Signal",
LCD_COLOR_BLACK, LCD_COLOR_LIGHTRED },
{ "FFT", "Spectr",
LCD_COLOR_BLACK, LCD_COLOR_LIGHTCYAN },
{ "Effect", "Menu",
LCD_COLOR_BLACK, LCD_COLOR_LIGHTMAGENTA },
{ "Peak", "Detect",
LCD_COLOR_BLACK, LCD_COLOR_ORANGE },
{ "Level", "Meter",
LCD_COLOR_BLACK, LCD_COLOR_LIGHTBLUE },
{ "Radar", "BPM",
LCD_COLOR_BLACK, LCD_COLOR_LIGHTCYAN },
{ "Log", "Data",
LCD_COLOR_BLACK, LCD_COLOR_LIGHTGREEN },
{ "RMS", "Level",
LCD_COLOR_BLACK, LCD_COLOR_LIGHTBLUE },
{ "EKG", "BPM",
LCD_COLOR_BLACK, LCD_COLOR_LIGHTRED },
{ "DAC", "PA5",
LCD_COLOR_BLACK, LCD_COLOR_LIGHTRED } };		///< All the menu entries

/******************************************************************************
 * Functions
 *****************************************************************************/

/** ***************************************************************************
 * @brief Draw the menu onto the display.
 *
 * Each menu entry has two lines.
 * Text and background colors are applied.
 * @n These attributes are defined in the variable MENU_draw[].
 * @n Includes left and right arrow navigation buttons.
 *****************************************************************************/
void MENU_draw(void)
{
    BSP_LCD_SetFont(MENU_FONT);
    uint32_t x, y, m, w, h;
    uint32_t visible_slot;
    y = MENU_POSITION;
    m = MENU_MARGIN;
    w = BSP_LCD_GetXSize() / MENU_VISIBLE_ENTRIES;
    h = MENU_HEIGHT;
    
    // Draw left arrow slot (position 0)
    visible_slot = 0;
    x = visible_slot * w;
    uint32_t arrow_back_color = MENU_can_scroll_left() ? MENU_arrow_active_back_color : MENU_arrow_disabled_back_color;
    uint32_t arrow_text_color = MENU_can_scroll_left() ? MENU_arrow_active_text_color : MENU_arrow_disabled_text_color;
    BSP_LCD_SetTextColor(arrow_back_color);
    BSP_LCD_FillRect(x + m, y + m, w - 2 * m, h - 2 * m);
    BSP_LCD_SetBackColor(arrow_back_color);
    BSP_LCD_SetTextColor(arrow_text_color);
    BSP_LCD_DisplayStringAt(x + 3 * m, y + h / 2 - 6,
                            (uint8_t*) MENU_arrow_left_text, LEFT_MODE);
    
    // Draw content entries (positions 1, 2, 3)
    for (uint32_t i = 0; i < MENU_CONTENT_SLOTS; i++)
    {
        visible_slot = i + 1;  // Positions 1, 2, 3
        x = visible_slot * w;
        uint32_t entry_index = MENU_scroll_offset + i;
        
        if (entry_index < MENU_TOTAL_ENTRIES)
        {
            BSP_LCD_SetTextColor(MENU_entry[entry_index].back_color);
            BSP_LCD_FillRect(x + m, y + m, w - 2 * m, h - 2 * m);
            BSP_LCD_SetBackColor(MENU_entry[entry_index].back_color);
            BSP_LCD_SetTextColor(MENU_entry[entry_index].text_color);
            BSP_LCD_DisplayStringAt(x + 3 * m, y + 3 * m,
                                    (uint8_t*) MENU_entry[entry_index].line1, LEFT_MODE);
            BSP_LCD_DisplayStringAt(x + 3 * m, y + h / 2,
                                    (uint8_t*) MENU_entry[entry_index].line2, LEFT_MODE);
        }
        else
        {
            // Empty slot if not enough entries
            BSP_LCD_SetTextColor(MENU_empty_slot_back_color);
            BSP_LCD_FillRect(x + m, y + m, w - 2 * m, h - 2 * m);
            BSP_LCD_SetBackColor(MENU_empty_slot_back_color);
            BSP_LCD_SetTextColor(MENU_empty_slot_text_color);
            BSP_LCD_DisplayStringAt(x + 3 * m, y + h / 2 - 6,
                            (uint8_t*) MENU_empty_slot_text, LEFT_MODE);
        }
    }
    
    // Draw right arrow slot (position 4)
    visible_slot = 4;
    x = visible_slot * w;
    arrow_back_color = MENU_can_scroll_right() ? MENU_arrow_active_back_color : MENU_arrow_disabled_back_color;
    arrow_text_color = MENU_can_scroll_right() ? MENU_arrow_active_text_color : MENU_arrow_disabled_text_color;
    BSP_LCD_SetTextColor(arrow_back_color);
    BSP_LCD_FillRect(x + m, y + m, w - 2 * m, h - 2 * m);
    BSP_LCD_SetBackColor(arrow_back_color);
    BSP_LCD_SetTextColor(arrow_text_color);
    BSP_LCD_DisplayStringAt(x + 3 * m, y + h / 2 - 6,
                            (uint8_t*) MENU_arrow_right_text, LEFT_MODE);
}

/** ***************************************************************************
 * @brief Check if menu can scroll left
 * @return true if scrolling left is possible, false otherwise
 *****************************************************************************/
bool MENU_can_scroll_left(void)
{
    return (MENU_scroll_offset > 0);
}

/** ***************************************************************************
 * @brief Check if menu can scroll right
 * @return true if scrolling right is possible, false otherwise
 *****************************************************************************/
bool MENU_can_scroll_right(void)
{
    return (MENU_scroll_offset + MENU_CONTENT_SLOTS < MENU_TOTAL_ENTRIES);
}

/** ***************************************************************************
 * @brief Get current scroll offset
 * @return Current scroll offset value
 *****************************************************************************/
uint32_t MENU_get_scroll_offset(void)
{
    return MENU_scroll_offset;
}

/** ***************************************************************************
 * @brief Scroll menu to the left
 *
 * Decrements scroll offset if possible and redraws the menu.
 *****************************************************************************/
void MENU_scroll_left(void)
{
    if (MENU_can_scroll_left())
    {
        MENU_scroll_offset--;
        MENU_draw();
    }
}

/** ***************************************************************************
 * @brief Scroll menu to the right
 *
 * Increments scroll offset if possible and redraws the menu.
 *****************************************************************************/
void MENU_scroll_right(void)
{
    if (MENU_can_scroll_right())
    {
        MENU_scroll_offset++;
        MENU_draw();
    }
}

/** ***************************************************************************
 * @brief Set a menu entry.
 * @param [in] item number of menu bar
 * @param [in] entry attributes for that item
 *
 * @note Call MENU_draw() to update the display.
 *****************************************************************************/
void MENU_set_entry(const MENU_item_t item, const MENU_entry_t entry)
{
    if ((0 <= item) && (MENU_TOTAL_ENTRIES > item))
    {
        MENU_entry[item] = entry;
    }
}

/** ***************************************************************************
 * @brief Get a menu entry.
 * @param [in] item number of menu bar
 * @return Menu_entry[item] or Menu_entry[0] if item not in range
 *****************************************************************************/
MENU_entry_t MENU_get_entry(const MENU_item_t item)
{
    MENU_entry_t entry = MENU_entry[0];
    if ((0 <= item) && (MENU_TOTAL_ENTRIES > item))
    {
        entry = MENU_entry[item];
    }
    return entry;
}

void MENU_check_transition(void)
{
    static MENU_item_t item_old = MENU_NONE;
    static MENU_item_t item_new = MENU_NONE;
    static TS_StateTypeDef TS_State;	// State of the touch controller
    BSP_TS_GetState(&TS_State);			// Get the state

// Evalboard revision E (blue) has an inverted y-axis in the touch controller
#ifdef EVAL_REV_E
    TS_State.Y = BSP_LCD_GetYSize() - TS_State.Y;	// Invert the y-axis
#endif
    // Invert x- and y-axis if LCD ist flipped
#ifdef FLIPPED_LCD
	TS_State.X = BSP_LCD_GetXSize() - TS_State.X;	// Invert the x-axis
	TS_State.Y = BSP_LCD_GetYSize() - TS_State.Y;	// Invert the y-axis
#endif

    /*
     #if (defined(EVAL_REV_E) && !defined(FLIPPED_LCD)) || (!defined(EVAL_REV_E) && defined(FLIPPED_LCD))
     TS_State.Y = BSP_LCD_GetYSize() - TS_State.Y;	// Invert the y-axis
     #endif
     #ifdef EVAL_REV_E
     #endif
     */
     
    // Detect touch DOWN edge (transition from not-touched to touched)
    bool touch_just_pressed = (!touch_was_detected && TS_State.TouchDetected);
    
    // Update touch state for next iteration
    touch_was_detected = TS_State.TouchDetected;
    
    if (TS_State.TouchDetected)
    {		// If a touch was detected
        /* Do only if last transition not pending anymore */
        if (MENU_NONE == MENU_transition)
        {
            item_old = item_new;		// Store old item
            /* If touched within the menu bar? */
            if ((MENU_POSITION < TS_State.Y)
                    && (MENU_POSITION + MENU_HEIGHT > TS_State.Y))
            {
                // Calculate which visible slot was touched (0-4)
                uint32_t visible_slot = TS_State.X / (BSP_LCD_GetXSize() / MENU_VISIBLE_ENTRIES);
                
                if ((0 <= visible_slot) && (MENU_VISIBLE_ENTRIES > visible_slot))
                {
                    bool is_arrow = false;
                    
                    // Check if left arrow was touched (position 0)
                    if (visible_slot == 0)
                    {
                        item_new = MENU_SCROLL_LEFT;
                        is_arrow = true;
                    }
                    // Check if right arrow was touched (position 4)
                    else if (visible_slot == 4)
                    {
                        item_new = MENU_SCROLL_RIGHT;
                        is_arrow = true;
                    }
                    // Content slots (positions 1, 2, 3)
                    else
                    {
                        // Map visible slot to actual menu item index
                        uint32_t menu_index = MENU_scroll_offset + (visible_slot - 1);
                        
                        // Ensure menu_index is within valid range
                        if (menu_index < MENU_TOTAL_ENTRIES)
                        {
                            // Cast menu_index to MENU_item_t enum
                            item_new = (MENU_item_t)menu_index;
                        }
                        else
                        {
                            item_new = MENU_NONE;	// Out of bounds
                        }
                    }
                    
                    // Handle arrow buttons with edge detection and debounce
                    if (is_arrow)
                    {
                        // Only trigger on touch DOWN edge
                        if (touch_just_pressed)
                        {
                            // Apply time-based debounce
                            uint32_t current_time = HAL_GetTick();
                            if ((current_time - last_scroll_time) >= MENU_SCROLL_DEBOUNCE_MS)
                            {
                                // Valid scroll action - set transition
                                MENU_transition = item_new;
                                last_scroll_time = current_time;
                            }
                        }
                        // Reset item_new to prevent double-tap logic from triggering
                        item_new = MENU_NONE;
                    }
                    // Handle content entries with existing double-tap logic
                    else
                    {
                        if (item_new == item_old)
                        {	// 2 times the same menu item
                            item_new = MENU_NONE;
                            MENU_transition = item_old;
                        }
                    }
                }
                else
                {
                    item_new = MENU_NONE;	// Out of bounds
                }
            }
        }
    }
}

/** ***************************************************************************
 * @brief Get menu selection/transition
 *
 * @return the selected MENU_item or MENU_NONE if no MENU_item was selected
 *
 * MENU_transition is used as a flag.
 * When the value is read by calling MENU_get_transition()
 * this flag is cleared, respectively set to MENU_NONE.
 * MENU_active is updated only for content menu selections (MENU_ZERO..MENU_TEN).
 * Scroll transitions keep the currently active content menu unchanged.
 *****************************************************************************/
MENU_item_t MENU_get_transition(void)
{
    MENU_item_t temp = MENU_transition;
    if (temp != MENU_NONE)
    {
        MENU_transition = MENU_NONE;
        if ((temp >= MENU_ZERO) && (temp <= MENU_TEN))
        {
            MENU_active = temp;
        }
    }
    return temp;
}

/** ***************************************************************************
 * @brief Get active menu
 *
 * @return the active MENU_item or MENU_NONE if no MENU_item is active
 *****************************************************************************/
MENU_item_t MENU_get_active(void)
{
    return MENU_active;
}

/** ***************************************************************************
 * @brief Interrupt handler for the touchscreen
 *
 * @note BSP_TS_ITConfig(); must be called in the main function
 * to enable touchscreen interrupt.
 * @note There are timing issues when interrupt is enabled.
 * It seems that polling is the better choice with this evaluation board.
 * @n Call MENU_check_transition() from the while loop in main for polling.
 *
 * The touchscreen interrupt is connected to PA15.
 * @n The interrupt handler for external line 15 to 10 is called.
 *****************************************************************************/
void EXTI15_10_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR15)
    {		// Check if interrupt on touchscreen
        EXTI->PR |= EXTI_PR_PR15;		// Clear pending interrupt on line 15
        if (BSP_TS_ITGetStatus())
        {		// Get interrupt status
            BSP_TS_ITClear();				// Clear touchscreen controller int.
            MENU_check_transition();
        }
        EXTI->PR |= EXTI_PR_PR15;		// Clear pending interrupt on line 15
    }
}
