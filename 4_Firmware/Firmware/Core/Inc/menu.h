/** ***************************************************************************
 * @file
 * @brief See menu.c
 *
 * Prefix MENU
 *
 *****************************************************************************/

#ifndef MENU_H_
#define MENU_H_


/******************************************************************************
 * Includes
 *****************************************************************************/
#include "stdint.h"
#include "stdbool.h"


/******************************************************************************
 * Defines
 *****************************************************************************/
#define MENU_TOTAL_ENTRIES		9		///< Total number of menu entries available
#define MENU_VISIBLE_ENTRIES	5		///< Number of menu entries visible on screen
#define MENU_ARROW_SLOTS		2		///< Number of arrow slots (left + right)
#define MENU_CONTENT_SLOTS		3		///< Number of content slots (visible - arrows)

#define MENU_SCROLL_DEBOUNCE_MS	200		///< Minimum time (ms) between scroll actions

#define MENU_HEIGHT				40		///< Height of menu bar
#define MENU_MARGIN				2		///< Margin around a menu entry
/** Position of menu bar: 0 = top, (BSP_LCD_GetYSize()-MENU_HEIGHT) = bottom */
#define MENU_POSITION			(BSP_LCD_GetYSize()-MENU_HEIGHT)



/******************************************************************************
 * Types
 *****************************************************************************/
/** Enumeration of possible menu items */
typedef enum {
	MENU_ZERO = 0, MENU_ONE, MENU_TWO, MENU_THREE, MENU_FOUR,
	MENU_FIVE, MENU_SIX, MENU_SEVEN, MENU_EIGHT,
	MENU_SCROLL_LEFT, MENU_SCROLL_RIGHT, MENU_NONE
} MENU_item_t;
/** Struct with fields of a menu entry */
typedef struct {
	char line1[16];						///< First line of menu text
	char line2[16];						///< Second line of menu text
	uint32_t text_color;				///< Text color
	uint32_t back_color;				///< Background color
} MENU_entry_t;


/******************************************************************************
 * Functions
 *****************************************************************************/
void MENU_draw(void);
void MENU_set_entry(const MENU_item_t item, const MENU_entry_t entry);
MENU_entry_t MENU_get_entry(const MENU_item_t item);
void MENU_check_transition(void);
MENU_item_t MENU_get_transition(void);
MENU_item_t MENU_get_active(void);

// Scroll functions
void MENU_scroll_left(void);
void MENU_scroll_right(void);
uint32_t MENU_get_scroll_offset(void);
bool MENU_can_scroll_left(void);
bool MENU_can_scroll_right(void);


#endif
