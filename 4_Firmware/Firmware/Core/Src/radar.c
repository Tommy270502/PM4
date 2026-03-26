/**
 * @file    radar.c
 * @brief   Radar ADC I/Q acquisition implementation.
 */

#include "radar.h"

#include <stdint.h>

#include "stm32f4xx.h"
#include "stm32f429i_discovery.h"

static uint32_t radar_iq_buffer_ping[RADAR_CHANNEL_SAMPLES];
static uint32_t radar_iq_buffer_pong[RADAR_CHANNEL_SAMPLES];

static float32_t *radar_i_buffer_pointer = 0;
static float32_t *radar_q_buffer_pointer = 0;

static uint8_t radar_data_ready = 0;

static void timer2_init_sample_rate(void);
static void adc_dual_dma_init(void);
static void unpack_iq_samples(uint32_t *packed_buffer);

void DMA2_Stream0_IRQHandler(void);
void DMA2_Stream1_IRQHandler(void);
void DMA2_Stream5_IRQHandler(void);

HAL_StatusTypeDef radar_init(float32_t *i_channel_buffer,
                                   float32_t *q_channel_buffer, uint32_t size)
{
    GPIO_InitTypeDef gpio_init = {0};

    if ((i_channel_buffer == 0) || (q_channel_buffer == 0) || (size < RADAR_CHANNEL_SAMPLES))
    {
        return HAL_ERROR;
    }

    radar_i_buffer_pointer = i_channel_buffer;
    radar_q_buffer_pointer = q_channel_buffer;

    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PC1 = ADC123_IN11 (I), PC3 = ADC123_IN13 (Q). */
    gpio_init.Pin = GPIO_PIN_1 | GPIO_PIN_3;
    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    timer2_init_sample_rate();
    adc_dual_dma_init();

    return HAL_OK;
}

void radar_start(void)
{
    /* Enable ADC2 first, then ADC1 (master in multimode). */
    ADC2->CR2 |= ADC_CR2_ADON;
    ADC1->CR2 |= ADC_CR2_ADON;

    /* Clear stale flags and start timer-triggered conversions. */
    radar_data_ready = 0;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 |= TIM_CR1_CEN;
}

uint8_t radar_frame_ready(void)
{
    return radar_data_ready;
}

void radar_clear_frame_ready(void)
{
    radar_data_ready = 0;
}

static void timer2_init_sample_rate(void)
{
    uint32_t ticks_per_sample;

    __HAL_RCC_TIM2_CLK_ENABLE();

    TIM2->CR1 = 0;
    TIM2->CR2 = 0;
    TIM2->PSC = 83U;     /* 84 MHz / (83 + 1) = 1 MHz timer tick */

    ticks_per_sample = (1000000U + (RADAR_SAMPLE_RATE_HZ / 2U)) / RADAR_SAMPLE_RATE_HZ;
    if (ticks_per_sample == 0U)
    {
        ticks_per_sample = 1U;
    }
    TIM2->ARR = ticks_per_sample - 1U;

    TIM2->CNT = 0;

    /* TRGO on update event. */
    TIM2->CR2 |= TIM_CR2_MMS_1;
    TIM2->EGR = TIM_EGR_UG;
}

static void adc_dual_dma_init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC2_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* Disable ADCs before configuration. */
    ADC1->CR2 &= ~ADC_CR2_ADON;
    ADC2->CR2 &= ~ADC_CR2_ADON;

    /* Disable DMA stream before reconfiguration. */
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream0->CR & DMA_SxCR_EN)
    {
    }

    DMA2->LIFCR = DMA_LIFCR_CFEIF0 | DMA_LIFCR_CDMEIF0 |
                  DMA_LIFCR_CTEIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTCIF0;

    /* ADC common: dual regular simultaneous mode, DMA access mode 2. */
    ADC->CCR = 0;
    ADC->CCR |= ADC_CCR_ADCPRE_0 | ADC_CCR_ADCPRE_1; /* PCLK2/8 */
    ADC->CCR |= ADC_CCR_MULTI_0;                     /* Regular simultaneous mode */
    ADC->CCR |= ADC_CCR_DMA_1;                       /* DMA mode 2 */
    ADC->CCR |= ADC_CCR_DDS;                         /* DMA requests issued continuously */

    /* ADC1 = I channel (PC1 / IN11). */
    ADC1->CR1 = 0;
    ADC1->CR2 = 0;
    ADC1->SMPR1 &= ~ADC_SMPR1_SMP11_Msk;
    ADC1->SMPR1 |= (5UL << ADC_SMPR1_SMP11_Pos);     /* 84 cycles sample time */
    ADC1->SQR1 = 0;
    ADC1->SQR3 = 11U;
    ADC1->CR2 |= ADC_CR2_EXTEN_0;                    /* Trigger on rising edge */
    ADC1->CR2 |= ADC_CR2_EXTSEL_1 | ADC_CR2_EXTSEL_2; /* TIM2_TRGO */
    ADC1->CR2 |= ADC_CR2_DMA;

    /* ADC2 = Q channel (PC3 / IN13). */
    ADC2->CR1 = 0;
    ADC2->CR2 = 0;
    ADC2->SMPR1 &= ~ADC_SMPR1_SMP13_Msk;
    ADC2->SMPR1 |= (5UL << ADC_SMPR1_SMP13_Pos);
    ADC2->SQR1 = 0;
    ADC2->SQR3 = 13U;
    ADC2->CR2 |= ADC_CR2_EXTEN_0;
    ADC2->CR2 |= ADC_CR2_EXTSEL_1 | ADC_CR2_EXTSEL_2;

    /* DMA2 Stream0 Channel0 reads packed ADC_CDR values. */
    DMA2_Stream0->CR = 0;
    DMA2_Stream0->CR |= (0UL << DMA_SxCR_CHSEL_Pos); /* Channel 0 */
    DMA2_Stream0->CR |= DMA_SxCR_PL_1;               /* High priority */
    DMA2_Stream0->CR |= DMA_SxCR_MSIZE_1;            /* Memory 32-bit */
    DMA2_Stream0->CR |= DMA_SxCR_PSIZE_1;            /* Peripheral 32-bit */
    DMA2_Stream0->CR |= DMA_SxCR_MINC;
    DMA2_Stream0->CR |= DMA_SxCR_CIRC;
    DMA2_Stream0->CR |= DMA_SxCR_DBM;
    DMA2_Stream0->CR |= DMA_SxCR_TCIE;

    DMA2_Stream0->NDTR = RADAR_CHANNEL_SAMPLES;
    DMA2_Stream0->PAR = (uint32_t)&(ADC->CDR);
    DMA2_Stream0->M0AR = (uint32_t)radar_iq_buffer_ping;
    DMA2_Stream0->M1AR = (uint32_t)radar_iq_buffer_pong;

    DMA2_Stream0->CR |= DMA_SxCR_EN;

    NVIC_SetPriority(DMA2_Stream0_IRQn, 1);
    NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

static void unpack_iq_samples(uint32_t *packed_buffer)
{
    if ((packed_buffer == 0) || (radar_i_buffer_pointer == 0) || (radar_q_buffer_pointer == 0))
    {
        return;
    }

    for (uint32_t i = 0; i < RADAR_CHANNEL_SAMPLES; i++)
    {
        uint32_t pair = packed_buffer[i];

        /* CDR layout in dual regular mode: [31:16]=ADC2 (Q), [15:0]=ADC1 (I). */
        radar_i_buffer_pointer[i] = (float32_t)(pair & 0xFFFFU);
        radar_q_buffer_pointer[i] = (float32_t)((pair >> 16) & 0xFFFFU);
    }
}

void DMA2_Stream0_IRQHandler(void)
{
    if (DMA2->LISR & DMA_LISR_TCIF0)
    {
        uint32_t *completed_buffer;

        DMA2->LIFCR = DMA_LIFCR_CTCIF0;

        if ((DMA2_Stream0->CR & DMA_SxCR_CT) == 0U)
        {
            completed_buffer = radar_iq_buffer_pong;
        }
        else
        {
            completed_buffer = radar_iq_buffer_ping;
        }

        unpack_iq_samples(completed_buffer);
        radar_data_ready = 1;
        BSP_LED_Toggle(LED4);
    }
}

/* Legacy handlers kept to preserve vector symbol compatibility. */
void DMA2_Stream1_IRQHandler(void)
{
}

void DMA2_Stream5_IRQHandler(void)
{
}
