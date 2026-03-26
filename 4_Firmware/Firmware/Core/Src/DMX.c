/**
 * @file    DMX.c
 * @brief   DMX output to LED party panel
 * @author Hanspeter Hochreutener, hhrt@zhaw.ch
 * @modified_by Patrick Rennhard, renn@zhaw.ch
 * @modified_date 12.08.2025
 */

/******************************************************************************
 * Includes
 *****************************************************************************/

#include <stdio.h>
#include "stm32f4xx.h"
#include "stm32f429i_discovery.h"

#include "DMX.h"

/******************************************************************************
 * Defines
 *****************************************************************************/

/******************************************************************************
 * Variables
 *****************************************************************************/
uint8_t DMX_data[DMX_CHANNEL_COUNT + 1];			///< Transmission buffer

UART_HandleTypeDef UartHandle;			///< UART handler definition
HAL_StatusTypeDef status;				///< UART status or error

/******************************************************************************
 * Functions
 *****************************************************************************/

void DMX_init(void) {
	// Configure GPIOs
	__HAL_RCC_GPIOC_CLK_ENABLE();		// Enable Clock for GPIO port C
	GPIOC->MODER |= (1UL << GPIO_MODER_MODER12_Pos);		// PC12 as output
	GPIOC->BSRR = GPIO_BSRR_BS12;		// Set PC12 to high
	GPIOC->MODER &= ~(1UL << GPIO_MODER_MODER12_Pos);// PC12 not as output, but
	GPIOC->MODER |= (2UL << GPIO_MODER_MODER12_Pos);	// as alternate function
	GPIOC->AFR[1] |= (8UL << GPIO_AFRH_AFSEL12_Pos);	// and set it to UART5
	// Configure UART5
	__HAL_RCC_UART5_CLK_ENABLE();// Enable Clock for UART5
	UartHandle.Instance = UART5;
	UartHandle.Init.BaudRate = 250000;
	UartHandle.Init.WordLength = UART_WORDLENGTH_8B;
	UartHandle.Init.StopBits = UART_STOPBITS_2;
	UartHandle.Init.Parity = UART_PARITY_NONE;
	UartHandle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	UartHandle.Init.Mode = UART_MODE_TX;
	UartHandle.Init.OverSampling = UART_OVERSAMPLING_16;
	status = HAL_UART_Init(&UartHandle);

	memset(DMX_data, 0, sizeof(DMX_data));

	//LightmaXX LED NANO PAR
	DMX_data[1] = 0; // No Funcition
	DMX_data[2] = 0; // Colour Chase
	DMX_data[3] = 0; // Speed
	DMX_data[4] = 255; // Dimmer

	DMX_data[DMX_CHANNEL_OFFSET + 0] = 0;	// Red
	DMX_data[DMX_CHANNEL_OFFSET + 1] = 0; 	// Green
	DMX_data[DMX_CHANNEL_OFFSET + 2] = 0;	// Blue
	DMX_data[DMX_CHANNEL_OFFSET + 3] = 0;	// White
}

void DMX_setColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t white) {
	DMX_data[DMX_CHANNEL_OFFSET + 0] = red;
	DMX_data[DMX_CHANNEL_OFFSET + 1] = green;
	DMX_data[DMX_CHANNEL_OFFSET + 2] = blue;
	DMX_data[DMX_CHANNEL_OFFSET + 3] = white;
}

void DMX_transmit(void) {
	volatile uint32_t l = 0;
	const uint32_t t_start = 4000;		// loop start value for ns timer
	const uint32_t t_inc = 85;			// loop increment for ns timer
	GPIOC->MODER &= ~(2UL << GPIO_MODER_MODER12_Pos); 	// Not as alternate f.
	GPIOC->MODER |= (1UL << GPIO_MODER_MODER12_Pos);	// but PC12 as output
	GPIOC->BSRR = GPIO_BSRR_BR12;		// Set PC12 to low for BREAK
// HAL_Delay() always adds 1ms => HAL_Delay(0); waits for 1ms = 1000us
//HAL_Delay(0);						// BREAK must be at least 100us
	l = t_start;
	while (l < 100000) {					// 100us = 100'000ns delay
		l += t_inc;
	}
	GPIOC->BSRR = GPIO_BSRR_BS12;		// Set PC12 to high for MAB
//HAL_Delay(0);						// MAB must be at least 12us
	l = t_start;
	while (l < 12000) {					// 12us = 12'000ns delay
		l += t_inc;
	}
	GPIOC->MODER &= ~(1UL << GPIO_MODER_MODER12_Pos);// PC12 not as output, but
	GPIOC->MODER |= (2UL << GPIO_MODER_MODER12_Pos);	// as alternate function
// Send the data over UART
	status = HAL_UART_Transmit(&UartHandle, (uint8_t*) DMX_data,
	DMX_CHANNEL_COUNT + 1, 1000);
}

