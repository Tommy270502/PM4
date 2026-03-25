/** ***************************************************************************
 * @file
 * @brief Heart-rate extraction from radar I/Q data
 *
 * Converts a raw radar frame into a heart-rate estimate via FFT spectral
 * peak detection.
 *
 * Prefix: HR
 *
 ****************************************************************************/

#ifndef HR_H_
#define HR_H_


/******************************************************************************
 * Includes
 *****************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include "measuring.h"


/******************************************************************************
 * Types
 *****************************************************************************/

/** Heart-rate result structure */
typedef struct {
    float    bpm;                ///< Final heart-rate estimate in beats/min
    float    peak_hz;            ///< Dominant spectral peak frequency in Hz
    uint32_t frame_id;           ///< Source frame identifier
    bool     valid;              ///< true = valid result
    bool     clipped;            ///< true if source frame had clip_i or clip_q
} HR_Result_t;


/******************************************************************************
 * Functions
 *****************************************************************************/

/** Process one radar frame and return a heart-rate result.
 *  @param frame  Pointer to the acquired radar frame
 *  @return HR_Result_t with BPM, validity, and metadata
 */
HR_Result_t HR_process_radar_frame(const MEAS_RadarFrame_t *frame);


#endif
