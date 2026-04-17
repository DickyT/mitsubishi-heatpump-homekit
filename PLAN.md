# ESP-IDF + ESP HomeKit SDK 迁移计划（按当前仓库状态重排）

## Summary

当前仓库已经不是原始 `PLAN.md` 里“从零开始”的状态，而是已经完成了当前 README 中记录的 **M0-M7 platform + WebUI + CN105 offline core + HomeKit over mock code**。为避免混乱，后续计划统一用 **Milestone** 表示，不再使用旧 Phase 编号描述未来开发顺序。

当前真实基线：

- `main` 已是 ESP-IDF 工程，不再是 Arduino/HomeSpan 工程。
- 已完成组件：`app_config`、`platform_log`、`platform_fs`、`platform_uart`、`platform_wifi`、`web`、`core_cn105`。
- CN105 UART 固定为 `RX=GPIO26`、`TX=GPIO32`、`2400 8E1`。
- Wi-Fi 已改为 STA-only，不再 fallback AP。
- WebUI 已恢复，当前端口为 `8080`；HomeKit/HAP 使用默认端口 `80`。
- CN105 offline core 已恢复，包含 SET payload builder、decode、mock state 和 Fahrenheit roundtrip 验证。
- WebUI feature layer 已恢复：`/` 为虚拟遥控器，`/debug` 为 raw decode/API 调试入口，当前仍只操作 mock state。
- `/logs` 和 `/files` 维护页面已恢复，支持日志查看/live tail 和 SPIFFS 文件管理。
- `esp-homekit-sdk` 已作为 submodule 接入，`components/homekit_bridge` 已绑定 mock CN105 state。
- 本地 build wrapper 已支持 `./build.py --quiet-first build`。
- 目标已从 Matter 改为 `ESP-IDF + Espressif esp-homekit-sdk`。

## Milestone Plan

### M0-M3: Platform Foundation（已完成，作为当前基线）

这些不再作为未来 TODO：

- `M0`: ESP-IDF skeleton，可 build/flash。
- `M1`: 基础组件化结构，CN105 UART 初始化。
- `M2`: SPIFFS 分区、文件系统挂载、persistent log。
- `M3`: STA-only Wi-Fi、禁用 power save、heartbeat 网络状态日志。

需要补验证但不改设计：

- 关闭 Arduino 串口监视器后重新 flash 当前固件。
- serial 确认 `Mitsubishi Heat Pump HomeKit`、`STA_GOT_IP`、`fallbackAp=no`。
- 若验证 OK，再 commit/push 之后进入 M4。

### M4: Minimal WebUI Foundation（已完成）

目标是恢复“能访问网页 + 能打通 API”的最小 Web 层，不恢复完整旧 WebUI。

实现要求：

- 新增 `components/web`，使用 ESP-IDF `esp_http_server`。M4 当时端口为 `80`，M7 之后 WebUI 已迁移到 `8080` 以避开 HomeKit/HAP。
- 初始化顺序：filesystem/log/uart/wifi 完成后启动 Web server。
- 首批接口：
  - `GET /`: 一个极简中文状态页。
  - `GET /api/health`: 返回 `{ ok, uptime_ms, device, phase }`。
  - `GET /api/status`: 返回 Wi-Fi 状态、SPIFFS 状态、CN105 UART 配置、当前 transport 占位状态。
- 页面只显示平台状态，不做 CN105 遥控器。
- 代码注释英文，UI 文案中文。

验收：

- `./build.py --quiet-first build` 成功。
- `./build.py --quiet-first flash-auto --no-build` 成功。
- flash 后访问 `http://192.168.1.205/` 返回 `200`。
- `curl http://192.168.1.205/api/health` 返回 `200`。
- `curl http://192.168.1.205/api/status` 返回 `200`。
- serial heartbeat 继续正常，无 watchdog/noise 问题。

### M5: CN105 Offline Core + Mock State（已完成）

目标是先把旧项目中已经验证过的 CN105 协议核心恢复到 ESP-IDF 组件里，但不依赖真实空调线缆。

实现要求：

- 新增 `components/core_cn105`。
- 恢复协议常量、SET payload builder、INFO/response parser、checksum、状态模型。
- 温度逻辑继续以华氏度为项目默认输入/输出；内部可按协议需要转换，但 API 对 Web/HomeKit 暴露华氏度。
- 提供 Mock transport，WebUI 可以调用 build/decode/status。
- 暂不做真实 UART 轮询。

验收：

- 固定输入 `77F` build 后，再 decode/mock apply 回读仍是 `77F`。
- `GET /api/cn105/decode?hex=...` 可 decode raw packet 并验证 checksum。
- `/api/status` 能返回完整 CN105 mock state。
- serial 自检显示 `CN105 offline self-test passed: 77F SET roundtrip`。

### M6: WebUI Feature Restore（已完成）

目标是恢复旧 WebUI 的主要调试体验，但继续用 ESP-IDF 原生 HTTP server。

实现要求：

- 首页变成当前状态 + 虚拟遥控器。
- `/debug` 恢复 ping、serial message、raw decode、mock decode。
- `/logs` 恢复日志列表、下载和 live 当前日志。
- `/files` 恢复 SPIFFS 文件列表、下载、上传、删除、新建空文件和伪目录。
- WebUI 文案中文；避免重新引入 React/build pipeline。

验收：

- 手机/电脑浏览器都能打开首页。
- 点击遥控器只本地 build payload，按发送才调用 API。
- `/debug` 可 decode raw packet，并可调用 status/mock API。
- 不明显增加 app size 到接近 2MB 分区上限。

### M7: ESP HomeKit SDK over Mock CN105（已实现，待设备验证）

目标是先接入 Espressif `esp-homekit-sdk`，但暂时只绑定 mock CN105 state。这样可以先验证 HomeKit pairing、NVS persistence、Home App 控制流、WebUI 与 HomeKit 双向同步，而不被真实线缆/CN105 transport 风险阻塞。

实现要求：

- 新增 `components/homekit_bridge`。（已完成）
- 参考 `esp-homekit-sdk/examples/common` 的 setup/factory/common 结构，但当前 Wi-Fi 仍使用本项目 hardcoded STA config。（已完成，无 provisioning/fallback AP）
- WebUI 迁移到 `8080`，避免和 HomeKit/HAP 默认 `80` 冲突。（已完成）
- HomeKit 首版只暴露稳定核心能力：（已完成）
  - power
  - target mode
  - current temperature
  - target temperature
  - Fahrenheit display units
- HomeKit 写入先更新同一份 mock CN105 state。（已完成）
- WebUI 写入 mock state 后，HomeKit characteristic 需要同步更新；HomeKit 写入后，WebUI 刷新也要看到更新。（已完成，待真机验证）
- pairing code、setup id、manufacturer、model、device name 继续集中放 `app_config`。（已完成）
- HomeKit 不支持或容易误映射的 CN105 能力先只保留在 WebUI。（已完成）

验收：

- `./build.py --quiet-first build` 成功。（已完成）
- Apple Home 可配对。（待设备验证）
- 断电重启后 pairing 保留。（待设备验证）
- Home App 改温度/开关/模式后，WebUI 刷新可看到同一份 mock state。（待设备验证）
- WebUI 改温度/开关/模式后，Home App 能看到同步后的 characteristic。（待设备验证）
- 不引入真实 CN105 UART 读写，也不要求插空调线。（已完成）

### M8: Real CN105 Transport

目标是线缆到位后接入真实空调通信。

实现要求：

- `platform_uart` 提供 read/write transport 接口给 `core_cn105`。
- 实现 CONNECT/ACK、INFO 轮询、SET 发送、超时和错误日志。
- 保留 config 开关：`Mock` / `Real` transport，默认先由 `app_config` 编译期决定。
- WebUI 必须显示当前 transport mode 和最后一次 CN105 错误。

验收：

- 插线后可 CONNECT。
- INFO 能读到空调状态。
- WebUI 修改温度/模式后，空调状态能变化并回填。
- 断线时不会 crash，WebUI 能显示离线或错误。

### M9: HomeKit over Real CN105 Polish

目标是在 M7 HomeKit 和 M8 real transport 都跑通之后，把 HomeKit 从 mock state 切到真实 CN105 state/command flow，并补齐长期运行细节。

实现要求：

- HomeKit bridge 支持 `Mock` / `Real` transport 下的同一套状态接口。
- Home App 改温度/模式能进入真实 CN105 SET command flow。
- 真实 CN105 INFO 轮询回来的状态能推送到 HomeKit characteristic。
- 保留遥控器/空调侧状态优先原则：HomeKit 不支持的 DRY/FAN/特殊状态，不主动覆写为空调错误状态。
- 加入必要 grace period，避免刚发 HomeKit 命令后被旧 CN105 轮询结果立刻覆盖。

验收：

- HomeKit 与真实空调状态长期同步。
- WebUI、HomeKit、真实遥控器三方状态不会互相错误覆盖。
- 断线/重连时 HomeKit 显示合理 online/offline/active 状态。

## Test Plan

每个 Milestone 都按同一节奏执行：

- 先 `./build.py --quiet-first build`。
- 再 `./build.py --quiet-first flash-auto --no-build`。
- 再 `./build.py serial-log --seconds 20`。
- 用户验证 OK 后，再 commit；需要远端备份时 push。
- 如果串口被 Arduino/serial monitor 占用，先停下并提示，不 kill 用户进程，除非用户明确同意。

## Assumptions

- `README.md` 的当前状态是事实来源；`PLAN.md` 应改成这份 Milestone 计划，避免再出现 Phase 编号冲突。
- 当前本地 Wi-Fi credential 文件 `app_config_local.h` 继续 gitignored，不进入 commit。
- 暂不恢复 fallback AP；未来若需要 provisioning，也优先参考 `esp-homekit-sdk/examples/common`，不是恢复旧 AP fallback。
- 下一步实际验证应从 `M7 ESP HomeKit SDK over Mock CN105` 真机配对开始；验证通过后再进入 M8 real CN105 transport。
