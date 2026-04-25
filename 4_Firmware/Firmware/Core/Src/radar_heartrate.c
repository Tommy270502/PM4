/**
 * @file    radar_heartrate.c
 * @brief   Folded-spectrum radar heart-rate estimator implementation.
 *
 * Implements Steps 1-6 of the algorithm described in heart_rate_algorith.md.
 */

/******************************************************************************
 * Includes
 *****************************************************************************/
#include <math.h>
#include <string.h>

#include "radar_heartrate.h"
#include "math_constants.h"

/******************************************************************************
 * Private helpers – forward declarations
 *****************************************************************************/
static void build_folded_spectrum(radar_hr_state_t *st,
                                  const float32_t *spectrum_shifted);
static void extract_candidates(radar_hr_state_t *st);
static float32_t compute_noise_floor(const radar_hr_state_t *st);
static void confidence_check(radar_hr_state_t *st, float32_t noise_floor);
static void sm_reset_to_unlocked(radar_hr_state_t *st);
static float32_t median3(float32_t a, float32_t b, float32_t c);
static void median_buf_push(radar_hr_state_t *st, float32_t val);
static float32_t median_buf_get(const radar_hr_state_t *st);

/******************************************************************************
 * Default configuration (matches heart_rate_algorith.md table)
 *****************************************************************************/
static const radar_hr_config_t default_cfg = {
    .f_min           = 0.55f,
    .f_max           = 3.70f,
    .K               = 3U,
    .W_mask          = 2U,
    .PNR_min         = 7.0f,
    .prominence_min  = 1.8f,
    .jump_limit      = 12.0f,
    .N_lock          = 3U,
    .N_reset         = 8U,
    .alpha_high      = 0.35f,
    .alpha_mid       = 0.55f,
    .alpha_low       = 0.75f,
};

/******************************************************************************
 * Public API
 *****************************************************************************/

void radar_hr_init(radar_hr_state_t *st, const radar_hr_config_t *cfg)
{
    if (st == NULL) {
        return;
    }

    memset(st, 0, sizeof(*st));

    /* Apply configuration ------------------------------------------------- */
    if (cfg != NULL) {
        st->cfg = *cfg;
    } else {
        st->cfg = default_cfg;
    }

    /* Cache constants ----------------------------------------------------- */
    st->N       = RADAR_CHANNEL_SAMPLES;
    st->fs      = (float32_t)RADAR_SAMPLE_RATE_HZ;
    st->delta_f = st->fs / (float32_t)st->N;

    /* Precompute (i, j) index pairs for the HR band ----------------------- */
    /*
     * In the fft-shifted spectrum DC is at index N/2.
     * Positive signed frequency for bin k: f = (k - N/2) * delta_f
     * Mirror index for signed freq -f:     j = N/2 - (k - N/2) = N - k
     */
    uint32_t half = st->N / 2U;
    st->band_len = 0U;

    for (uint32_t k = half + 1U; k < st->N; k++) {
        float32_t f_k = (float32_t)((int32_t)k - (int32_t)half) * st->delta_f;
        if ((f_k >= st->cfg.f_min) && (f_k <= st->cfg.f_max)) {
            uint32_t j = st->N - k;  /* mirror index */
            if (st->band_len < RADAR_HR_MAX_BAND_BINS) {
                st->band_i[st->band_len] = k;
                st->band_j[st->band_len] = j;
                st->band_len++;
            }
        }
    }

    /* State machine starts UNLOCKED --------------------------------------- */
    sm_reset_to_unlocked(st);
}

/* ------------------------------------------------------------------------- */

void radar_hr_process_frame(radar_hr_state_t *st,
                            const float32_t *spectrum_shifted,
                            radar_hr_output_t *out)
{
    if ((st == NULL) || (spectrum_shifted == NULL) || (out == NULL)) {
        return;
    }

    /* Default output: invalid. */
    out->bpm   = 0.0f;
    out->valid = false;
    out->state = st->sm_state;

    if (st->band_len == 0U) {
        return;  /* No valid HR band — nothing to do. */
    }

    /* ---- Step 1: Build folded HR spectrum ---- */
    build_folded_spectrum(st, spectrum_shifted);

    /* ---- Step 2: Peak extraction & sub-bin interpolation ---- */
    extract_candidates(st);

    if (st->num_candidates == 0U) {
        /* No peaks at all. Treat as invalid frame. */
        goto invalid_frame;
    }

    /* ---- Step 3: Confidence check ---- */
    float32_t noise_floor = compute_noise_floor(st);
    confidence_check(st, noise_floor);

    /* Check if any candidate passes confidence. */
    bool any_valid = false;
    for (uint32_t c = 0U; c < st->num_candidates; c++) {
        if (st->candidates[c].valid_conf) {
            any_valid = true;
            break;
        }
    }

    if (!any_valid) {
        goto invalid_frame;
    }

    /* ---- Step 4 + 5 + 6: State machine ---- */
    if (st->sm_state == RADAR_HR_UNLOCKED) {
        /* UNLOCKED: pick highest-PNR valid candidate. */
        float32_t best_pnr = -1.0f;
        uint32_t  best_idx = 0U;
        for (uint32_t c = 0U; c < st->num_candidates; c++) {
            if (st->candidates[c].valid_conf && (st->candidates[c].PNR_dB > best_pnr)) {
                best_pnr = st->candidates[c].PNR_dB;
                best_idx = c;
            }
        }

        /* Push to median buffer. */
        median_buf_push(st, st->candidates[best_idx].bpm_raw);
        st->consecutive_valid++;
        st->consecutive_invalid = 0U;

        if (st->consecutive_valid >= st->cfg.N_lock) {
            /* Transition to LOCKED. */
            st->sm_state    = RADAR_HR_LOCKED;
            st->smoothed_bpm = median_buf_get(st);
            st->ref_bpm      = st->smoothed_bpm;
        }
        /* UNLOCKED always outputs invalid. */
        out->valid = false;
        out->state = st->sm_state;
        return;

    } else {
        /* LOCKED: Step 5 — candidate selection with jump gate. */
        float32_t best_pnr = -1.0f;
        int32_t   winner   = -1;

        for (uint32_t c = 0U; c < st->num_candidates; c++) {
            if (!st->candidates[c].valid_conf) {
                continue;
            }
            float32_t delta_bpm = st->candidates[c].bpm_raw - st->ref_bpm;
            if (delta_bpm < 0.0f) {
                delta_bpm = -delta_bpm;
            }
            if (delta_bpm > st->cfg.jump_limit) {
                continue;  /* Jump gate rejects this candidate. */
            }
            if (st->candidates[c].PNR_dB > best_pnr) {
                best_pnr = st->candidates[c].PNR_dB;
                winner   = (int32_t)c;
            }
        }

        if (winner < 0) {
            /* All candidates rejected by jump gate. */
            goto invalid_frame;
        }

        /* ---- Step 6: Temporal smoothing (accepted frame). ---- */
        st->consecutive_valid++;
        st->consecutive_invalid = 0U;

        float32_t bpm_raw_winner = st->candidates[(uint32_t)winner].bpm_raw;
        float32_t pnr_winner     = st->candidates[(uint32_t)winner].PNR_dB;

        /* 6.1 - Push to median buffer. */
        median_buf_push(st, bpm_raw_winner);

        /* 6.2 - Median of buffer. */
        float32_t bpm_med = median_buf_get(st);

        /* 6.3 - PNR-tiered alpha selection. */
        float32_t alpha;
        if (pnr_winner >= 12.0f) {
            alpha = st->cfg.alpha_high;
        } else if (pnr_winner >= 9.0f) {
            alpha = st->cfg.alpha_mid;
        } else {
            alpha = st->cfg.alpha_low;
        }

        /* 6.4 - IIR smoothing. */
        st->smoothed_bpm = alpha * st->smoothed_bpm + (1.0f - alpha) * bpm_med;

        /* 6.5 - Update reference. */
        st->ref_bpm = st->smoothed_bpm;

        /* 6.6 - Emit valid output. */
        out->bpm   = st->smoothed_bpm;
        out->valid = true;
        out->state = st->sm_state;
        return;
    }

invalid_frame:
    st->consecutive_invalid++;
    st->consecutive_valid = 0U;  /* Only reset in UNLOCKED after threshold. */

    if (st->sm_state == RADAR_HR_UNLOCKED) {
        if (st->consecutive_invalid >= 5U) {
            /* Reset partial history. */
            st->consecutive_valid = 0U;
            st->median_count = 0U;
            st->median_idx   = 0U;
        }
    } else {
        /* LOCKED: check for N_reset. */
        if (st->consecutive_invalid >= st->cfg.N_reset) {
            sm_reset_to_unlocked(st);
        }
    }

    out->valid = false;
    out->state = st->sm_state;
}

/* ------------------------------------------------------------------------- */

radar_hr_sm_state_t radar_hr_get_state(const radar_hr_state_t *st)
{
    if (st == NULL) {
        return RADAR_HR_UNLOCKED;
    }
    return st->sm_state;
}

/******************************************************************************
 * Private helpers
 *****************************************************************************/

/** Step 1: Build folded HR spectrum S[n] = M[i]^2 + M[j]^2. */
static void build_folded_spectrum(radar_hr_state_t *st,
                                  const float32_t *spectrum_shifted)
{
    for (uint32_t n = 0U; n < st->band_len; n++) {
        float32_t mi = spectrum_shifted[st->band_i[n]];
        float32_t mj = spectrum_shifted[st->band_j[n]];
        st->S[n] = (mi * mi) + (mj * mj);
    }
}

/* ----- Step 2: Peak extraction ------------------------------------------- */

/** Simple insertion-sort of indices by descending S value (for small K). */
static void insert_sorted_desc(const float32_t *S, uint32_t *idx_buf,
                                uint32_t count, uint32_t new_idx)
{
    /* Find insertion position. */
    uint32_t pos = count;
    for (uint32_t i = 0U; i < count; i++) {
        if (S[new_idx] > S[idx_buf[i]]) {
            pos = i;
            break;
        }
    }
    /* Shift right. */
    for (uint32_t i = count; i > pos; i--) {
        idx_buf[i] = idx_buf[i - 1U];
    }
    idx_buf[pos] = new_idx;
}

static void extract_candidates(radar_hr_state_t *st)
{
    st->num_candidates = 0U;

    if (st->band_len < 3U) {
        return;  /* Need at least 3 bins for local-max detection. */
    }

    /* --- Compute median of S over HR band for pre-filter. --- */
    /* Copy to a scratch area (reuse candidates scratch for sorting). */
    float32_t sorted[RADAR_HR_MAX_BAND_BINS];
    memcpy(sorted, st->S, st->band_len * sizeof(float32_t));

    /* Simple selection of median via partial sort (band_len is small). */
    for (uint32_t i = 0U; i <= st->band_len / 2U; i++) {
        for (uint32_t j = i + 1U; j < st->band_len; j++) {
            if (sorted[j] < sorted[i]) {
                float32_t tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
    float32_t s_median = sorted[st->band_len / 2U];

    /* --- Find local maxima, pre-filter, keep top-K. --- */
    uint32_t top_k_idx[RADAR_HR_MAX_CANDIDATES];
    uint32_t top_k_count = 0U;

    for (uint32_t n = 1U; n < (st->band_len - 1U); n++) {
        if ((st->S[n] > st->S[n - 1U]) && (st->S[n] > st->S[n + 1U])) {
            /* Pre-filter: S must exceed median. */
            if (st->S[n] < s_median) {
                continue;
            }
            if (top_k_count < st->cfg.K) {
                insert_sorted_desc(st->S, top_k_idx, top_k_count, n);
                top_k_count++;
            } else if (st->S[n] > st->S[top_k_idx[top_k_count - 1U]]) {
                /* Replace weakest. */
                top_k_count--;  /* temporarily remove last */
                insert_sorted_desc(st->S, top_k_idx, top_k_count, n);
                top_k_count++;
            }
        }
    }

    /* --- Parabolic interpolation for each candidate. --- */
    for (uint32_t c = 0U; c < top_k_count; c++) {
        uint32_t n0 = top_k_idx[c];
        /* Map band-relative index to absolute shifted-spectrum bin. */
        uint32_t abs_bin = st->band_i[n0];

        float32_t f_bin = (float32_t)((int32_t)abs_bin - (int32_t)(st->N / 2U))
                          * st->delta_f;

        float32_t f_interp = f_bin;

        /* Edge guard: skip interpolation if within 1 bin of band boundary. */
        if ((n0 >= 1U) && (n0 < (st->band_len - 1U))) {
            float32_t s_prev = st->S[n0 - 1U];
            float32_t s_curr = st->S[n0];
            float32_t s_next = st->S[n0 + 1U];
            float32_t denom  = 2.0f * (s_prev - 2.0f * s_curr + s_next);

            if (fabsf(denom) > PM4_NUMERIC_EPSILON_F) {
                float32_t delta = (s_prev - s_next) / denom;
                /* Clamp delta to +/-0.5 */
                if (delta > 0.5f)  { delta =  0.5f; }
                if (delta < -0.5f) { delta = -0.5f; }
                f_interp = f_bin + delta * st->delta_f;
            }
        }

        radar_hr_candidate_t *cand = &st->candidates[st->num_candidates];
        cand->bin        = abs_bin;
        cand->f_interp_hz = f_interp;
        cand->bpm_raw    = PM4_BPM_PER_HZ * f_interp;
        cand->PNR_dB     = 0.0f;
        cand->prominence = 0.0f;
        cand->valid_conf = false;
        st->num_candidates++;
    }
}

/* ----- Step 3: Confidence check ------------------------------------------ */

static float32_t compute_noise_floor(const radar_hr_state_t *st)
{
    /*
     * Noise floor = median of S[n] over HR band, excluding +/-W_mask bins
     * around each candidate peak.
     */
    float32_t buf[RADAR_HR_MAX_BAND_BINS];
    uint32_t  buf_count = 0U;

    for (uint32_t n = 0U; n < st->band_len; n++) {
        bool excluded = false;
        for (uint32_t c = 0U; c < st->num_candidates; c++) {
            /* Convert candidate absolute bin to band-relative index. */
            /* band_i[n] is the abs bin for band index n. */
            /* We need the band-relative index of the candidate peak. */
            /* Find by scanning (cheap for K<=3). */
            uint32_t cand_abs = st->candidates[c].bin;
            for (uint32_t m = 0U; m < st->band_len; m++) {
                if (st->band_i[m] == cand_abs) {
                    int32_t diff = (int32_t)n - (int32_t)m;
                    if (diff < 0) { diff = -diff; }
                    if ((uint32_t)diff <= st->cfg.W_mask) {
                        excluded = true;
                    }
                    break;
                }
            }
            if (excluded) { break; }
        }
        if (!excluded) {
            buf[buf_count++] = st->S[n];
        }
    }

    if (buf_count == 0U) {
        return PM4_NUMERIC_EPSILON_F;  /* Avoid division by zero. */
    }

    /* Simple partial sort to find median. */
    for (uint32_t i = 0U; i <= buf_count / 2U; i++) {
        for (uint32_t j = i + 1U; j < buf_count; j++) {
            if (buf[j] < buf[i]) {
                float32_t tmp = buf[i];
                buf[i] = buf[j];
                buf[j] = tmp;
            }
        }
    }
    float32_t nf = buf[buf_count / 2U];
    return (nf > PM4_NUMERIC_EPSILON_F) ? nf : PM4_NUMERIC_EPSILON_F;
}

static void confidence_check(radar_hr_state_t *st, float32_t noise_floor)
{
    for (uint32_t c = 0U; c < st->num_candidates; c++) {
        /* Find S value for this candidate. */
        float32_t s_peak = 0.0f;
        for (uint32_t n = 0U; n < st->band_len; n++) {
            if (st->band_i[n] == st->candidates[c].bin) {
                s_peak = st->S[n];
                break;
            }
        }

        float32_t ratio = s_peak / noise_floor;
        st->candidates[c].PNR_dB     = 10.0f * log10f(ratio);
        st->candidates[c].prominence = (s_peak - noise_floor) / noise_floor;
        st->candidates[c].valid_conf = (st->candidates[c].PNR_dB >= st->cfg.PNR_min)
                                     && (st->candidates[c].prominence >= st->cfg.prominence_min);
    }
}

/* ----- State machine helpers --------------------------------------------- */

static void sm_reset_to_unlocked(radar_hr_state_t *st)
{
    st->sm_state           = RADAR_HR_UNLOCKED;
    st->consecutive_valid  = 0U;
    st->consecutive_invalid = 0U;
    st->ref_bpm            = 0.0f;
    st->smoothed_bpm       = 0.0f;
    st->median_count       = 0U;
    st->median_idx         = 0U;
}

/* ----- Median buffer ----------------------------------------------------- */

static float32_t median3(float32_t a, float32_t b, float32_t c)
{
    if ((a >= b && a <= c) || (a <= b && a >= c)) { return a; }
    if ((b >= a && b <= c) || (b <= a && b >= c)) { return b; }
    return c;
}

static void median_buf_push(radar_hr_state_t *st, float32_t val)
{
    st->median_buf[st->median_idx] = val;
    st->median_idx = (st->median_idx + 1U) % RADAR_HR_MEDIAN_BUF_SIZE;
    if (st->median_count < RADAR_HR_MEDIAN_BUF_SIZE) {
        st->median_count++;
    }
}

static float32_t median_buf_get(const radar_hr_state_t *st)
{
    if (st->median_count == 0U) {
        return 0.0f;
    }
    if (st->median_count == 1U) {
        return st->median_buf[0];
    }
    if (st->median_count == 2U) {
        return (st->median_buf[0] + st->median_buf[1]) * 0.5f;
    }
    return median3(st->median_buf[0], st->median_buf[1], st->median_buf[2]);
}
