## Heart rate Estimation Algorithm

### Configuration parameters

| Parameter | Initial value | Notes |
|---|---|---|
| `f_min`, `f_max` | 0.55, 3.70 Hz | HR search band |
| `K` | 3 | Top candidates |
| `W_mask` | 2 bins | Noise floor exclusion half-width |
| `PNR_min` | 7.0 dB | Confidence gate |
| `prominence_min` | 1.8 | Confidence gate |
| `jump_limit` | 12 bpm/frame | Hard continuity window |
| `N_lock` | 3 frames | Consecutive valid frames to enter LOCKED |
| `N_reset` | 8 frames | Consecutive invalid frames to exit LOCKED |
| `α_high/mid/low` | 0.35 / 0.55 / 0.75 | IIR smoothing by PNR tier |

---

### Step 1 — Build the folded HR spectrum

Compute magnitude spectrum from the complex FFT output `X[k]`:
```
M[k] = |X[k]|
```

Map each bin to signed frequency:
```
f[k] = (k − N/2) · Δf,    Δf = Fs/N
```

For each positive-frequency index `i` with `f[i] ∈ [f_min, f_max]`, find the mirror index `j` where `f[j] = −f[i]`. Fold using energy sum:
```
S[i] = M[i]² + M[j]²
```

Precompute the `(i, j)` index pairs once at initialisation.

---

### Step 2 — Peak extraction and sub-bin interpolation

**Local maxima**: find all `k` in the HR band where `S[k] > S[k−1]` and `S[k] > S[k+1]`.

**Pre-filter**: discard any candidate where `S[k] < median(S)` over the HR band.

**Top-K selection**: keep K = 3 candidates with highest `S[k]`.

**Parabolic interpolation** for each candidate at bin `k₀`:
```
δ = (S[k₀−1] − S[k₀+1]) / (2·(S[k₀−1] − 2·S[k₀] + S[k₀+1]))
δ ← clamp(δ, −0.5, +0.5)
f_interp = f[k₀] + δ · Δf
bpm_raw  = 60 · f_interp
```

**Edge guard**: if `k₀` is within 1 bin of the band boundary, skip interpolation and use `k₀ · Δf` directly.

---

### Step 3 — Confidence check

**Noise floor**: median of `S[i]` over the HR band, excluding ±`W_mask` bins around each of the K candidate peaks.

For each candidate:
```
PNR_dB[i]     = 10·log10(S[k₀] / noise_floor)
prominence[i] = (S[k₀] − noise_floor) / noise_floor

valid_conf[i] = (PNR_dB[i] ≥ 7.0) AND (prominence[i] ≥ 1.8)
```

If no candidate passes `valid_conf`: emit `valid = false`, do not update any state, proceed to next frame.

---

### Step 4 — State machine

**UNLOCKED** *(initial and recovery state)*

- *Entry conditions*: system start, or `consecutive_invalid ≥ N_reset` while LOCKED.
- *On entry*: clear median buffer, clear `ref_bpm`, set `consecutive_valid = 0`, `consecutive_invalid = 0`.
- *Per frame (confidence-valid)*: push `bpm_raw` of highest-PNR passing candidate to median buffer, increment `consecutive_valid`.
  - If `consecutive_valid = N_lock`: transition to LOCKED, set `ref_bpm = smoothed_bpm = median(buffer)`.
- *Per frame (invalid)*: increment `consecutive_invalid`. If `consecutive_invalid ≥ 5`: reset `consecutive_valid = 0`, clear median buffer to prevent locking on stale partial history.
- *Output*: always `valid = false`.

**LOCKED** *(normal operation)*

- *Per frame (valid after confidence + jump gate)*: update state, emit `valid = true`.
- *Per frame (invalid or jump-rejected)*: increment `consecutive_invalid`, do not update state, emit `valid = false`.
  - If `consecutive_invalid ≥ N_reset`: transition to UNLOCKED.

---

### Step 5 — Candidate selection *(LOCKED only)*

**Stage 1 — hard continuity window**: discard any candidate where:
```
|bpm_raw[i] − ref_bpm| > jump_limit
```

If all candidates are discarded: reject frame, emit `valid = false`, do not update state.

**Stage 2 — select by PNR**: among surviving candidates, select `i*` with highest `PNR_dB[i]`.

---

### Step 6 — Temporal smoothing *(LOCKED, accepted frame only)*

1. Push `bpm_raw[i*]` into circular median buffer (size 3).
2. `bpm_med = median(buffer)`.
3. Select `α` by PNR tier:
   - `PNR_dB ≥ 12`: `α = 0.35`
   - `9 ≤ PNR_dB < 12`: `α = 0.55`
   - `7 ≤ PNR_dB < 9`: `α = 0.75`
4. `smoothed_bpm = α · smoothed_bpm_prev + (1−α) · bpm_med`
5. `ref_bpm ← smoothed_bpm`
6. Emit `bpm_out = smoothed_bpm`, `valid = true`.

---

### Debug outputs per frame

```
state, consecutive_valid, consecutive_invalid
candidates[K]: {bin, f_interp_hz, bpm_raw, PNR_dB, prominence, score}
noise_floor
winning: {candidate_idx, bpm_raw, Δbpm_from_ref, α_used}
reject_reason: {none | no_conf_candidates | jump_gate_all_failed}
output: {bpm_out, valid}
```

---

If you encounter the 2×/3× harmonic misidentification in testing, the hypothesis system slots in cleanly at Step 5 — the rest of the pipeline is unchanged.