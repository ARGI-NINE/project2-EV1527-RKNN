# project2_master 阅读总目录

这个目录不是一篇单独说明，而是一组按真实调用链拆开的讲解文档。

`project2_master` 里同时有 RF 链路和 vision 链路，但两条链路的边界并不一样：

- RF 的最小真实运行核心是 `linux_driver + linux_app`。
- Qt 不是 RF 实时链路的必要条件，它只是启动 `rf_gateway`、消费 JSON envelope stdout，并把结果聚合成页面和日志。
- vision 则是 Qt 进程里的本地运行时：`/dev/video*` 或可读本地视频文件（含 MP4）-> `V4L2Capture` / `MppDecoder` -> `RGA -> RKNN -> VisionSnapshot -> 页面`。

先把最关键的一句话记住：

`STM32 -> UART -> linux_driver/rf433_drv -> /dev/rf433 -> linux_app/rf_gateway(JSON envelope stdout) -> qt_gui`

Qt 在这条 RF 链路上是消费者，不是生产者。

## 阅读顺序

1. [Overview](project2_iot_design.md)
2. [Shared Protocol](project2_shared_protocol_deep_dive.md)
3. [Driver](project2_master_driver_deep_dive.md)
4. [Linux App](project2_master_userland_deep_dive.md)
5. [Qt and Vision](project2_master_qt_vision_deep_dive.md)
6. [Function and Module Index](project2_master_function_index.md)

## 每份文档负责什么

`project2_iot_design.md`
: 项目目标、构建入口、运行入口、模块图、最小真实运行核心、Qt/RF/vision 的真实边界。

`project2_shared_protocol_deep_dive.md`
: 共享协议的 ABI、编码规则，以及 hardware / driver 如何在公共 ABI 之外各自实现同构 parser 来消费同一套字节契约。

`project2_master_driver_deep_dive.md`
: `rf433_drv.c` 和 `rf433_ioctl.h` 的逐段深读，讲清楚 serdev 接收、状态机、kfifo、`read/poll/ioctl`、`online` 位。

`project2_master_userland_deep_dive.md`
: `linux_app` 的完整调用链：路径白名单、epoll 读帧、EV1527 解码、稳定分组、JSON envelope stdout ABI。

`project2_master_qt_vision_deep_dive.md`
: Qt 启动链路、`DashboardBackend` 共享状态、`RFGatewayClient` 如何消费 JSON envelope stdout、vision 本地线程如何产出 `VisionSnapshot`。

`project2_master_function_index.md`
: 关键函数和模块的回查索引，告诉你“函数在哪、作用是什么、应该去前面哪篇深读继续看”。

## 建议读法

- 先读 `project2_iot_design.md`，先把全局边界立住。
- 再读 `project2_shared_protocol_deep_dive.md` 和 `project2_master_driver_deep_dive.md`，把 `/dev/rf433` 是怎么长出来的看懂。
- 然后读 `project2_master_userland_deep_dive.md`，把 `rf_gateway` 的 JSON envelope stdout 契约看懂。
- 最后读 `project2_master_qt_vision_deep_dive.md`，再看 Qt 为什么只是 RF 的消费者、却又是 vision 的宿主。
- 回查任何函数时，用 `project2_master_function_index.md`。

## 这套文档怎么写

这套文档尽量按下面顺序讲代码：

1. 先给完整代码片段，不用省略号跳过关键逻辑。
2. 再解释这段代码在链路里的位置。
3. 再解释它依赖什么、把结果交给谁。
4. 最后补一句你读这一段时最该抓住的点。

## 最后一遍强调

如果你的目标是确认“板端 RF 链路是否真实跑通”，最小判断标准不是 Qt 页面亮没亮，而是：

1. `linux_driver/rf433_drv.ko` 已经把 UART 字节流变成 `/dev/rf433`
2. `linux_app/rf_gateway` 已经能从 `/dev/rf433` 读到帧、解码并输出 `type="rf_event"` 的单行 JSON envelope，且 `payload.pulse_us[]` 为真实脉冲数组

只要这两步成立，RF 的真实运行核心就成立了。Qt 只是后面的展示聚合层。
