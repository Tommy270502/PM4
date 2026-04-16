# Firmware Architecture

This firmware is a non-RTOS STM32F429 application built around one foreground superloop in `main.c`. The time-critical acquisition happens in interrupts, while the heavier signal processing and all LCD rendering happen in the main loop.

## High-Level Flowchart

```mermaid
flowchart TD
    classDef bootCls fill:#FFF4CC,stroke:#B7791F,color:#111827
    classDef isrCls fill:#DBEAFE,stroke:#2563EB,color:#111827
    classDef mainCls fill:#DCFCE7,stroke:#15803D,color:#111827
    classDef uiCls fill:#FCE7F3,stroke:#BE185D,color:#111827
    classDef noteCls fill:#F3F4F6,stroke:#6B7280,color:#111827

    subgraph Boot["Boot and setup"]
        b0([Reset])
        b1["HAL_Init<br/>SystemClock_Config"]
        b2["LCD + Touch init<br/>MENU_draw<br/>disp_info"]
        b3["Pushbutton IRQ + LEDs<br/>gyro_disable<br/>dac_output_init"]
        b4["radar_init + radar_start<br/>TIM2 + ADC1/ADC2 + DMA2"]
        b5["Configure 5 radar biquad filters<br/>fft_init"]
        b6["ekg_init<br/>TIM3 + ADC3 + IRQ enable"]
        b0 --> b1 --> b2 --> b3 --> b4 --> b5 --> b6
    end

    subgraph RadarIRQ["Radar path: interrupt-driven acquisition"]
        r1["TIM2 TRGO<br/>100 Hz sample timing"]
        r2["ADC1 PC1 = I<br/>ADC2 PC3 = Q<br/>dual regular simultaneous"]
        r3["DMA2 Stream0<br/>circular + double buffer"]
        r4["DMA2_Stream0_IRQHandler"]
        r5["unpack_iq_samples<br/>256 I + 256 Q samples"]
        r6["radar_data_ready = 1"]
        r1 --> r2 --> r3 --> r4 --> r5 --> r6
    end

    subgraph ECGIRQ["ECG path: interrupt-driven acquisition"]
        e1["TIM3 update<br/>250 Hz"]
        e2["ADC3 SWSTART<br/>PF6 / AD8232"]
        e3["ADC_IRQHandler"]
        e4["Latest raw sample<br/>sample-ready flag<br/>overrun counter"]
        e1 --> e2 --> e3 --> e4
    end

    subgraph Input["User input"]
        u1["Touchscreen polling<br/>MENU_check_transition"]
        u2["Scroll arrows<br/>or double-tap page entry"]
        u3["EXTI0 IRQ on PA0"]
        u4["PB_pressed_flag"]
        u1 --> u2
        u3 --> u4
    end

    subgraph Foreground["Foreground superloop"]
        m0["while (1)"]
        m1["Process menu transition<br/>scroll or select active page"]
        m2{"PB_pressed?"}
        m3["Cycle filter<br/>BYPASS, LOWPASS, HIGHPASS,<br/>BANDPASS, NOTCH"]
        m4["If a full radar window exists:<br/>re-run filter + FFT + peak update"]
        m5{"ekg_process_if_ready?"}
        m6["ECG processing<br/>high-pass -> low-pass -> diff squared envelope<br/>adaptive threshold -> R-peak + BPM"]
        m7{"radar_frame_ready?"}
        m8["Critical section<br/>clear flag + append latest chunk<br/>into rolling history"]
        m9{"512-sample radar window full?"}
        m10["Copy history to processing window<br/>optional biquad filter"]
        m11["fft_iq_centered<br/>DC removal + Hann window<br/>complex FFT -> magnitude -> fft shift"]
        m12["radar_update_peak_readout<br/>positive and negative dominant Hz"]
        m13{"DAC page active<br/>and touch changed?"}
        m14["Set PA5 DAC voltage/code"]
        m15["disp_refresh = true"]

        m0 --> m1
        m0 --> m2
        m0 --> m5
        m0 --> m7
        m0 --> m13

        m1 -.->|page change| m15
        m2 -- yes --> m3 --> m4 --> m15
        m5 -- yes --> m6
        m6 -.->|if MENU_NINE active| m15
        m7 -- yes --> m8 --> m9
        m9 -- yes --> m10 --> m11 --> m12 --> m15
        m13 -- yes --> m14 --> m15
    end

    subgraph Display["Display rendering"]
        d1["Build disp_menu_data snapshot"]
        d2["disp_menu_render(active_menu, data)"]
        d0["Menu 0<br/>Info screen"]
        d3["Menu 1<br/>recent radar I/Q time signal"]
        d4["Menu 2<br/>centered FFT spectrum around 0 Hz"]
        d5["Menu 3<br/>active filter list"]
        d6["Menu 4<br/>positive and negative peak readout"]
        d7["Menu 9<br/>ECG BPM, raw ADC, R-peak, overrun"]
        d8["Menu 10<br/>DAC slider and plus/minus controls"]
        d9["Menu 5 static demo level meter<br/>Menus 6 to 8 currently blank"]

        m15 --> d1 --> d2
        d2 --> d0
        d2 --> d3
        d2 --> d4
        d2 --> d5
        d2 --> d6
        d2 --> d7
        d2 --> d8
        d2 -.-> d9
    end

    b6 --> m0
    r6 --> m7
    e4 --> m5
    u2 --> m1
    u4 --> m2

    class b0,b1,b2,b3,b4,b5,b6 bootCls
    class r1,r2,r3,r4,r5,r6,e1,e2,e3,e4,u3,u4 isrCls
    class u1,u2,d0,d1,d2,d3,d4,d5,d6,d7,d8 uiCls
    class m0,m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15 mainCls
    class d9 noteCls
```

## What The Code Is Really Doing

- `main.c` is the conductor: it boots the board, starts radar and ECG acquisition, polls the touchscreen, handles the pushbutton, and decides when the display should redraw.
- Radar is block-based: the ISR only moves fresh I/Q samples into buffers, and the main loop turns those overlapped windows into filtered time signals, FFT data, and peak frequencies.
- ECG is sample-based: interrupts capture one ADC sample at a time, and the main loop runs the beat-detection math that produces `R-peak` events and BPM.
- The LCD is menu-driven: the renderer picks one page from the current menu and draws either radar, ECG, DAC, or info content.
- Menu 5 draws a fixed demo level meter; menus 6, 7, and 8 do not currently render live processing results.
