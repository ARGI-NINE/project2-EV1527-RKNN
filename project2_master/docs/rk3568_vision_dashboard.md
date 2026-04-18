# RK3568 AIoT Dashboard（Qt5 C++ 前端）

## 1. 目标

前端实现为 **Qt5 Widgets + C++17**，入口在 `qt_gui/app/main.cpp`。

- 不使用 PyQt 作为运行前端。
- 保留 3 个页面：RF 状态、视觉检测、系统日志。
- 前端以子进程方式启动 `rf_gateway`，解析其 stdout 输出作为统一监控窗口。
- 板侧版本仅对接板端真实采集数据。

## 2. 前端架构

```
┌────────────────────────────────────────────────────┐
│             Qt5 C++ Dashboard (qt_gui)             │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────┐ │
│  │ RF 状态页 │  │ 视觉检测页   │  │ 系统日志页    │ │
│  └─────┬────┘  └──────┬───────┘  └───────┬───────┘ │
│        └──────────────┴──────────────────┘         │
│             DashboardBackend (线程安全)             │
└──────────────┬──────────────────────────────────────┘
               │
      ┌────────▼────────┐
      │ rf_gateway (C)  │
      │ --rf-input      │
      │   /dev/ttyS9    │
      │ stdout [RF] 行  │
      └─────────────────┘
```

### RF 数据源

- 仅支持启动并解析 `rf_gateway --rf-input /dev/ttyS9` 的标准输出（`[RF] addr=... key=...`）。
- 除 `/dev/ttyS9` 外的输入一律拒绝。

### 视觉数据源

- 视觉页面对接真实 **RKNN 推理管线**（`vision/rknn_pipeline.py`）。
- 管线架构：USB 摄像头采集 → RKNNLite YOLOv5 推理 → NMS 后处理 → 共享状态更新。
- 板侧版本不提供替代视觉输入入口。
- 若视觉管线未启动，页面显示离线状态并等待外部接入。

---

## 3. 页面功能

### 页面 1：RF 状态页

- 串口在线状态（绿/红指示）
- 实时波形预览
- 最近一次 EV1527 解码结果
- RF 历史事件表

### 页面 2：视觉检测页

- 视频区域（对接 RKNN 推理管线的实时画面）
- FPS 大字显示
- 摄像头/模型状态
- 检测列表

### 页面 3：系统日志页

- MQTT 上报统计
- CRC/丢包/总帧数
- 模型状态与 FPS
- CPU/内存/运行时长
- 按来源过滤日志 + 清空日志

#### 系统资源采集

CPU 和内存数据从 Linux 系统接口实时读取，**不使用随机数**：

- **CPU 使用率**：读取 `/proc/stat`，计算相邻两次采样的 busy/total 差值得出百分比。
- **内存使用率**：读取 `/proc/meminfo`，解析 `MemTotal` 和 `MemAvailable` 字段计算占用率。

---

## 4. 构建与运行

在 `project2_master` 目录执行：

```bash
export CROSS_COMPILE=aarch64-linux-gnu-
cmake -S . -B build \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=${CROSS_COMPILE}gcc \
    -DCMAKE_CXX_COMPILER=${CROSS_COMPILE}g++ \
    -DCMAKE_PREFIX_PATH=/path/to/qt5-aarch64 \
    -DBUILD_LINUX_APP=ON \
    -DBUILD_QT5_GUI=ON
cmake --build build
```

### 板侧运行

```bash
# 启动 Qt 仪表盘（自动以子进程启动 rf_gateway）
./build/qt_gui/rf_dashboard_qt5
```

Qt 仪表盘通过 `RFGatewayClient` 只加载应用目录下固定文件 `./rf_gateway`（不支持命令行覆盖和多路径回退）。
同时仅透传 `--rf-input /dev/ttyS9`，其他输入全部拒绝。

---

## 5. 依赖

| 依赖                | 用途                           | 必选 |
|:--------------------|:-------------------------------|:-----|
| Qt5 Core/Gui/Widgets | C++ 图形界面                  | ✓    |
| CMake ≥ 3.15        | 工程构建                       | ✓    |
| C++17 编译器        | 编译 qt_gui                    | ✓    |
| aarch64 交叉工具链  | RK3568 目标平台编译             | ✓    |
| Python 3 + rknnlite | 视觉推理模块（可选）            | 可选 |

---

## 6. 输入约束结论

- RF 输入仅允许 `/dev/ttyS9` 实时串口。
- 视觉输入仅允许板端真实采集链路。
- 非主链路输入在板侧部署中全部拒绝。
