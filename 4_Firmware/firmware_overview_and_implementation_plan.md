# Firmware System Overview and Implementation Roadmap
### Contactless Heart-Rate Radar Prototype

---

## 1. Project goal

Build a working embedded prototype on STM32F429 Discovery that measures heart rate
from radar I/Q signals, shows the result on the LCD, and is controlled through the
touchscreen.

**Milestone 1**
- radar only
- single-shot measurement flow
- LCD result display
- no OpenLog yet
- no ECG processing yet

**Later milestones**
- continuous radar measurement
- ECG reference acquisition and comparison
- OpenLog result logging
- optional robustness improvements

---

## 2. Hardware summary

This section is the consolidated hardware reference for firmware work.

| Item | Value / role | Notes |
|---|---|---|
| MCU board | STM32F429 Discovery | Main development platform |
| LCD | 240 × 320 px | Layout target for UI screens |
| Main sensor | Radar module with conditioned I and Q outputs | Primary HR source |
| Radar analog conditioning | ~80 dB gain, analog band-pass ~0.1 Hz to 10 Hz | Implemented on custom PCB |
| Radar signal DC level | About 2.25–2.5 V | Must be removed in firmware before analysis |
| Radar I channel | PC1 = ADC123_IN11 | Fixed as I |
| Radar Q channel | PC3 = ADC123_IN13 | Fixed as Q |
| Recommended ADC mapping | ADC1 → PC1 (I), ADC2 → PC3 (Q) | Chosen to remove ambiguity in unpacking and FFT preparation |
| Preferred radar ADC mode | ADC1 + ADC2 dual simultaneous | Best fit for true I/Q phase consistency |
| Optional reference sensor | ECG conditioned analog signal | Later comparison only |
| ECG input | PF6 = ADC3_IN4 | Optional later feature |
| UI | LCD + touchscreen | Main user interaction |
| Extra control | Blue user pushbutton on PA0 | Fallback/manual action |
| Logging (later) | PG14 = USART6_TX to OpenLog | TX-only, not part of milestone 1 |
| Board-specific caveat | Discovery-board gyro conflict around PC1 | Keep `gyro_disable()` logic |

The current project already initializes LCD, touchscreen, LEDs, and the user button
in `main.c`, and `main.h` currently defines `EVAL_REV_E` and `FLIPPED_LCD`. The
board-specific gyro-disable workaround is also already part of startup.

### Milestone-1 acquisition choice

For the radar path, the recommended first architecture is:
- ADC1 on PC1 = I
- ADC2 on PC3 = Q
- dual simultaneous mode
- timer-triggered
- DMA transfer of packed 32-bit dual-ADC samples

This matches the existing dual-ADC demo concept and the FFT note, which explicitly
describes packed dual-ADC data and conversion into complex FFT input. In the packed
register, `ADC_CDR[31:0] = ADC2_DR[15:0] | ADC1_DR[15:0]`, so with the mapping
above the lower 16 bits are I and the upper 16 bits are Q.

---

## 3. Existing project shape and refactor strategy

Keep the current CubeIDE layout unchanged:

```
Root/
   Core/
      Inc/
      Src/
      Startup/
   Debug/
   Drivers/
      BSP/
      CMSIS/
      STM32F4xx_HAL_Driver/
   Utilities/
      Fonts/
   .project
```

- `Core/Inc` stays the place for headers
- `Core/Src` stays the place for source files
- no large-scale folder reorganization

**Existing files that remain central**
- `main.c` / `main.h`
- `measuring.c` / `measuring.h`
- `menu.c` / `menu.h`
- `pushbutton.c` / `pushbutton.h`

**Focused additions for milestone 1**
- `fft.c` / `fft.h`
- `hr.c` / `hr.h`

**Later additions only**
- `log.c` / `log.h`
- `ecg.c` / `ecg.h`

This follows the project note while staying close to the current structure and naming
style.

---

## 4. Milestone-1 decisions: fixed vs tunable

### 4.1 Fixed for milestone 1

These should not be revisited unless a concrete hardware problem appears:
- use the current CubeIDE project structure
- keep `main`, `measuring`, `menu`, and `pushbutton`
- add `fft` and `hr`
- use radar-only
- use single-shot user flow: Start → Measuring… → Result
- use ADC1 + ADC2 dual simultaneous mode
- use ADC1 → I (PC1) and ADC2 → Q (PC3)
- use timer-triggered acquisition + DMA
- use CMSIS complex FFT
- keep touchscreen handling by polling, not by interrupt
- keep the board-specific `gyro_disable()` step before analog use of PC1

The touchscreen polling choice is supported by the current menu code, which explicitly
notes timing issues when touchscreen interrupt mode is enabled.

### 4.2 Tunable later

These are starting values, not permanent truth:
- sample rate
- measurement window length
- FFT size
- exact HR search range
- result smoothing strategy
- whether to zero-pad
- whether to display radar and ECG on separate or combined screens
- logging format and logging mechanism

**Recommended starting values**

| Parameter | Value |
|---|---|
| Sample rate | 100 Hz |
| Window length | 1024 samples |
| Measurement time | 10.24 s |
| FFT size | 1024 |

**HR search band**

Use a provisional search band of 0.8 Hz to 3.0 Hz, equivalent to 48 bpm to 180 bpm.

**Frequency resolution reminder**

With 1024 samples at 100 Hz:
- frequency-bin spacing ≈ 0.098 Hz
- equivalent HR spacing ≈ 5.9 bpm

That is acceptable for a first single-shot prototype.

---

## 5. Practical architecture overview

The project does not need rigid MVC enforcement. A lighter split is better:

| Module | Role |
|---|---|
| `main.*` | coordinates application flow |
| `measuring.*` | acquires and validates raw data |
| `hr.*` | converts radar data into HR |
| `fft.*` | wraps CMSIS FFT usage |
| `menu.*` | handles screen/menu interaction |
| `pushbutton.*` | handles the blue button |
| `log.*` (later) | handles OpenLog |
| `ecg.*` (later) | handles ECG reference |

This is consistent with the project note, which places acquisition in `measuring.*`,
FFT in `fft.*`, and user-input/display logic outside the model.

---

## 6. Module ownership

### 6.1 `main.*`

Owns:
- initialization order
- application state
- current screen
- current measurement mode
- transitions between states
- start/stop coordination
- reacting to frame-ready and result-ready events
- storing the last result for the Result screen

`main.c` already acts as the coordinator; it just needs to become product-oriented
instead of demo-oriented.

### 6.2 `measuring.*`

Owns:
- GPIO analog configuration
- timer setup
- ADC setup
- DMA setup
- acquisition start/stop
- raw sample buffers
- unpacking of dual-ADC packed samples
- frame-ready flag
- frame metadata
- clipping / acquisition-quality checks
- overrun detection

**Explicit ownership**

Clip detection belongs in `measuring.*`. Clipping is an acquisition-quality problem
and should be attached to the frame before processing starts.

**Recommended first clip rule**

Set a channel clip flag if any 12-bit raw sample is near either rail:
- `sample <= 8`
- or `sample >= 4087`

The exact thresholds can be tuned later.

**`dma_overrun` rule**

`dma_overrun` is set `true` if a newly completed DMA frame would overwrite a frame
that the application has not yet consumed. A practical first rule is:
- keep an internal "frame pending / not yet consumed" condition
- if the DMA transfer-complete handler fires while that condition is still true,
  set `dma_overrun = true`

In milestone-1 single-shot mode this flag should normally stay `false`, but keeping
it in the shared frame struct means the same struct can be reused for continuous mode
later. The current project already uses the global readiness flag `MEAS_data_ready`,
so this concept fits naturally into the existing style.

### 6.3 `hr.*`

Owns:
- conversion of raw ADC counts to float
- per-window offset removal
- optional normalization
- building the complex I/Q FFT input buffer
- HR-band spectral analysis
- final BPM calculation
- returning one result structure

**Explicit ownership**

Offset removal belongs in `hr.*`. `measuring.*` should stay close to raw acquisition,
while `hr.*` owns the conversion from raw samples into analysis-ready radar data.

### 6.4 `fft.*`

Owns:
- CMSIS FFT instance initialization
- FFT execution wrapper
- magnitude computation
- helper functions for spectral peak search

**Important hard-fault rule**

For the complex CMSIS FFT, the input/output buffer must be allocated as:

```c
float32_t cfft_inout[2 * FFT_SIZE];
```

That is 2 × N floats, because real and imaginary parts are interleaved. Using only
`FFT_SIZE` floats is a known way to trigger a hard fault when calling
`arm_cfft_f32()`. The FFT note states both the required 2·N buffer length and the
hard-fault risk explicitly.

### 6.5 `menu.*`

Owns:
- menu drawing
- touch polling
- transition detection
- basic navigation support

The current menu module is reusable, but its text and menu semantics must change from
ADC demos to product actions. The project currently uses a six-entry menu model via
`MENU_ENTRY_COUNT`.

### 6.6 `pushbutton.*`

Owns:
- button initialization
- IRQ setup
- debounced pressed flag

In the current code the blue button toggles DAC behavior; in the product firmware it
should become a fallback action such as start measurement, cancel measurement, or
return to menu. The current pushbutton module already uses a simple
interrupt-plus-flag design and identifies the USER button as PA0.

### 6.7 `log.*` (later)

Owns:
- USART6 TX initialization on PG14
- formatting final result + metadata
- best-effort transmit

For sparse result logging, interrupt-driven UART transmit is the simplest good
starting point.

### 6.8 `ecg.*` (later)

Owns:
- optional ECG acquisition
- ECG HR extraction or reference handling
- comparison with radar HR

ECG stays separate because it is a validation path, not the primary measurement path.

---

## 7. Core shared data structures

These are conceptual interface anchors so that `main`, `measuring`, `hr`, and later
`log` do not invent incompatible formats.

**Flag-type convention**

Use `bool` from `<stdbool.h>` consistently for shared flags such as `valid`,
`clip_i`, `clip_q`, `dma_overrun`, and `last_result_available`. This matches the
current project headers, which already use `bool` for shared state such as
`MEAS_data_ready`, `DAC_active`, and the pushbutton API.

### 7.1 Radar frame structure

```c
typedef struct {
    uint16_t raw_i[MEAS_FRAME_LEN];
    uint16_t raw_q[MEAS_FRAME_LEN];
    uint16_t sample_count;       // valid samples per channel
    uint16_t sample_rate_hz;     // e.g. 100
    uint32_t frame_id;           // incrementing frame counter
    bool     clip_i;             // true if I channel clipped
    bool     clip_q;             // true if Q channel clipped
    bool     dma_overrun;        // true if previous frame was not consumed
} MEAS_RadarFrame_t;
```

Why this shape is recommended:
- simple to understand
- raw I and Q remain clearly separated
- metadata and quality flags travel with the frame
- easy to hand into `hr.*`

For milestone 1, this is preferable to exposing only a packed DMA buffer outside
`measuring.*`.

### 7.2 HR result structure

```c
typedef struct {
    float    bpm;                // final heart-rate estimate
    float    peak_hz;            // dominant spectral peak in HR band
    uint32_t frame_id;           // source frame identifier
    bool     valid;              // true = valid result
    bool     clipped;            // true if source frame had clip_i or clip_q
} HR_Result_t;
```

- `valid` is enough for milestone 1; a confidence score can be added later
- `clipped` helps the UI and later logging explain suspicious results
- `peak_hz` is useful during debugging and later result review

These definitions are not intended as final compiled code yet, but they should be
treated as the interface anchor when creating `measuring.h` and `hr.h`.

---

## 8. Signal path and data flow

### 8.1 Radar path

1. timer triggers acquisition
2. ADC1 samples I on PC1
3. ADC2 samples Q on PC3
4. DMA transfers packed 32-bit dual-ADC words
5. `measuring.*` unpacks them into `raw_i[]` and `raw_q[]`
6. `measuring.*` checks clipping and overrun flags
7. `hr.*` converts raw data to float
8. `hr.*` subtracts the mean of each channel
9. `hr.*` builds complex interleaved FFT input: `[I0, Q0, I1, Q1, ...]`
10. `fft.*` computes the spectrum
11. `hr.*` searches the peak within the HR band
12. `main.*` stores and displays the result

This is aligned with both the project note and the FFT guidance for complex I/Q
processing.

### 8.2 Raw-to-analysis conversion split

**In `measuring.*`**
- packed DMA buffer acquired
- unpack into integer arrays using the packed register layout
  `ADC_CDR[31:0] = ADC2_DR[15:0] | ADC1_DR[15:0]`:

```c
raw_i[n] = (uint16_t)( packed_sample        & 0x0FFF);  // ADC1, lower 16 bits = I
raw_q[n] = (uint16_t)((packed_sample >> 16) & 0x0FFF);  // ADC2, upper 16 bits = Q
```

The 12-bit mask `0x0FFF` is required. Without it, alignment bits from the ADC data
register are included in the values passed to the clip check and float conversion,
producing subtly wrong results rather than an obvious failure.

- fill `MEAS_RadarFrame_t`
- set clip and overrun flags
- raise frame-ready flag

**In `hr.*`**
- cast integer arrays to float
- compute `mean(I)` and `mean(Q)`
- subtract means
- optionally normalize
- build complex FFT input buffer
- compute spectral estimate
- output `HR_Result_t`

This separation removes ambiguity about where offset removal happens.

---

## 9. Runtime states and transitions

### 9.1 Milestone-1 states

| State | Meaning |
|---|---|
| `BOOT` | System initialization in progress |
| `IDLE` | Main menu visible, waiting for user input |
| `MEASURING` | DMA acquisition active, screen shows "Measuring…" |
| `PROCESSING` | Frame complete, HR extraction running |
| `RESULT` | HR result displayed on screen |
| `ERROR` | Unrecoverable fault |

### 9.2 Transition model

```
BOOT        -> IDLE         on initialization complete

IDLE        -> MEASURING    on "Measure" menu action or button fallback
MEASURING   -> PROCESSING   when DMA frame is complete
MEASURING   -> IDLE         on cancel/back action
PROCESSING  -> RESULT       when HR_Result_t is available
PROCESSING  -> ERROR        on processing timeout (e.g. 30 s watchdog)
RESULT      -> IDLE         on touch/back/restart

Any state   -> ERROR        on unrecoverable initialization or acquisition fault
ERROR       -> IDLE         on retry/reset action
```

The `PROCESSING -> ERROR` timeout path is important: if a bug in `hr.*` or `fft.*`
causes `HR_Result_t` to never be produced, the device would otherwise be permanently
stuck in PROCESSING with "Measuring…" on screen and no way out except a hardware
reset. A simple elapsed-time check in the main loop is sufficient for milestone 1.

### 9.3 PROCESSING duration

At 1024 samples, the CMSIS FFT and HR extraction will complete in well under a
second on this MCU. For milestone 1, HR processing can therefore run to completion
in the main loop immediately after the frame-ready flag is detected. Non-blocking or
deferred processing is not required at this stage.

### 9.4 Later state extensions

```
IDLE            -> CONTINUOUS
CONTINUOUS      -> CONTINUOUS   repeated acquire/process/display loop
CONTINUOUS      -> IDLE         on stop action

IDLE            -> ECG_REFERENCE
ECG_REFERENCE   -> RESULT / COMPARISON
```

---

## 10. Screen and menu concept

The current menu system is fixed at six top-level entries, so the milestone-1 UI
should keep six entries as well.

### 10.1 Suggested menu entries

| Entry | Milestone-1 behavior |
|---|---|
| Measure | Starts single-shot radar measurement flow |
| Result | Shows last stored `HR_Result_t`; shows "No result yet" if none |
| Continuous | Placeholder — shows "Not implemented yet" |
| ECG Ref | Placeholder — shows "Not implemented yet" |
| Settings | Reserved for later options |
| About | Static project information screen |

**Result entry clarification:** Result is a last-result screen, not a second way to
trigger measurement. This removes any ambiguity about its role.

**Placeholder entries:** non-functional entries should show a brief message and
return to the menu on touch, so that the menu never presents a silent dead-end during
a demo or review.

### 10.2 Measuring screen

Show:
- `Radar measurement`
- `Measuring…`

The "Measuring…" screen can remain visible through the PROCESSING state for
simplicity. At the expected processing time, the user will not notice the transition.

### 10.3 Result screen

Show:
- large radar HR value in bpm
- `Valid result` or `No valid result`
- simple return hint, e.g. `Touch to return`

### 10.4 Later screens

- ECG reference screen
- radar vs ECG comparison screen

---

## 11. Unified implementation roadmap

### Step 1 — Align HAL configuration

Update `stm32f4xx_hal_conf.h` to enable the modules actually needed now:
- ADC
- TIM
- UART (later)
- DAC only if still temporarily needed during migration

Right now ADC, TIM, and UART are commented out.

### Step 2 — Convert the UI shell from demo to product

In `menu.c` and `main.c`:
- replace ADC demo labels and startup text
- remove demo wording such as `Touch a menu item to start an ADC demo`
- stop routing menu items into unrelated ADC experiments
- keep LCD init, touchscreen init, and polling-based interaction

The current `menu.c` still contains ADC-demo labels, so this change should happen
early.

### Step 3 — Remove DAC-demo semantics from product flow

In `main.c` and `pushbutton.c`:
- stop using the blue button to toggle DAC demo behavior
- repurpose it as fallback start/cancel/back input

The current `main.c` still toggles `DAC_active` from the button path.

### Step 4 — Define shared frame/result interfaces

Before deeper refactoring, add the project-level interface anchors to headers:
- `MEAS_RadarFrame_t`
- `HR_Result_t`

This avoids incompatible representations appearing independently in `main`,
`measuring`, `hr`, and later `log`.

### Step 5 — Rework `measuring.*` around the real radar path

Keep:
- timer-driven acquisition concept
- DMA use
- dual-ADC concept
- completion interrupt pattern

Change:
- map channels to the real radar inputs (ADC1 → PC1 = I, ADC2 → PC3 = Q)
- use ADC1 + ADC2 dual simultaneous mode
- unpack packed ADC data into the frame struct using the 12-bit mask rule from §8.2
- add clipping and overrun flags
- stop treating display as a responsibility of `measuring.*`

This is consistent with the project note, which places sampling and converted sample
storage in `measuring.*`. `measuring.c` itself also states that displaying should be
moved to a separate file in the final version.

### Step 6 — Add `fft.*`

Implement:
- CMSIS complex FFT init
- FFT execution wrapper
- magnitude helper
- peak-search helper

Use the FFT note directly for buffer layout and initialization shape. Remember the
`2 * FFT_SIZE` float buffer rule from §6.4.

### Step 7 — Add `hr.*`

Implement:
- raw I/Q to float conversion
- offset subtraction
- optional normalization
- FFT input preparation
- HR-band search
- BPM conversion
- `HR_Result_t` output

This is the core milestone-1 algorithm layer.

### Step 8 — Wire up the single-shot flow in `main.c`

Target flow:

```
IDLE
 -> start command
 -> MEAS_start_radar_single()
 -> wait for frame-ready flag
 -> HR_process_radar_frame()   // runs to completion in main loop
 -> store last result
 -> show result screen
 -> return to IDLE on user action
```

Include a processing timeout check covering the `PROCESSING` state as described in
§9.2.

### Step 9 — Verify on-screen milestone

The first real milestone is reached when:
- the user can start a radar measurement from the menu
- the device shows "Measuring…"
- one `HR_Result_t` is produced
- the result is shown on the LCD
- the Result menu entry can re-display the same last result

### Step 10 — Clean up interrupt declarations

`stm32f4xx_it.h` currently declares only the core exception handlers. That is
acceptable temporarily because the current template places peripheral IRQ handlers
directly in feature modules such as `pushbutton.c`, `menu.c`, and `measuring.c`.

Once the radar acquisition path stabilizes, add declarations for the project-used
peripheral IRQ handlers to `stm32f4xx_it.h`:
- DMA completion IRQ handler(s)
- ADC-related IRQ handler(s), if used
- any timer IRQ handler kept in the final design

### Step 11 — Later extensions

Only after the above works:
- add continuous mode
- add ECG reference path
- add OpenLog result logging

---

## 12. Main technical risks and containment

| Risk | Containment |
|---|---|
| PC1 board conflict | Keep `gyro_disable()` exactly as a board-specific startup step before analog use of PC1. The current code already relies on that. |
| Touchscreen interrupt mode | Use polling first. The current menu code explicitly notes timing issues with touchscreen interrupts. |
| Wrong FFT buffer sizing | Do not allocate only `FFT_SIZE` floats for the complex FFT input buffer. Use `2 * FFT_SIZE` floats. Getting this wrong causes a hard fault. |
| Wrong FFT input data | Do not run FFT on raw DC-biased ADC counts. Offset subtraction in `hr.*` is mandatory. |
| Wrong I/Q assignment | PC1 is I, PC3 is Q. ADC1 maps to I, ADC2 maps to Q. If this mapping ever changes, the unpacking and complex-buffer fill order must be adjusted accordingly. |
| Wrong unpack masking | Apply the `0x0FFF` mask when unpacking both channels. See §8.2. |
| Misleading demo constants | Do not treat the current demo sampling constants as final HR settings. The existing code is explicitly demo-oriented. |
| Wrong public API naming | Do not keep demo-style function names tied to old ADC examples. Public interfaces should reflect real project roles. |
| Stuck PROCESSING state | Implement a processing timeout as described in §9.2 to ensure the device can always recover to `ERROR` and then `IDLE`. |

---

## 13. Best current recommendation

The best next implementation direction is:
- keep the current CubeIDE structure
- preserve `main`, `menu`, `pushbutton`, and `measuring`
- refactor them from demo semantics to product semantics
- add only `hr` and `fft` for milestone 1
- introduce shared `MEAS_RadarFrame_t` and `HR_Result_t`
- implement radar-only single-shot end to end
- postpone ECG and OpenLog until after the first stable radar result appears on the LCD

That gives the best balance between minimal disruption, clear module ownership,
professor reviewability, and fast progress toward a live demo.
