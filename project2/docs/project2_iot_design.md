# Universal RF Signal Analyzer & Adaptive IoT Gateway

## 1. 系统总体架构

系统采用四层架构，链路为：

1. STM32 采集层：`Input Capture` 捕获 433MHz OOK 波形边沿，输出微秒脉冲宽度数组。
2. Linux 驱动层：`/dev/rf433` 字符设备缓存 UART 原始流，提供 `read/poll`。
3. Linux 应用层：`epoll + decode + sqlite + mqtt` 完成协议解析、解码、存储与控制。
4. UI 层：Qt 实时显示波形、解码结果和历史记录。

核心路径：

`RF -> STM32 pulse[] -> UART frame -> /dev/rf433 -> epoll -> decode -> DB/MQTT -> UI`

---

## 2. 硬件设计

### 2.1 STM32 前端

- MCU: `STM32F103C8T6`
- TIM2：`1MHz` 输入捕获，分辨率 `1us`
- USART1：上传波形帧
- RF DATA: `PA0 (TIM2_CH1)`

### 2.2 Linux 网关

- 平台：`i.MX6ULL Pro`
- UART2：接 STM32
- Ethernet：MQTT 上云
- RGB LCD：Qt 本地监控

### 2.3 RF 模块

- 接收模块：`SYN470R / RXB6 / MX-RM-5V`
- 发送模块：433MHz OOK TX（用于 replay）

---

## 3. 软件架构（STM32 / Linux / UI）

### 3.1 STM32 固件

- `rf_capture.c`：采集与帧检测
- `rf_uart.c`：`AA55 + LEN + payload + CRC` 打包上传
- `rf_tx.c`：重放脉冲序列

### 3.2 Linux 应用

- `rf_epoll.c`：事件循环，监听 `rf_fd` 与 `mqtt_fd`
- `rf_decode.c`：解码编排（C解码优先，Python兜底）
- `rf_decode_c.c`：EV1527 C 版本解码（嵌入式可部署）
- `rf_db.c`：SQLite 持久化
- `rf_mqtt.c`：MQTT FD 模拟/接入抽象

### 3.3 UI 层（Qt）

- `wave_widget`：波形绘制
- `device_list`：设备列表
- `log_view`：事件日志

---

## 4. 数据协议设计

UART 帧格式：

- `SYNC`: `0xAA 0x55`
- `LEN`: 2 字节小端，单位是 pulse 数量
- `PAYLOAD`: `LEN * uint16_t(us)` 小端
- `CRC`: 1 字节，`XOR(LEN + PAYLOAD)`

示例：

`AA 55 | 32 00 | B0 04 50 91 ... | CRC`

---

## 5. 虚拟数据仿真方案（重点）

无硬件联调主链：

1. `wav_to_pulses.py` 从 WAV 提取候选 EV1527 pulse frame。
2. 输出 `pulse.txt`（模拟器输入）和 `pulse.json`（解码输入）。
3. `rf_simulator` 将 `pulse.txt` 打包成 UART 协议流。
4. `rf_gateway` 读取协议流，走真实 epoll/解码/数据库流程。

关键脚本：

- `python/wav_to_pulses.py`
- `python/ev1527_decode_bridge.py`
- `python/run_virtual_pipeline.py`

关键程序：

- `simulator/rf_simulator.c`
- `linux_app/main.c`

---

## 6. Linux 驱动与 Epoll 框架

### 6.1 驱动接口

- 设备：`/dev/rf433`
- 核心：`RingBuffer + read + poll + UART IRQ push`
- 模板实现：`linux_driver/rf433_drv.c`

### 6.2 epoll 主循环

`linux_app/rf_epoll.c`：

1. `epoll_wait` 监听 `rf_fd/mqtt_fd`
2. `rf_fd` 就绪时读字节流并状态机解包协议
3. 组帧成功后回调 `on_frame`
4. `mqtt_fd` 就绪时读取控制命令并触发重放

---

## 7. 关键模块函数设计（可直接编码）

### 7.1 STM32

- `void rf_capture_init(rf_capture_ctx_t*, rf_frame_ready_cb_t, void*)`
- `void rf_capture_isr(rf_capture_ctx_t*, uint16_t pulse_us)`
- `void rf_frame_detect(rf_capture_ctx_t*, uint16_t pulse_us)`
- `void rf_uart_send_frame(const rf_frame_t*)`
- `void rf_tx_replay(const uint16_t *pulse, uint16_t len)`

### 7.2 Linux 应用

- `int rf_epoll_run(const rf_epoll_config_t *cfg)`
- `int rf_decode_frame(const rf_frame_t*, const char*, const char*, rf_decoded_packet_t*)`
- `int rf_decode_call_python(char *pulse_file, char *result)`
- `int rf_db_upsert(rf_db_t*, const rf_decoded_packet_t*, const char*)`
- `int rf_mqtt_read_command(rf_mqtt_client_t*, rf_mqtt_cmd_t*)`

### 7.3 协议模块

- `size_t rf_proto_encode(const rf_frame_t*, uint8_t*, size_t)`
- `int rf_proto_parser_consume(rf_proto_parser_t*, uint8_t, rf_frame_t*)`

---

## 8. 项目实施步骤

1. 完成 STM32 采集链：`capture -> frame detect -> uart encode`
2. 完成 Linux 驱动：`/dev/rf433 + ring + poll`
3. 完成虚拟仿真链：`WAV -> pulse -> simulator -> gateway`
4. 接入解码与持久化：`C decode + Python fallback + SQLite`
5. 接入 MQTT 命令与 replay 控制闭环
6. 最后接 Qt UI，统一展示状态与历史


## 9. Online Real-time Processing Extension

To support continuous STM32 capture and robust decode under noise, add this runtime policy:

1. STM32 keeps edge-level real-time capture (TIM2 input capture, 1 MHz tick, 1 us resolution).
2. STM32 performs lightweight front filtering only (glitch reject + frame split by long sync low).
3. Linux gateway maintains a short frame window and does consensus decode.
4. Output is published only when the same decoded code appears repeatedly within the window.

Implemented interfaces:

- STM32 online capture:
  - `rf_capture_online_init(...)`
  - `rf_capture_process_ccr(...)`
  - `rf_capture_process_dma(...)`
  - `rf_capture_set_filter(...)`
- Linux online decode window:
  - `rf_decode_window_init(...)`
  - `rf_decode_window_push(...)`
  - `rf_decode_window_try_decode(...)`
- Runtime args in `rf_gateway`:
  - `--decode-mode window|frame`
  - `--window-size N`
  - `--stable-repeat N`
  - `--min-win-confidence F`
  - `--publish-gap N`
