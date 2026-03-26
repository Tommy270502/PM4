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

| Item                      | Value / role                                   | Notes                                                                               |
| ------------------------- | ---------------------------------------------- | ----------------------------------------------------------------------------------- |
| MCU board                 | STM32F429 Discovery                            | Main development platform                                                           |
| LCD                       | 240 × 320 px                                   | Layout target for UI screens                                                        |
| Main sensor               | Radar module with conditioned I and Q outputs  | Primary HR source                                                                   |
| Radar analog conditioning | ~80 dB gain, analog band-pass ~0.1 Hz to 10 Hz | Implemented on custom PCB                                                           |
| Radar signal DC level     | About 2.25–2.5 V                               | Must be removed in firmware before analysis                                         |
| Radar I channel           | PC1 = ADC123_IN11                              | Fixed in hardware                                                                   |
| Radar Q channel           | PC3 = ADC123_IN13                              | Fixed in hardware                                                                   |
| Preferred radar ADC mode  | ADC1 + ADC2 dual simultaneous                  | Best fit for true I/Q phase consistency                                             |
| ADC-to-channel mapping    | Implementation choice                          | The code and document must state explicitly which ADC samples I and which samples Q |
| Optional reference sensor | ECG conditioned analog signal                  | Later comparison only                                                               |
| ECG input                 | PF6 = ADC3_IN4                                 | Fixed hardware choice for the ECG path                                              |
| UI                        | LCD + touchscreen                              | Main user interaction                                                               |
| Extra control             | Blue user pushbutton on PA0                    | Fallback/manual action                                                              |
| Logging (later)           | PG14 = USART6_TX to OpenLog                    | TX-only, not part of milestone 1                                                    |
| Board-specific caveat     | Discovery-board gyro conflict around PC1       | Keep `gyro_disable()` logic                                                         |

### Important mapping rule

The **hardware truth** is:

- PC1 is **I**
- PC3 is **Q**

The exact ADC numbering is **not** the hardware truth and is therefore not the
primary architectural anchor. The implementation may choose either of these:

- ADC1 = I on PC1 and ADC2 = Q on PC3
- or ADC1 = Q on PC3 and ADC2 = I on PC1

Both are acceptable **only if documented unambiguously** in all three places:

1. ADC initialization comments and function naming
2. packed dual-ADC unpacking logic
3. complex FFT input construction order

### Current code-base reality

The current code base already contains a working dual-ADC demo path and is therefore
a useful starting point for milestone 1, but it is still demo-oriented rather than
product-oriented:

- menu text still describes ADC demonstrations
- the main loop still contains demo behavior such as periodic LED toggling and a
  fixed `HAL_Delay(200)`
- the blue pushbutton still toggles DAC demo behavior
- the dual-ADC function names still reflect the original demo mapping rather than
  the product terminology
- display code is still partly embedded in `measuring.*` for demo purposes

This means the architecture itself remains valid, but the document must describe the
current code honestly as a **starting point for refactoring**, not as an already
implemented product firmware.

---

## 3. Existing project shape and refactor strategy

Keep the current CubeIDE layout unchanged:

```text
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

This stays close to the current structure and avoids unnecessary disruption.

---

## 4. Milestone-1 decisions: fixed vs tunable

### 4.1 Fixed for milestone 1

These should not be revisited unless a concrete hardware problem appears:

- use the current CubeIDE project structure
- keep `main`, `measuring`, `menu`, and `pushbutton`
- add `fft` and `hr`
- use radar-only
- use single-shot user flow: Start -> Measuring… -> Result
- use ADC1 + ADC2 dual simultaneous mode
- use timer-triggered acquisition + DMA
- use CMSIS complex FFT
- keep touchscreen handling by polling, not by interrupt
- keep the board-specific `gyro_disable()` step before analog use of PC1
- keep ECG entirely out of milestone 1
- keep PF6 reserved for the future ECG path

### 4.2 Fixed hardware truth vs implementation choice

The following distinction must remain explicit:

**Fixed hardware truth**

- PC1 is I
- PC3 is Q

**Implementation choice**

- which ADC number samples PC1
- which ADC number samples PC3

Because the current code base already contains a legacy dual-ADC demo with its own
naming, the safest documentation rule is:

> Never describe I and Q only through ADC numbers. Always describe them first
> through their physical pins, then state the chosen ADC mapping.

### 4.3 Tunable later

These are starting values, not permanent truth:

- sample rate
- measurement window length
- FFT size
- exact HR search range
- result smoothing strategy
- whether to zero-pad
- UI wording
- logging format and logging mechanism

**Recommended starting values**

| Parameter        | Value        |
| ---------------- | ------------ |
| Sample rate      | 100 Hz       |
| Window length    | 1024 samples |
| Measurement time | 10.24 s      |
| FFT size         | 1024         |

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

| Module          | Role                            |
| --------------- | ------------------------------- |
| `main.*`        | coordinates application flow    |
| `measuring.*`   | acquires and validates raw data |
| `hr.*`          | converts radar data into HR     |
| `fft.*`         | wraps CMSIS FFT usage           |
| `menu.*`        | handles screen/menu interaction |
| `pushbutton.*`  | handles the blue button         |
| `log.*` (later) | handles OpenLog                 |
| `ecg.*` (later) | handles ECG reference           |

This remains the best balance between clarity and minimal refactoring.

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

`main.c` already acts as the coordinator, but still needs to be converted from
demo-oriented behavior to product-oriented behavior.

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

**Important code-base note**

The current dual-ADC demo is a useful basis, but it must not be treated as already
validated product acquisition code. During refactoring, the radar acquisition path
must be cleaned up in at least these areas:

- function naming and comments must match the chosen I/Q mapping
- sample-time configuration must be checked and rewritten so it matches the actual
  selected channels
- packed DMA data must be unpacked with 12-bit masking
- display-specific behavior must be removed from `measuring.*`

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
later.

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
`arm_cfft_f32()`.

### 6.5 `menu.*`

Owns:

- menu drawing
- touch polling
- transition detection
- basic navigation support

The current menu module is reusable, but its text and semantics must change from ADC
demos to product actions. The six-entry top-level menu concept should remain.

### 6.6 `pushbutton.*`

Owns:

- button initialization
- IRQ setup
- a simple pressed flag

The current pushbutton module is **not yet a debounced button abstraction**.
It is currently a simple interrupt-plus-flag design and should be described that way.

For the product firmware, the blue button should become a fallback action such as:

- start measurement
- cancel measurement
- return to menu

Debounce can be added later if needed, but it should not be claimed as already
implemented.

### 6.7 `log.*` (later)

Owns:

- USART6 TX initialization on PG14
- formatting final result + metadata
- best-effort transmit

For sparse result logging, TX-only transmission remains the right starting point.

### 6.8 `ecg.*` (later)

Owns:

- optional ECG acquisition on PF6 / ADC3_IN4
- ECG HR extraction or reference handling
- comparison with radar HR

ECG stays separate because it is a validation path, not the primary measurement path.

---

## 7. Core shared data structures

These are conceptual interface anchors so that `main`, `measuring`, `hr`, and later
`log` do not invent incompatible formats.

**Flag-type convention**

Use `bool` from `<stdbool.h>` consistently for shared flags such as `valid`,
`clip_i`, `clip_q`, `dma_overrun`, and `last_result_available`.

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

- `valid` is enough for milestone 1
- `clipped` helps the UI and later logging explain suspicious results
- `peak_hz` is useful during debugging and later result review

---

## 8. Signal path and data flow

### 8.1 Radar path

1. timer triggers acquisition
2. one ADC samples PC1 = I
3. the other ADC samples PC3 = Q
4. DMA transfers packed 32-bit dual-ADC words
5. `measuring.*` unpacks them into `raw_i[]` and `raw_q[]`
6. `measuring.*` checks clipping and overrun flags
7. `hr.*` converts raw data to float
8. `hr.*` subtracts the mean of each channel
9. `hr.*` builds complex interleaved FFT input: `[I0, Q0, I1, Q1, ...]`
10. `fft.*` computes the spectrum
11. `hr.*` searches the peak within the HR band
12. `main.*` stores and displays the result

### 8.2 Required unpacking rule

In dual mode the packed register layout is:

```c
ADC_CDR[31:0] = ADC2_DR[15:0] | ADC1_DR[15:0]
```

Therefore the unpacking code must always follow the **chosen** ADC-to-channel
mapping.

Two valid examples are shown below.

**Example A: ADC1 = I, ADC2 = Q**

```c
raw_i[n] = (uint16_t)( packed_sample        & 0x0FFF);  // ADC1 -> I
raw_q[n] = (uint16_t)((packed_sample >> 16) & 0x0FFF);  // ADC2 -> Q
```

**Example B: ADC1 = Q, ADC2 = I**

```c
raw_q[n] = (uint16_t)( packed_sample        & 0x0FFF);  // ADC1 -> Q
raw_i[n] = (uint16_t)((packed_sample >> 16) & 0x0FFF);  // ADC2 -> I
```

The 12-bit mask `0x0FFF` is required. Without it, alignment bits from the ADC data
register are included in the values passed to the clip check and float conversion.

### 8.3 Raw-to-analysis conversion split

**In `measuring.*`**

- packed DMA buffer acquired
- unpack into integer arrays
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

| State        | Meaning                                           |
| ------------ | ------------------------------------------------- |
| `BOOT`       | System initialization in progress                 |
| `IDLE`       | Main menu visible, waiting for user input         |
| `MEASURING`  | DMA acquisition active, screen shows "Measuring…" |
| `PROCESSING` | Frame complete, HR extraction running             |
| `RESULT`     | HR result displayed on screen                     |
| `ERROR`      | Unrecoverable fault                               |

### 9.2 Transition model

```text
BOOT        -> IDLE         on initialization complete

IDLE        -> MEASURING    on "Measure" menu action or button fallback
MEASURING   -> PROCESSING   when DMA frame is complete
MEASURING   -> IDLE         on cancel/back action
PROCESSING  -> RESULT       when HR_Result_t is available
PROCESSING  -> ERROR        on processing timeout
RESULT      -> IDLE         on touch/back/restart

Any state   -> ERROR        on unrecoverable initialization or acquisition fault
ERROR       -> IDLE         on retry/reset action
```

### 9.3 PROCESSING duration

At 1024 samples, the CMSIS FFT and HR extraction should complete well under a second
on this MCU. For milestone 1, HR processing can therefore run to completion in the
main loop immediately after the frame-ready flag is detected.

A timeout is still recommended so the device cannot get stuck permanently in the
`PROCESSING` state.

---

## 10. Screen and menu concept

The current menu system is fixed at six top-level entries, so the milestone-1 UI
should keep six entries as well.

### 10.1 Suggested menu entries

| Entry      | Milestone-1 behavior                                           |
| ---------- | -------------------------------------------------------------- |
| Measure    | Starts single-shot radar measurement flow                      |
| Result     | Shows last stored `HR_Result_t`; shows "No result yet" if none |
| Continuous | Placeholder — shows "Not implemented yet"                      |
| ECG Ref    | Placeholder — shows "Not implemented yet"                      |
| Settings   | Reserved for later options                                     |
| About      | Static project information screen                              |

**Result entry clarification**

Result is a last-result screen, not a second way to trigger measurement.

**Placeholder entries**

Non-functional entries should show a brief message and return to the menu on touch,
so that the menu never presents a silent dead-end during a demo or review.

### 10.2 Measuring screen

Show:

- `Radar measurement`
- `Measuring…`

The same screen may remain visible during the short processing phase.

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

### Step 1 — Convert the UI shell from demo to product

In `menu.c` and `main.c`:

- replace ADC-demo labels and startup text
- remove demo wording such as `Touch a menu item to start an ADC demo`
- stop routing menu items into unrelated ADC experiments
- remove continuous demo LED behavior from the product flow
- remove the fixed `HAL_Delay(200)` from the normal application loop
- keep LCD init, touchscreen init, and polling-based interaction

### Step 2 — Remove DAC-demo semantics from the product path

In `main.c`, `measuring.c`, and `pushbutton.c`:

- stop using the blue button to toggle DAC demo behavior
- repurpose the button as fallback start/cancel/back input
- keep DAC-related code only if still needed temporarily during migration
- otherwise remove it from the milestone-1 runtime path

### Step 3 — Define shared frame/result interfaces

Before deeper refactoring, add the project-level interface anchors to headers:

- `MEAS_RadarFrame_t`
- `HR_Result_t`

This avoids incompatible representations appearing independently in `main`,
`measuring`, `hr`, and later `log`.

### Step 4 — Rework `measuring.*` around the real radar path

Keep:

- timer-driven acquisition concept
- DMA use
- dual-ADC concept
- completion interrupt pattern

Change:

- rename or clearly re-comment legacy demo functions so they match the chosen
  implementation mapping
- document explicitly which ADC number samples PC1 = I and which ADC number samples
  PC3 = Q
- correct sample-time configuration so it matches the actual selected channels
- unpack packed ADC data using the 12-bit mask rule
- add clipping and overrun flags
- stop treating display as a responsibility of `measuring.*`

### Step 5 — Add `fft.*`

Implement:

- CMSIS complex FFT init
- FFT execution wrapper
- magnitude helper
- peak-search helper

Remember the `2 * FFT_SIZE` float buffer rule.

### Step 6 — Add `hr.*`

Implement:

- raw I/Q to float conversion
- offset subtraction
- optional normalization
- FFT input preparation
- HR-band search
- BPM conversion
- `HR_Result_t` output

### Step 7 — Wire up the single-shot flow in `main.c`

Target flow:

```text
IDLE
 -> start command
 -> MEAS_start_radar_single()
 -> wait for frame-ready flag
 -> HR_process_radar_frame()
 -> store last result
 -> show result screen
 -> return to IDLE on user action
```

Include a processing timeout check covering the `PROCESSING` state.

### Step 8 — Verify on-screen milestone

The first real milestone is reached when:

- the user can start a radar measurement from the menu
- the device shows `Measuring…`
- one `HR_Result_t` is produced
- the result is shown on the LCD
- the Result menu entry can re-display the same last result

### Step 9 — Clean up comments, headers, and declarations

During or after the radar refactor, clean up the current code base so the source
matches reality:

- correct misleading comments around PF8 in `gyro_disable()`
- keep PF6 as the documented ECG input
- add missing standard integer includes in headers that expose fixed-width types
- rename public APIs away from old demo semantics where practical
- keep `stm32f4xx_it.h` minimal for now, but later add the project-used peripheral
  IRQ declarations once the final interrupt set is settled

### Step 10 — HAL configuration only where actually needed

Do **not** treat HAL module enabling as an automatic first step.

The current acquisition path is register-based, not HAL-ADC/TIM/UART based.
Therefore:

- enable HAL modules only when newly added code actually depends on them
- ADC/TIM/UART can remain disabled in `stm32f4xx_hal_conf.h` until a concrete need
  appears
- DMA / GPIO / RCC / LTDC support already used by the template should remain as-is

### Step 11 — Later extensions

Only after the above works:

- add continuous mode
- add ECG reference path on PF6
- add OpenLog result logging

---

## 12. Main technical risks and containment

| Risk                       | Containment                                                                                                                                                   |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PC1 board conflict         | Keep `gyro_disable()` exactly as a board-specific startup step before analog use of PC1. Note that current code also relies on it to free PC1 for analog use. |
| Misleading legacy comments | Correct comments and function names so they describe the real radar roles rather than the old demo names.                                                     |
| Wrong I/Q assignment       | PC1 is always I and PC3 is always Q. ADC-number mapping is secondary and must be documented explicitly.                                                       |
| Wrong unpack mapping       | The unpacking code must follow the chosen ADC-to-channel mapping exactly.                                                                                     |
| Wrong unpack masking       | Apply the `0x0FFF` mask when unpacking both channels.                                                                                                         |
| Sample-time mismatch       | Recheck sample-time register fields against the actually selected ADC channels during refactoring.                                                            |
| Touchscreen interrupt mode | Use polling first. The current code already indicates interrupt timing issues.                                                                                |
| Wrong FFT buffer sizing    | Do not allocate only `FFT_SIZE` floats for the complex FFT input buffer. Use `2 * FFT_SIZE` floats.                                                           |
| Wrong FFT input data       | Do not run FFT on raw DC-biased ADC counts. Offset subtraction in `hr.*` is mandatory.                                                                        |
| Demo-only loop behavior    | Remove fixed-delay and demo-toggle behavior from the product runtime loop.                                                                                    |
| Stuck PROCESSING state     | Implement a processing timeout so the device can recover to `ERROR` and then `IDLE`.                                                                          |

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
- keep the document anchored to physical I/Q pins first, and to ADC numbers only as
  a documented implementation detail

That gives the best balance between minimal disruption, honest alignment with the
present code base, clear module ownership, and fast progress toward a live demo.
