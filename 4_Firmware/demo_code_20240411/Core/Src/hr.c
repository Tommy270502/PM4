/** ***************************************************************************
 * @file
 * @brief Heart-rate extraction from radar I/Q data
 *
 * Converts raw radar I/Q samples to float, removes DC offset, runs FFT,
 * and finds the spectral peak in the heart-rate band (0.8–3.0 Hz).
 *
 ****************************************************************************/


/******************************************************************************
 * Includes
 *****************************************************************************/
#include "stm32f4xx_hal.h"
#include "arm_math.h"

#include "hr.h"
#include "fft.h"
#include "measuring.h"


/******************************************************************************
 * Defines
 *****************************************************************************/
#define HR_BAND_LOW_HZ   0.8f	///< Lower bound of HR search band (48 bpm)
#define HR_BAND_HIGH_HZ  3.0f	///< Upper bound of HR search band (180 bpm)


/******************************************************************************
 * Variables
 *****************************************************************************/
/** Complex FFT input/output buffer: 2*FFT_SIZE floats (interleaved Re, Im).
 *  MUST be 2*FFT_SIZE — using only FFT_SIZE causes a hard fault. */
static float32_t cfft_buf[2 * FFT_SIZE];

/** Magnitude buffer for spectral bins */
static float32_t mag_buf[FFT_SIZE];


/******************************************************************************
 * Functions
 *****************************************************************************/


/** ***************************************************************************
 * @brief Process one radar frame and return a heart-rate result.
 *
 * Steps:
 * 1. Convert raw uint16_t I/Q samples to float
 * 2. Subtract the DC mean from each channel
 * 3. Build interleaved complex FFT input [I0, Q0, I1, Q1, ...]
 * 4. Compute FFT
 * 5. Compute magnitude spectrum
 * 6. Find peak in the HR band
 * 7. Convert peak bin to BPM
 *
 * @param frame  Pointer to the acquired radar frame
 * @return HR_Result_t with BPM, validity, and metadata
 ****************************************************************************/
HR_Result_t HR_process_radar_frame(const MEAS_RadarFrame_t *frame)
{
	HR_Result_t result;
	result.bpm = 0.0f;
	result.peak_hz = 0.0f;
	result.frame_id = frame->frame_id;
	result.valid = false;
	result.clipped = (frame->clip_i || frame->clip_q);

	uint16_t n_samples = frame->sample_count;
	if (n_samples == 0 || n_samples > FFT_SIZE) {
		return result;
	}

	/* --- Step 1 & 2: Convert to float and compute means --- */
	float32_t mean_i = 0.0f;
	float32_t mean_q = 0.0f;

	for (uint16_t n = 0; n < n_samples; n++) {
		mean_i += (float32_t)frame->raw_i[n];
		mean_q += (float32_t)frame->raw_q[n];
	}
	mean_i /= (float32_t)n_samples;
	mean_q /= (float32_t)n_samples;

	/* --- Step 3: Build complex FFT input with DC removed --- */
	for (uint16_t n = 0; n < n_samples; n++) {
		cfft_buf[2 * n]     = (float32_t)frame->raw_i[n] - mean_i;  /* Real = I */
		cfft_buf[2 * n + 1] = (float32_t)frame->raw_q[n] - mean_q;  /* Imag = Q */
	}

	/* Zero-pad remaining entries if n_samples < FFT_SIZE */
	for (uint16_t n = n_samples; n < FFT_SIZE; n++) {
		cfft_buf[2 * n]     = 0.0f;
		cfft_buf[2 * n + 1] = 0.0f;
	}

	/* --- Step 4: Compute FFT --- */
	FFT_compute(cfft_buf);

	/* --- Step 5: Compute magnitude spectrum --- */
	FFT_compute_magnitude(cfft_buf, mag_buf, FFT_SIZE);

	/* --- Step 6: Find peak in HR band --- */
	float32_t bin_resolution = (float32_t)frame->sample_rate_hz / (float32_t)FFT_SIZE;

	uint32_t bin_start = (uint32_t)(HR_BAND_LOW_HZ / bin_resolution);
	uint32_t bin_end   = (uint32_t)(HR_BAND_HIGH_HZ / bin_resolution);

	/* Clamp to valid range */
	if (bin_start < 1) bin_start = 1;
	if (bin_end >= FFT_SIZE / 2) bin_end = FFT_SIZE / 2 - 1;

	uint32_t peak_bin = FFT_find_peak(mag_buf, bin_start, bin_end);

	/* --- Step 7: Convert to Hz and BPM --- */
	float32_t peak_hz = (float32_t)peak_bin * bin_resolution;
	float32_t bpm = peak_hz * 60.0f;

	/* Basic validity: peak magnitude should be significantly above zero */
	if (mag_buf[peak_bin] > 0.0f) {
		result.valid = true;
	}

	result.peak_hz = peak_hz;
	result.bpm = bpm;

	return result;
}
