# 当前状态与剩余工作

## Summary

这个仓库的迁移工作本质上已经完成了。

当前 `main` 已经是完整的 **ESP-IDF + Espressif esp-homekit-sdk** 工程，不再是“正在从 Arduino/HomeSpan 迁移中”的中间态。之前文档里提到的 `M7 HomeKit over mock CN105`、`M8 Real CN105 Transport` 更适合视为历史实施阶段，不应该继续当成当前 UI 或 README 的主状态文案。

更准确的描述是：

- 平台层已经完成
- WebUI 已恢复并可用
- CN105 offline protocol core 已完成
- HomeKit bridge 已完成
- real CN105 transport 代码已接入
- 当前默认构建已经使用 `real` transport，`mock` 仅保留为开发/回退模式

## Current Repository Status

目前仓库里已经具备这些能力：

- `app_config` 统一管理编译期配置
- `platform_log` 支持串口日志镜像到 SPIFFS、per-boot log、清理旧日志、live log
- `platform_fs` 支持 SPIFFS 文件管理、上传、下载、删除、伪目录
- `cn105_uart` 固定使用 `UART1 RX=GPIO26 TX=GPIO32 2400 8E1`
- `platform_wifi` 为 STA-only，失败后只重连，不 fallback AP
- `web` 提供 `/`、`/debug`、`/logs`、`/files`、`/admin`
- WebUI 静态页面使用 build-time gzip 小分片和浏览器端串行 loader，避免 ESP32 上 runtime 拼整页和大 chunked response
- `core_cn105` 提供 packet builder、parser、checksum、Fahrenheit roundtrip、shared state
- `homekit_bridge` 已绑定当前 CN105 state model，并支持 Home App / WebUI 同步
- `cn105_transport` 已具备 connect / info / set 的真实 UART transport 逻辑

## What Is Still Actually Remaining

严格来说，现在剩下的不是“大功能开发”，而是最后的定版验证：

1. 真实 CN105 线缆接入后的硬件验证
2. 默认 real transport 的长期运行稳定性观察
3. 若出现兼容性问题，再决定是否临时回退到 `mock` 作为调试模式

也就是说：

- `mock` 模式现在更像开发/调试 fallback
- `real` 模式已经是默认主路径，剩下的是实机稳定性验证

## Recommended Next Checkpoint

建议按这个顺序完成最后收尾：

1. 接上 CN105 线缆，直接验证默认 `real` transport
2. 验证 connect / info poll / set command / HomeKit 同步
3. 做一轮持续运行观察
4. 如果真实空调控制稳定，就把当前状态视为 stable baseline
5. `mock` 保留为开发回退模式，不再作为默认路线

## Validation Checklist

- `./build.py --quiet-first build`
- `./build.py --quiet-first flash-auto --no-build`
- `./build.py serial-log --seconds 20`
- `http://<esp-ip>:8080/` 可访问
- `http://<esp-ip>:8080/debug` 可访问
- `http://<esp-ip>:8080/logs` 可访问
- `http://<esp-ip>:8080/files` 可访问
- Apple Home 可配对
- HomeKit 与 WebUI 状态同步正常
- 若开启 real transport，CN105 connect / info / set 正常

## Practical Status

一句话总结当前项目状态：

**代码层面已经基本完成，剩下的是默认 real transport 的最终硬件验证与稳定性观察。**
