# HW Subsystem Test Plan

## 1. Power Supply, Reference Voltage and VCO Modulation Path

### 1.1 Test parameters
- Scope: power supply, reference voltage circuits, and optional VCO modulation input path
- Nominal PCB supply voltage: 5 V
- Nominal on-board regulated voltage: 3.3 V
- Nominal radar reference voltage: 1.65 V

### 1.2 Acceptance criteria
- Supply/reference checks pass if measured values are within their stated tolerances and remain stable without abnormal ripple or oscillation
- VCO modulation path checks pass if the output follows the applied input stimulus with the expected gain within the stated tolerance and without visible instability or distortion

### 1.3 Test plan

| ID | Test description | Input signals / setup | Expected result | Measured result | Verdict | Remarks |
| - | - | - | - | - | - | - |
| HW-1.01 | **3.3 V on-board regulated voltage** | Power the PCB under nominal operating conditions | On-board regulated voltage = **3.3 V ± 1%** and stable | 3.2998 V | <span style="color:green;">**passed**</span> | --- |
| HW-1.02 | **Current consumption** | Power the PCB under nominal operating conditions with no additional modules connected (e.g. radar, OpenLog etc.) | Total current consumption **≤ 15 mA** | 9.03 mA | <span style="color:green;">**passed**</span> | --- |
| HW-1.03 | **Radar reference voltage** | Power the PCB under nominal operating conditions | Reference voltage = **1.65 V ± 16.5 mV** and stable | 1.6470 V | <span style="color:green;">**passed**</span> | --- |
| HW-1.04 | **VCO_in DC transfer check** | Apply a constant **1 V DC** level to the **VCO_in** path | Output at VCO_in pin = **1.1 V DC ± 1%** and stable | 1.0971 V | <span style="color:green;">**passed**</span> | ---|

## 2. I/Q Channel Tests

### 2.1 Test parameters
- Scope: I and Q analog paths
- Nominal analog-path DC operating point before ADC: 1.5 V
- Intended final output operating range: 0..3 V
- Nominal in-band gain target: 60 dB
- Cut-off frequencies: 0.3 Hz and 15 Hz
- In-band gain test frequencies: 0.5, 1, 2, 5, 10 Hz
- Out-of-band attenuation check frequencies: 80 mHz and 150 Hz
- Dynamic input amplitudes: 0.5 mVpp and 2.5 mVpp
- Inactive channel condition: grounded

### 2.2 Acceptance criteria
- DC operating-point checks pass if both I and Q path outputs before ADC are centered at 1.5 V within the stated tolerance and show no instability
- In-band gain checks pass if measured gain remains within the stated tolerance at all defined passband points
- Cut-off checks pass if relative attenuation at the cut-off point is -3 dB ± 20% versus the midband response
- Out-of-band attenuation checks pass if relative attenuation at 80 mHz and 150 Hz is -20 dB ± 20% versus the midband response
- Waveform-quality checks pass if waveforms remain sinusoidal and unclipped at all test amplitudes
- Simultaneous I/Q checks pass if both channels satisfy the corresponding single-channel criteria under the same input condition
- Channel-matching checks pass if I/Q gain difference and phase difference do not exceed the stated tolerance across the passband
- Crosstalk / channel-isolation checks pass if the inactive grounded channel shows no unintended amplified response beyond the stated tolerance while the other channel is driven

### 2.3 Test plan

| ID | Test description | Input signals / setup | Expected result | Measured result | Verdict | Remarks |
| - | - | - | - | - | - | - |
| HW-2.01 | **DC operating point** | Power the PCB under nominal operating conditions with both I and Q inputs grounded | Outputs biased at **1.5 V** DC on both paths | 1.47 V DC | <span style="color:green;">**passed**</span> | --- |
| HW-2.02 | **I-path in-band gain / waveform check** | Apply sine to **I input only**. Ground **Q input**. Frequencies: **0.5, 1, 2, 5, 10 Hz**. Run once at **0.5 mVpp** and once at **2.5 mVpp** | Passband gain **60 dB ± 20%** at all in-band points. Expected output ≈ **0.5 Vpp** and **2.5 Vpp** respectively, centered around **1.5 V DC**. Clean sinusoidal waveform, no clipping at either amplitude | as expected, within tolerance | <span style="color:green;">**passed**</span> | --- |
| HW-2.03 | **Q-path in-band gain / waveform check** | Same as HW-2.02, with sine applied to **Q input only** and **I input** grounded | Same as HW-2.02 | as expected, within tolerance | <span style="color:green;">**passed**</span> | --- |
| HW-2.04 | **Simultaneous in-band response** | Same as HW-2.02, with the same sine signal applied simultaneously to **both I and Q inputs** | Same as HW-2.02, on both outputs simultaneously. No instability | as expected, within tolerance | <span style="color:green;">**passed**</span> | --- |
| HW-2.05 | **I-path cut-off check** | Apply sine to **I input only**. Ground **Q input**. Frequencies: **0.3 Hz** and **15 Hz**. Run once at **0.5 mVpp** and once at **2.5 mVpp** | Attenuation **-3 dB ± 20%** relative to midband response at both cut-off points. Expected output ≈ **355 mVpp** and **1.78 Vpp** respectively, centered around **1.5 V DC** | as expected, within tolerance | <span style="color:green;">**passed**</span> | --- |
| HW-2.06 | **Q-path cut-off check** | Same as HW-2.05, with sine applied to **Q input only** and **I input** grounded | Same as HW-2.05 | as expected, within tolerance | <span style="color:green;">**passed**</span> | --- |
| HW-2.07 | **Simultaneous cut-off check** | Same as HW-2.05, with the same sine signal applied simultaneously to **both I and Q inputs** | Same as HW-2.05, on both outputs simultaneously. No instability | as expected, within tolerance | <span style="color:green;">**passed**</span> | --- |
| HW-2.08 | **I-path out-of-band attenuation check** | Apply sine to **I input only**. Ground **Q input**. Frequencies: **80 mHz** and **150 Hz**. Run once at **0.5 mVpp** and once at **2.5 mVpp** | Attenuation **-20 dB ± 20%** relative to midband response at both out-of-band points. Expected output ≈ **50 mVpp** and **250 mVpp** respectively, centered around **1.5 V DC** | as expected, within tolerance | <span style="color:green;">**passed**</span> | --- |
| HW-2.09 | **Q-path out-of-band attenuation check** | Same as HW-2.08, with sine applied to **Q input only** and **I input** grounded | Same as HW-2.08 | as expected, within tolerance | <span style="color:green;">**passed**</span> | --- |
| HW-2.10 | **Simultaneous out-of-band attenuation check** | Same as HW-2.08, with the same sine signal applied simultaneously to **both I and Q inputs** | Same as HW-2.08, on both outputs simultaneously. No instability | as expected, within tolerance | <span style="color:green;">**passed**</span> | --- |
| HW-2.11 | **I/Q channel matching in passband** | Apply the same sine signal simultaneously to **both I and Q inputs**. Frequencies: **0.5, 1, 2, 5, 10 Hz**. Run once at **0.5 mVpp** and once at **2.5 mVpp** | **I/Q gain difference ≤ 10%** and **I/Q phase difference ≤ 1%** across the passband at both amplitudes. Both outputs remain centered around **1.5 V DC** | as expected, within tolerance | <span style="color:green;">**passed**</span> | --- |
| HW-2.12 | **Crosstalk / channel isolation check** | Drive one channel at a time with sine while keeping the other channel input grounded (I active / Q grounded, then Q active / I grounded). Frequencies: **0.5, 1, 2, 5, 10 Hz**. Run once at **0.5 mVpp** and once at **2.5 mVpp** | The inactive grounded path remains centered around **1.5 V DC** and any coupled signal remains below **1%** at all test frequencies and amplitudes | ≤ 10 mVpp | <span style="color:green;">**passed**</span> | --- |

## 3. Radar-Connected Tests

### 3.1 Test parameters
- Scope: end-to-end verification of the connected K-LC5 radar module together with the I/Q analog paths
- Radar target simulator: RFbeam K-DT1
- Recommended minimum-speed setting for this section: 1 km/h (lowest available K-DT1 Doppler frequency)
- K-DT1 adjustable parameters relevant for this section: speed, direction, signal amplitude / reach, and signal duration
- Recommended mechanical setup: fix both K-DT1 and sensor board mechanically, align K-DT1 toward the radar sensor, keep spacing ≥ 30 cm, and minimize nearby parasitic reflections
- Intended final output operating range at the external amplifier outputs: 0..3 V
- Nominal analog-path DC operating point before ADC: 1.5 V

### 3.2 Acceptance criteria
- Radar-connected basic-response checks pass if activation of K-DT1 produces stable observable periodic signals on both I and Q outputs at the expected Doppler frequency, centered around 1.5 V DC, without clipping or instability
- Radar-connected direction checks pass if changing the K-DT1 movement direction reverses the relative I/Q phase order while keeping the Doppler frequency consistent
- Radar-connected reach / amplitude sanity checks pass if increasing the K-DT1 signal amplitude / reach increases the observed AC output amplitude correspondingly, while outputs remain within the intended 0..3 V range

*Note:* Since the minimum available K-DT1 Doppler frequency (44 Hz) lies above the intended analog passband, this section is treated as an integration and functionality check rather than a strict nominal-band amplitude verification

### 3.3 Test plan

| ID | Test description | Input signals / setup | Expected result | Measured result | Verdict | Remarks |
| - | - | - | - | - | - | - |
| HW-3.01 | **Radar-connected basic response** | Place **K-DT1** in front of the radar sensor in a fixed aligned setup with spacing **≥ 30 cm**. Set K-DT1 to **1 km/h**, one fixed direction, and one representative reach setting (recommended: start with **100%**, reduce only if clipping occurs) | Both **I** and **Q** outputs show stable observable periodic signals at approx. **44 Hz**, centered around **1.5 V DC**. Both channels remain within **0..3 V** and show no clipping or instability | --- | --- | --- |
| HW-3.02 | **Radar-connected direction check** | Use the same fixed setup as in **HW-3.01**. Keep speed at **1 km/h** and the same reach setting. Run once with **forward** direction and once with **backward** direction on K-DT1 | In both cases, observable signals remain present on **I** and **Q** at approx. **44 Hz**, centered around **1.5 V DC**. Reversing K-DT1 direction reverses the relative **I/Q** phase order. No clipping or instability | --- | --- | --- |
| HW-3.03 | **Radar-connected reach / amplitude sanity check** | Use the same fixed setup as in **HW-3.01**. Keep speed at **1 km/h** and one fixed direction. Compare at least two K-DT1 reach settings, for example **low** and **high** (e.g. **20%** and **100%**) | Higher K-DT1 reach produces a correspondingly larger observed AC amplitude on both outputs. Outputs remain centered around **1.5 V DC**, stay within **0..3 V**, and show no clipping or instability | --- | --- | --- |