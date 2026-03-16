# project2_hardware

Hardware-only extraction of `project2` for STM32 bare-metal deployment.

Goals:

- keep only firmware code intended for on-chip execution
- remove simulation entrypoints and virtual data paths
- enforce bare-metal + STM32 Standard Peripheral Library (SPL)
- no HAL, no RTOS
- provide a Keil-style folder layout (`Hardware/User/system`) consistent with classic STM32 SPL projects

## Current layout

```
project2_hardware/
  Hardware/
    RF_Protocol.c/h
    RF_Capture.c/h
    RF_Uart.c/h
    RF_Tx.c/h
  User/
    main.c
  system/
    Delay.c/h
```

Notes:

- Preferred integration path is now `Hardware + User + system`.
- `RF_Capture.c` now contains TIM2 capture runtime + frame split + idle flush logic.
- `RF_Uart.c` contains direct USART1 upload logic.
- `stm32f10x_it.c/h` should come from your existing Keil project template (do not duplicate here).
  Add these calls in your own `stm32f10x_it.c`:
  - `RF_Capture_SysTickHandler();` inside `SysTick_Handler`
  - `RF_Capture_TIM2_IRQHandler();` inside `TIM2_IRQHandler`

## Not migrated (left in original `project2`)

Simulation and virtual input:

- `project2/simulator/`
- `project2/sim_data/`
- `project2/stm32_firmware/main_sim.c`
- `project2/stm32_firmware/main_online_sim.c`
- `project2/python/run_virtual_pipeline.py`
- `project2/python/wav_to_pulses.py`

PC/gateway-side runtime modules:

- `project2/linux_app/`
- `project2/linux_driver/`
- `project2/python/ev1527_decode_bridge.py`
- `project2/ev1527_decode.py`
- `project2/vision/`
- `project2/qt_dashboard/`
- `project2/qt_gui/`
- `project2/tests/`
- `project2/requirements.txt`

Build outputs and caches:

- `project2/build/`
- `project2/__pycache__/`

## Flash preparation (bare-metal SPL, Keil style)

1. Add groups/files to Keil project:
   - `Hardware/*.c`
   - `User/main.c`
   - `system/*.c`
2. Include paths should cover:
   - `Hardware`
   - `User`
   - `system`
   - your SPL `Library` and CMSIS/startup headers
3. Add preprocessor macros:
   - `USE_STDPERIPH_DRIVER`
   - `STM32F10X_MD` (change for your exact target)
4. Default pin mapping in `RF_Capture.c` / `RF_Uart.c`:
   - `USART1 TX: PA9`
   - `TIM2 CH1 input capture: PA0`
