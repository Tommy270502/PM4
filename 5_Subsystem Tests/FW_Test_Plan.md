# FW Subsystem Test Plan

## Test parameters
- Channels under test: I and Q only
- Sampling rate: 100 Hz
- FFT size per channel: 512 samples
- FFT bin spacing: Δf = 100 / 512 ≈ 0.195 Hz
- Displayed spectrum range: -4 Hz to +4 Hz
- Numerical peak frequency readout is required
- DC offset for all injected analog signals: 1.5 V
- Dynamic tests shall be executed at two amplitudes:
    - A = 50 mVpp
    - A = 3.0 Vpp
- To avoid unnecessary spectral leakage during subsystem verification, use exact FFT-bin frequencies:
    - 2 · Δf ≈ 0.391 Hz
    - 6 · Δf ≈ 1.172 Hz
    - 10 · Δf ≈ 1.953 Hz
    - 16 · Δf ≈ 3.125 Hz
    - 19 · Δf ≈ 3.711 Hz

## Common acceptance criteria
- Bin-centered tests pass if the detected dominant peak lies within **±1 bin = ±0.195 Hz** of the expected frequency
- Off-bin tests pass if the detected dominant peak lies at the nearest bin or otherwise within **±1 bin** of the true frequency
- Signed tests pass only if the dominant peak remains on the correct side of 0 Hz
- Single-channel tests pass if the expected symmetric peaks at **+f** and **-f** are visible within **±1 bin**
- Stability means that the displayed spectrum and numerical peak readout remain consistent during a **30 s observation time**
- Minor adjacent-bin leakage is acceptable if the expected peak remains dominant and no wrong-side dominance occurs

## Test Plan

| ID | Test description | Input signals / setup | Expected result | Measured result | Verdict | Remarks |
| - | - | - | - | - | - | - |
| FW-01 | **Baseline DC / zero-frequency sanity** | Apply **I = 1.5 V DC**, **Q = 1.5 V DC** | Spectrum shows only DC / near-0 content. No stable discrete peaks away from 0 Hz in the displayed band. No clipping, no stable spurious mirrored peaks, and stable behavior | as expected | <span style="color:green;">**passed**</span> | --- |
| FW-02 | **Signed frequency, positive, low band** | Apply $I(t) = 1.5 \text{ V} + (A/2) \cdot \cos(2πft)$, $Q(t) = 1.5 \text{ V} + (A/2) \cdot \sin(2πft)$ with **f = 0.391 Hz**. Run once at **A = 50 mVpp** and once at **A = 3.0 Vpp** | Dominant spectral peak is at **+0.391 Hz ± 1 bin**. The negative-frequency side does not become dominant. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-03 | **Signed frequency, positive, mid band** | Same as FW-02, with **f = 1.172 Hz**. Run at **A = 50 mVpp** and **A = 3.0 Vpp** | Dominant peak at **+1.172 Hz ± 1 bin**. No sign inversion. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-04 | **Signed frequency, positive, upper band** | Same as FW-02, with **f = 3.125 Hz**. Run at **A = 50 mVpp** and **A = 3.0 Vpp** | Dominant peak at **+3.125 Hz ± 1 bin**. No sign inversion. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-05 | **Signed frequency, positive, near display edge** | Same as FW-02, with **f = 3.711 Hz**. Run at **A = 50 mVpp** and **A = 3.0 Vpp** | Dominant peak at **+3.711 Hz ± 1 bin** and still correctly displayed inside the visible range. No false wraparound or wrong-side dominance. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-06 | **Signed frequency, positive, off-bin** | Same as FW-02, with **f = 1.300 Hz**. Run at **A = 50 mVpp** and **A = 3.0 Vpp** | Dominant peak around **+1.300 Hz**, at the nearest bin or otherwise within **±1 bin** of the true frequency. Some leakage over adjacent bins is expected, but the negative-frequency side shall not become dominant. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-07 | **Signed frequency, positive, I/Q amplitude mismatch** | Apply $I(t) = 1.5 \text{ V} + (A_I/2) \cdot \cos(2πft)$, $Q(t) = 1.5 \text{ V} + (A_Q/2) \cdot \sin(2πft)$ with **f = 1.172 Hz**. Run once with **A_I = 50 mVpp, A_Q = 35 mVpp** and once with **A_I = 3.0 Vpp, A_Q = 2.1 Vpp** | Dominant peak at **+1.172 Hz ± 1 bin**. Some degradation of spectral symmetry or additional minor leakage is acceptable due to the amplitude mismatch, but the **negative-frequency side shall not become dominant**. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-08 | **Signed frequency, negative, low band** | Apply $I(t) = 1.5 \text{ V} + (A/2) \cdot \cos(2πft)$, $Q(t) = 1.5 \text{ V} - (A/2) \cdot \sin(2πft)$ with **f = 0.391 Hz**. Run at **A = 50 mVpp** and **A = 3.0 Vpp** | Dominant spectral peak is at **-0.391 Hz ± 1 bin**. The positive side does not become dominant. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-09 | **Signed frequency, negative, mid band** | Same as FW-08, with **f = 1.172 Hz**. Run at **A = 50 mVpp** and **A = 3.0 Vpp** | Dominant peak at **-1.172 Hz ± 1 bin**. No sign inversion. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-10 | **Signed frequency, negative, upper band** | Same as FW-08, with **f = 3.125 Hz**. Run at **A = 50 mVpp** and **A = 3.0 Vpp** | Dominant peak at **-3.125 Hz ± 1 bin**. No sign inversion. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-11 | **Signed frequency, negative, off-bin** | Same as FW-08, with **f = 2.700 Hz**. Run at **A = 50 mVpp** and **A = 3.0 Vpp** | Dominant peak around **-2.700 Hz**, at the nearest bin or otherwise within **±1 bin** of the true frequency. Some leakage over adjacent bins is expected, but the positive-frequency side shall not become dominant. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-12 | **Signed frequency, negative, I/Q amplitude mismatch** | Apply $I(t) = 1.5 \text{ V} + (A_I/2) \cdot \cos(2πft)$, $Q(t) = 1.5 \text{ V} - (A_Q/2) \cdot \sin(2πft)$ with **f = 1.172 Hz**. Run once with **A_I = 50 mVpp, A_Q = 35 mVpp** and once with **A_I = 3.0 Vpp, A_Q = 2.1 Vpp** | Dominant peak at **-1.172 Hz ± 1 bin**. Some degradation of spectral symmetry or additional minor leakage is acceptable due to the amplitude mismatch, but the **positive-frequency side shall not become dominant**. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-13 | **Single-channel injection on I only (unsigned behavior)** | Apply $I(t) = 1.5 \text{ V} + (A/2) \cdot \cos(2πft)$ with **f = 1.953 Hz**. Keep **Q = 1.5 V DC**. Run at **A = 50 mVpp** and **A = 3.0 Vpp** | Spectrum shows the expected **symmetric pair at +1.953 Hz and -1.953 Hz**, both within **±1 bin**. No false signed dominance shall be interpreted from this test. Spectrum and numerical peak readout are stable | ... | ... | --- |
| FW-14 | **Single-channel injection on Q only (unsigned behavior)** | Keep **I = 1.5 V DC**. Apply $Q(t) = 1.5 \text{ V} + (A/2) \cdot \cos(2πft)$ with **f = 1.953 Hz**. Run at **A = 50 mVpp** and **A = 3.0 Vpp** | Spectrum shows the expected **symmetric pair at +1.953 Hz and -1.953 Hz**, both within **±1 bin**. No false signed dominance shall be interpreted from this test. Spectrum and numerical peak readout are stable | ... | ... | --- |