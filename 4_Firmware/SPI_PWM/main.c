/** ***************************************************************************
 * @file
 * @brief Sets up the microcontroller, the clock system and the peripherals.
 *
 * Initialization is done for the system, the blue user button, the user LEDs,
 * and the LCD display with the touchscreen.
 * @n Then the code enters an infinite while-loop, where it checks for
 * user input and starts the requested demonstration.
 *
 * @author  Hanspeter Hochreutener, hhrt@zhaw.ch
 * @date	17.06.2021
 *
 * SPI and PWM Example added: Beat Käfer kaeb@zhaw.ch  11.04.2022
 *****************************************************************************/


/******************************************************************************
 * Includes
 *****************************************************************************/
#include "stm32f4xx.h"
#include "stm32f429i_discovery.h"
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_ts.h"


#include "main.h"
#include "pushbutton.h"
#include "menu.h"
#include "measuring.h"


/******************************************************************************
 * Defines
 *****************************************************************************/


/******************************************************************************
 * Variables
 *****************************************************************************/


/******************************************************************************
 * Functions
 *****************************************************************************/
static void SystemClock_Config(void);	///< System Clock Configuration
static void gyro_disable(void);			///< Disable the onboard gyroscope
static void pwm_init(TIM_HandleTypeDef *const tim_handle);
static void spi_init(SPI_HandleTypeDef * const spi_handle);


/** ***************************************************************************
 * @brief  Main function
 * @return not used because main ends in an infinite loop
 *
 * Initialization and infinite while loop
 *****************************************************************************/
int main(void) {
	HAL_Init();							// Initialize the system

	SystemClock_Config();				// Configure system clocks

	PB_init();							// Initialize the user pushbutton
	PB_enableIRQ();						// Enable interrupt on user pushbutton

	SPI_HandleTypeDef spi_handle;
	uint8_t spi_data = 0xAA;
	spi_init(&spi_handle);
	TIM_HandleTypeDef pwm_handle;
	pwm_init(&pwm_handle);

	/* Infinite while loop */
	while (1) {							// Infinitely loop in main function

		if (PB_pressed()) {
			HAL_SPI_Transmit(&spi_handle, &spi_data, 1, HAL_MAX_DELAY);
		}
	}
}


void pwm_init(TIM_HandleTypeDef *const tim_handle){

	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_TIM4_CLK_ENABLE();

	tim_handle->Instance = TIM4;
	tim_handle->Init.Prescaler = 0;
	tim_handle->Init.CounterMode = TIM_COUNTERMODE_UP;
	tim_handle->Init.Period = 500;
	tim_handle->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	HAL_TIM_PWM_Init(tim_handle);

	TIM_MasterConfigTypeDef tim_master_conf;
	tim_master_conf.MasterOutputTrigger = TIM_TRGO_RESET;
	tim_master_conf.MasterSlaveMode = TIM_SLAVEMODE_DISABLE;
	HAL_TIMEx_MasterConfigSynchronization(tim_handle, &tim_master_conf);

	TIM_OC_InitTypeDef tim_oc_conf;
	tim_oc_conf.OCMode = TIM_OCMODE_PWM1;
	tim_oc_conf.Pulse = 100;
	tim_oc_conf.OCFastMode = TIM_OCFAST_DISABLE;
	tim_oc_conf.OCIdleState = TIM_OCIDLESTATE_RESET;
	HAL_TIM_PWM_ConfigChannel(tim_handle, &tim_oc_conf, TIM_CHANNEL_2);

	GPIO_InitTypeDef   GPIO_InitStructure;
	GPIO_InitStructure.Pin    = (GPIO_PIN_7);
	GPIO_InitStructure.Mode   = GPIO_MODE_AF_PP;
	GPIO_InitStructure.Speed  = GPIO_SPEED_MEDIUM;
	GPIO_InitStructure.Alternate = GPIO_AF2_TIM4;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStructure);

	HAL_TIM_PWM_Start(tim_handle, TIM_CHANNEL_2);
}

void spi_init(SPI_HandleTypeDef * const spi_handle){
	  GPIO_InitTypeDef   GPIO_InitStructure;
	__HAL_RCC_GPIOE_CLK_ENABLE();
	__HAL_RCC_SPI4_CLK_ENABLE();


	spi_handle->Instance = SPI4;
	spi_handle->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
	spi_handle->Init.Mode = SPI_MODE_MASTER;
    spi_handle->Init.Direction      = SPI_DIRECTION_2LINES;
    spi_handle->Init.CLKPhase       = SPI_PHASE_2EDGE;
    spi_handle->Init.CLKPolarity    = SPI_POLARITY_HIGH;
    spi_handle->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLED;
    spi_handle->Init.CRCPolynomial  = 7;
    spi_handle->Init.DataSize       = SPI_DATASIZE_8BIT;
    spi_handle->Init.FirstBit       = SPI_FIRSTBIT_LSB;
    spi_handle->Init.NSS            = SPI_NSS_SOFT;
    spi_handle->Init.TIMode         = SPI_TIMODE_DISABLED;
    HAL_SPI_Init(spi_handle);

	GPIO_InitStructure.Pin    = (GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6);
	GPIO_InitStructure.Mode   = GPIO_MODE_AF_PP;
	GPIO_InitStructure.Pull   = GPIO_PULLDOWN;
	GPIO_InitStructure.Speed  = GPIO_SPEED_MEDIUM;
	GPIO_InitStructure.Alternate = GPIO_AF5_SPI4;
	HAL_GPIO_Init(GPIOE, &GPIO_InitStructure);
}


/** ***************************************************************************
 * @brief System Clock Configuration
 *
 *****************************************************************************/
static void SystemClock_Config(void){
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
	/* Configure the main internal regulator output voltage */
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
	/* Initialize High Speed External Oscillator and PLL circuits */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLM = 8;
	RCC_OscInitStruct.PLL.PLLN = 336;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
	RCC_OscInitStruct.PLL.PLLQ = 7;
	HAL_RCC_OscConfig(&RCC_OscInitStruct);
	/* Initialize gates and clock dividers for CPU, AHB and APB busses */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
	HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
	/* Initialize PLL and clock divider for the LCD */
	PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
	PeriphClkInitStruct.PLLSAI.PLLSAIN = 192;
	PeriphClkInitStruct.PLLSAI.PLLSAIR = 4;
	PeriphClkInitStruct.PLLSAIDivR = RCC_PLLSAIDIVR_8;
	HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
	/* Set clock prescaler for ADCs */
	ADC->CCR |= ADC_CCR_ADCPRE_0;
}


/** ***************************************************************************
 * @brief Disable the GYRO on the microcontroller board.
 *
 * @note MISO of the GYRO is connected to PF8 and CS to PC1.
 * @n Some times the GYRO goes into an undefined mode at startup
 * and pulls the MISO low or high thus blocking the analog input on PF8.
 * @n The simplest solution is to pull the CS of the GYRO low for a short while
 * which is done with the code below.
 * @n PF8 is also reconfigured.
 * @n An other solution would be to remove the GYRO
 * from the microcontroller board by unsoldering it.
 *****************************************************************************/
static void gyro_disable(void)
{
	__HAL_RCC_GPIOC_CLK_ENABLE();		// Enable Clock for GPIO port C
	/* Disable PC1 and PF8 first */
	GPIOC->MODER &= ~GPIO_MODER_MODER1; // Reset mode for PC1
	GPIOC->MODER |= GPIO_MODER_MODER1_0;	// Set PC1 as output
	GPIOC->BSRR |= GPIO_BSRR_BR1;		// Set GYRO (CS) to 0 for a short time
	HAL_Delay(10);						// Wait some time
	GPIOC->MODER |= GPIO_MODER_MODER1_Msk; // Analog mode PC1 = ADC123_IN11
	__HAL_RCC_GPIOF_CLK_ENABLE();		// Enable Clock for GPIO port F
	GPIOF->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED8;	// Reset speed of PF8
	GPIOF->AFR[1] &= ~GPIO_AFRH_AFSEL8;			// Reset alternate func. of PF8
	GPIOF->PUPDR &= ~GPIO_PUPDR_PUPD8;			// Reset pulup/down of PF8
	HAL_Delay(10);						// Wait some time
	GPIOF->MODER |= GPIO_MODER_MODER8_Msk; // Analog mode for PF6 = ADC3_IN4
}
