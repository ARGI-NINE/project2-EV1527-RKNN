# RK3568 AIoT Dashboard — RF + RKNN 视觉集成方案

## 1. 系统概述

在原有 EV1527 自适应解码器 + AIoT 网关的基础上，新增：

- **RKNN 视觉 Pipeline**：基于 RK3568 NPU 加速，3 线程架构部署 YOLOv5s 目标检测
- **Qt 统一 Dashboard**：3 页 Tab 界面，同时监控 RF 子系统和视觉子系统

两个子系统独立运行，不做数据 fusion，仅在 Qt Dashboard 中统一展示。

## 2. 架构图

```
┌─────────────────────────────────────────────────────┐
│                  Qt Dashboard (PyQt5)                │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ RF 状态页 │  │ 视觉检测页   │  │ 系统日志页    │  │
│  └─────┬────┘  └──────┬───────┘  └───────┬───────┘  │
│        │              │                  │           │
│        └──────────┬───┴──────────────────┘           │
│              DashboardBackend (线程安全)               │
└──────────────┬──────────────────┬────────────────────┘
               │                  │
    ┌──────────▼──────────┐  ┌───▼───────────────────┐
    │  RF Gateway 子系统   │  │ RKNN Vision Pipeline  │
    │  (C epoll + decode)  │  │  (3 线程 Python)      │
    │  串口 → 协议解析     │  │  采集→推理→后处理     │
    │  → EV1527 解码       │  │  MockCamera/RKNNLite  │
    │  → SQLite / MQTT     │  │  → YOLOv5 检测        │
    └──────────────────────┘  └───────────────────────┘
```

## 3. 视觉 Pipeline 3 线程架构

```
线程1: Capture Thread          线程2: Inference Thread       线程3: PostProcess Thread
┌─────────────────┐           ┌───────────────────┐         ┌──────────────────────┐
│ 打开摄像头/Mock   │           │ 加载 RKNN 模型    │         │ YOLOv5 后处理        │
│ 读取帧           │  Queue    │ 运行 NPU 推理     │  Queue  │ NMS + 过滤           │
│ BGR→RGB + Resize │ ──────►  │ 输出 3 个特征图    │ ──────► │ 更新共享状态          │
│ 帧率控制          │           │                   │         │ FPS 计算             │
└─────────────────┘           └───────────────────┘         └──────────────────────┘
```

- **Queue maxsize=2**：生产者过快时自动丢帧，保证实时性
- **VisionPipelineState**：线程安全共享状态，UI 定时轮询

## 4. Qt Dashboard 三页设计

### 页面 1：RF 状态页
| 区域 | 内容 |
|------|------|
| 串口状态 | 绿/红指示灯 + 端口名 + 在线帧数 |
| 波形预览 | 最近一帧脉冲序列的高低电平波形 |
| 解码结果 | 地址、按键、置信度、解码来源 |
| 历史事件 | 时间、地址、按键、置信度、来源（表格） |

### 页面 2：视觉检测页
| 区域 | 内容 |
|------|------|
| 视频画面 | 摄像头实时视频 + 检测框 |
| FPS | 大号字体显示当前帧率 |
| 状态 | 摄像头在线/离线，模型加载状态 |
| 检测列表 | 类别名 + 置信度列表 |

### 页面 3：系统日志页
| 区域 | 内容 |
|------|------|
| MQTT 日志 | 上报次数 + 最近消息 |
| 串口错误 | CRC 错误数、丢包数、总帧数 |
| 模型状态 | 加载状态、推理 FPS、错误信息 |
| 系统资源 | CPU%、内存%、运行时长 |
| 全量日志 | 可过滤的日志文本区（暗色终端风格） |

## 5. 新增文件结构

```
project2/
  vision/                        # 视觉子系统（新增）
    __init__.py
    yolov5_postprocess.py        # YOLOv5 后处理（sigmoid, NMS, draw）
    rknn_pipeline.py             # 3 线程 Pipeline + VisionPipelineState
    mock_camera.py               # 模拟摄像头 + 模拟 RKNN（测试用）
  qt_dashboard/                  # Qt Dashboard（新增）
    __init__.py
    main.py                      # 主入口（暗色主题 + 3 Tab）
    backend.py                   # 统一后端数据管理
    rf_page.py                   # RF 状态页
    vision_page.py               # 视觉检测页
    log_page.py                  # 系统日志页
  tests/
    test_integration.py          # 集成测试（20 项，全部通过）
```

## 6. 运行方式

### 模拟模式（无硬件，PC 测试）

```bash
cd project2
python -m qt_dashboard.main --mock
```

- RF：自动生成模拟 EV1527 解码事件
- 视觉：MockCamera 生成合成帧，MockRKNNLite 产生模拟检测结果

### 开发板模式（RK3568 + 摄像头 + 串口）

```bash
python -m qt_dashboard.main --model /path/to/yolov5s.rknn --camera 0
```

- 使用真实 `rknnlite` 和 `cv2.VideoCapture`
- RF 需外部进程或集成 rf_gateway C 程序

### 视频文件测试

```bash
python -m qt_dashboard.main --mock --camera ./test.mp4
```

## 7. 测试

```bash
cd project2
python -m unittest tests.test_integration -v
```

全部 20 项测试覆盖：
- YOLOv5 后处理（sigmoid, xywh2xyxy, NMS, 空输入）
- MockCamera（创建、读帧、多帧差异）
- MockRKNNLite（加载、初始化、推理）
- VisionPipelineState（初始状态、更新、线程安全）
- VisionPipeline（启停生命周期）
- DashboardBackend（RF事件、波形、日志、清空、系统统计）
- 端到端集成（Pipeline → Backend → 数据读取）

## 8. 依赖

| 包 | 用途 | 必选 |
|----|------|------|
| numpy | 数组运算 | ✓ |
| opencv-python-headless | 图像处理 | ✓ |
| PyQt5 | GUI | ✓ |
| psutil | 系统资源监控 | 可选（无则用模拟数据） |
| rknnlite | NPU 推理 | 开发板专用 |
