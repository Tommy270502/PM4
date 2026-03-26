# Firmware Overview
### Contactless Heart-Rate Radar Prototype

---

## 1. Scope

This document describes only the **milestone-1 target system**.

Milestone 1 shall provide:
- heart-rate measurement from radar signals
- user interaction via LCD and touchscreen
- result display on the screen
- a simple single-shot measurement flow

Further features such as ECG reference processing, continuous operation, and data
logging may be added later, but they are not part of milestone 1.

---

## 2. Fixed hardware decisions

The following points are fixed by the hardware and shall be treated as binding
requirements for the firmware:

| Item | Fixed decision |
|---|---|
| Main platform | STM32F429 Discovery |
| Main sensor | Radar module with conditioned analog I/Q outputs |
| Radar I channel | **PC1** |
| Radar Q channel | **PC3** |
| Reference input for later use | **PF6** |
| Logging output for later use | **PG14** as TX to OpenLog |
| User interface | LCD + touchscreen |
| Extra user input | Blue user pushbutton |
| Radar front-end behavior | Analog conditioning with strong gain and low-frequency band limitation |
| Radar signal characteristic | I/Q signals contain a DC offset that must be removed in firmware before spectral evaluation |

### Important interpretation rule

The physical signal meaning is fixed as:
- **PC1 = I**
- **PC3 = Q**

This must remain true independently of the internal firmware implementation.

---

## 3. Milestone-1 target behavior

At the end of milestone 1, the firmware shall perform the following complete user-visible function:

1. the user starts a radar measurement from the touchscreen interface / user button
2. the system acquires the radar I and Q signals
3. the system processes the acquired data
4. the system estimates heart rate from the radar signal
5. the system displays the result on the LCD
6. the system allows the user to return to the main screen

### Expected user flow

A simple target flow is:

**Menu -> Measuring -> Result -> Menu**

### Required milestone-1 behavior

- single-shot measurement only
- radar-only processing
- no ECG processing in milestone 1
- no OpenLog communication in milestone 1
- no requirement for continuous mode in milestone 1

### Result behavior

The displayed result should clearly indicate either:
- a valid heart-rate value in bpm
- or that no valid result could be determined

---

## 4. Functional signal-processing target

The milestone-1 firmware shall implement the following functional chain:

1. acquire radar I and Q signals
2. convert the samples into a form suitable for digital processing
3. remove the DC offset of both channels
4. build the complex radar signal from I and Q
5. compute the frequency spectrum
6. search the heart-rate frequency range
7. determine the dominant spectral component
8. convert that frequency into bpm
9. display the result

### Recommended initial FFT settings

These values are suitable as starting parameters and may be tuned later:

| Parameter | Recommended starting value |
|---|---|
| Sample rate | 100 Hz |
| Window length | 1024 samples |
| FFT size | 1024 |
| Measurement time | 10.24 s |
| Heart-rate search band | 0.8 Hz to 3.0 Hz |
| Heart-rate search band in bpm | 48 bpm to 180 bpm |

### Practical note

With 1024 samples at 100 Hz:
- frequency resolution is approximately 0.098 Hz
- this corresponds to about 5.9 bpm resolution

This is acceptable as a first milestone-1 starting point.

---

## 5. Functional implementation requirements

The implementation shall ensure the following, independent of the chosen code template:

### Acquisition
- both radar channels must be sampled in a time-consistent manner
- the implementation must preserve the distinction between I and Q
- the mapping of the two acquired channels must remain documented unambiguously

### Processing
- the DC component of both channels must be removed before spectral evaluation
- the I/Q pair must be interpreted as a complex signal
- the spectral analysis must search only in the defined heart-rate band
- the final output must be expressed in bpm

### User interface
- the user must be able to start a measurement
- the display must show that a measurement is in progress
- the final result must be shown clearly on the LCD
- the user must be able to return to the main screen

### Robustness
- the implementation should detect obviously invalid measurements where possible
- the system should not remain stuck permanently in a processing state
- the firmware should remain structured so later features can be added without changing the hardware assumptions

---

## 6. Later extensions

The following features are possible future extensions, but are not required for milestone 1:

- continuous measurement mode
- storage and redisplay of the last result
- explicit invalid/clipped-result indication
- cancel or back action during measuring
- ECG reference acquisition on PF6
- radar-versus-ECG comparison
- OpenLog result logging via PG14
- additional settings and configuration screens
- improved filtering, smoothing, and validation logic

---

## 7. Final milestone-1 endpoint

Milestone 1 is complete when the system can do the following end to end:

- acquire radar I and Q from the fixed hardware inputs
- process the data with FFT-based evaluation
- estimate heart rate
- show the result on the LCD
- support a basic user flow through the touchscreen interface

This is the complete milestone-1 target. All other functions are optional later extensions.