# Firmware Architecture

This document describes the firmware in this repository as implemented in the
source tree. It is based on `Core/Src`, `Core/Inc`, the STM32CubeIDE project
metadata, and the generated build files under `Debug`.

## Summary

The project is an STM32F429I-DISC1 firmware named `RadarHeartRate`. It runs a
bare-metal superloop with interrupt-driven acquisition paths for:

- A radar I/Q front end sampled by ADC1 and ADC2 in dual regular simultaneous
  mode.
- An AD8232 EKG input sampled by ADC3 from PF6.
- A touchscreen/LCD user interface using the STM32F429I Discovery BSP.
- Optional CSV logging to an OpenLog module through USART6 TX.
- A simple analog DAC output control on PA5.

There is no RTOS in the application. The main loop polls UI state, consumes
interrupt-produced samples, performs radar and EKG processing, and refreshes the
LCD when new data or user input requires it.

## Confirmation Of The Previous Analysis

The earlier workspace analysis was broadly correct that the firmware combines
radar-based heart-rate processing, EKG signal processing, LCD display, filters,
FFT, menu handling, and UART logging. Several important details differ from
that summary:

- The configured MCU clock is 168 MHz, not 200 MHz. `SystemClock_Config()` uses
  HSE, PLLM=8, PLLN=336, PLLP=2.
- EKG acquisition is single-channel AD8232 on PF6 / ADC3_IN4, not multi-channel.
- The radar code is written for an RFbeam K-LC5-style wavelength
  (`0.0124266 m`), which is a 24 GHz class radar. The firmware does not contain
  a 60 GHz radar driver.
- There is no implemented radar/EKG data fusion layer. The two estimators run
  side by side and are displayed separately.
- The project includes many BSP component drivers, but the application directly
  uses only a subset. For example, the onboard gyroscope is explicitly disabled
  so its pins do not interfere with analog input use.
- The code is a research/teaching-style monitor implementation. The EKG header
  explicitly says it is not a certified medical device implementation.

## Hardware And Board Configuration

### MCU And Memory

The CubeIDE project targets:

- Board: `STM32F429I-DISC1`
- MCU: `STM32F429ZITx`
- Core: ARM Cortex-M4 with single-precision FPU, hard-float ABI
- Flash linker script: `STM32F429ZITX_FLASH.ld`
- Memory map from the linker script:
  - Flash: 2048 KB at `0x08000000`
  - SRAM: 192 KB at `0x20000000`
  - CCMRAM: 64 KB at `0x10000000`

### Clocks

`Core/Src/main.c` configures the main clocks:

- HSE oscillator enabled.
- PLL source is HSE.
- PLLM = 8, PLLN = 336, PLLP = 2, PLLQ = 7.
- SYSCLK = PLLCLK = 168 MHz.
- AHB = 168 MHz.
- APB1 = HCLK / 4 = 42 MHz. APB1 timers run at 84 MHz because the APB1
  prescaler is not 1.
- APB2 = HCLK / 2 = 84 MHz.
- LTDC uses PLLSAI for LCD timing.

`Core/Inc/board_config.h` defines the APB1 timer prescaler used by TIM2 and
TIM3:

- `BOARD_TIM_APB1_PSC_1MHZ = 83`
- `BOARD_TIM_APB1_TICK_HZ = 1000000`

That produces a 1 MHz timer counter clock from the 84 MHz APB1 timer clock.

### Board Orientation And Touch Coordinates

`Core/Inc/board_config.h` enables:

- `EVAL_REV_E`
- `FLIPPED_LCD`

The menu and touch handlers compensate for the revision-E inverted Y axis and
the 180 degree flipped LCD orientation. Touch coordinate adjustment is done in
both `MENU_check_transition()` and the local touch helpers in `main.c`.

### Pin And Peripheral Map

| Function | MCU peripheral | Pin(s) | Source |
| --- | --- | --- | --- |
| Radar I channel | ADC1 regular channel 11 | PC1 | `radar.c` |
| Radar Q channel | ADC2 regular channel 13 | PC3 | `radar.c` |
| Radar sample trigger | TIM2 TRGO update | internal | `radar.c` |
| Radar DMA | DMA2 Stream0 Channel0 | ADC common CDR | `radar.c` |
| EKG input | ADC3 regular channel 4 | PF6 | `ekg.c` |
| EKG sample timer | TIM3 update interrupt | internal | `ekg.c` |
| DAC output | DAC channel 2 | PA5 | `dac_output.c` |
| OpenLog TX | USART6 TX AF8 | PG14 | `openlog_uart.c` |
| User button | EXTI0 | PA0 | `pushbutton.c` |
| LCD and touch | BSP drivers | board wiring | `main.c`, BSP |
| Gyroscope CS workaround | GPIOC/PF8 | PC1, PF8 | `gyro_disable()` |

The radar uses PC1 as an ADC input after the gyroscope disable sequence. The
comment in `gyro_disable()` explains that the onboard gyroscope can hold MISO
in a bad state at startup, so the firmware briefly pulls its CS and then leaves
PC1/PF8 configured for analog use.

## Source Tree Structure

Important project-owned source files:

| File | Responsibility |
| --- | --- |
| `Core/Src/main.c` | System startup, initialization order, superloop, high-level control flow. |
| `Core/Src/radar.c` | Radar ADC/TIM2/DMA acquisition, I/Q unpacking, rolling windows, phase-to-displacement conversion, spectrum helper functions. |
| `Core/Src/radar_heartrate.c` | Folded-spectrum radar BPM estimator and lock/search state machine. |
| `Core/Src/ekg.c` | AD8232 ADC3/TIM3 acquisition, sample queue, digital cleanup, R-peak and BPM estimation. |
| `Core/Src/fft.c` | CMSIS-DSP FFT setup and centered magnitude spectrum generation. |
| `Core/Src/filters.c` | RBJ cookbook biquad filters used as switchable radar effects. |
| `Core/Src/display.c` | LCD drawing primitives and per-menu page renderer. |
| `Core/Src/menu.c` | Touchscreen menu bar, scrolling, active menu selection. |
| `Core/Src/openlog_uart.c` | TX-only CSV logging to OpenLog over USART6. |
| `Core/Src/dac_output.c` | PA5 DAC channel 2 setup and voltage control. |
| `Core/Src/pushbutton.c` | User button interrupt and one-shot press flag. |
| `Core/Src/stm32f4xx_it.c` | Core exception handlers and SysTick. Peripheral ISRs are implemented in their owning modules. |

Vendor and generated code:

- `Drivers/STM32F4xx_HAL_Driver`: STM32 HAL.
- `Drivers/BSP/STM32F429I-Discovery`: board support for LCD, touch, SDRAM,
  gyroscope, and related board services.
- `Drivers/BSP/Components`: board component drivers. The build includes many
  component sources even if the application uses only a subset.
- `Drivers/CMSIS`: CMSIS core and CMSIS-DSP.
- `Utilities/Fonts`: LCD font assets.
- `Core/Startup/startup_stm32f429zitx.s`: vector table and reset startup.

## Build Configuration

The project is a STM32CubeIDE managed build:

- Eclipse project name: `RadarHeartRate`
- Debug output: `Debug/RadarHeartRate.elf`
- Compiler defines include:
  - `USE_HAL_DRIVER`
  - `DEBUG`
  - `STM32F429xx`
  - `ARM_MATH_CM4`
- CMSIS-DSP library: `arm_cortexM4lf_math`
- Library search path: `Drivers/CMSIS/Lib/GCC`
- Linker script: `STM32F429ZITX_FLASH.ld`

Generated build metadata in `Debug/sources.mk` and `Debug/objects.list` shows
that the application modules, BSP drivers, HAL drivers, startup file, and
`Drivers/CMSIS/DSP/Source/arm_cfft_init_f32.c` are part of the Debug build.

## Runtime Model

The firmware has three major timing domains:

1. Interrupt context for raw acquisition.
2. Foreground superloop processing in `main()`.
3. LCD/UI updates paced by foreground flags and per-page refresh counters.

The foreground loop does not sleep. It repeatedly checks for:

- Menu transitions.
- User button presses.
- Pending EKG samples.
- Touch input on active DAC or logger menus.
- Completed radar DMA chunks.
- Display refresh requests.

### Initialization Order

`main()` performs this sequence:

1. `HAL_Init()`.
2. `SystemClock_Config()`.
3. LCD init, foreground layer init, display on, white clear.
4. Touchscreen init with LCD dimensions.
5. User button init and EXTI enable.
6. LED3 and LED4 init.
7. Draw menu and show the info screen.
8. Disable the onboard gyroscope/pin conflict.
9. Initialize PA5 DAC output.
10. Initialize radar acquisition with foreground I/Q chunk buffers.
11. Initialize radar phase/displacement state.
12. Start radar acquisition.
13. Configure five radar biquad filter choices for each I/Q channel.
14. Initialize FFT support and Hann window.
15. Initialize EKG sampling using a tuned 100 Hz configuration.
16. Initialize radar heart-rate estimator defaults.
17. Initialize OpenLog UART. Logging starts OFF.
18. Enter the infinite foreground loop.

### Superloop Data Flow

High-level foreground flow:

```text
while (1):
    poll touchscreen menu
    handle menu transition or scroll
    if user button pressed:
        cycle radar filter and refresh effect menu
    if EKG sample queued:
        process one EKG sample
        refresh EKG menu when active
    if active DAC menu:
        handle DAC touch input
    if active logger menu:
        handle START/STOP touch input
    if radar DMA chunk ready:
        unpack latest chunk
        update phase/displacement chunk
        append I/Q and displacement rolling windows
        if full analysis window available:
            prepare processing windows
            compute I/Q spectrum
            compute displacement spectrum
            update radar HR estimator
            write one OpenLog CSV row if logging is enabled
            request display refresh
    if display refresh requested:
        render active menu page
```

The radar path is the main producer of periodic display refreshes because a full
radar processing frame becomes available every 256 samples at 100 Hz, after the
initial fill. That is one new processed radar frame every 2.56 seconds with a
5.12 second analysis window.

## Interrupt Model

Peripheral interrupt handlers are defined in the module that owns the hardware:

| IRQ | Owner | Priority | Purpose |
| --- | --- | --- | --- |
| `DMA2_Stream0_IRQHandler` | `radar.c` | 1 | Marks completed radar DMA ping/pong buffer. |
| `ADC_IRQHandler` | `ekg.c` | 2 | Reads ADC3 sample and pushes it into EKG queue. |
| `TIM3_IRQHandler` | `ekg.c` | 3 | Starts an ADC3 conversion at the configured EKG sample rate. |
| `EXTI0_IRQHandler` | `pushbutton.c` | default | Sets one-shot user button flag. |
| `EXTI15_10_IRQHandler` | `menu.c` | default | Optional touchscreen interrupt path; not enabled in `main.c`. |
| `SysTick_Handler` | `stm32f4xx_it.c` | HAL default | Increments HAL tick. |

The touchscreen interrupt is present, but `main.c` leaves `BSP_TS_ITConfig()`
commented out and instead calls `MENU_check_transition()` from the superloop.

## Radar Acquisition

### Sampling Configuration

Radar constants in `Core/Inc/radar.h`:

- `RADAR_FRAME_SIZE = 1024`
- `RADAR_CHANNEL_SAMPLES = RADAR_FRAME_SIZE / 2 = 512`
- `RADAR_FRAME_ADVANCE_SAMPLES = RADAR_CHANNEL_SAMPLES / 2 = 256`
- `RADAR_SAMPLE_RATE_HZ = 100`

This means:

- Each analysis window contains 512 I samples and 512 Q samples.
- Each DMA completion provides 256 new I/Q sample pairs.
- The analysis window has 50 percent overlap.
- FFT bin spacing is `100 / 512 = 0.1953125 Hz`.
- The full time window is 5.12 seconds.
- The frame advance is 2.56 seconds.

`radar_init()` configures:

- PC1 and PC3 as analog inputs.
- TIM2 as the 100 Hz trigger source.
- ADC1 as I channel on IN11.
- ADC2 as Q channel on IN13.
- ADC common dual regular simultaneous mode.
- DMA2 Stream0 in circular double-buffer mode.

The ADC common data register `ADC->CDR` packs one simultaneous sample pair:

- Bits `[15:0]`: ADC1, I channel.
- Bits `[31:16]`: ADC2, Q channel.

The DMA buffers are `radar_iq_buffer_ping` and `radar_iq_buffer_pong`, each
holding 256 packed 32-bit sample pairs.

### DMA Handoff

The DMA transfer-complete ISR does very little:

1. Checks DMA error flags and increments `radar_dma_error_count` if needed.
2. Determines which ping/pong buffer just completed.
3. Increments `radar_overrun_count` if the previous completed chunk was not yet
   consumed by the foreground.
4. Stores the completed buffer index.
5. Increments `radar_dma_sequence`.
6. Sets `radar_data_ready`.

The foreground calls `radar_get_latest_chunk()`. That function briefly disables
interrupts only to claim the completed buffer and clear the ready flag. It then
unpacks the selected ping/pong buffer outside the critical section into the
caller-provided `radar_i_acquired` and `radar_q_acquired` arrays.

### Rolling I/Q Window

After a chunk is claimed, `main.c` calls `radar_append_latest_chunk()`.

The rolling history buffers keep the last 512 samples per channel. On each new
chunk:

1. The oldest 256 samples are discarded.
2. The newest 256 samples are appended.
3. The fill counter grows until a full 512-sample window is available.

The first full window is available after two chunks. Every later chunk produces
another 50 percent overlapped window.

### Phase And Displacement Path

The radar BPM estimator does not operate directly on raw I/Q magnitude. It uses
relative displacement derived from the I/Q phase.

`radar_phase_process_chunk()` converts each 256-sample raw I/Q chunk into
displacement:

1. Estimate I and Q DC offsets from the chunk mean and update state with
   `offset_alpha`.
2. Estimate I and Q RMS values after offset removal.
3. Calculate gain correction so the I/Q circle is roughly balanced.
4. Detect clipping against low/high ADC count thresholds.
5. For each sample, subtract offsets, apply gains, calculate radius, and reject
   low-radius samples.
6. Calculate `atan2(q, i)` phase for valid samples.
7. Unwrap phase by keeping the delta inside `[-pi, +pi]`.
8. Convert unwrapped phase to displacement:

```text
displacement_m = wavelength_m * (phase - reference_phase) / (4 * pi)
```

The default wavelength is `0.0124266 m`. The source comment says the default is
for RFbeam K-LC5.

The quality structure tracks:

- Number of samples.
- Number of valid samples.
- Number of clipped samples.
- Number of low-signal samples.
- Mean corrected radius.
- Current I/Q offsets and gains.
- Flags:
  - `RADAR_PHASE_FLAG_VALID`
  - `RADAR_PHASE_FLAG_CLIPPING`
  - `RADAR_PHASE_FLAG_LOW_SIGNAL`

The chunk is considered valid only when there is no clipping and the fraction
of low-signal samples stays within the configured limit.

The displacement samples are shifted into a separate rolling 512-sample history
by `radar_append_displacement_chunk()`. That window is copied to
`radar_displacement_samples` before the real FFT used by the radar BPM
algorithm.

## Radar FFT And Peak Readout

`fft_init()` selects the CMSIS CFFT instance based on `RADAR_CHANNEL_SAMPLES`.
With the current configuration it uses the 512-point complex FFT instance.

It also precomputes a Hann window and a magnitude scaling factor. For I/Q
spectra the scale converts ADC counts to volts and compensates the Hann
coherent gain.

### Complex I/Q Spectrum

`fft_iq_centered()`:

1. Computes and removes the mean of the I and Q windows.
2. Applies the Hann window to both channels.
3. Builds a complex buffer as `I + jQ`.
4. Runs `arm_cfft_f32()`.
5. Calculates magnitude with `arm_cmplx_mag_f32()`.
6. Scales magnitudes.
7. FFT-shifts the result so DC is at `RADAR_CHANNEL_SAMPLES / 2`.

This shifted I/Q spectrum is used by:

- The FFT spectrum menu.
- The positive/negative peak readout menu.

`radar_update_peak_readout()` searches a symmetric display band around DC. The
display span is `SPECTRUM_DISPLAY_HZ = 4.0`, so the UI shows roughly -4 Hz to
+4 Hz. Positive and negative dominant peaks are both tracked, with threshold
and dominance logic to suppress one side if it is much weaker.

### Real Displacement Spectrum

`fft_real_centered()` uses the same complex FFT engine with imaginary samples
set to zero:

1. Remove mean from the displacement window.
2. Apply Hann window.
3. Run CFFT.
4. Compute magnitude.
5. Apply real-signal scaling.
6. FFT-shift so DC is centered.

This spectrum is passed to `radar_hr_process_frame()` when the phase quality is
valid.

## Radar Heart-Rate Estimator

`Core/Src/radar_heartrate.c` implements a folded-spectrum heart-rate estimator.
It is stateful and has two public processing functions:

- `radar_hr_process_frame()` for valid displacement spectra.
- `radar_hr_process_invalid_frame()` for upstream-invalid radar phase chunks.

Default configuration:

| Parameter | Value | Meaning |
| --- | ---: | --- |
| `f_min` | 0.80 Hz | Lower HR search frequency, about 48 BPM. |
| `f_max` | 3.00 Hz | Upper HR search frequency, about 180 BPM. |
| `K` | 3 | Keep top 3 candidate peaks. |
| `W_mask` | 2 bins | Exclude around peaks for noise floor estimate. |
| `PNR_min` | 7.0 dB | Minimum peak-to-noise ratio. |
| `prominence_min` | 1.8 | Minimum linear prominence. |
| `jump_limit` | 12 BPM | Maximum accepted change while locked. |
| `N_lock` | 3 frames | Consecutive valid frames needed to lock. |
| `N_reset` | 8 frames | Invalid frames needed to leave locked state. |
| `alpha_high` | 0.35 | Smoothing alpha for PNR >= 12 dB. |
| `alpha_mid` | 0.55 | Smoothing alpha for 9 <= PNR < 12 dB. |
| `alpha_low` | 0.75 | Smoothing alpha for 7 <= PNR < 9 dB. |

### Algorithm Stages

Initialization precomputes shifted-spectrum bin pairs in the HR band. For each
positive-frequency bin `i`, it stores the mirror negative-frequency bin `j`.

Each valid frame runs:

1. Folded spectrum:

```text
S[n] = M[i]^2 + M[j]^2
```

2. Local peak extraction:
   - Find local maxima in the folded HR band.
   - Reject peaks below the HR-band median.
   - Keep the top `K` peaks.
   - Apply parabolic sub-bin interpolation.
   - Convert interpolated frequency to raw BPM.

3. Confidence check:
   - Estimate noise floor as median folded-spectrum energy, excluding bins
     around candidates.
   - Compute PNR in dB.
   - Compute linear prominence.
   - Mark candidates valid only if both gates pass.

4. Search/lock state machine:
   - In `RADAR_HR_UNLOCKED`, pick the highest-PNR valid candidate and push it
     into a 3-sample median buffer.
   - After `N_lock` consecutive valid frames, transition to
     `RADAR_HR_LOCKED`.
   - While unlocked, output remains invalid.

5. Locked candidate selection:
   - Consider only confidence-valid candidates.
   - Reject candidates more than `jump_limit` BPM away from the current
     reference.
   - Pick the highest-PNR remaining candidate.

6. Output smoothing:
   - Push accepted raw BPM into the median buffer.
   - Use the median as the short-term BPM.
   - Choose an IIR alpha based on PNR tier.
   - Update `smoothed_bpm` and `ref_bpm`.
   - Emit valid BPM only while locked and accepted.

If no candidate passes confidence, or if all locked candidates fail the jump
gate, the frame is invalid. Invalid frames age the state machine. Enough
invalid frames in locked state reset the estimator back to search.

## Radar Filter Effects

The user button cycles through five preconfigured filter modes:

1. `BYPASS`
2. `LOWPASS`
3. `HIGHPASS`
4. `BANDPASS`
5. `NOTCH`

`filters.c` implements a direct-form II transposed biquad using RBJ cookbook
coefficient formulas. `main.c` configures one filter bank for I and one for Q
with:

- Sample rate: `RADAR_SAMPLE_RATE_HZ` = 100 Hz.
- Center/cutoff frequency: 2.0 Hz.
- Q: 0.707.

The selected filter is applied when preparing the I/Q processing window, not in
the DMA ISR. The filter state used for a window is copied and reset before
processing, so filtering is applied consistently to the displayed/FFT window
without accumulating state across windows.

The displacement-based radar HR estimator is fed from the raw phase path, not
from this optional I/Q display filter path.

## EKG Acquisition And Processing

### Hardware Setup

`ekg.c` is written specifically for STM32F429 and checks for `STM32F429xx`.
The AD8232 OUT signal is expected on:

- PF6
- ADC3 regular channel 4

The code configures PF6 as analog input with no pull-up/down, enables ADC3, and
enables ADC end-of-conversion and overrun interrupts.

The EKG sample timer is TIM3. Its update interrupt starts each ADC3 conversion
in software.

### EKG Sampling Configuration

`EKG_CONFIG_DEFAULT` is 250 Hz with optional digital high-pass and low-pass
disabled. `main.c` overrides this with a tuned configuration:

| Field | Value |
| --- | ---: |
| `sample_rate_hz` | 100 |
| `highpass_hz` | 0.5 |
| `lowpass_hz` | 40.0 |
| `envelope_hz` | 8.0 |
| `refractory_s` | 0.30 |
| `min_rr_s` | 0.45 |
| `max_rr_s` | 2.0 |

The header notes that the AD8232 analog front end is normally the main ECG
waveform-shaping filter. In this application, the tuned config also enables
digital cleanup at 0.5 Hz high-pass and 40 Hz low-pass.

### EKG Interrupt Queue

The EKG acquisition chain is:

```text
TIM3 update IRQ
    -> start ADC3 conversion
ADC IRQ on EOC
    -> read ADC3->DR
    -> push raw sample and sequence number into 64-entry ring queue
main loop
    -> ekg_process_if_ready()
    -> process one queued sample
```

The queue size is 64 samples. At the active 100 Hz sample rate, the queue holds
about 640 ms of samples before old entries are overwritten. If the queue fills,
the ADC ISR advances the tail and increments `g_irq_overrun_count`.

`ekg_process_if_ready()` uses the stored sequence number to derive the logical
sample index relative to the current processing origin. This keeps detector
timing tied to the ISR sample sequence rather than to foreground loop timing.

### EKG Signal Processing

For each raw sample:

1. Convert raw ADC count to voltage:

```text
voltage = raw * 3.3 / 4095
```

2. Apply optional first-order high-pass cleanup:

```text
y_hp[n] = alpha_hp * (y_hp[n-1] + x[n] - x[n-1])
```

   If disabled, the code subtracts mid-supply (`1.65 V`) instead.

3. Apply optional first-order low-pass cleanup:

```text
y_lp[n] = y_lp[n-1] + alpha_lp * (x[n] - y_lp[n-1])
```

4. Emphasize QRS activity with a squared derivative:

```text
diff = bandpassed[n] - bandpassed[n-1]
sq = diff * diff
```

5. Smooth that squared derivative with a first-order low-pass envelope.

6. During the first 2 seconds, learn baseline noise and hold off normal peak
   detection.

7. After warmup, compare the envelope to an adaptive threshold:

```text
threshold = noise_level + 0.25 * (signal_level - noise_level)
```

8. When the envelope is above threshold, maintain a candidate peak.

9. When the envelope falls below threshold, evaluate the candidate:
   - Reject if inside the refractory period.
   - Reject if outside the configured RR interval range.
   - Accept as an R-peak otherwise.

10. For accepted peaks after the first accepted peak, compute instant BPM from
    the RR interval:

```text
instant_bpm = 60 * sample_rate_hz / rr_samples
```

11. Smooth EKG BPM:

```text
latest_bpm = 0.80 * latest_bpm + 0.20 * instant_bpm
```

The public output `ekg_output_t` contains:

- Raw ADC value.
- Raw voltage.
- Bandpassed/cleaned signal.
- Detector envelope.
- Current adaptive threshold.
- One-sample `r_peak` flag.
- BPM.
- BPM valid flag.

The first accepted R-peak does not produce a valid BPM because there is no
previous accepted peak to form an RR interval. BPM becomes valid after a later
accepted peak.

## Display And User Interface

The application uses the STM32F429I Discovery LCD BSP on the foreground layer.
`display.c` owns drawing primitives and full menu-page rendering.

### Menu Bar

`menu.c` implements a bottom menu bar:

- Total entries: 9.
- Visible slots: 5.
- Slots 0 and 4 are left/right scroll arrows.
- Slots 1, 2, and 3 are content entries.
- Scroll debounce: 200 ms.
- Menu height: 40 pixels.

The application uses polling, not touch IRQs. `MENU_check_transition()` reads
touch state and maps touches in the menu bar to scroll or menu selection.

Content menu selection uses a double-tap style: the same content item must be
touched twice before `MENU_transition` is set. Scroll arrows use touch-down edge
detection plus the debounce timer.

`MENU_get_transition()` clears the pending transition when read. For real menu
items, it also updates `MENU_active`. Scroll transitions do not change the
active content page.

### Menu Pages

| Entry | Label | Page |
| --- | --- | --- |
| `MENU_ZERO` | Info Screen | Project info and version text. |
| `MENU_ONE` | Time Signal | Recent I/Q time-domain curves. |
| `MENU_TWO` | FFT Spectr | Centered I/Q spectrum from -4 Hz to +4 Hz. |
| `MENU_THREE` | Effect Menu | Current radar filter selection. |
| `MENU_FOUR` | Peak Detect | Positive and negative dominant I/Q peak frequencies. |
| `MENU_FIVE` | Radar BPM | Radar BPM, lock/search state, phase quality, DMA counters. |
| `MENU_SIX` | Log Data | OpenLog START/STOP controls and drop counter. |
| `MENU_SEVEN` | EKG BPM | EKG BPM, raw ADC, R-peak indicator, EKG overrun counter. |
| `MENU_EIGHT` | DAC PA5 | PA5 DAC voltage slider and +/- buttons. |

`disp_menu_render()` uses per-page loop counters to avoid redrawing slower
pages every foreground pass. The time signal and FFT pages redraw on every
display refresh request, while most status pages refresh every few requests
unless forced.

### Display Data Boundary

`main.c` collects all render inputs into `disp_menu_data_t` before calling
`disp_menu_render()`. This keeps display code mostly read-only with respect to
the application model. The display still calls a few getters such as
`ekg_get_overrun_count()` and `dac_output_get_voltage()`.

## OpenLog CSV Logging

`openlog_uart.c` provides best-effort TX-only logging over USART6:

- Pin: PG14
- Alternate function: AF8
- Baud: 9600
- UART mode: TX only
- Format: 8-N-1
- Logging default: OFF

The logger page in the UI toggles sessions:

- START:
  - Sends OpenLog escape character 26 three times.
  - Enters command mode.
  - Opens/appends `LOG0001.CSV`, `LOG0002.CSV`, and so on.
  - Writes a CSV header.
  - Enables row logging.
- STOP:
  - Enters command mode if needed.
  - Sends `sync`.
  - Disables row logging.

Rows are written once per processed radar frame, not once per raw sample. The
CSV fields are:

```text
tick_ms,bpm,valid,state,phase_flags,clip_count,low_signal_count,
mean_radius_counts,radar_overruns,dma_errors
```

BPM is formatted as an integer to avoid pulling in floating-point printf
support. If `HAL_UART_Transmit()` fails, times out, or the formatted row does
not fit, the logger increments a cumulative drop counter.

## DAC Output

`dac_output.c` controls DAC channel 2 on PA5:

- GPIOA clock enabled.
- PA5 configured as analog.
- DAC peripheral clock enabled.
- DAC channel 2 enabled.
- Output value stored in `DAC->DHR12R2`.

The API accepts a voltage and clamps it into the 0.0 V to 3.3 V range. The code
converts to a 12-bit value using `DAC_OUTPUT_MAX_CODE = 4095`.

The DAC menu lets the user:

- Tap a horizontal slider to set an absolute voltage.
- Tap `-` or `+` buttons to step by 0.1 V.

The display shows the current voltage and raw DAC code.

## Shared ADC/Common Register Considerations

Radar and EKG both touch the STM32 ADC common register `ADC->CCR`.

- `radar.c` clears and configures `ADC->CCR` for ADC1/ADC2 dual simultaneous
  mode, DMA mode 2, continuous DMA requests, and PCLK2/8 ADC prescaler.
- `ekg.c`, initialized after radar start in `main.c`, clears and rewrites only
  the ADC prescaler bits to PCLK2/8. It leaves the radar multimode and DMA bits
  intact.

With the current initialization order, both modules end with the same ADC
prescaler and radar's dual-mode bits remain configured. Any later changes to
ADC common configuration should be made carefully because ADC1/ADC2 radar and
ADC3 EKG share that register.

## Error Handling And Diagnostics

`error_handling()` in `main.c` spins forever if a HAL status is not `HAL_OK`.
It is used after radar and FFT initialization.

Counters exposed to the UI and logger:

- Radar DMA sequence count through `radar_get_dma_sequence()`.
- Radar foreground overrun count through `radar_get_overrun_count()`.
- Radar DMA error flag count through `radar_get_dma_error_count()`.
- EKG queue/ADC overrun count through `ekg_get_overrun_count()`.
- OpenLog TX drop count through `openlog_get_drop_count()`.

LED use:

- LED3 is initialized as a general status LED but is not actively used in
  `main.c`.
- LED4 toggles when the foreground consumes a completed radar DMA chunk.

Debug console:

- `_write()` sends characters through ITM using `ITM_SendChar()`.
- Minimal weak syscall stubs are provided to prevent build errors.

## Timing And Throughput Notes

Radar timing:

- 100 Hz I/Q sample pairs.
- 256-sample acquisition chunks every 2.56 seconds.
- 512-sample processing windows every 2.56 seconds after initial fill.
- DMA ping/pong buffering protects acquisition from foreground processing, but
  only one completed chunk flag is retained. If the foreground does not consume
  a chunk before the next completion, `radar_overrun_count` increments.

EKG timing:

- Active sample rate is 100 Hz from `main.c`.
- ADC conversions are interrupt-driven.
- Queue depth is 64 samples, about 640 ms at 100 Hz.
- Long foreground blocks can cause EKG queue overruns.

Foreground blocking points:

- LCD clears call `HAL_Delay(2)`.
- OpenLog command transitions use `HAL_Delay()` guard and command delays.
- UART transmit is blocking with `OPENLOG_TX_TIMEOUT_MS = 200`.

Because acquisition is interrupt-driven, short display delays are tolerable, but
long UI/logging delays can still increase radar or EKG overrun counters.

## What Is Not Implemented

The source tree does not show:

- A combined radar/EKG fusion algorithm.
- Multi-lead or multi-channel EKG acquisition.
- A sensor driver that controls the radar module over SPI/I2C/UART. The radar
  interface is analog I/Q into ADCs.
- Medical-device compliance logic, alarms, patient records, or calibration
  workflows.
- RTOS tasks or scheduler objects.

## End-To-End Signal Paths

### Radar BPM Path

```text
PC1 ADC1 I + PC3 ADC2 Q
    -> TIM2-triggered simultaneous conversion at 100 Hz
    -> ADC common CDR packed sample pairs
    -> DMA2 Stream0 ping/pong buffers
    -> foreground unpack to I/Q chunk arrays
    -> phase unwrap and displacement chunk
    -> rolling 512-sample displacement window
    -> Hann + real FFT + centered magnitude spectrum
    -> folded-spectrum HR estimator
    -> radar BPM/status
    -> LCD Radar BPM page and optional OpenLog CSV row
```

### Radar Display Spectrum Path

```text
PC1 ADC1 I + PC3 ADC2 Q
    -> same DMA acquisition path
    -> rolling 512-sample I/Q windows
    -> optional selected biquad filter for I/Q display processing
    -> Hann + complex FFT + centered magnitude spectrum
    -> FFT spectrum menu and peak frequency menu
```

### EKG BPM Path

```text
PF6 ADC3_IN4
    -> TIM3 update IRQ at 100 Hz
    -> ADC3 software conversion
    -> ADC IRQ queue push
    -> foreground queue pop
    -> voltage conversion
    -> optional HP/LP cleanup
    -> squared derivative envelope
    -> adaptive threshold and R-peak candidate logic
    -> RR interval BPM smoothing
    -> LCD EKG BPM page
```

### User Control Path

```text
Touchscreen polling
    -> menu scroll or active page selection
    -> active page-specific touch handling
        -> OpenLog START/STOP on MENU_SIX
        -> DAC slider/buttons on MENU_EIGHT

User button EXTI0
    -> one-shot PB_pressed flag
    -> foreground cycles radar filter mode
    -> effect menu refresh
```

