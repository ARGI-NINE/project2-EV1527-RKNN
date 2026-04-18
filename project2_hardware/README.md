# project2_hardware

STM32F103C8T6 bare-metal firmware for RF pulse capture and UART upload to RK3568 (`project2_master`).

## Scope

This firmware does three things:

1. Capture RF pulse widths on `TIM2 CH1` (PA0).
2. Detect valid frames with sync-gap and pulse-count rules.
3. Encode and send frames over `USART1`.

## Code Layout

```text
project2_hardware/
  Hardware/
    RF_Capture.c / RF_Capture.h
    RF_Protocol.c / RF_Protocol.h
    RF_Uart.c / RF_Uart.h
    RF_Tx.c / RF_Tx.h
  system/
    Timer.c / Timer.h
    Delay.c / Delay.h
  User/
    main.c
```

## UART Parameters

- Peripheral: `USART1` (`PA9` TX, `PA10` RX)
- Baud rate: `9600`
- Data bits: `8`
- Parity: `None`
- Stop bits: `1`
- Flow control: `None`

These parameters must match master-side serial settings.

## Frame Format (Aligned with `project2_master/common/rf_protocol.c`)

Do not change this protocol unless both sides are updated together.

| Offset | Field | Size | Description |
|---|---|---|---|
| 0 | Sync | 2 bytes | Fixed `0xAA 0x55` |
| 2 | LEN0/LEN1 | 2 bytes | Little-endian `uint16_t` pulse count, valid range `1..1024` |
| 4 | Payload | `len*2` bytes | Pulse widths, each as little-endian `uint16_t` |
| `4 + len*2` | CRC8(XOR) | 1 byte | XOR over `LEN0 LEN1 Payload...` |

Total packet length: `2 + 2 + len*2 + 1`.

## Interrupt Entry and TIM2 Bridge

`startup_stm32f10x_md.s` uses `TIM2_IRQHandler` in the vector table.

This repository now provides a default bridge in `Hardware/RF_Capture.c`:

- `TIM2_IRQHandler()` -> `RF_Capture_TIM2_IRQHandler()`

So startup vector entry for TIM2 can directly enter RF capture logic.

If your project already defines `TIM2_IRQHandler` elsewhere, add compile macro:

- `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE`

Then forward manually in your own ISR:

```c
#include "RF_Capture.h"

void TIM2_IRQHandler(void)
{
    RF_Capture_TIM2_IRQHandler();
}
```

`TIM3_IRQHandler` remains in `system/Timer.c` and calls `RF_Capture_Tick1msHandler()`.

## Reliability Fixes (Current)

### TIM2 multi-overflow handling

`Hardware/RF_Capture.c` now enables both `TIM_IT_CC1` and `TIM_IT_Update` and builds an extended capture timestamp:

- `Tim2OverflowCount` is incremented on every TIM2 update IRQ.
- Capture ISR composes timestamp as `overflow_count * (period + 1) + CCR`.
- When `UIF` is pending, `CCR` and current `CNT` are compared to determine whether overflow happened before or after this capture event.

This removes the previous "at most one wrap" assumption and prevents long gaps (multiple wraps) from being misinterpreted as short pulses.

### Reduced real-time frame loss pressure

- `RF_CAPTURE_READY_QUEUE_SIZE` is increased from `3` to `4` (effective queue capacity from 2 to 3 frames).
- UART transmit path is now interrupt-driven with a software TX FIFO (`RF_UART_TX_FIFO_SIZE = 2304`), so `RF_Capture_ProcessLoop()` no longer blocks on byte-by-byte polling send.
- `RF_Capture_ProcessLoop()` keeps one pending frame for retry when UART FIFO is full, avoiding immediate frame loss on transient backpressure.

### Known limits

- If IRQs are globally disabled for very long intervals, timer event timing precision still degrades.
- Under sustained UART backpressure, capture queue and UART FIFO can still saturate; in that case, frames may be dropped (tracked internally).

## Integration Notes for `project2_master`

1. Keep using the real UART device expected by master (currently `/dev/ttyS9`).
2. Keep protocol contract unchanged: `AA55 + LEN16 + payload + XOR`.
3. Do not change LEN to 1 byte, and do not change CRC rule, or master parsing will fail.
4. Wiring:
   - `STM32 PA9 (TX) -> RK3568 UART9 RX`
   - `STM32 PA10 (RX) <- RK3568 UART9 TX`
   - `GND <-> GND`
5. Use 3.3V logic on both sides.

## Default Capture Parameters

- `MinPulseUs = 80`
- `MaxPulseUs = 65535`
- `SyncUs = 8000`
- `MinFramePulses = 50`

`RF_Capture_SetFilter(...)` can override these values. `MinFramePulses` is clamped to `1..RF_BUFFER_SIZE`.
