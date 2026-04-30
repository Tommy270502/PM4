/** ***************************************************************************
 * @file
 * @brief TX-only OpenLog logger – USART6 on PG14 implementation.
 *
 * Configures USART6 TX at 9600-8-N-1 on PG14 (AF8) for one-way serial
 * output to an OpenLog module.  All writes are best-effort: if the UART
 * is busy or an error occurs the frame is silently dropped and a counter
 * is incremented.
 *
 *****************************************************************************/

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "openlog_uart.h"

#include <stdio.h>
#include <string.h>

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"

/** Maximum formatted CSV row length including \r\n and NUL terminator. */
enum { OPENLOG_LINE_BUF_SIZE = 80U };

/** CSV header string written on every START transition. */
static const char openlog_csv_header[] = "tick_ms,bpm,valid,state\r\n";

/** OpenLog command mode control. */
enum {
    OPENLOG_ESCAPE_CHAR = 26U,
    OPENLOG_ESCAPE_COUNT = 3U,
    OPENLOG_ESCAPE_DELAY_MS = 20U,
    OPENLOG_CMD_GUARD_MS = 50U,
    OPENLOG_CMD_DELAY_MS = 50U,
    OPENLOG_MAX_SESSIONS = 9999U
};

/******************************************************************************
 * Variables
 *****************************************************************************/
static UART_HandleTypeDef openlog_huart;
static bool     openlog_enabled = false;
static bool     openlog_in_command_mode = false;
static uint16_t openlog_session_index = 0U;
static uint32_t openlog_drop_count = 0U;

/******************************************************************************
 * Private helpers
 *****************************************************************************/

/**
 * @brief  Best-effort transmit of a raw buffer.
 *
 * If the transmit fails for any reason (busy, error, timeout) the drop
 * counter is incremented and the function returns immediately.
 */
static bool openlog_tx(const uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef status;

    status = HAL_UART_Transmit(&openlog_huart, (uint8_t *)buf, len, OPENLOG_TX_TIMEOUT_MS);

    if (status != HAL_OK)
    {
        openlog_drop_count++;
        return false;
    }

    return true;
}

static bool openlog_send_escape_sequence(void)
{
    bool ok = true;
    uint8_t esc = (uint8_t)OPENLOG_ESCAPE_CHAR;

    for (uint32_t i = 0U; i < OPENLOG_ESCAPE_COUNT; i++)
    {
        if (!openlog_tx(&esc, 1U))
        {
            ok = false;
        }
        HAL_Delay(OPENLOG_ESCAPE_DELAY_MS);
    }

    return ok;
}

static bool openlog_send_command(const char *command)
{
    if (command == NULL)
    {
        openlog_drop_count++;
        return false;
    }

    return openlog_tx((const uint8_t *)command, (uint16_t)strlen(command));
}

/**
 * @brief  Emit the CSV header row.
 */
static void openlog_write_header(void)
{
    openlog_tx((const uint8_t *)openlog_csv_header,
               (uint16_t)(sizeof(openlog_csv_header) - 1U));
}

static void openlog_format_next_filename(char *buffer, size_t buffer_size)
{
    uint16_t next_index = (uint16_t)(openlog_session_index + 1U);

    if (next_index > OPENLOG_MAX_SESSIONS)
    {
        next_index = 1U;
    }

    openlog_session_index = next_index;

    snprintf(buffer, buffer_size, "LOG%04u.CSV", (unsigned int)openlog_session_index);
}

/******************************************************************************
 * Public API
 *****************************************************************************/

void openlog_init(void)
{
    GPIO_InitTypeDef gpio_init;

    /* ---- Clock gates --------------------------------------------------- */
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_USART6_CLK_ENABLE();

    /* ---- PG14 -> USART6_TX (AF8) --------------------------------------- */
    gpio_init.Pin       = GPIO_PIN_14;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = GPIO_NOPULL;
    gpio_init.Speed     = GPIO_SPEED_FREQ_LOW;
    gpio_init.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(GPIOG, &gpio_init);

    /* ---- USART6 – 9600-8-N-1, TX only --------------------------------- */
    openlog_huart.Instance          = USART6;
    openlog_huart.Init.BaudRate     = OPENLOG_BAUD_RATE;
    openlog_huart.Init.WordLength   = UART_WORDLENGTH_8B;
    openlog_huart.Init.StopBits     = UART_STOPBITS_1;
    openlog_huart.Init.Parity       = UART_PARITY_NONE;
    openlog_huart.Init.Mode         = UART_MODE_TX;
    openlog_huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    openlog_huart.Init.OverSampling = UART_OVERSAMPLING_16;

    HAL_UART_Init(&openlog_huart);

    /* Logging starts OFF by default. */
    openlog_enabled = false;
    openlog_in_command_mode = false;
    openlog_session_index = 0U;
    openlog_drop_count = 0U;
}

bool openlog_start_session(void)
{
    char filename[13];
    char command[24];
    bool ok = true;

    if (openlog_enabled)
    {
        return false;
    }

    if (!openlog_in_command_mode)
    {
        ok = openlog_send_escape_sequence();
        HAL_Delay(OPENLOG_CMD_GUARD_MS);
        if (!ok)
        {
            return false;
        }
        openlog_in_command_mode = true;
    }

    openlog_format_next_filename(filename, sizeof(filename));
    snprintf(command, sizeof(command), "append %s\r", filename);

    if (!openlog_send_command(command))
    {
        return false;
    }

    HAL_Delay(OPENLOG_CMD_DELAY_MS);
    openlog_write_header();

    openlog_enabled = true;
    openlog_in_command_mode = false;

    return true;
}

bool openlog_stop_session(void)
{
    bool was_enabled = openlog_enabled;

    if (!openlog_in_command_mode)
    {
        openlog_send_escape_sequence();
        HAL_Delay(OPENLOG_CMD_GUARD_MS);
        openlog_in_command_mode = true;
    }

    openlog_send_command("sync\r");
    HAL_Delay(OPENLOG_CMD_DELAY_MS);

    openlog_enabled = false;

    return was_enabled;
}

bool openlog_is_enabled(void)
{
    return openlog_enabled;
}

uint32_t openlog_get_drop_count(void)
{
    return openlog_drop_count;
}

void openlog_write_row(uint32_t tick_ms,
                       float bpm,
                       bool valid,
                       radar_hr_sm_state_t state)
{
    char buf[OPENLOG_LINE_BUF_SIZE];
    int len;

    if (!openlog_enabled)
    {
        return;
    }

    /* Format: tick_ms,bpm,valid,state\r\n
     * Use integer BPM to avoid pulling in printf-float on nano specs. */
    len = snprintf(buf, sizeof(buf), "%lu,%d,%d,%d\r\n",
                   (unsigned long)tick_ms,
                   (int)(bpm + 0.5f),
                   (int)valid,
                   (int)state);

    if ((len > 0) && ((uint32_t)len < sizeof(buf)))
    {
        openlog_tx((const uint8_t *)buf, (uint16_t)len);
    }
    else
    {
        openlog_drop_count++;
    }
}
