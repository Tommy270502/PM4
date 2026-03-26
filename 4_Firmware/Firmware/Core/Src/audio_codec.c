/**
 * @file    audio_codec.c
 * @brief   Compatibility acquisition API now backed by radar ADC I/Q capture.
 *
 * Public API names are intentionally kept so the existing main loop can be
 * migrated in small steps.
 */

#include "audio_codec.h"

#include "stm32f429i_discovery.h"

static uint32_t radar_iq_buffer_ping[AUDIO_CHANNEL_SIZE];
static uint32_t radar_iq_buffer_pong[AUDIO_CHANNEL_SIZE];

static float32_t *left_channel_buffer_pointer = 0;
static float32_t *right_channel_buffer_pointer = 0;

static uint8_t audio_codec_data_ready = 0;

static void timer2_init_100hz(void);
static void adc_dual_dma_init(void);
static void unpack_iq_samples(uint32_t *packed_buffer);

void DMA2_Stream0_IRQHandler(void);
void DMA2_Stream1_IRQHandler(void);
void DMA2_Stream5_IRQHandler(void);

HAL_StatusTypeDef codec_init(float32_t *left_channel_buffer,
                             float32_t *right_channel_buffer, uint32_t size)
{
    GPIO_InitTypeDef gpio_init = {0};

    if ((left_channel_buffer == 0) || (right_channel_buffer == 0) || (size < AUDIO_CHANNEL_SIZE))
    {
        return HAL_ERROR;
    }

    left_channel_buffer_pointer = left_channel_buffer;
    right_channel_buffer_pointer = right_channel_buffer;

    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PC1 = ADC123_IN11 (I), PC3 = ADC123_IN13 (Q). */
    gpio_init.Pin = GPIO_PIN_1 | GPIO_PIN_3;
    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    timer2_init_100hz();
    adc_dual_dma_init();

    return HAL_OK;
}

void codec_start(void)
{
    /* Enable ADC2 first, then ADC1 (master in multimode). */
    ADC2->CR2 |= ADC_CR2_ADON;
    ADC1->CR2 |= ADC_CR2_ADON;

    /* Clear stale flags and start timer-triggered conversions. */
    audio_codec_data_ready = 0;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 |= TIM_CR1_CEN;
}

uint8_t codec_data_ready(void)
{
    return audio_codec_data_ready;
}

void codec_clear_data_ready(void)
{
    audio_codec_data_ready = 0;
}

void codec_update_output_buffer(uint8_t channel, float32_t *data, uint32_t size)
{
    (void)channel;
    (void)data;
    (void)size;
}

void codec_mirror_left_channel(void)
{
    if ((left_channel_buffer_pointer == 0) || (right_channel_buffer_pointer == 0))
    {
        return;
    }

    for (uint32_t i = 0; i < AUDIO_CHANNEL_SIZE; i++)
    {
        right_channel_buffer_pointer[i] = left_channel_buffer_pointer[i];
    }
}

uint8_t codec_is_right_channel_present(void)
{
    /* Radar front-end provides both I and Q channels by design. */
    return 1;
}

static void timer2_init_100hz(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();

    TIM2->CR1 = 0;
    TIM2->CR2 = 0;
    TIM2->PSC = 8399U;   /* 84 MHz / (8399 + 1) = 10 kHz */
    TIM2->ARR = 99U;     /* 10 kHz / (99 + 1) = 100 Hz */
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

    DMA2_Stream0->NDTR = AUDIO_CHANNEL_SIZE;
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
    if ((packed_buffer == 0) || (left_channel_buffer_pointer == 0) || (right_channel_buffer_pointer == 0))
    {
        return;
    }

    for (uint32_t i = 0; i < AUDIO_CHANNEL_SIZE; i++)
    {
        uint32_t pair = packed_buffer[i];

        /* CDR layout in dual regular mode: [31:16]=ADC2 (Q), [15:0]=ADC1 (I). */
        left_channel_buffer_pointer[i] = (float32_t)(pair & 0xFFFFU);
        right_channel_buffer_pointer[i] = (float32_t)((pair >> 16) & 0xFFFFU);
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
        audio_codec_data_ready = 1;
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
