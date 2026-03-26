ARM.CMSIS-DSP.1.17.0.zip   --> 	The CMSIS DSP Library Pack V1.17.0 as downloaded from https://www.keil.arm.com/packs/cmsis-dsp-arm/versions/
CMSIS_DSP_Lib_20260222.zip --> 	The STM32CubeIDE Project which can be used to generate a static library. 
				It includes source files from above CMSIS DSP Library Pack

You may generate new/updated staticCMSIS DSP Library file as follows:
- Import CMSIS_DSP_Lib_20260222 into the STM32CubeIDE.
- Modify/update the source files upon your demand.
- Build the library, this generates the library file CMSIS_DSP_Lib\Debug\libCMSIS_DSP_Lib.a.
- Replace the new generated library file in your demo_code project: ...demo_code\Drivers\CMSIS\Lib\GCC\libCMSIS_DSP_Lib.a.