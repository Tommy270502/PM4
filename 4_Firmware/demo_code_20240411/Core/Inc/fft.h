/** ***************************************************************************
 * @file
 * @brief CMSIS DSP FFT wrapper
 *
 * Wraps the CMSIS arm_cfft_f32 complex FFT for use by the HR module.
 *
 * Prefix: FFT
 *
 ****************************************************************************/

#ifndef FFT_H_
#define FFT_H_


/******************************************************************************
 * Includes
 *****************************************************************************/
#include "arm_math.h"


/******************************************************************************
 * Defines
 *****************************************************************************/
#define FFT_SIZE  1024


/******************************************************************************
 * Functions
 *****************************************************************************/

/** Initialize the FFT instance. Call once at startup. */
void FFT_init(void);

/** Compute in-place complex FFT.
 *  @param cfft_inout  array of 2*FFT_SIZE float32_t (interleaved Re,Im).
 *                     On entry: time-domain samples. On exit: spectral data.
 */
void FFT_compute(float32_t *cfft_inout);

/** Compute magnitude of complex spectral bins.
 *  @param cfft_out  complex spectral data (2*len floats, interleaved)
 *  @param mag_out   output magnitude array (len floats)
 *  @param len       number of complex bins
 */
void FFT_compute_magnitude(const float32_t *cfft_out,
                           float32_t *mag_out, uint32_t len);

/** Find the index of the maximum value in mag[start_bin..end_bin].
 *  @return index of peak bin
 */
uint32_t FFT_find_peak(const float32_t *mag,
                       uint32_t start_bin, uint32_t end_bin);


#endif
