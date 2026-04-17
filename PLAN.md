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
- 当前默认构建仍使用 `mock` transport，等待最终硬件验证后再决定是否切成默认 `real`

## Current Repository Status

目前仓库里已经具备这些能力：

- `app_config` 统一管理编译期配置
- `platform_log` 支持串口日志镜像到 SPIFFS、per-boot log、清理旧日志、live log
- `platform_fs` 支持 SPIFFS 文件管理、上传、下载、删除、伪目录
- `cn105_uart` 固定使用 `UART1 RX=GPIO26 TX=GPIO32 2400 8E1`
- `platform_wifi` 为 STA-only，失败后只重连，不 fallback AP
- `web` 提供 `/`、`/debug`、`/logs`、`/files`、`/admin`
- `core_cn105` 提供 packet builder、parser、checksum、Fahrenheit roundtrip、shared state
- `homekit_bridge` 已绑定当前 CN105 state model，并支持 Home App / WebUI 同步
- `cn105_transport` 已具备 connect / info / set 的真实 UART transport 逻辑

## What Is Still Actually Remaining

严格来说，现在剩下的不是“大功能开发”，而是最后的定版验证：

1. 真实 CN105 线缆接入后的硬件验证
2. 决定是否把 `kCn105UseRealTransport` 默认改成 `true`
3. 如果切到默认 real transport，再做一次长期运行稳定性观察

也就是说：

- `mock` 模式现在更像开发/调试 fallback
- `real` 模式已经不是“未来要写”的功能，而是“要不要默认启用”的定版选择

## Recommended Next Checkpoint

建议按这个顺序完成最后收尾：

1. 保持当前默认 `mock`，先确认 WebUI 和 HomeKit 在现有板子上持续稳定
2. 接上 CN105 线缆后，把 `kCn105UseRealTransport` 改成 `true`
3. 验证 connect / info poll / set command / HomeKit 同步
4. 如果真实空调控制稳定，再把默认 transport 改成 `real`
5. 文档改成“stable release baseline”，把 `mock` 明确写成开发回退模式

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

**代码层面已经基本完成，剩下的是默认 real transport 前的最终硬件验证与定版。**
