# RF Gateway userland 教程与实现说明

## 目标和前置条件

本文面向维护 `linux_app` 的开发者。目标是在 Linux 构建 `rf_gateway`、从 `/dev/rf433` 读取定长 frame、理解稳定/去重与 JSON/MQTT，并能按统计定位故障。

需要 CMake、C11 编译器和 libmosquitto 开发包。运行板端链路还需已加载的 `rf433_drv` 和读取 `/dev/rf433` 的权限。

## 构建与测试

```bash
cmake -S project2_master -B build/master \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/master
ctest --test-dir build/master --output-on-failure
```

`rf_gateway_core` 真实包含 `../common/rf_protocol.c`；libmosquitto 缺失时配置阶段会明确失败。成功标准：gateway 链接，Unix 上 `rf_decode_contract` 和 `postprocess_labels_lifetime` 通过。

## 运行参数

```bash
./build/master/linux_app/rf_gateway --rf-input /dev/rf433 \
  --stable-repeat 2 \
  --stable-window 12 \
  --stable-near-bits 4 \
  --min-publish-confidence 0.72 \
  --publish-gap 6
```

`--rf-input` 只允许 `/dev/rf433`，且 `rf_source_open` 要求它是字符设备。其它参数分别控制需要的重复命中、组存活窗口、24-bit code 的近码合并阈值、最低 confidence 和同码再次发布间隔。

## 调用路径

`main` 初始化 source/MQTT/context 后调用 `rf_epoll_run`。epoll 等待 driver fd，并可创建统计 timerfd：timer create、arming 或 epoll registration 失败时会关闭已创建资源并仅禁用统计 callback，RF frame 主路径仍运行；timer read 也检查返回值。不要把旧文档里未检查的示例当成当前代码。

每个 frame：

1. `rf_decode_frame` 清零输出并调用 `rf_decode_ev1527_c_with_stats`；失败 confidence 因此是确定的 0，而非未初始化值。
2. confidence 不足则 `low_conf_drop`。
3. 24-bit code 按 Hamming distance 合并稳定组；`hits` 为 `uint32_t`，到 `UINT32_MAX` 饱和。
4. 达到 stable repeat 后仍受 publish gap 去重。
5. `build_rf_event_payload` 组 JSON，`emit_protocol_message` 尝试 MQTT 并始终写 stdout envelope。

stdout 结构：

```json
{"type":"rf_event","topic":"argi/device/rk3568-001/rf/event","mqtt_published":false,"payload":{"type":"rf_event","device_id":"rk3568-001"}}
```

上例只展示 envelope 与关键字段，不是完整 event 样例。Qt 读取每行 NDJSON；不要向 stdout 混写诊断，诊断应去 stderr。

## 状态与发布

- 启动/driver stats/关闭会生成 device status，topic `status` retained。
- 稳定事件 topic `rf/event` non-retained。
- 周期统计 topic `rf/stats` non-retained。
- broker 不可用时 `mqtt_published=false`，本地 JSON 仍可被 Qt 消费。

MQTT 固定为 `192.168.30.26:1883` 与根 `argi/device/rk3568-001`。详细字段/观察命令见 [MQTT 教程](mqtt_publish_tutorial_zh.md)。

## 排错

- `Unsupported --rf-input`：不要在 master 传文件/stdin；回放用 `project2_pc_sim`。
- open 失败：检查 driver 绑定、字符设备与权限。
- `short_read/read_error`：ABI 或设备读取异常，先核对 kernel/user 的 `rf433_ioctl.h`。
- `decode_no_frame` 高：检查约 50-pulse、波形比例和同步位置。
- `stable_drop` 高：重复次数/窗口或 RF 噪声不匹配。
- `dup_drop` 高：相同 code 在 publish gap 内，属于预期去重。
- stdout 有 event 但 broker 无消息：看 `mqtt_published` 与 stderr，再查网络/ACL。

## 验证边界与清理

当前 gateway 没有 signal handler 或 stop flag；`rf_epoll_run` 是无条件循环，遇到 `EINTR` 也会继续。因此不能声称 `Ctrl+C` 会让循环优雅返回、发布 shutdown status 或执行 Mosquitto/fd 清理。只有 `rf_epoll_run` 因其它路径返回到 `main` 时，现有清理代码才会执行；默认 SIGINT 终止不保证该路径。主机 CTest 不覆盖信号退出、`/dev/rf433`、driver ioctl、live broker、timer syscall fault injection或完整 WAV 稳定策略；目标部署仍需运行验收。
