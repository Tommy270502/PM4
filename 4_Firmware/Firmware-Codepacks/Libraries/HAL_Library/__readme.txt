STM32F4xxHAL_Lib_20260223 --> STM32CubeIDE Project. It can be used to generate a STM32F4xx HAL library which is used by the demo_code project.
Repository                --> Collection of all source files for the HAL library (V1.8.5) and for the BSP library

To modify/update this library: 
- Import this project into the STM32CubeIDE, 
- modify/update the source files according to your demand, re-build the Project, 
- replace the library file ...Drivers\STM32F4xx_HAL_Driver\Lib\libSTM32F4xx_HAL_Lib.a in the demo_code project 
  with new created library file found in ...STM32F4xx_HAL_Lib\Debug\libSTM32F4xx_HAL_Lib.a in he HAL Driver project