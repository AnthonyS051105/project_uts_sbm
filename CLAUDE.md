# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Embedded firmware for **STM32F401CCUx** (ARM Cortex-M4, 256 KB Flash, 64 KB RAM) using STM32CubeIDE 1.18.0 with GCC ARM toolchain. The project configures 2 button inputs, 1 potentiometer analog input, and 8 LED outputs.

## Hardware Configuration

Defined in [Core/Inc/main.h](Core/Inc/main.h):

| Pin | Label | Direction |
|-----|-------|-----------|
| PA0 | POT   | Analog input (potentiometer) — ADC1 CH0, DMA2 Stream0 |
| PA1 | BTN1  | Digital input — EXTI1 (rising edge, no pull) |
| PA2 | BTN2  | Digital input — EXTI2 (rising edge, no pull) |
| PA8 | LED7  | Output |
| PA10| LED6  | Output |
| PA12| LED5  | Output |
| PB3 | LED4  | Output |
| PB5 | LED3  | Output |
| PB7 | LED2  | Output |
| PB9 | LED1  | Output |
| PB14| LED8  | Output |

## Build System

The project uses STM32CubeIDE's managed build (Eclipse/GNU Make). **Do not edit `.cproject` manually** — use the IDE GUI for build settings.

**To build from the command line** (requires `arm-none-eabi-gcc` on PATH and Eclipse headless build):
```bash
make -C Debug all
```

**Key compiler flags** (from `.cproject`):
- `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`
- `-DDEBUG -DUSE_HAL_DRIVER -DSTM32F401xC`
- Linker script: `STM32F401CCUX_FLASH.ld`

**Build output**: `Debug/project_uts.elf` + `.hex`, `.bin`, `.srec`

## Flashing

```bash
# Via ST-Link (requires st-flash)
st-flash write Debug/project_uts.bin 0x08000000

# Via OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program Debug/project_uts.elf verify reset exit"
```

Or use STM32CubeIDE: **Run → Debug** (F11) or **Run → Run** (Ctrl+F11).

## Code Architecture

- **[Core/Src/main.c](Core/Src/main.c)** — Entry point. `main()` calls `HAL_Init()`, `SystemClock_Config()`, then initializes GPIO, DMA, and ADC1. Application logic goes in the `while(1)` loop inside `USER CODE BEGIN 3`.
- **[Core/Inc/main.h](Core/Inc/main.h)** — GPIO pin/port definitions for all named I/O.
- **[Core/Src/stm32f4xx_it.c](Core/Src/stm32f4xx_it.c)** — ISRs: `EXTI1_IRQHandler` (BTN1), `EXTI2_IRQHandler` (BTN2), `DMA2_Stream0_IRQHandler` (ADC DMA). Add button/ADC logic via HAL callbacks, not directly in these handlers.
- **[Core/Src/stm32f4xx_hal_msp.c](Core/Src/stm32f4xx_hal_msp.c)** — Low-level HAL peripheral init (MSP = MCU Support Package).
- **[project_uts.ioc](project_uts.ioc)** — STM32CubeMX pin/peripheral configuration. Regenerating code from this file will overwrite `main.c` sections between `USER CODE BEGIN/END` markers — always keep custom code inside those markers.

## HAL Usage Patterns

```c
// Toggle LED
HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);

// Set LED on/off
HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);

// Delay (uses SysTick, 1 ms resolution)
HAL_Delay(500);

// Read ADC via DMA (continuous mode already configured)
// Declare buffer, start once, then read anytime:
uint32_t adc_val;
HAL_ADC_Start_DMA(&hadc1, &adc_val, 1);
// Implement callback for conversion complete:
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) { /* use adc_val */ }

// Button interrupt callback (implement in USER CODE section of main.c or stm32f4xx_it.c)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == BTN1_Pin) { /* BTN1 pressed */ }
    if (GPIO_Pin == BTN2_Pin) { /* BTN2 pressed */ }
}
```

## System Clock

Running at **16 MHz HSI** (no PLL). `HAL_GetTick()` returns milliseconds via SysTick.

## ADC Configuration

ADC1 is initialized in continuous mode with DMA (DMA2 Stream0). Resolution: 12-bit (0–4095). Sampling time: 84 cycles. Use `HAL_ADC_Start_DMA()` to start continuous conversion into a buffer; the `HAL_ADC_ConvCpltCallback` fires on each completion.
