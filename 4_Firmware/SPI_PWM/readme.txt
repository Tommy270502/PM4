
SPI and PWM Example using the HAL Driver inside "STM32F4xx_HAL_Driver"


The example is based on the demo code from the PM3:
//shared.zhaw.ch/pools/t/T-ISC-ET-PM3/Microcontroller_DemoCode/

The following two files have been changed from the PM3 demo code:

main.c
stm32f4xx_hal_conf.h   (Only "HAL_TIM_MODULE_ENABLED" was activated / changed)


PWM Output: PB7

SPI:
PE2: SPI4_SCK
PE4: SPI4_NSS
PE5: SPI4_MISO
PE6: SPI4_MOSI




Alternative to the above HAL Driver Implementation for PWM:

__HAL_RCC_GPIOB_CLK_ENABLE();
__HAL_RCC_TIM4_CLK_ENABLE();
GPIOB->MODER &= ~(GPIO_MODER_MODER7_Msk);
GPIOB->MODER |= (GPIO_MODER_MODER7_1);

GPIOB->OTYPER &= ~(GPIO_OTYPER_OT7_Msk);
GPIOB->AFR[0] &= ~(GPIO_AFRL_AFRL7);
GPIOB->AFR[0] |= GPIO_AFRL_AFRL7_1;

GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPD7_Msk);
GPIOB->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEEDR7);

TIM4->CR1 = 0;
TIM4->CR2 = 0;
TIM4->SMCR = 0;
TIM4->DIER = 0;

TIM4->PSC = 0x0;
TIM4->ARR = 500;
TIM4->CCMR1 = (0x6 <<12);
TIM4->CCR2 = 100;
TIM4->CCER = (0x01<<4);
TIM4->CR1 |= TIM_CR1_CEN;
