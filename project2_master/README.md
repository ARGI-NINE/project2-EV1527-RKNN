# project2_master — RK3568 板侧部署代码

## 1. 项目定位

本目录是 **RK3568 Linux 开发板**的板侧最终运行代码，实现两大功能：

- **RF433 IoT 网关**：通过 UART9（`/dev/ttyS9`）接收下位机 STM32 采集的 RF433 射频数据，完成 EV1527 协议解码、稳定分组与去重，最终将解码结果发布到 stdout。
- **Qt5 可视化仪表盘**：C++ Qt5 桌面端程序，以子进程方式启动 rf_gateway，解析其 stdout 输出，提供 RF 状态页、视觉页和系统日志页三大功能面板。
- **视觉推理**（可选）：基于 RKNNLite 的 3 线程流水线，使用 USB 摄像头实时采集 + YOLOv5 推理。

本目录仅面向 RK3568 板侧实机部署。  
RF 输入只允许 `/dev/ttyS9` 实时串口，其他输入一律拒绝。STM32 下位机固件位于 `../project2_hardware/`。

---

## 2. 系统架构

```
┌─────────────────┐     UART (9600 8N1)    ┌─────────────────────────┐
│  STM32 下位机    │ ───────────────────────▶ │  RK3568 /dev/ttyS9      │
│  RF433 采集      │     /dev/ttyS9          │                         │
└─────────────────┘                         │  rf_gateway (C)         │
                                            │  ├ rf_source: 串口读取   │
                                            │  ├ rf_epoll: epoll 事件  │
                                            │  ├ rf_decode: EV1527 解码│
                                            │  └ main: 稳定分组+发布   │
                                            │       │                  │
                                            │       │ stdout [RF] 行   │
                                            │       ▼                  │
                                            │  rf_dashboard_qt5 (C++) │
                                            │  ├ rf_gateway_client:   │
                                            │  │   子进程管理+stdout解析│
                                            │  ├ rf_status_page: RF页 │
                                            │  ├ vision_page: 视觉页  │
                                            │  ├ system_log_page: 日志│
                                            │  └ waveform_widget: 波形│
                                            │                         │
                                            │  Vision (Python, 可选)  │
                                            │  ├ rknn_pipeline: 3线程 │
                                            │  └ yolov5_postprocess   │
                                            └─────────────────────────┘
```

### 数据通路

1. **STM32** 采集 RF433 射频信号，按自定义协议（`rf_protocol.h`）封帧后通过 USART1 发出。
2. **RK3568 UART9**（`/dev/ttyS9`）接收串口字节流（9600 波特率，8N1，无校验，无流控）。
3. **rf_gateway** 通过 epoll 读取串口 fd，调用 `rf_decode` 进行 EV1527 解码，经稳定分组（Hamming 距离合并）和去重后，以固定格式输出到 stdout：
   ```
   [RF] addr=0x12D1B1 key=1 conf=0.95 source=C pulses=50 seq=42 decode_us=123
   ```
4. **rf_dashboard_qt5** 以 `QProcess` 启动 rf_gateway 子进程，逐行解析 `[RF]` 前缀的 stdout 行，更新 UI 页面。

---

## 3. 目录结构

```
project2_master/
├── CMakeLists.txt          # 顶层构建入口（控制 linux_app / qt_gui 子项目）
├── README.md               # 本文件
├── README_IOT.md           # IoT 设计补充文档
├── requirements.txt        # Python 视觉模块依赖（numpy, opencv 等）
│
├── common/                 # 公共协议定义
│   ├── rf_protocol.c       #   RF 协议帧编解码实现
│   └── rf_protocol.h       #   RF 协议帧结构定义
│
├── linux_app/              # rf_gateway — C 语言网关主程序
│   ├── CMakeLists.txt      #   构建脚本（生成 rf_gateway 可执行文件）
│   ├── main.c              #   入口：参数解析、稳定分组逻辑、stdout 发布
│   ├── rf_source.c/.h      #   串口打开 + termios 配置（9600 8N1）
│   ├── rf_epoll.c/.h       #   epoll 事件循环，读取串口帧
│   ├── rf_decode.c/.h      #   EV1527 解码核心（C 实现）
│   └── rf_decode_c.c/.h    #   解码辅助/统计
│
├── linux_driver/           # 历史驱动样例（master 运行路径不启用）
│   ├── README.md           #   样例边界/弃用说明
│   └── rf433_drv.c         #   仅保留源码参考，不作为当前输入路径
│
├── qt_gui/                 # rf_dashboard_qt5 — Qt5 C++ 仪表盘
│   ├── CMakeLists.txt      #   构建脚本（需要 Qt5 Core/Gui/Widgets）
│   ├── app/                #   程序入口
│   │   ├── main.cpp        #     Qt Application 启动
│   │   └── main_window.cpp/.h  # 主窗口（页面容器）
│   ├── core/               #   后端逻辑
│   │   ├── app_options.h   #     命令行选项定义
│   │   ├── app_palette.cpp/.h  # UI 主题/调色板
│   │   ├── common_types.h  #     公共类型
│   │   ├── dashboard_backend.cpp/.h  # 后端状态管理
│   │   └── rf_utils.cpp/.h #     RF 数据解析工具
│   ├── rf/                 #   RF 功能页
│   │   ├── rf_gateway_client.cpp/.h  # 子进程管理（QProcess 启动 rf_gateway）
│   │   └── rf_status_page.cpp/.h     # RF 状态展示页
│   ├── log/                #   日志功能页
│   │   └── system_log_page.cpp/.h    # 系统日志页
│   ├── vision/             #   视觉功能页
│   │   └── vision_page.cpp/.h        # 视觉展示页（对接 Python 推理）
│   └── widgets/            #   自定义控件
│       └── waveform_widget.cpp/.h    # 波形显示控件
│
├── vision/                 # Python 视觉推理模块（RK3568 RKNN）
│   ├── __init__.py
│   ├── rknn_pipeline.py    #   3 线程流水线：采集→推理→后处理
│   └── yolov5_postprocess.py   # YOLOv5 后处理（NMS 等）
│
├── docs/                   # 设计文档
│   ├── project2_iot_design.md      # IoT 整体设计
│   └── rk3568_vision_dashboard.md  # 视觉仪表盘设计
│
└── build_verify/           # 构建验证输出（仅供 CI 检查）
```

---

## 4. 串口配置

rf_gateway 通过 `rf_source.c` 中的 `rf_source_configure_serial()` 配置串口参数，**必须与 STM32 下位机（`RF_Uart.c`）保持一致**：

| 参数       | 值          | 说明                          |
|:-----------|:------------|:------------------------------|
| 设备节点   | `/dev/ttyS9` | RK3568 UART9                  |
| 波特率     | 9600        | `cfsetispeed(&tty, B9600)`    |
| 数据位     | 8           | `CS8`                         |
| 校验位     | 无          | `~PARENB`                     |
| 停止位     | 1           | `~CSTOPB`                     |
| 流控       | 无          | `~CRTSCTS`                    |
| 模式       | Raw         | `cfmakeraw()`                 |
| 读超时     | VMIN=0, VTIME=0 | 非阻塞读取，无数据立即返回 |

---

## 5. 构建说明

### 5.1 环境要求

- **目标平台**：RK3568 Linux（aarch64）
- **交叉编译工具链**：aarch64-linux-gnu-gcc / g++（或 Rockchip 官方 SDK 工具链）
- **CMake** ≥ 3.15
- **Qt5** ≥ 5.12（需 Core、Gui、Widgets 模块，aarch64 交叉编译版本）
- **Python 3**（仅视觉模块需要，板端安装 numpy、opencv-python-headless、rknnlite）

### 5.2 交叉编译步骤

```bash
# 设置交叉编译工具链（以 Rockchip SDK 为例）
export CROSS_COMPILE=aarch64-linux-gnu-
export CC=${CROSS_COMPILE}gcc
export CXX=${CROSS_COMPILE}g++

# 配置（指定 Qt5 交叉编译安装路径）
cmake -S . -B build \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=${CC} \
    -DCMAKE_CXX_COMPILER=${CXX} \
    -DCMAKE_PREFIX_PATH=/path/to/qt5-aarch64 \
    -DBUILD_LINUX_APP=ON \
    -DBUILD_QT5_GUI=ON

# 编译
cmake --build build -j$(nproc)
```

编译产物：
- `build/linux_app/rf_gateway` — RF 网关可执行文件
- `build/qt_gui/rf_dashboard_qt5` — Qt5 仪表盘可执行文件

### 5.3 仅编译网关（不需要 Qt5）

```bash
cmake -S . -B build -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF
cmake --build build
```

---

## 6. 运行说明

### 6.1 运行 rf_gateway（独立模式）

```bash
# 默认参数：读取 /dev/ttyS9，稳定分组 repeat=2
./rf_gateway

# 自定义参数
./rf_gateway \
    --rf-input /dev/ttyS9 \
    --stable-repeat 2 \
    --stable-window 12 \
    --stable-near-bits 4 \
    --preferred-code 0x12D1B1 \
    --min-publish-confidence 0.72 \
    --publish-gap 6
```

**参数说明：**

| 参数                       | 默认值      | 说明                                       |
|:---------------------------|:------------|:-------------------------------------------|
| `--rf-input`               | /dev/ttyS9  | 串口设备路径（仅允许 `/dev/ttyS9`，其余直接拒绝） |
| `--stable-repeat`          | 2           | 稳定分组需要的最少一致帧数                   |
| `--stable-window`          | 12          | 分组记忆窗口（帧数）                         |
| `--stable-near-bits`       | 4           | Hamming 距离合并阈值（24bit 编码空间）       |
| `--preferred-code`         | 0x12D1B1    | 优先编码（分组中出现则优先输出）              |
| `--min-publish-confidence` | 0.72        | 最低发布置信度                               |
| `--publish-gap`            | 6           | 同一编码重复发布最小帧间隔                   |

**输出格式：**

```
rf_gateway running: rf=/dev/ttyS9 stable=2 near=4 stable_win=12 preferred=0x12D1B1 pub_conf=0.72 gap=6
[RF] addr=0x12D1B1 key=1 conf=0.95 source=C pulses=50 seq=42 decode_us=123
[RF_PARSE_STATS] crc_errors=3 parse_errors=7 last_error=crc_mismatch
[RF] addr=0x0A3B2C key=2 conf=0.88 source=C pulses=48 seq=87 decode_us=110
...
[RF_STATS] frames_total=1000 decode_ok=800 ... published=42 proto_crc_err=3 proto_parse_err=7
[RF_IO_STATS] read_eintr=0 read_eagain=1024 read_eof=0 read_error=0 epoll_eintr=1 epoll_error=0
```

### 6.2 运行 Qt5 仪表盘

```bash
# 将 rf_gateway 放到 Qt 应用目录下（同级文件）
# Qt 仪表盘会以固定路径策略启动该二进制
./rf_dashboard_qt5
```

Qt 仪表盘通过 `RFGatewayClient` 仅加载应用目录下固定文件 `./rf_gateway`。
不支持 `--gateway` 覆盖、上级目录回退或多候选搜索，避免网关程序注入。

### 6.3 视觉模块（可选）

```bash
# 安装依赖
pip3 install numpy opencv-python-headless
# rknnlite 需从 Rockchip NPU SDK 安装

# 视觉模块由 Qt 仪表盘的 vision_page 调用，也可独立测试：
python3 -c "
from vision.rknn_pipeline import VisionPipeline
p = VisionPipeline(model_path='yolov5s.rknn', camera_source=0)
p.start()
"
```

视觉流水线 3 线程架构：
- **Capture 线程**：USB 摄像头读帧 → resize + BGR→RGB → 入队
- **Inference 线程**：出队 → RKNNLite 推理 → 入队
- **PostProcess 线程**：出队 → YOLOv5 后处理（NMS）→ 更新共享状态

---

## 7. 输入约束摘要

- 唯一有效 RF 输入：`/dev/ttyS9`。
- 串口配置固定：`9600 8N1 + Raw + 无流控`。
- 非 `/dev/ttyS9` 输入参数在启动阶段直接拒绝。

---

## 8. 输入源约束（2026-04）

当前 `project2_master` 已收敛为**单一路径**：

- 仅支持 `RK3568 UART9`：`/dev/ttyS9`
- 串口打开方式固定为：`O_RDWR | O_NOCTTY | O_NONBLOCK`
- termios 固定为：`9600 8N1 + Raw + 无流控`
- 除 `/dev/ttyS9` 外的所有输入路径均拒绝
- 网关与 UI 仅按 `/dev/ttyS9` 主链路运行

`linux_driver/rf433_drv.c` 仅保留为历史样例，不参与当前 master 运行链路（详见 `linux_driver/README.md`）。

### 8.1 串口驱动结论（为何不能替代厂商 UART 驱动）

结论：`linux_driver/rf433_drv.c` 不能替代 RK3568 厂商 UART 驱动，master 仍必须依赖厂商 tty/UART 驱动提供 `/dev/ttyS9`。

依据文件位置：

- `linux_app/rf_source.c`：`rf_source_open()` 直接 `open("/dev/ttyS9", O_RDWR|O_NOCTTY|O_NONBLOCK)` 并通过 `termios` 配置串口，依赖标准 tty 设备。
- `linux_app/main.c`：`--rf-input` 仅允许 `/dev/ttyS9`，并拒绝其他路径。
- `linux_driver/rf433_drv.c`：样例创建设备是 `/dev/rf433`，且注释明确为历史/实验代码；未实现 RK3568 UART9 的设备树 probe 与 SoC 资源接管。
- `linux_app/CMakeLists.txt`：master 运行目标仅构建 `linux_app/*`，未编译或链接 `linux_driver` 样例。

---

## 9. 运行链路边界

| 维度 | project2_master（板侧） | project2_hardware（下位机） |
|:--|:--|:--|
| RF 输入源 | `/dev/ttyS9` 实时串口（唯一） | RF 接收模块 + TIM2 采集 |
| 其他输入 | 全部拒绝 | 不涉及 |
| 串口职责 | 上位机读取与解码 | USART1 发送 AA55 帧 |
| 当前实现状态 | 已落地（代码强约束） | 保持现状 |

---

## 10. 运行与验证步骤

1. 最小编译验证（仅网关）：
   ```bash
   cmake -S . -B build_verify -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF
   cmake --build build_verify --target rf_gateway
   ```
2. 启动网关（默认 UART9）：
   ```bash
   ./build_verify/linux_app/rf_gateway --rf-input /dev/ttyS9
   ```
3. 负向验证（应被拒绝）：
   - 将 `--rf-input` 设为任意非 `/dev/ttyS9` 值时，程序应立即拒绝并退出。

---

## 11. 相关目录

| 目录                     | 说明                              |
|:-------------------------|:---------------------------------|
| `../project2_hardware/`  | STM32 下位机固件（RF 采集 + 串口发送）|
| `docs/`                  | 本项目设计文档                     |
