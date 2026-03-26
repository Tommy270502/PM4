/**
 * @file    audio_codec.c
 * @brief   API for audio codec initialization, SAI/DMA handling, and audio buffer management.
 * @author  Patrick Rennhard (renn@zhaw.ch)
 * @date    2025-09-03
 */

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "audio_codec.h"
#include "calc.h"

#include <stdio.h>

#include "stm32f4xx.h"

#include "stm32f429i_discovery.h"
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_ts.h"
#include "stm32f4xx_hal_uart.h"
#include "stm32f4xx_hal_i2c.h"

/******************************************************************************
 * Defines
 *****************************************************************************/

/******************************************************************************
 * Variables
 *****************************************************************************/

static int32_t audio_in_buffer_ping[AUDIO_FRAME_SIZE];
static int32_t audio_in_buffer_pong[AUDIO_FRAME_SIZE];

static int32_t audio_out_buffer_ping[AUDIO_FRAME_SIZE];
static int32_t audio_out_buffer_pong[AUDIO_FRAME_SIZE];
static int32_t *next_audio_out_buffer_pointer = audio_out_buffer_ping;

static float32_t *left_channel_buffer_pointer = 0;
static float32_t *right_channel_buffer_pointer = 0;

static uint8_t audio_codec_data_ready = 0;

static int32_t *last_completed_rx_buffer = NULL;

/******************************************************************************
 * Functions
 *****************************************************************************/

/**
 * @brief Initializes the SAI interface DMA for audio streaming.
 */
static void sai_dma_init(void);

#ifdef AUDIO_INPUT_I2S
/**
 * @brief Initializes the I2S2 peripheral for external I2S audio input.
 */
static void i2s_input_init(void);
#endif

/**
 * @brief Split and convert interleaved I2S data into float buffers.
 */
static void split_and_cast_i2s_buffer(int32_t *i2s_buffer, float32_t *left_out,
                                      float32_t *right_out, uint32_t out_buffer_size);

void DMA2_Stream1_IRQHandler(void);
void DMA2_Stream5_IRQHandler(void);

void codec_reset(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_Delay(1000);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
}

HAL_StatusTypeDef codec_init(float32_t *left_channel_buffer,
                             float32_t *right_channel_buffer, uint32_t size)
{
    HAL_StatusTypeDef ret_val;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitTypeDef GPIO_InitStructSAI = {0};

    if (size >= AUDIO_FRAME_SIZE / 2)
    {
        left_channel_buffer_pointer = left_channel_buffer;
        right_channel_buffer_pointer = right_channel_buffer;

        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOE_CLK_ENABLE();

        /* ============================================================
         * CONTROL GPIOs
         *  - PB4: codec reset (used by codec_reset())
         * ============================================================ */

        // --- GPIOB: PB4 as Output, initial Low (hold codec in reset) ---
        GPIO_InitStruct.Pin = GPIO_PIN_4;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);

        /* ============================================================
         * SAI1 PINS (PE2..PE6) FOR CODEC
         *
         *  PE2 : SAI1_MCLK_A → CS4271 MCLK
         *  PE3 : SAI1_SD_B   → Codec SDIN  (STM -> codec)
         *  PE4 : SAI1_FS_A   → Codec LRCK
         *  PE5 : SAI1_SCK_A  → Codec SCLK/BCLK
         *  PE6 : SAI1_SD_A   → Codec SDOUT (codec -> STM)
         *
         * All must be AF6 for SAI1.
         * ============================================================ */

        GPIO_InitStructSAI.Pin = GPIO_PIN_3 | GPIO_PIN_4 |
                                 GPIO_PIN_5 | GPIO_PIN_6;

        GPIO_InitStructSAI.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStructSAI.Pull = GPIO_NOPULL;
        GPIO_InitStructSAI.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStructSAI.Alternate = GPIO_AF6_SAI1;
        HAL_GPIO_Init(GPIOE, &GPIO_InitStructSAI);

        ret_val = HAL_OK;
    }
    else
    {
        ret_val = HAL_ERROR;
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
    return ret_val;
}

void codec_start(void)
{
    /* --- COMMON: enable SAI1 peripheral clock --- */
    RCC->APB2ENR |= RCC_APB2ENR_SAI1EN;

    /* ============================================================
     * SAI1 BLOCK A
     * ------------------------------------------------------------
     * Always configure Block A as a slave receiver so that:
     *  - FS_A / SCK_A (PE4 / PE5) are used as the frame/bit clock,
     *  - Block B can be synchronous to Block A and share these clocks.
     * In AUDIO_INPUT_I2S mode we simply DON'T use DMA on Block A.
     * ============================================================ */
    SAI1_Block_A->CR1 &= ~SAI_xCR1_SAIEN; // Disable before config

    /* --- CR1 (Block A: slave receiver) --- */
    SAI1_Block_A->CR1 =
        (3U << SAI_xCR1_MODE_Pos)                                                                                                // 3: Slave receiver
        | (7U << SAI_xCR1_DS_Pos)                                                                                                // 32-bit data
        | (0U << SAI_xCR1_PRTCFG_Pos) | (0U << SAI_xCR1_LSBFIRST_Pos) | (1U << SAI_xCR1_CKSTR_Pos) | (0U << SAI_xCR1_SYNCEN_Pos) // Asynchronous, but uses FS_A/SCK_A pins
        | (0U << SAI_xCR1_OUTDRIV_Pos) | (0U << SAI_xCR1_MONO_Pos) | (0U << SAI_xCR1_DMAEN_Pos)                                  // DMA enabled later only in CODEC mode
        | (0U << SAI_xCR1_NODIV_Pos) | (0U << SAI_xCR1_MCKDIV_Pos);

    /* --- CR2 --- */
    SAI1_Block_A->CR2 =
        (1U << SAI_xCR2_FTH_Pos) // FIFO threshold = 1/4
        | (0U << SAI_xCR2_TRIS_Pos) | (0U << SAI_xCR2_COMP_Pos);

    /* --- FRCR --- */
    SAI1_Block_A->FRCR =
        (63U << SAI_xFRCR_FRL_Pos)     // Frame length = 64 bits
        | (31U << SAI_xFRCR_FSALL_Pos) // Active frame = 32 bits
        | (0U << SAI_xFRCR_FSDEF_Pos) | (0U << SAI_xFRCR_FSPOL_Pos) | (1U << SAI_xFRCR_FSOFF_Pos);

    /* --- SLOTR --- */
    SAI1_Block_A->SLOTR =
        (0x3 << SAI_xSLOTR_SLOTEN_Pos)  // 2 slots (stereo)
        | (1U << SAI_xSLOTR_NBSLOT_Pos) // NBSLOT = 1 → 2 slots
        | (2U << SAI_xSLOTR_SLOTSZ_Pos) // 32-bit slots
        | (0U << SAI_xSLOTR_FBOFF_Pos);

    /* ============================================================
     * SAI1 BLOCK B
     * ------------------------------------------------------------
     * Slave transmitter, **synchronous to Block A** so it uses
     * the same FS_A/SCK_A (PE4/PE5) clocks from the CS4271 (master).
     * ============================================================ */
    SAI1_Block_B->CR1 &= ~SAI_xCR1_SAIEN; // Disable before config

    /* --- CR1 (Block B: slave transmitter, synchronous to A) --- */
    SAI1_Block_B->CR1 =
        (2U << SAI_xCR1_MODE_Pos)                                                                                                // 2: Slave transmitter
        | (7U << SAI_xCR1_DS_Pos)                                                                                                // 32-bit data
        | (0U << SAI_xCR1_PRTCFG_Pos) | (0U << SAI_xCR1_LSBFIRST_Pos) | (1U << SAI_xCR1_CKSTR_Pos) | (1U << SAI_xCR1_SYNCEN_Pos) // <-- IMPORTANT: synchronous with Block A
        | (1U << SAI_xCR1_OUTDRIV_Pos) | (0U << SAI_xCR1_MONO_Pos) | (0U << SAI_xCR1_DMAEN_Pos)                                  // DMA enabled later
        | (0U << SAI_xCR1_NODIV_Pos) | (0U << SAI_xCR1_MCKDIV_Pos);

    /* --- CR2 --- */
    SAI1_Block_B->CR2 =
        (1U << SAI_xCR2_FTH_Pos) | (0U << SAI_xCR2_TRIS_Pos) | (0U << SAI_xCR2_COMP_Pos);

    /* --- FRCR --- */
    SAI1_Block_B->FRCR =
        (63U << SAI_xFRCR_FRL_Pos) | (31U << SAI_xFRCR_FSALL_Pos) | (0U << SAI_xFRCR_FSDEF_Pos) | (0U << SAI_xFRCR_FSPOL_Pos) | (1U << SAI_xFRCR_FSOFF_Pos);

    /* --- SLOTR --- */
    SAI1_Block_B->SLOTR =
        (0x3 << SAI_xSLOTR_SLOTEN_Pos) | (1U << SAI_xSLOTR_NBSLOT_Pos) | (2U << SAI_xSLOTR_SLOTSZ_Pos) | (0U << SAI_xSLOTR_FBOFF_Pos);

    /* ============================================================
     * I2S2 INPUT (ESP32 → STM32) + DMA + SAI DMA
     * ============================================================ */

#ifdef AUDIO_INPUT_I2S
    // Configure I2S2 as slave RX (already talking to ESP32 master)
    i2s_input_init();
#endif

    // Configure DMA for RX (SAI/I2S2) and TX (SAI1_B)
    sai_dma_init();

#ifndef AUDIO_INPUT_I2S
    // OLD CODEC MODE: Use SAI1 Block A RX DMA
    SAI1_Block_A->CR1 |= SAI_xCR1_DMAEN;
#else
    // I2S INPUT MODE: SAI1 Block A has NO DMA; input comes from I2S2
    // RX DMA for I2S2 is enabled in codec_start() after sai_dma_init():
    SPI2->CR2 |= SPI_CR2_RXDMAEN;
#endif

    // Enable SAI1 Block B TX DMA (common to both modes)
    SAI1_Block_B->CR1 |= SAI_xCR1_DMAEN;

    /* ============================================================
     * Finally enable both SAI blocks
     *  - Block A: to lock to CS4271 clocks on FS_A/SCK_A
     *  - Block B: to stream audio to CS4271 DAC
     * ============================================================ */
    SAI1_Block_A->CR1 |= SAI_xCR1_SAIEN;
    SAI1_Block_B->CR1 |= SAI_xCR1_SAIEN;
}

uint8_t codec_data_ready(void)
{
    return audio_codec_data_ready;
}

void codec_clear_data_ready(void)
{
    audio_codec_data_ready = 0;
}

void codec_mirror_left_channel(void)
{
    int32_t *current_out_buffer = next_audio_out_buffer_pointer;

    for (uint32_t i = 0; i < AUDIO_FRAME_SIZE; i += 2)
    {
        current_out_buffer[i + 1] = current_out_buffer[i];
    }
}

/**
 * @brief Read ADC value from PF8 (ADC3_IN6) to detect right channel presence.
 */
static uint16_t read_right_channel_adc(void)
{
    static uint8_t adc_initialized = 0;

    if (!adc_initialized)
    {
        __HAL_RCC_ADC3_CLK_ENABLE();

        ADC3->CR2 &= ~ADC_CR2_ADON;
        ADC3->CR1 &= ~ADC_CR1_RES;
        ADC3->CR2 &= ~ADC_CR2_CONT;

        ADC3->SQR3 = 6;
        ADC3->SQR1 = 0;

        ADC3->SMPR2 &= ~ADC_SMPR2_SMP6_Msk;
        ADC3->SMPR2 |= (4UL << ADC_SMPR2_SMP6_Pos);

        ADC3->CR2 |= ADC_CR2_ADON;
        adc_initialized = 1;

        for (volatile int i = 0; i < 100; i++)
            ;
    }

    ADC3->CR2 |= ADC_CR2_SWSTART;

    while (!(ADC3->SR & ADC_SR_EOC))
        ;

    return (uint16_t)ADC3->DR;
}

uint8_t codec_is_right_channel_present(void)
{
#ifdef AUDIO_INPUT_I2S
    // I2S input mode: Always assume stereo (no jack detection)
    return 1;
#else
#define ADC_THRESHOLD_LOW 100
#define ADC_THRESHOLD_HIGH 150
#define DETECTION_COUNT 5

    static uint8_t right_channel_active = 1;
    static uint8_t detection_counter = 0;

    uint16_t adc_value = read_right_channel_adc();
    uint16_t adc_center = 2048;

    int16_t deviation = (int16_t)adc_value - (int16_t)adc_center;
    if (deviation < 0)
        deviation = -deviation;

    uint8_t condition_met;

    if (right_channel_active)
    {
        condition_met = (deviation < ADC_THRESHOLD_LOW);
    }
    else
    {
        condition_met = (deviation > ADC_THRESHOLD_HIGH);
    }

    if (condition_met)
    {
        detection_counter++;
        if (detection_counter >= DETECTION_COUNT)
        {
            right_channel_active = !right_channel_active;
            detection_counter = 0;
        }
    }
    else
    {
        detection_counter = 0;
    }

    return right_channel_active;
#endif
}

#ifdef AUDIO_INPUT_I2S
/**
 * @brief Initializes the I2S2 peripheral for external I2S audio input.
 */
static void i2s_input_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Enable clocks
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    // Configure GPIO pins for I2S2 alternate function (AF5)
    GPIO_InitStruct.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2; // I2S2 uses AF5
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // Disable I2S2 before configuration
    SPI2->I2SCFGR &= ~SPI_I2SCFGR_I2SE;

    // Reset I2SCFGR
    SPI2->I2SCFGR = 0;

    // I2S mode (not SPI)
    SPI2->I2SCFGR |= SPI_I2SCFGR_I2SMOD;

    /*
     * I2SCFG bits:
     * 00: Slave transmit
     * 01: Slave receive
     * 10: Master transmit
     * 11: Master receive
     *
     * We want Slave receive (01)
     */
    SPI2->I2SCFGR |= (1U << SPI_I2SCFGR_I2SCFG_Pos);

    // Philips I2S standard
    SPI2->I2SCFGR |= (0U << SPI_I2SCFGR_I2SSTD_Pos);

    /*
     * Data & channel length:
     * DATLEN:
     *   00: 16-bit
     *   01: 24-bit
     *   10: 32-bit
     * CHLEN:
     *   0: 16-bit channel length
     *   1: 32-bit channel length
     *
     * Use 32-bit data + 32-bit channel → 32-bit words for int32_t DMA.
     */
    SPI2->I2SCFGR |= (2U << SPI_I2SCFGR_DATLEN_Pos); // 32-bit data
    SPI2->I2SCFGR |= SPI_I2SCFGR_CHLEN;              // 32-bit channel length

    // Clock polarity low
    SPI2->I2SCFGR |= (0U << SPI_I2SCFGR_CKPOL_Pos);

    // Prescaler: ignored in slave mode but must be valid
    SPI2->I2SPR = 2;

    // Enable I2S2
    SPI2->I2SCFGR |= SPI_I2SCFGR_I2SE;
}
#endif

static void sai_dma_init(void)
{

    __HAL_RCC_DMA2_CLK_ENABLE();

#ifndef AUDIO_INPUT_I2S
    // === CODEC MODE: Configure DMA2 Stream1 for SAI1 Block A RX ===
    DMA2_Stream1->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream1->CR & DMA_SxCR_EN)
    {
    }

    // Clear transfer complete interrupt flag for Stream1
    DMA2->LIFCR |= DMA_LIFCR_CTCIF1;
#else
    // === I2S INPUT MODE: Configure DMA1 Stream3 for I2S2 RX ===
    __HAL_RCC_DMA1_CLK_ENABLE();

    DMA1_Stream3->CR &= ~DMA_SxCR_EN;
    while (DMA1_Stream3->CR & DMA_SxCR_EN)
    {
    }

    // Clear transfer complete interrupt flag for Stream3
    DMA1->LIFCR |= DMA_LIFCR_CTCIF3;
#endif

    // === Configure DMA2 Stream5 for SAI1 Block B TX (common) ===
    DMA2_Stream5->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream5->CR & DMA_SxCR_EN)
    {
    }

    // Clear transfer complete interrupt flag for Stream5
    DMA2->HIFCR |= DMA_HIFCR_CTCIF5;

#ifndef AUDIO_INPUT_I2S
    // --- SAI1_Block_A RX (DMA2 Stream1, Channel0) ---
    DMA2_Stream1->CR = 0;

    DMA2_Stream1->CR |= (0UL << DMA_SxCR_CHSEL_Pos); // Channel 0
    DMA2_Stream1->CR |= DMA_SxCR_PL_1;               // High priority
    DMA2_Stream1->CR |= DMA_SxCR_MSIZE_1;            // Mem 32-bit
    DMA2_Stream1->CR |= DMA_SxCR_PSIZE_1;            // Periph 32-bit
    DMA2_Stream1->CR |= DMA_SxCR_MINC;               // Mem inc
    DMA2_Stream1->CR |= DMA_SxCR_CIRC;               // Circular
    DMA2_Stream1->CR |= DMA_SxCR_DBM;                // Double-buffer
    DMA2_Stream1->CR |= DMA_SxCR_TCIE;               // TC interrupt

    DMA2_Stream1->NDTR = AUDIO_FRAME_SIZE;
    DMA2_Stream1->PAR = (uint32_t)&(SAI1_Block_A->DR);
    DMA2_Stream1->M0AR = (uint32_t)audio_in_buffer_ping;
    DMA2_Stream1->M1AR = (uint32_t)audio_in_buffer_pong;

    DMA2_Stream1->CR |= DMA_SxCR_EN;

    NVIC_SetPriority(DMA2_Stream1_IRQn, 1);
    NVIC_ClearPendingIRQ(DMA2_Stream1_IRQn);
    NVIC_EnableIRQ(DMA2_Stream1_IRQn);
#else
    // --- I2S2 RX (DMA1 Stream3, Channel0) ---
    DMA1_Stream3->CR = 0;

    DMA1_Stream3->CR |= (0UL << DMA_SxCR_CHSEL_Pos); // Channel 0
    DMA1_Stream3->CR |= DMA_SxCR_PL_1;               // High priority
    DMA1_Stream3->CR |= DMA_SxCR_MSIZE_1;            // Mem 32-bit
    DMA1_Stream3->CR |= DMA_SxCR_PSIZE_1;            // Periph 32-bit
    DMA1_Stream3->CR |= DMA_SxCR_MINC;               // Mem inc
    DMA1_Stream3->CR |= DMA_SxCR_CIRC;               // Circular
    DMA1_Stream3->CR |= DMA_SxCR_DBM;                // Double-buffer
    DMA1_Stream3->CR |= DMA_SxCR_TCIE;               // TC interrupt

    DMA1_Stream3->NDTR = AUDIO_FRAME_SIZE;
    DMA1_Stream3->PAR = (uint32_t)&(SPI2->DR);
    DMA1_Stream3->M0AR = (uint32_t)audio_in_buffer_ping;
    DMA1_Stream3->M1AR = (uint32_t)audio_in_buffer_pong;

    DMA1_Stream3->CR |= DMA_SxCR_EN;

    NVIC_SetPriority(DMA1_Stream3_IRQn, 1);
    NVIC_ClearPendingIRQ(DMA1_Stream3_IRQn);
    NVIC_EnableIRQ(DMA1_Stream3_IRQn);
#endif

    // --- SAI1_Block_B TX (DMA2 Stream5, Channel0) ---
    DMA2_Stream5->CR = 0;

    DMA2_Stream5->CR |= (0UL << DMA_SxCR_CHSEL_Pos); // Channel 0
    DMA2_Stream5->CR |= DMA_SxCR_PL_1;               // High priority
    DMA2_Stream5->CR |= DMA_SxCR_MSIZE_1;            // Mem 32-bit
    DMA2_Stream5->CR |= DMA_SxCR_PSIZE_1;            // Periph 32-bit
    DMA2_Stream5->CR |= DMA_SxCR_MINC;               // Mem inc
    DMA2_Stream5->CR |= DMA_SxCR_CIRC;               // Circular
    DMA2_Stream5->CR |= DMA_SxCR_DBM;                // Double-buffer
    DMA2_Stream5->CR |= DMA_SxCR_TCIE;               // TC interrupt
    DMA2_Stream5->CR |= DMA_SxCR_DIR_0;              // Mem-to-periph

    DMA2_Stream5->NDTR = AUDIO_FRAME_SIZE;
    DMA2_Stream5->PAR = (uint32_t)&(SAI1_Block_B->DR);
    DMA2_Stream5->M0AR = (uint32_t)audio_out_buffer_ping;
    DMA2_Stream5->M1AR = (uint32_t)audio_out_buffer_pong;

    DMA2_Stream5->CR |= DMA_SxCR_EN;

    NVIC_SetPriority(DMA2_Stream5_IRQn, 2);
    NVIC_ClearPendingIRQ(DMA2_Stream5_IRQn);
    NVIC_EnableIRQ(DMA2_Stream5_IRQn); // Enable TX IRQ for debug
}

static void split_and_cast_i2s_buffer(int32_t *i2s_buffer, float32_t *left_out,
                                      float32_t *right_out, uint32_t out_buffer_size)
{
    for (uint32_t i = 0; i < out_buffer_size; i++)
    {
        left_out[i] = (float32_t)(i2s_buffer[i * 2] >> 8);
        right_out[i] = (float32_t)(i2s_buffer[i * 2 + 1] >> 8);
    }
}

void codec_update_output_buffer(uint8_t channel, float32_t *data, uint32_t size)
{
    if (size > AUDIO_CHANNEL_SIZE)
    {
        size = AUDIO_CHANNEL_SIZE;
    }

    int32_t *current_out_buffer = next_audio_out_buffer_pointer;

    for (uint32_t i = 0; i < size; i++)
    {
        int32_t sample = ((int32_t)data[i]) << 8;

        if (channel == 0)
        {
            current_out_buffer[i * 2] = sample;
        }
        else
        {
            current_out_buffer[i * 2 + 1] = sample;
        }
    }
}

void DMA2_Stream1_IRQHandler(void)
{
    if (DMA2->LISR & DMA_LISR_TCIF1)
    {
        DMA2->LIFCR |= DMA_LIFCR_CTCIF1;

        int32_t *completed;

        /*
         * Double-buffer logic:
         *  - CT = 0 → current target is M0 → M1 (pong) just finished
         *  - CT = 1 → current target is M1 → M0 (ping) just finished
         * According to ST’s DBM behavior, CT has already toggled at TC.
         */
        if ((DMA2_Stream1->CR & DMA_SxCR_CT) == 0)
        {
            completed = audio_in_buffer_pong; // M1 just finished
        }
        else
        {
            completed = audio_in_buffer_ping; // M0 just finished
        }

        /* Remember which RX buffer is complete */
        last_completed_rx_buffer = completed;

        /* Update float channels for your calculations / display */
        if (left_channel_buffer_pointer && right_channel_buffer_pointer)
        {
            split_and_cast_i2s_buffer(completed,
                                      left_channel_buffer_pointer,
                                      right_channel_buffer_pointer,
                                      AUDIO_CHANNEL_SIZE);
        }

        /* Tell main loop that new data is available */
        audio_codec_data_ready = 1;
    }
}

/* SAI1 Block B TX */
void DMA2_Stream5_IRQHandler(void)
{
    if (DMA2->HISR & DMA_HISR_TCIF5)
    {
        DMA2->HIFCR |= DMA_HIFCR_CTCIF5;

        /*
         * Double-buffer logic for TX (mem-to-periph):
         *  - CT = 0 → DMA is now reading from M0 (ping)
         *             → M1 (pong) has just finished and is safe to overwrite.
         *  - CT = 1 → DMA is now reading from M1
         *             → M0 has just finished.
         */
        uint32_t ct = (DMA2_Stream5->CR & DMA_SxCR_CT) ? 1U : 0U;
        int32_t *buf_to_fill;

        if (ct == 0U)
        {
            // Now reading M0 → M1 just finished
            buf_to_fill = audio_out_buffer_pong;
        }
        else
        {
            // Now reading M1 → M0 just finished
            buf_to_fill = audio_out_buffer_ping;
        }

        /* Optional: keep this for effects / mirror function */
        next_audio_out_buffer_pointer = buf_to_fill;

        /* Source for new TX frame: last completed RX buffer */
        int32_t *src = last_completed_rx_buffer;

        if (src != NULL)
        {
            for (uint32_t i = 0; i < AUDIO_FRAME_SIZE; i++)
            {
                buf_to_fill[i] = src[i];
            }
        }
        else
        {
            // No RX data yet → output silence
            for (uint32_t i = 0; i < AUDIO_FRAME_SIZE; i++)
            {
                buf_to_fill[i] = 0;
            }
        }

        BSP_LED_Toggle(LED4); // TX running debug
    }
}

#ifdef AUDIO_INPUT_I2S
/* I2S2 RX (I2S input mode only) */
void DMA1_Stream3_IRQHandler(void)
{
    if (DMA1->LISR & DMA_LISR_TCIF3)
    {
        DMA1->LIFCR |= DMA_LIFCR_CTCIF3;

        BSP_LED_Toggle(LED3); // RX debug

        int32_t *completed;

        /*
         * Double-buffer logic:
         *  - CT = 0 → current target is M0 → M1 (pong) just finished
         *  - CT = 1 → current target is M1 → M0 (ping) just finished
         */
        if ((DMA1_Stream3->CR & DMA_SxCR_CT) == 0)
        {
            completed = audio_in_buffer_pong; // M1 just finished
        }
        else
        {
            completed = audio_in_buffer_ping; // M0 just finished
        }

        /* Remember which RX buffer is complete */
        last_completed_rx_buffer = completed;

        /* Update float channels for calculations / display */
        if (left_channel_buffer_pointer && right_channel_buffer_pointer)
        {
            split_and_cast_i2s_buffer(completed,
                                      left_channel_buffer_pointer,
                                      right_channel_buffer_pointer,
                                      AUDIO_CHANNEL_SIZE);
        }

        /* Inform main loop */
        audio_codec_data_ready = 1;
    }
}
#endif
