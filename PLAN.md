# ESP-IDF + ESP HomeKit SDK 从零重建迁移计划

## Summary

目标是把当前 Arduino/HomeSpan 项目视为“功能参考实现”，在 `main` 上直接重建为一个 **纯 ESP-IDF + Espressif ESP HomeKit SDK** 工程。
不保留 Arduino 代码路径，不做双轨维护；旧实现已经在分支里作为回滚和对照来源。

新的 HomeKit 方向：

- 使用 Espressif [`esp-homekit-sdk`](https://github.com/espressif/esp-homekit-sdk)，不是 HomeSpan，也不是 esp-matter。
- 优先参考 `esp-homekit-sdk/examples/common` 里的 Wi-Fi、provisioning、factory/setup 辅助结构。
- 首版仍然以 Apple Home / HomeKit 为目标，不做 Matter/Android commission 目标。
- Wi-Fi 只做 STA。连接失败时保持离线并持续重连，**不要回退开启 AP**。

执行策略是：

1. 先把 `main` 清到只剩一个可编译的 IDF hello world。
2. 建立新的组件化工程骨架。
3. 先恢复基础设施与 CN105 核心。
4. 再恢复 WebUI 与诊断能力。
5. 最后接入 `esp-homekit-sdk`，并逐步补齐 HomeKit 能表达的控制能力。

首版原则：

- WebUI 尽量保留当前大部分能力。
- HomeKit 目标是稳定覆盖开关、模式、当前温度、目标温度、风速等核心控制面。
- 对 HomeKit 标准模型无法完整表达的 CN105 细粒度语义，先保留在本地 WebUI/后端，不阻塞主迁移。

## Implementation Changes

### 1. 仓库重置与新工程骨架

- 在 `main` 上移除当前 Arduino 入口与旧结构，只保留文档、参考分支信息和必要配置模板。
- 新建标准 ESP-IDF 工程结构：
  - `main/app_main.cpp`
  - `components/app_config`
  - `components/platform_log`
  - `components/platform_fs`
  - `components/platform_wifi`
  - `components/platform_uart`
  - `components/core_cn105`
  - `components/web`
  - `components/homekit_bridge`
- 使用 `ESP-IDF v5.4.1` 作为当前本地验证版本。
- 后续接入 `esp-homekit-sdk` 作为 HomeKit 主依赖，优先对齐它的 example/common 结构。
- `main/app_main.cpp` 只负责系统启动、组件初始化顺序和主生命周期编排。

### 2. 先建立最小可运行基线

- Phase 0/1/2/3 只做 IDF skeleton、日志、SPIFFS、CN105 UART、Wi-Fi STA。
- 先验证：
  - `./build.py build` 正常
  - `./build.py flash-auto --no-build` 正常
  - 串口日志正常
  - 分区表满足 app + SPIFFS + NVS 需求
  - `GPIO26 RX / GPIO32 TX` 作为 CN105 UART 初始化
  - Wi-Fi STA 能连接；连接失败时不启动 fallback AP
- 这一阶段不接 CN105 protocol、不接 WebUI、不接 HomeKit，只把平台地基打稳。

### 3. 代码分层与 .h/.cpp 拆分规则

- 所有 header-heavy 代码迁入明确的 `.h/.cpp` 对：
  - `MitsubishiProtocol`
  - `DebugLog`
  - `FileSystemManager`
  - `WiFiManager`
  - `WebUI`
  - `Cn105Serial`
  - HomeKit 服务绑定改为基于 `esp-homekit-sdk` 的 `homekit_bridge`
- 保留为 header-only 的只限：
  - `app_config`
  - `MitsubishiTypes`
  - 极小型纯函数/constexpr 工具
- `core_cn105` 不允许直接依赖以下对象：
  - Arduino `HardwareSerial`
  - Arduino `String`
  - Arduino `WebServer`
  - Arduino `WiFiClass`
  - SPIFFS 具体 API
  - HomeSpan
  - esp-homekit-sdk
- 平台相关能力通过窄接口注入：
  - `IUartTransport`
  - `ILogSink`
  - `IClock`
  - `IFileStore`
  - `INetworkStatus`

### 4. 基础设施恢复顺序

- 先恢复 `platform_log`
  - 保留串口日志和文件日志。
  - SPIFFS 满时，当前 active log 清空后继续记录，不中断运行。
- 再恢复 `platform_fs`
  - 后续支持 `/logs`、`/files` 页面所需的文件列举、下载、上传、删除、伪目录能力。
- 再恢复 `platform_wifi`
  - 只使用 STA。
  - 禁用 Wi-Fi power save。
  - heartbeat 状态日志保留 IP、RSSI、MAC、last event。
  - 连接失败或无 credentials 时不启动 AP，只保持离线并按间隔重连。
- 再恢复 `platform_uart`
  - 固定 `RX=26`、`TX=32`
  - 2400 8E1
  - debug 串口保持默认系统日志通道，不与 CN105 复用。

### 5. CN105 业务核心恢复顺序

- 先恢复 `Mock` 模式，确保：
  - payload builder
  - 状态模型
  - 华氏度优先行为
  - 原始包解析
  - WebUI 状态同步
- 再恢复 `Real` transport：
  - CONNECT/ACK
  - INFO 轮询
  - SET payload builder
  - 状态回填
- 这一层完成后，系统即便还没 HomeKit，也必须具备“旧项目同等级别的本地调试能力”。

### 6. WebUI 恢复策略

- 用 `esp_http_server` 重写 Web 层，不保留 Arduino `WebServer`。
- 首阶段保留当前主要页面与接口：
  - `/`
  - `/debug`
  - `/logs`
  - `/files`
  - `/api/status`
  - `/api/remote/build`
  - `/api/mock/decode`
  - `/api/raw/decode`
  - 日志与文件管理相关 API
- WebUI 文案继续中文，代码注释继续英文。
- 页面结构保留现有信息架构，但实现上允许先不追求完全像素级一致；行为一致优先于视觉一致。

### 7. ESP HomeKit SDK 集成策略

- 不迁移 `HomeSpan` 代码；`homekit_bridge` 直接基于 `esp-homekit-sdk` 重新实现。
- 优先参考 `esp-homekit-sdk/examples/common`：
  - Wi-Fi/provisioning 结构可以参考，但本项目当前使用本地 hardcoded STA config。
  - setup code/setup id/factory NVS 的设计要和 SDK 习惯保持兼容。
- HomeKit 首先建立一个稳定可 pair 的空调主设备模型，优先恢复：
  - 开关
  - 模式
  - 当前温度
  - 目标温度
  - 设备在线/连接状态
- 之后再补 fan/swing/更多运行状态映射。
- 对标准 HomeKit 模型无法精确承载的 CN105 功能：
  - 内部状态仍完整保留
  - WebUI 继续完整可见可调
  - HomeKit 暂时映射到最接近的标准语义，不为了“全量暴露”破坏设备一致性

### 8. 删除旧实现的时机

- 真正删除旧 Arduino 文件发生在 Phase 0 开始时，即创建 IDF hello world 骨架之前。
- 删除范围包括：
  - `.ino` 入口
  - 旧 `src/` 中不再复用的 Arduino/HomeSpan 绑定实现
  - PlatformIO/Arduino 专属构建痕迹
- 但在逻辑层面，`MitsubishiTypes`、协议常量、协议映射表、状态语义、Mock 数据行为仍作为迁移参考继续使用。

## Test Plan

### Phase 0: IDF skeleton

- `./build.py build` 成功
- `./build.py flash-auto --no-build` 成功
- 设备启动打印版本、分区、heap、挂载状态

### Phase 1: platform services

- SPIFFS 可写
- 持久日志文件能创建
- UART1 使用 `GPIO26/32` 正常初始化

### Phase 2: Wi-Fi STA

- `MUJI` 本地配置能连接
- Wi-Fi power save 关闭
- heartbeat 显示 STA IP/RSSI/MAC/last event
- Wi-Fi 失败时不会启动 fallback AP

### Phase 3: minimal WebUI

- HTTP server 端口 80 可访问
- `/api/health` 或等价最小接口正常
- 页面能显示 Wi-Fi/platform status

### Phase 4: mock CN105

- `/api/status` 返回与旧项目等价字段
- `/api/remote/build` 行为与旧项目一致
- 华氏度输入/回读一致
- raw packet decode 与 mock state apply 正常

### Phase 5: real CN105

- CONNECT 成功
- INFO 轮询稳定
- SET 指令能触发真实空调状态变化
- 真实状态变化能回填到内部模型和 WebUI

### Phase 6: HomeKit

- 设备可被 Apple Home 配对
- 开关/模式/温度双向同步
- 断电重启后 pairing 信息与设备状态恢复符合预期
- HomeKit 不支持的 CN105 状态不被错误覆写

## Assumptions

- 旧 Arduino/HomeSpan 代码不再作为生产路径维护，只作为参考来源。
- `main` 可以直接被重建，不需要保留现有文件布局。
- 线缆到位后才做真实 CN105 端到端回归，但在此之前可以完成大部分平台与 Mock 迁移。
- WebUI 第一阶段保留大部分功能，不做前端工程化重构。
- HomeKit 目标是“稳定可用优先，全量能力逐步补齐”。若标准模型不支持某些 CN105 细粒度能力，这些能力先保留在 WebUI，不阻塞首版上线。
