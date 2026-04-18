# RF433 自适应 IoT 网关 — 系统设计文档

---

## 1. 系统总体架构

系统采用四层架构，链路为：

1. **STM32 采集层**：Input Capture 捕获 433MHz OOK 波形边沿，输出微秒脉冲宽度数组，通过 USART1 上传。
2. **Linux 驱动层**：RK3568 UART9（`/dev/ttyS9`）接收串口字节流（唯一输入路径）。
3. **Linux 应用层**：`epoll + decode + sqlite + mqtt` 完成协议解析、EV1527 解码、稳定分组、存储与上报。
4. **UI 层**：Qt5 C++ 仪表盘实时显示波形、解码结果和系统状态。

核心路径：

```
RF 433MHz → STM32 pulse[] → UART 协议帧 → /dev/ttyS9 → epoll → decode → DB/MQTT → Qt UI
```

---

## 2. 硬件设计

### 2.1 STM32 前端

| 项目       | 规格                                           |
|:-----------|:-----------------------------------------------|
| MCU        | STM32F103C8T6                                  |
| 定时器     | TIM2，1MHz 输入捕获，分辨率 1μs                 |
| 串口       | USART1，上传波形帧                              |
| RF 数据引脚 | PA0（TIM2_CH1）                                |

### 2.2 RK3568 Linux 网关

| 项目       | 规格                                           |
|:-----------|:-----------------------------------------------|
| 平台       | **iTOP-RK3568** 开发板（aarch64 Linux）         |
| 串口       | **UART9**（`/dev/ttyS9`），接 STM32（9600 8N1） |
| 网络       | Ethernet，MQTT 上云                             |
| 显示       | HDMI / MIPI LCD，Qt5 本地监控                   |
| NPU        | RK3568 内置 NPU 0.8TOPS（视觉推理可选）         |

### 2.3 RF 模块

| 类型       | 型号                                           |
|:-----------|:-----------------------------------------------|
| 接收模块   | SYN470R / RXB6 / MX-RM-5V                      |
| 发送模块   | 433MHz OOK TX（用于脉冲序列发送）               |

---

## 3. 软件架构

### 3.1 STM32 固件

| 模块            | 功能                                         |
|:----------------|:---------------------------------------------|
| `rf_capture.c`  | 输入捕获与帧检测                              |
| `rf_uart.c`     | `AA55 + LEN + payload + CRC` 打包上传         |
| `rf_tx.c`       | 脉冲序列发送                                  |

### 3.2 Linux 应用（rf_gateway）

| 模块              | 功能                                        |
|:------------------|:--------------------------------------------|
| `rf_source.c`     | 串口打开 + termios 配置（9600 8N1）          |
| `rf_epoll.c`      | epoll 事件循环，监听串口 fd                   |
| `rf_decode.c`     | EV1527 解码编排（纯 C 实现）                  |
| `rf_decode_c.c`   | EV1527 C 版本解码核心（嵌入式可部署）          |
| `main.c`          | 入口：参数解析、稳定分组、去重、stdout 发布    |

> 注：板侧版本**不包含** Python 兜底解码，所有解码均由 C 代码完成。

### 3.3 UI 层（Qt5 C++）

| 模块              | 功能                                        |
|:------------------|:--------------------------------------------|
| `rf_status_page`  | RF 在线状态、波形预览、解码结果、历史事件      |
| `vision_page`     | 视觉检测页面（对接 RKNN 推理管线）            |
| `system_log_page` | MQTT 统计、CRC/丢包、CPU/内存、日志过滤       |
| `waveform_widget` | 波形绘制控件                                  |

Qt 前端启动 `rf_gateway` 采用固定路径策略：仅允许应用目录下 `./rf_gateway`，不支持命令行注入和多候选回退。

---

## 4. 数据协议设计

UART 帧格式（STM32 → RK3568）：

| 字段      | 长度                  | 说明                                  |
|:----------|:----------------------|:--------------------------------------|
| SYNC      | 2 字节                | `0xAA 0x55`                           |
| LEN       | 2 字节（小端）         | 脉冲数量（pulse count）                |
| PAYLOAD   | `LEN × 2` 字节（小端）| 每个 pulse 的微秒宽度（uint16_t）      |
| CRC       | 1 字节                | `XOR(LEN 字节 + PAYLOAD 字节)`        |

示例：

```
AA 55 | 32 00 | B0 04 50 91 ... | CRC
```

---

## 5. Linux 驱动与串口

### 5.1 唯一路径：标准 tty（/dev/ttyS9）

rf_gateway 通过标准 tty 层直接读取 `/dev/ttyS9`，使用 termios API 配置串口参数（9600 8N1 Raw）。这是 master 唯一允许的实时输入路径。

### 5.2 输入约束（仅允许实时串口）

- `rf_gateway` 仅接受 `/dev/ttyS9`。
- 串口打开方式固定：`O_RDWR | O_NOCTTY | O_NONBLOCK`。
- 明确拒绝所有非 `/dev/ttyS9` 输入。
- `linux_driver/rf433_drv.c` 仅保留历史样例，不参与 master 运行链路（边界见 `linux_driver/README.md`）。

### 5.3 epoll 主循环

`linux_app/rf_epoll.c` 实现：

1. `epoll_wait` 监听串口 fd，`EINTR` 时继续等待并记录日志
2. 串口 fd 就绪后循环 `read` 直到 `EAGAIN/EWOULDBLOCK`，`EINTR` 重试，`EOF` 记录并退出
3. 协议状态机解析失败时输出 `[RF_PARSE_STATS]`，区分 `crc_errors` 与 `parse_errors`
4. 组帧成功后回调 `on_frame` 触发解码流程

### 5.4 串口驱动结论（不能替代厂商 UART 驱动）

结论：`linux_driver/rf433_drv.c` 不能替代 RK3568 厂商 UART 驱动。

依据文件位置：

1. `linux_app/rf_source.c`：运行时直接 `open("/dev/ttyS9") + termios`，依赖厂商 tty/UART 驱动提供设备节点。
2. `linux_app/main.c`：参数层强约束只允许 `/dev/ttyS9`。
3. `linux_driver/rf433_drv.c`：样例仅创建 `/dev/rf433`，未实现 RK3568 UART9 的 DT probe/pinctrl/clock/DMA 接管。
4. `linux_app/CMakeLists.txt`：master 构建链路不包含 `linux_driver` 样例目标。

---

## 6. 在线实时处理

支持 STM32 持续采集与噪声环境下的鲁棒解码：

### 6.1 运行策略

1. STM32 保持边沿级实时捕获（TIM2 Input Capture，1MHz tick，1μs 分辨率）
2. STM32 执行轻量前端滤波（毛刺抑制 + 长同步低电平帧分割）
3. Linux 网关维护短帧窗口，执行共识解码
4. 仅当同一编码在窗口内重复出现时才输出发布

### 6.2 Linux 窗口解码（3 级评分）

1. **结构门控**：最少脉冲数 + "首低是最长低"
2. **时序评分**：EV1527 比例拟合（`4/12/124`），含 bit/sync/jitter 误差
3. **重复评分**：短窗口共识排名，按重复次数和平均置信度

### 6.3 时序对齐参数

- EV1527 时钟门控范围：`230μs ~ 4.24ms`（`rf_decode_c.c`）
- 同步门控：`sync_low >= 8000μs` 作为候选帧结构前提
- pair-period 累积使用 32 位总和，避免高时钟边界处 `16T` 溢出

### 6.4 相关接口

**STM32 在线捕获：**

- `rf_capture_online_init(...)`
- `rf_capture_process_ccr(...)`
- `rf_capture_process_dma(...)`
- `rf_capture_set_filter(...)`

**Linux 在线解码窗口：**

- `rf_decode_window_init(...)`
- `rf_decode_window_push(...)`
- `rf_decode_window_try_decode(...)`

**rf_gateway 运行时参数（当前实现）：**

| 参数                       | 说明                        |
|:---------------------------|:----------------------------|
| `--rf-input <path>`        | 仅允许 `/dev/ttyS9`         |
| `--stable-repeat N`        | 稳定重复次数                 |
| `--stable-window N`        | 稳定分组窗口                 |
| `--stable-near-bits N`     | 分组合并阈值（Hamming）      |
| `--preferred-code <hex>`   | 目标码优先策略               |
| `--min-publish-confidence F` | 最低发布置信度             |
| `--publish-gap N`          | 发布间隔                     |

---

## 7. 关键模块函数接口

### 7.1 STM32

```c
void rf_capture_init(rf_capture_ctx_t*, rf_frame_ready_cb_t, void*);
void rf_capture_isr(rf_capture_ctx_t*, uint16_t pulse_us);
void rf_frame_detect(rf_capture_ctx_t*, uint16_t pulse_us);
void rf_uart_send_frame(const rf_frame_t*);
```

### 7.2 Linux 应用

```c
int rf_epoll_run(const rf_epoll_config_t *cfg);
int rf_decode_frame(const rf_frame_t*, const char*, const char*, rf_decoded_packet_t*);
int rf_db_upsert(rf_db_t*, const rf_decoded_packet_t*, const char*);
int rf_mqtt_read_command(rf_mqtt_client_t*, rf_mqtt_cmd_t*);
```

### 7.3 协议模块

```c
size_t rf_proto_encode(const rf_frame_t*, uint8_t*, size_t);
int rf_proto_parser_consume(rf_proto_parser_t*, uint8_t, rf_frame_t*);
```

---

## 8. 项目实施步骤

1. 完成 STM32 采集链：capture → frame detect → uart encode
2. 完成 Linux 串口读取：`/dev/ttyS9` + termios + epoll
3. 接入 C 解码与持久化：EV1527 C decode + SQLite
4. 接入 MQTT 上报与下位机控制闭环
5. 接入 Qt5 UI，统一展示状态与历史

---

## 9. 运行链路边界（2026-04）

| 维度 | project2_master | project2_hardware |
|:--|:--|:--|
| RF 输入源 | `/dev/ttyS9` 实时串口（唯一） | RF 前端采样并通过 USART1 输出 |
| 其他输入 | 全部拒绝 | 不涉及 |
| 当前实现状态 | 已完成代码约束 | 保持现状 |

---

## 10. 运行与验证步骤（master）

```bash
# 最小构建
cmake -S . -B build_verify -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF
cmake --build build_verify --target rf_gateway

# 正向运行（通过）
./build_verify/linux_app/rf_gateway --rf-input /dev/ttyS9

# 负向验证（应拒绝）
# 将 --rf-input 设置为任何非 /dev/ttyS9 值，程序应拒绝启动并返回失败码
```
