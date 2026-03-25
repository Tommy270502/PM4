/** ***************************************************************************
 * @file
 * @brief CMSIS DSP FFT wrapper implementation
 *
 * Uses arm_cfft_f32 from the CMSIS DSP library for a 1024-point complex FFT.
 *
 ****************************************************************************/


/******************************************************************************
 * Includes
 *****************************************************************************/
#include "stm32f4xx_hal.h"
#include "arm_math.h"
#include "arm_const_structs.h"
#include "fft.h"


/******************************************************************************
 * Defines
 *****************************************************************************/
#define IFFT_FLAG        0		///< Forward FFT (not inverse)
#define BIT_REVERSE_FLAG 1		///< Output in normal order


/******************************************************************************
 * Variables
 *****************************************************************************/


/******************************************************************************
 * Functions
 *****************************************************************************/


/** ***************************************************************************
 * @brief Initialize the FFT instance.
 *
 * For a 1024-point complex FFT the pre-defined constant structure
 * arm_cfft_sR_f32_len1024 from arm_const_structs.h is used.
 * No dynamic initialization is needed.
 ****************************************************************************/
void FFT_init(void)
{
	/* The constant struct arm_cfft_sR_f32_len1024 is ready to use.
	 * Nothing to allocate or configure at runtime. */
}


/** ***************************************************************************
 * @brief Compute in-place complex FFT.
 *
 * @param cfft_inout  Buffer of 2*FFT_SIZE float32_t values.
 *                    Format: {Re[0], Im[0], Re[1], Im[1], ...}
 * @note The buffer MUST be 2*FFT_SIZE floats. Using only FFT_SIZE
 *       floats causes a hard fault!
 ****************************************************************************/
void FFT_compute(float32_t *cfft_inout)
{
	arm_cfft_f32(&arm_cfft_sR_f32_len1024,
	             cfft_inout, IFFT_FLAG, BIT_REVERSE_FLAG);
}


/** ***************************************************************************
 * @brief Compute magnitudes of complex spectral bins.
 *
 * @param cfft_out  Complex spectral data (2*len floats, interleaved Re/Im)
 * @param mag_out   Output magnitude array (len floats)
 * @param len       Number of complex bins to process
 ****************************************************************************/
void FFT_compute_magnitude(const float32_t *cfft_out,
                           float32_t *mag_out, uint32_t len)
{
	/* arm_cmplx_mag_f32 expects a non-const pointer in older CMSIS versions */
	arm_cmplx_mag_f32((float32_t *)cfft_out, mag_out, len);
}


/** ***************************************************************************
 * @brief Find peak bin within a sub-range of the magnitude array.
 *
 * @param mag        Magnitude array
 * @param start_bin  First bin to search (inclusive)
 * @param end_bin    Last bin to search (inclusive)
 * @return Index of the bin with the highest magnitude
 ****************************************************************************/
uint32_t FFT_find_peak(const float32_t *mag,
                       uint32_t start_bin, uint32_t end_bin)
{
	float32_t max_val = 0.0f;
	uint32_t  max_idx = start_bin;
	for (uint32_t i = start_bin; i <= end_bin; i++) {
		if (mag[i] > max_val) {
			max_val = mag[i];
			max_idx = i;
		}
	}
	return max_idx;
}
