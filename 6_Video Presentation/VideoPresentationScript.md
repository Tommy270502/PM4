# Video Presentation Script

Target length: about 2 minutes 40 seconds

## Contactless Radar-Based Heart Rate Monitoring Device

| # | Text / Voiceover | Action on Screen | Time |
| --- | --- | --- | --- |
| 01 | none | Static title slide with minimal text: `Contactless Radar-Based Heart Rate Monitoring Device`, `ZHAW PM4 Electronics Project` | 0:00-0:06 |
| 02 | Heart rate tells a lot. But measuring it often still means contact: electrodes, straps, watches, or wearable sensors. | Problem visuals: ECG electrodes, smartwatch, chest strap. Minimal text: `Heart rate. Still contact-based.` | 0:07-0:18 |
| 03 | For sleep, long-term monitoring, or sensitive users, contact can become the limitation. The ideal measurement is comfortable, continuous, and almost invisible. | Sleeping person, patient monitoring, contact-free symbol. Minimal text: `comfortable`, `continuous`, `contact-free` | 0:19-0:32 |
| 04 | This device detects heart-rate-related chest movement without touching the body. It processes the radar signal in real time on an STM32 microcontroller and shows the result directly on an integrated display. | Product reveal / feature slide: hardware photo or clean render. Short labels: `Contactless`, `Real-time`, `LCD Display` | 0:33-0:48 |
| 05 | The principle is Doppler sensing. A microwave radar module sends a signal toward the chest. Small movements caused by cardiac activity change the phase of the reflected signal, creating a measurable heartbeat pattern. | Animation: radar beam, reflected signal, moving chest, signal wave. | 0:49-1:05 |
| 06 | Inside the system, the K-LC5 radar module is connected to an analog front end and an STM32 Discovery board. The microcontroller acquires the I and Q radar channels, processes them, and drives the LCD touchscreen. | Block diagram: `K-LC5 radar -> analog front end -> STM32 -> LCD touchscreen`. Brief real-hardware shot. | 1:06-1:22 |
| 07 | The complete processing chain runs directly on the embedded platform. No external PC is required during operation, while heart-rate data can also be logged to an SD card with the integrated OpenLog module for later analysis. | Live video of standalone board operation. Brief close-up of OpenLog. Optional text: `Standalone operation`, `SD card logging` | 1:23-1:39 |
| 08 | The radar signals are sampled at 100 hertz. Each analysis window contains 512 samples per channel, equal to 5.12 seconds of data, with updates every 2.56 seconds using 50 percent overlap. | Processing pipeline slide with key numbers: `100 Hz`, `512 samples`, `5.12 s window`, `2.56 s update` | 1:40-1:55 |
| 09 | From the sampled I and Q radar signals, the system computes the frequency spectrum, detects the dominant heart-rate peak and calculates the BPM value. The interface makes this process visible step by step: from the time signal, to the FFT spectrum, to peak detection, and finally to the radar BPM output. | UI recording: show `Time Signal`, then `FFT Spectr`, then `Peak Detect`, then `Radar BPM`. | 1:56-2:14 |
| 10 | For validation, the device also includes an integrated ECG front end with a connection slot for ECG cables. This allows a direct reference measurement to be taken with the same platform, with the result displayed on the ECG monitor page for comparison with the radar BPM value. | Hardware close-up: ECG chip / ECG input connector / cable slot. Then quick cut to `EKG BPM` page and radar peak/BPM view. Minimal labels: `Integrated ECG reference`, `ECG cable input`, `Radar vs. ECG`. | 2:15-2:32 |
| 11 | The device targets short-range operation up to about one meter. The displayed spectrum covers approximately plus or minus four hertz, with ECG comparison and SD-card logging supporting validation and later analysis. | Static technical summary slide: `Range: up to ~1 m`, `Spectrum: +/-4 Hz`, `ECG reference`, `SD logging`. | 2:33-2:45 |
| 12 | The FFT bin spacing is approximately 0.195 hertz, or 11.7 beats per minute. However, parabolic peak interpolation refines the peak position, so the BPM output is not limited to raw FFT-bin steps. Practical accuracy still depends on signal quality, leakage, motion artifacts, and temporal stability. | Technical graphic: FFT peak between bins, parabolic curve over three bins. Labels: `0.195 Hz`, `11.7 bpm/bin`, `sub-bin interpolation`, `accuracy depends on signal quality` | 2:46-3:06 |
| 13 | Contactless sensing, real-time embedded processing, touchscreen visualization, ECG-supported validation, and SD-card logging come together in one compact platform for comfortable vital-sign monitoring. | **Final slide:** project title, authors, date, ZHAW affiliation, repository/project link or QR code. | 3:07-3:17 |

## Final Slide Content

Use the last slide to repeat the required summary in compact form:

- Contactless heart-rate monitoring
- Short-range operation: up to about 1 m
- Real-time embedded processing on STM32F429
- Touchscreen UI with time, FFT, peak, and ECG pages
- FFT bin spacing: about 0.195 Hz
- Accuracy: add measured ECG comparison result here
- Authors: Thomas Perri, Bogdans Grebnevs
- Date: add recording or submission date
- Link: add repository or demo link

## Before Recording

Replace these placeholders if you have the measured data:

- `Accuracy: add measured ECG comparison result here`
- `Date: add recording or submission date`
- `Link: add repository or demo link`

If you do not have a validated accuracy number yet, keep the final slide honest and write:

`Accuracy currently under validation against reference ECG`
