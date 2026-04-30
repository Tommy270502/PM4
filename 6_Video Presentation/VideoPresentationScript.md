# Video Presentation Script

Aligned with:
- `ET-PM4-SW9-VideoPresentationRequirements.pdf`
- `ET-PM4-SW9-VideoPresentation-Script.pdf`
- `OnePager.pdf`
- current firmware features in the STM32 project

Target length: about 2 minutes 40 seconds

## Contactless Radar-Based Heart Rate Monitoring Device

| Text / Voiceover | Action on Screen | Time |
| --- | --- | --- |
| none | Static title slide with minimal text: `Contactless Radar-Based Heart Rate Monitoring Device`, `ZHAW PM4 Electronics Project` | 0:00-0:08 |
| Our project is a contactless radar-based heart rate monitoring device. It detects tiny chest movements without electrodes or wearable sensors, processes the signal in real time on an STM32 microcontroller, and displays the result on an integrated touchscreen interface. | Static feature slide with only 4 short bullets: `Contactless`, `Real-time`, `Touchscreen UI`, `ECG reference comparison` | 0:09-0:22 |
| The motivation is simple: heart rate is important in healthcare, sleep monitoring, and safety applications, but most systems still require direct contact with the body. That can be uncomfortable or impractical for long measurements, sleeping users, or sensitive patients. | Static slide with one use-case image of a sleeping person and one of patient monitoring. Minimal text: `comfortable`, `continuous`, `contact-free` | 0:23-0:38 |
| Our goal is to show that Doppler radar can be used as a practical embedded solution for contactless heart-rate monitoring at short range. | Static transition slide with one sentence and a simplified radar icon | 0:39-0:46 |
| The core idea is Doppler sensing. A microwave radar module transmits a signal toward the chest. Very small chest movements caused by cardiac activity change the phase of the reflected signal. These changes are then measured and analyzed. | Short animated video or recorded PowerPoint animation of person, radar beam, reflected signal, and moving chest | 0:47-1:02 |
| The hardware platform combines the K-LC5 Doppler radar module with an STM32 Discovery board. The embedded system acquires I and Q radar channels, performs on-board signal processing, and renders the results directly on the display. | Static block diagram: `Radar module -> analog front end -> STM32 -> LCD touchscreen` Then cut to a short live video of the real hardware | 1:03-1:18 |
| In firmware, the radar signals are sampled at 100 hertz. We process 512 samples per channel, which corresponds to a 5.12 second analysis window. The window advances every 2.56 seconds with 50 percent overlap, allowing continuous updates. | Static slide with simple processing pipeline and the numbers `100 Hz`, `512 samples`, `5.12 s window`, `2.56 s update` | 1:19-1:37 |
| After acquisition, the STM32 applies filtering, computes the frequency spectrum, and detects dominant peaks. The current firmware also supports multiple selectable filter modes, including bypass, low-pass, high-pass, band-pass, and notch filtering. | Recorded video of the device switching between the time-domain screen, FFT spectrum, and effect menu. Keep on-screen labels visible, no extra text overlay | 1:38-1:57 |
| The user interface is designed to make the signal processing transparent. It includes a time-signal view, an FFT spectrum view, peak-frequency readout, and an ECG monitor page for reference measurements. | Live video recording of the touchscreen UI. Show at least: `Time Signal`, `FFT Spectr`, `Peak Detect`, and `EKG BPM` | 1:58-2:16 |
| This is one of the main strengths of the project: the system does not only measure radar data, it also visualizes the intermediate steps. That makes debugging easier and helps the user understand how the heart-rate estimate is generated. | Continue live UI video. Brief zoom or crop on the spectrum peak readout and ECG BPM page | 2:17-2:30 |
| Another advantage is that the full processing chain runs directly on the embedded platform. No external PC is required during operation, and the touchscreen interface allows direct interaction with the system. | Short live video of standalone operation on the board and touchscreen interaction | 2:31-2:42 |
| In terms of technical characteristics, the current design targets short-range operation up to about one meter. The spectral display covers approximately plus or minus four hertz, and the FFT bin spacing is about 0.195 hertz, which corresponds to about 11.7 beats per minute per raw bin. Accuracy is evaluated by comparison with a reference ECG sensor. | Static summary slide with minimal text: `Range: up to ~1 m`, `Display span: +/-4 Hz`, `FFT resolution: 0.195 Hz`, `Reference: ECG comparison` | 2:43-2:58 |
| In summary, our system offers contactless heart-rate monitoring, real-time embedded processing, a touchscreen-based user interface, and ECG-supported validation. This makes it a compact platform for comfortable vital-sign monitoring and further development. | Final slide with repeated main features and metadata: project title, authors, date, affiliation, and project link or repository link | 2:59-3:10 |

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
