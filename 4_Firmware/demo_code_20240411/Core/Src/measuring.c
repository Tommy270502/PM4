/** ***************************************************************************
 * @file
 * @brief Radar I/Q signal acquisition using dual-ADC mode
 *
 * Configures ADC1 (PC1 = I) and ADC2 (PC3 = Q) in dual simultaneous mode,
 * triggered by TIM2 at 100 Hz, with DMA transfer of packed 32-bit words.
 * After DMA completion, unpacks into MEAS_RadarFrame_t with clip/overrun
 * quality flags.
 *
 * Based on the ADC dual-mode demo from the original project.
 *
 * @author  Original demo: Hanspeter Hochreutener, hhrt@zhaw.ch
 * @date    Reworked for radar HR prototype
 ****************************************************************************/


/******************************************************************************
 * Includes
 *****************************************************************************/
#include <stdio.h>
#include "stm32f4xx.h"
#include "stm32f429i_discovery.h"

#include "measuring.h"


/******************************************************************************
 * Defines
 *****************************************************************************/
#define ADC_DAC_RES     12          ///< ADC resolution in bits
#define ADC_CLOCK       84000000    ///< APB2 peripheral clock frequency
#define TIM_CLOCK       84000000    ///< APB1 timer clock frequency
#define TIM_TOP         9           ///< Timer top value (auto-reload)

/** Clock prescaler for 100 Hz sample rate:
 *  84000000 / 100 / (9+1) - 1 = 83999 */
#define TIM_PRESCALE    (TIM_CLOCK / MEAS_SAMPLE_RATE / (TIM_TOP + 1) - 1)

/** Clip detection thresholds (near 12-bit rails) */
#define CLIP_LOW        8
#define CLIP_HIGH       4087


/******************************************************************************
 * Variables
 *****************************************************************************/
bool MEAS_data_ready = false;               ///< New frame is ready

static MEAS_RadarFrame_t radar_frame;       ///< Current radar frame
static uint32_t dma_buf[MEAS_FRAME_LEN];    ///< Packed DMA buffer
static uint32_t frame_counter = 0;          ///< Incrementing frame ID
static bool frame_pending = false;          ///< Frame not yet consumed


/******************************************************************************
 * Functions
 *****************************************************************************/


/** ***************************************************************************
 * @brief Configure GPIOs in analog mode for the radar inputs.
 *
 * @note Pin mapping:
 * - PC1 = ADC123_IN11 = I channel (already set to analog by gyro_disable)
 * - PC3 = ADC123_IN13 = Q channel
 ****************************************************************************/
void MEAS_GPIO_analog_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();           // Enable Clock for GPIO port C
    /* PC1 is already configured as analog by gyro_disable() in main.c */
    GPIOC->MODER |= (3UL << GPIO_MODER_MODER3_Pos);   // Analog PC3 = Q
}


/** ***************************************************************************
 * @brief Resets the ADCs and the timer.
 ****************************************************************************/
void ADC_reset(void)
{
    RCC->APB2RSTR |= RCC_APB2RSTR_ADCRST;      // Reset ADCs
    RCC->APB2RSTR &= ~RCC_APB2RSTR_ADCRST;     // Release reset of ADCs
    TIM2->CR1 &= ~TIM_CR1_CEN;                 // Disable timer
}


/** ***************************************************************************
 * @brief Configure TIM2 to trigger the ADCs at MEAS_SAMPLE_RATE.
 *
 * @note TIM2 generates TRGO on update events. The timer interrupt is
 *       not strictly needed for production but is kept for debug.
 ****************************************************************************/
void MEAS_timer_init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();            // Enable Clock for TIM2
    TIM2->PSC = TIM_PRESCALE;              // Prescaler
    TIM2->ARR = TIM_TOP;                   // Auto reload = counter top value
    TIM2->CR2 |= TIM_CR2_MMS_1;           // TRGO on update
}


/** ***************************************************************************
 * @brief Initialize ADC1+ADC2 dual simultaneous mode with DMA.
 *
 * ADC1 → IN11 (PC1 = I channel, master)
 * ADC2 → IN13 (PC3 = Q channel, slave)
 *
 * DMA2_Stream4 Channel0 transfers packed 32-bit words:
 *   ADC_CDR[31:0] = ADC2_DR[15:0] | ADC1_DR[15:0]
 *
 * @note This function only configures peripherals. Call
 *       MEAS_start_radar_single() to begin acquisition.
 ****************************************************************************/
void MEAS_radar_dual_init(void)
{
    /* --- ADC clock & multi-mode setup --- */
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC2_CLK_ENABLE();
    ADC->CCR |= ADC_CCR_DMA_1;                 // DMA mode 2 = dual DMA
    ADC->CCR |= (ADC_CCR_MULTI_1 | ADC_CCR_MULTI_2);  // Simultaneous mode

    /* --- ADC1 (master): IN11 = PC1 = I --- */
    ADC1->CR2 |= (1UL << ADC_CR2_EXTEN_Pos);   // Ext trigger on rising edge
    ADC1->CR2 |= (6UL << ADC_CR2_EXTSEL_Pos);  // Timer 2 TRGO event
    ADC1->SQR3 |= (11UL << ADC_SQR3_SQ1_Pos);  // Input 11 = first conversion

    /* --- ADC2 (slave): IN13 = PC3 = Q --- */
    ADC2->SQR3 |= (13UL << ADC_SQR3_SQ1_Pos);  // Input 13 = first conversion

    /* --- DMA2 Stream4 Channel0 setup --- */
    __HAL_RCC_DMA2_CLK_ENABLE();
    DMA2_Stream4->CR &= ~DMA_SxCR_EN;          // Disable the DMA stream
    while (DMA2_Stream4->CR & DMA_SxCR_EN) { ; }
    DMA2->HIFCR |= DMA_HIFCR_CTCIF4;          // Clear transfer complete flag
    DMA2_Stream4->CR |= (0UL << DMA_SxCR_CHSEL_Pos);   // Channel 0
    DMA2_Stream4->CR |= DMA_SxCR_PL_1;         // Priority high
    DMA2_Stream4->CR |= DMA_SxCR_MSIZE_1;      // Memory data size = 32 bit
    DMA2_Stream4->CR |= DMA_SxCR_PSIZE_1;      // Peripheral data size = 32 bit
    DMA2_Stream4->CR |= DMA_SxCR_MINC;         // Increment memory address
    DMA2_Stream4->CR |= DMA_SxCR_TCIE;         // Transfer complete IRQ enable
    DMA2_Stream4->NDTR = MEAS_FRAME_LEN;       // Number of transfers
    DMA2_Stream4->PAR  = (uint32_t)&ADC->CDR;  // Peripheral address
    DMA2_Stream4->M0AR = (uint32_t)dma_buf;    // Memory address
}


/** ***************************************************************************
 * @brief Start a single-shot radar acquisition.
 *
 * Enables DMA, ADCs, and timer. The DMA transfer-complete interrupt
 * will fire after MEAS_FRAME_LEN samples have been collected.
 ****************************************************************************/
void MEAS_start_radar_single(void)
{
    /* Reset peripherals for a clean start */
    ADC_reset();

    /* Re-initialize for this acquisition */
    MEAS_radar_dual_init();

    MEAS_data_ready = false;

    /* Enable DMA */
    DMA2_Stream4->CR |= DMA_SxCR_EN;
    NVIC_ClearPendingIRQ(DMA2_Stream4_IRQn);
    NVIC_EnableIRQ(DMA2_Stream4_IRQn);

    /* Enable ADCs */
    ADC1->CR2 |= ADC_CR2_ADON;
    ADC2->CR2 |= ADC_CR2_ADON;

    /* Enable timer — starts acquisition */
    TIM2->CR1 |= TIM_CR1_CEN;
}


/** ***************************************************************************
 * @brief Get pointer to the most recent radar frame.
 *
 * Only valid after MEAS_data_ready becomes true.
 * @return Pointer to the internal radar frame structure.
 ****************************************************************************/
const MEAS_RadarFrame_t* MEAS_get_frame(void)
{
    return &radar_frame;
}


/** ***************************************************************************
 * @brief Mark the current frame as consumed.
 *
 * Must be called after processing to clear the pending flag,
 * so that the next DMA completion does not flag an overrun.
 ****************************************************************************/
void MEAS_consume_frame(void)
{
    frame_pending = false;
}


/** ***************************************************************************
 * @brief DMA2 Stream4 transfer-complete interrupt handler.
 *
 * Unpacks the packed dual-ADC DMA buffer into separate I and Q arrays
 * in the radar frame structure. Applies 12-bit mask and checks for
 * clipping. Sets quality flags and raises MEAS_data_ready.
 *
 * @note In dual ADC mode, packed format is:
 *       ADC_CDR[31:0] = ADC2_DR[15:0] | ADC1_DR[15:0]
 *       => lower 12 bits = ADC1 = I, upper 16 bits lower 12 = ADC2 = Q
 ****************************************************************************/
void DMA2_Stream4_IRQHandler(void)
{
    if (DMA2->HISR & DMA_HISR_TCIF4) {
        /* Disable and clean up DMA */
        NVIC_DisableIRQ(DMA2_Stream4_IRQn);
        NVIC_ClearPendingIRQ(DMA2_Stream4_IRQn);
        DMA2_Stream4->CR &= ~DMA_SxCR_EN;
        while (DMA2_Stream4->CR & DMA_SxCR_EN) { ; }
        DMA2->HIFCR |= DMA_HIFCR_CTCIF4;

        /* Stop timer and ADCs */
        TIM2->CR1 &= ~TIM_CR1_CEN;
        ADC1->CR2 &= ~ADC_CR2_ADON;
        ADC2->CR2 &= ~ADC_CR2_ADON;
        ADC->CCR &= ~ADC_CCR_DMA_1;

        /* Check for overrun: if previous frame wasn't consumed */
        if (frame_pending) {
            radar_frame.dma_overrun = true;
        } else {
            radar_frame.dma_overrun = false;
        }

        /* Unpack and check clipping */
        bool clip_i = false;
        bool clip_q = false;

        for (uint32_t n = 0; n < MEAS_FRAME_LEN; n++) {
            uint16_t raw_i = (uint16_t)(dma_buf[n] & 0x0FFF);          // ADC1 = I
            uint16_t raw_q = (uint16_t)((dma_buf[n] >> 16) & 0x0FFF);  // ADC2 = Q

            radar_frame.raw_i[n] = raw_i;
            radar_frame.raw_q[n] = raw_q;

            /* Clip detection */
            if (raw_i <= CLIP_LOW || raw_i >= CLIP_HIGH) { clip_i = true; }
            if (raw_q <= CLIP_LOW || raw_q >= CLIP_HIGH) { clip_q = true; }
        }

        /* Fill frame metadata */
        radar_frame.sample_count = MEAS_FRAME_LEN;
        radar_frame.sample_rate_hz = MEAS_SAMPLE_RATE;
        radar_frame.frame_id = ++frame_counter;
        radar_frame.clip_i = clip_i;
        radar_frame.clip_q = clip_q;

        /* Signal readiness */
        frame_pending = true;
        ADC_reset();
        MEAS_data_ready = true;
    }
}
