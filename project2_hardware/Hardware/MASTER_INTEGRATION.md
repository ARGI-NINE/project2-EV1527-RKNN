# Hardware-Master Integration Notes

Quick reference for the interface contract between `project2_hardware` and `project2_master`.

## UART Parameters

- Port: `USART1` (`PA9` TX, `PA10` RX)
- Baud rate: `9600`
- Data bits: `8`
- Parity: `None`
- Stop bits: `1`
- Flow control: `None`

Master side currently expects real serial device `/dev/ttyS9`.

## Frame Format (Must Stay Stable)

| Offset | Field | Size | Description |
|---|---|---|---|
| 0 | Sync | 2 bytes | Fixed `0xAA 0x55` |
| 2 | LEN0/LEN1 | 2 bytes | Little-endian pulse count (`1..1024`) |
| 4 | Payload | `len*2` bytes | Pulse widths (`uint16_t`, little-endian) |
| `4 + len*2` | CRC8(XOR) | 1 byte | XOR over `LEN0 LEN1 Payload...` |

Total packet length: `2 + 2 + len*2 + 1`.

## Interrupt Entry Notes

- Startup vector symbol for TIM2 is `TIM2_IRQHandler`.
- `Hardware/RF_Capture.c` provides:
  - `TIM2_IRQHandler()` -> `RF_Capture_TIM2_IRQHandler()`
- If project code already defines `TIM2_IRQHandler`, compile with:
  - `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE`
  - and call `RF_Capture_TIM2_IRQHandler()` from your ISR.

## Runtime Behavior Notes (Fixes)

- TIM2 capture now uses overflow-count-based extended timestamping (`TIM_IT_Update` + `TIM_IT_CC1`), which covers multi-wrap intervals.
- Capture ready queue is enlarged (`RF_CAPTURE_READY_QUEUE_SIZE = 4`).
- UART TX is interrupt-driven with FIFO (`RF_UART_TX_FIFO_SIZE = 2304`) to reduce main-loop blocking.

## Runtime Limits

- Extreme continuous UART congestion can still lead to frame drops after queue/FIFO saturation.
- Long global IRQ disable windows reduce timing fidelity for capture.

## Wiring

- `STM32 PA9 (TX) -> RK3568 UART9 RX`
- `STM32 PA10 (RX) <- RK3568 UART9 TX`
- `GND <-> GND`
- 3.3V logic only.
