# linux_driver（历史样例边界说明）

`linux_driver/rf433_drv.c` 仅为历史研究样例，**不属于** `project2_master` 当前运行链路。

## 弃用结论

- 该样例创建设备节点为 `/dev/rf433`，而非板侧主链路的 `/dev/ttyS9`。
- 该样例未接管 RK3568 UART9 的厂商驱动职责（设备树绑定、pinctrl、clock、IRQ、DMA）。
- `project2_master/linux_app` 运行时直接通过标准 tty 层读取 `/dev/ttyS9`，因此不能用该样例替代厂商 UART 驱动。

## 适用边界

- 可用于字符设备环形缓冲/`poll` 接口教学与实验。
- 不可用于替代板侧量产串口驱动。
- 不作为 `rf_gateway` 的可选输入路径；主链路仍然只允许 `/dev/ttyS9`。
