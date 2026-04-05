# FW Subsystem Test Plan

## Test parameters
- Channels under test: I and Q only
- Sampling rate: 100 Hz
- FFT size per channel: 512 samples
- FFT bin spacing: Δf = 100 / 512 ≈ 0.195 Hz
- Displayed spectrum range: -4 Hz to +4 Hz
- DC offset for all injected analog signals: 1.5 V
- Dynamic tests shall be executed at two amplitudes:
    - A_low = 50 mVpp
    - A_high = 1.5 Vpp
- To avoid unnecessary spectral leakage during subsystem verification, use exact FFT-bin frequencies:
    - 2 · Δf ≈ 0.391 Hz
    - 6 · Δf ≈ 1.172 Hz
    - 10 · Δf ≈ 1.953 Hz
    - 16 · Δf ≈ 3.125 Hz
    - 19 · Δf ≈ 3.711 Hz

## Test Plan

| ID | Test description | Input signals / setup | Expected result | Measured result | Verdict | Remarks |
| ----- | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------- | ------- | ------- |
| FW-01 | **Baseline DC / zero-frequency sanity** | Apply **I = 1.5 V DC**, **Q = 1.5 V DC**. | Spectrum shows only DC / near-0 content. No stable discrete peaks away from 0 Hz in the displayed band. Numerical peak readout is **0 Hz or nearest displayed bin around 0 Hz**. Repeated runs remain stable. | ... | ... | ... |
| FW-02 | **Signed frequency, positive, low band** | Apply `I(t) = 1.5 V + (A/2)·cos(2πft)`, `Q(t) = 1.5 V + (A/2)·sin(2πft)` with **f = 0.391 Hz**. Run once at **A_low** and once at **A_high**. | Dominant spectral peak is at **+0.391 Hz ± 1 bin**. The negative-frequency side does not become the dominant peak. Spectrum is stable over repeated runs. | ... | ... | ... |
| FW-03 | **Signed frequency, positive, mid band** | Same as FW-02, with **f = 1.172 Hz**. Run at **A_low** and **A_high**. | Dominant peak at **+1.172 Hz ± 1 bin**. No sign inversion. Stable spectrum and peak readout over repeated runs. | ... | ... | ... |
| FW-04 | **Signed frequency, positive, upper band** | Same as FW-02, with **f = 3.125 Hz**. Run at **A_low** and **A_high**. | Dominant peak at **+3.125 Hz ± 1 bin**. No sign inversion. No abnormal peak splitting or instability. | ... | ... | ... |
| FW-05 | **Signed frequency, positive, near display edge** | Same as FW-02, with **f = 3.711 Hz**. Run at **A_low** and **A_high**. | Dominant peak at **+3.711 Hz ± 1 bin** and still correctly displayed inside the visible range. No false wraparound or wrong-side dominance. | ... | ... | ... |
| FW-06 | **Signed frequency, positive, off-bin** | Same as FW-02, with **f = 1.30 Hz**. Run at **A_low** and **A_high**. | Dominant peak around **+1.30 Hz**, at the nearest bin or otherwise within **±1 bin** of the true frequency. Some leakage over adjacent bins is expected, but the spectrum shall remain stable over repeated runs and the negative-frequency side shall not become dominant. | ... | ... | ... |
| FW-07 | **Signed frequency, negative, low band** | Apply `I(t) = 1.5 V + (A/2)·cos(2πft)`, `Q(t) = 1.5 V - (A/2)·sin(2πft)` with **f = 0.391 Hz**. Run at **A_low** and **A_high**. | Dominant spectral peak is at **-0.391 Hz ± 1 bin**. The positive side does not become the dominant peak. Stable over repeated runs. | ... | ... | ... |
| FW-08 | **Signed frequency, negative, mid band** | Same as FW-07, with **f = 1.172 Hz**. Run at **A_low** and **A_high**. | Dominant peak at **-1.172 Hz ± 1 bin**. No sign inversion. Stable spectrum and numerical readout. | ... | ... | ... |
| FW-09 | **Signed frequency, negative, upper band** | Same as FW-07, with **f = 3.125 Hz**. Run at **A_low** and **A_high**. | Dominant peak at **-3.125 Hz ± 1 bin**. No sign inversion. Stable spectrum and numerical readout. | ... | ... | ... |
| FW-10 | **Signed frequency, negative, off-bin** | Same as FW-07, with **f = 2.70 Hz**. Run at **A_low** and **A_high**. | Dominant peak around **-2.70 Hz**, at the nearest bin or otherwise within **±1 bin** of the true frequency. Some leakage over adjacent bins is expected, but the spectrum shall remain stable over repeated runs and the positive-frequency side shall not become dominant. | ... | ... | ... |
| FW-11 | **Single-channel injection on I only (unsigned behavior)** | Apply `I(t) = 1.5 V + (A/2)·cos(2πft)` with **f = 1.953 Hz**. Keep **Q = 1.5 V DC**. Run at **A_low** and **A_high**. | Spectrum shows the expected **symmetric pair at +1.953 Hz and -1.953 Hz**, both within **±1 bin**. No false signed dominance should be interpreted from this test. This verifies correct frequency location for a purely single-channel input. | ... | ... | ... |
| FW-12 | **Single-channel injection on Q only (unsigned behavior)** | Keep **I = 1.5 V DC**. Apply `Q(t) = 1.5 V + (A/2)·cos(2πft)` with **f = 1.953 Hz**. Run at **A_low** and **A_high**. | Spectrum shows the expected **symmetric pair at +1.953 Hz and -1.953 Hz**, both within **±1 bin**. Frequency location is correct; no false signed interpretation from this test. | ... | ... | ... |
| FW-13 | **Acquisition/display stability** | Continuously apply a stable signed tone, e.g. positive **1.172 Hz**, and let the subsystem run through repeated updates. | Display updates remain continuous, no freeze, no missing refresh, no corrupted spectrum, and detected peak remains within **±1 bin** over repeated runs. | ... | ... | ... |
