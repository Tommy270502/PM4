# FMCW Start Settings

## Abstract
In the FMCW settings there are several parameters that influence the final result. Namely the sample rate together with the FFT size, the sweep bandwidth and sweep slope. As soon as Range-Doppler is made, the repetition frequency of the sweeps is an additional factor.

In this document values are suggested, which serve as a starting point. Depending on the use case, these must be adapted accordingly.

## 1. Suggested start parameters
LC5 Parameters:
- Bandwidth (LC5): 260 MHz
- VCO sensitivity: 80 MHz / V

STM32F429 Parameters:
- DAC 12 Bit, max. Value = 2^12 = 4096

Firmware Parameters:
- ADC Sampling Frequency: 64 kHz
- FFT Size: 128
- DAC Frequency: 64 kHz
- DAC increment Value: (max. DAC Value) / (FFT Size) = 4096 / 128 = 32
- => Sweep Duration = 1/(DAC Frequency) * (FFT Size) = 1 / (64 kHz) * 128 = 2 ms
- => Rmin = (Speed of Light) / (2 * FMCW Bandwidth)) = 0.5765 m

DAC output and ADC sampling at the same time (synchronous) and connected to the same timer as time reference.

## 2. Recommended function test
Connect the DAC output directly to the ADC input without connecting your own PCB to the Eval-Board. The ADC values should thus give the same ramp in the time domain as the output data with the DAC. This is also a good test if the sampling is synchronous with the DAC.

## 3. Range FFT evaluation
Compared to the CW Doppler mode, all stationary objects are also visible in the range FFT in FMCW mode. In the building there are additional objects, which become visible, through multipath, which are not in the direct path.

After the FFT, calculate the magnitude and convert it to dB so that a higher dynamic range can be shown on the display.
```
// Absolut value
arm_cmplx_mag_f32(fmcw_cfft_inout, fmcw_fft_mag, FMCW_FFT_SIZE);
// Covert to dB
for(n=1; n<FMCW_FFT_SIZE; n++){
    fmcw_fft_mag[n] = 20*logf(fmcw_fft_mag[n]);
}

// Normalization, so that an object with given RCS has a constant level, no matter at which distance it is. -> Compensation of the distance dependence 1/R^4
for(n=1; n<FMCW_FFT_SIZE; n++){
    //fmcw_fft_mag[n] += 40*log10(n/(float) FMCW_FFT_SIZE);
    fmcw_fft_mag[n] += 40*log10((float)n); // simpler, FMCW_FFT_SIZE is just an offset
}
```