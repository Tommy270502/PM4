/** ***************************************************************************
 * @file
 * @brief TX-only OpenLog logger over USART6 on PG14.
 *
 * Provides best-effort CSV logging of radar HR summary data to an OpenLog
 * module connected to PG14 (USART6 TX, AF8) at 9600 baud.
 *
 * Prefix OPENLOG
 *
 *****************************************************************************/

#ifndef OPENLOG_UART_H_
#define OPENLOG_UART_H_

/******************************************************************************
 * Includes
 *****************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include "radar_heartrate.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
/** TX timeout for a single HAL_UART_Transmit call [ms]. */
#define OPENLOG_TX_TIMEOUT_MS   50U

/** Baud rate for OpenLog default serial interface. */
#define OPENLOG_BAUD_RATE       9600U

/******************************************************************************
 * Functions
 *****************************************************************************/

/**
 * @brief  One-time initialisation of USART6 TX on PG14 at 9600 baud.
 *
 * Enables GPIOG and USART6 clocks, configures PG14 for AF8 (USART6_TX),
 * and calls HAL_UART_Init in TX-only mode.  Logging starts in the OFF state.
 */
void openlog_init(void);

/**
 * @brief  Enable or disable CSV logging.
 *
 * When transitioning from OFF -> ON the CSV header row is emitted
 * automatically so each logging session begins with column names.
 *
 * @param[in] enable  true = start logging, false = stop.
 */
void openlog_set_enabled(bool enable);

/**
 * @brief  Query whether logging is currently enabled.
 * @return true if logging is ON.
 */
bool openlog_is_enabled(void);

/**
 * @brief  Return the cumulative count of TX drops / errors.
 *
 * A drop occurs when HAL_UART_Transmit returns busy, error, or timeout.
 * The counter is never reset; it only grows.
 *
 * @return Number of dropped transmissions since init.
 */
uint32_t openlog_get_drop_count(void);

/**
 * @brief  Write one CSV data row if logging is enabled.
 *
 * Format: tick_ms,bpm,valid,state\r\n
 *
 * Best-effort: if the UART is busy or fails, the row is silently dropped
 * and the internal drop counter is incremented.
 *
 * @param[in] tick_ms   Timestamp (e.g. HAL_GetTick()).
 * @param[in] bpm       Heart-rate estimate.
 * @param[in] valid     true if HR is valid (LOCKED + accepted).
 * @param[in] state     Current state-machine state.
 */
void openlog_write_row(uint32_t tick_ms,
                       float bpm,
                       bool valid,
                       radar_hr_sm_state_t state);

#endif /* OPENLOG_UART_H_ */
