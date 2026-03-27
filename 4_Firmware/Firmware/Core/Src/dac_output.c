/** ***************************************************************************
 * @file
 * @brief DAC output control on PA5 (DAC channel 2)
 *
 *****************************************************************************/

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "stm32f4xx.h"
#include "stm32f4xx_hal_gpio.h"

#include "dac_output.h"

/******************************************************************************
 * Variables
 *****************************************************************************/
static uint16_t dac_output_code = 0U;

/******************************************************************************
 * Functions
 *****************************************************************************/
static uint16_t dac_output_voltage_to_code(float voltage)
{
	if (voltage <= 0.0f) {
		return 0U;
	}

	if (voltage >= DAC_OUTPUT_MAX_VOLTAGE) {
		return DAC_OUTPUT_MAX_CODE;
	}

	return (uint16_t) (((voltage * (float) DAC_OUTPUT_MAX_CODE)
			/ DAC_OUTPUT_MAX_VOLTAGE) + 0.5f);
}

void dac_output_init(void)
{
	GPIO_InitTypeDef gpio_init = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();
	RCC->APB1ENR |= RCC_APB1ENR_DACEN;
	(void) RCC->APB1ENR;

	gpio_init.Pin = GPIO_PIN_5;
	gpio_init.Mode = GPIO_MODE_ANALOG;
	gpio_init.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOA, &gpio_init);

	DAC->CR &= ~(DAC_CR_EN2 | DAC_CR_BOFF2 | DAC_CR_TEN2 | DAC_CR_TSEL2
			| DAC_CR_WAVE2 | DAC_CR_MAMP2 | DAC_CR_DMAEN2 | DAC_CR_DMAUDRIE2);
	DAC->DHR12R2 = 0U;
	DAC->CR |= DAC_CR_EN2;
	dac_output_code = 0U;
}

void dac_output_set_voltage(float voltage)
{
	dac_output_code = dac_output_voltage_to_code(voltage);
	DAC->DHR12R2 = ((uint32_t) dac_output_code & DAC_DHR12R2_DACC2DHR);
}

float dac_output_get_voltage(void)
{
	return ((float) dac_output_code * DAC_OUTPUT_MAX_VOLTAGE)
			/ (float) DAC_OUTPUT_MAX_CODE;
}

uint16_t dac_output_get_code(void)
{
	return dac_output_code;
}
