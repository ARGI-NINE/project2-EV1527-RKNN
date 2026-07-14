# Project2 Master 当前函数与构建索引

本页用于定位，避免在文档复制整段实现。

## 构建目标

| 目标 | 主要成员 | 条件 |
|---|---|---|
| `rf_gateway_core` | `rf_epoll.c`、`rf_decode.c`、`rf_decode_c.c`、`rf_source.c`、`common/rf_protocol.c` | `BUILD_LINUX_APP=ON` |
| `rf_gateway` | `main.c`、`mqtt_publisher.c` + core + libmosquitto | `BUILD_LINUX_APP=ON` |
| `rf_dashboard_qt5` | `qt_gui` 的 app/core/rf/vision/log/widgets | `BUILD_QT5_GUI=ON` |
| `rf_decode_contract_test` | decode + shared encoder | Unix + `BUILD_TESTING=ON` |
| `postprocess_labels_lifetime_test` | `postprocess.cc` + lifecycle harness | `BUILD_TESTING=ON` |

## Linux driver

- parser：`parser_reset`、`parser_feed_byte`、`parser_emit_frame`。
- serdev：`rf433_receive_buf`、`rf433_write_wakeup_nop`。
- ABI：`rf433_misc_open/read/poll/ioctl`。
- 生命周期：`rf433_probe`、`rf433_remove`、`rf433_online_timer_fn`。

## Linux userland

```text
main
├─ rf_source_is_supported_path / rf_source_open
├─ mqtt_publisher_init
├─ emit_device_status(startup)
└─ rf_epoll_run
   ├─ consume_frames -> on_rf_frame
   │  └─ rf_decode_frame -> rf_decode_ev1527_c_with_stats
   └─ stats timer -> on_drv_stats
      ├─ ioctl GET_STATS/GET_STATUS
      └─ emit_rf_stats + emit_device_status
```

`rf_epoll.c` 的 `create_stats_timer` 会检查 timerfd create、settime、epoll add；失败时关闭部分资源并只禁用可选统计。`main.c` 的 `stable_group_update` 使用饱和 `uint32_t hits`，到 `UINT32_MAX` 后不回绕。

主要公共 API：

- `rf_source_is_supported_path/open/close`：只允许 `/dev/rf433` 并验证字符设备。
- `rf_epoll_run`：frame/stats callback 事件循环。
- `rf_decode_frame`、`rf_decode_get_runtime_stats`、`rf_decode_get_last_call_stats`。
- `mqtt_publisher_init/publish/is_connected/cleanup/error_string`。

## Qt RF

- `RFGatewayClient::start/stop/startGateway`：启动同目录 `rf_gateway`，直接使用 `defaultRFInputPath()`；不存在 `resolvedRfInputPath`。
- `drainProtocolBuffer/flushProtocolBuffer/handleProtocolLine`：NDJSON 分帧与 envelope 分派。
- `DashboardBackend`：保存 RF、Vision、protocol stats 和日志快照。
- `RFStatusPage::refresh`、`SystemLogPage::refresh`、`WaveformWidget::paintEvent`：UI 呈现。

## Qt Vision

- 生命周期：`VisionRuntime::start/stop/workerLoop`。
- 安全尺寸：`tryMultiplySize`、`computeNv12Bytes/RgbBytes/BgraBytes`。
- 数据搬运：`copyFrameToAiPool`、`copyPostInferFrameToPostStreamPool`。
- 输出：`paintDetections`、`publishDetection`、`publishStreamStatus`。

`publishDetection` 的调用位于 RGB 转换成功且检测组非空的路径；encoder-open 重试失败时，实际调用向 `publishStreamStatus` 分别传入 `state="offline"` 与 `reason="open_failed"`，所以 `open_failed` 不是状态名。

## 测试关联

- `rf_decode_contract_test`：协议 golden vector、50/49 pulse、确定性失败 stats。
- `postprocess_labels_lifetime_test`：标签加载、二次使用、冲突路径。
- PC/独立测试仓库另测 parser 恢复和超长长度。

硬件和板端媒体调用没有被主机 CTest 替代；函数索引不构成运行认证。
