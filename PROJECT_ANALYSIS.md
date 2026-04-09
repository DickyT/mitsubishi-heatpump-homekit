# MitsubishiCN105ESPHome - 项目深度分析

## 1. 项目概述

本项目是一个 **ESPHome 外部组件**，使 ESP32/ESP8266 微控制器能够通过 **CN105 串口连接器** 控制三菱热泵/空调。CN105 是三菱室内机控制板上的一个专有 5 针连接器。该组件将热泵作为 `climate` 实体暴露给 Home Assistant。

### 整体架构

```
+-----------+      UART 2400 8E1      +-----------------+     WiFi/API     +----------------+
| 三菱空调  | <---CN105 连接器------> | ESP32 (ESPHome) | <-------------> | Home Assistant |
|  (PAC)    |    串口协议             |  CN105Climate   |   ESPHome API   |   仪表盘       |
+-----------+                         +-----------------+                 +----------------+
```

### 硬件连接

CN105 连接器引脚定义：
- Pin 1: 12V（空调供电）
- Pin 2: GND
- Pin 3: 5V（空调供电）
- Pin 4: TX（空调 -> ESP）
- Pin 5: RX（ESP -> 空调）

UART 参数：**2400 波特率, 8 数据位, 偶校验, 1 停止位 (8E1)**

---

## 2. 三菱 CN105 串口协议（核心空调 API）

这是与热泵内部控制器通信的专有协议。它是**最关键的一层**——其他一切都是它的封装。

### 2.1 数据包结构

每个数据包遵循以下格式：

```
[0xFC] [CMD] [0x01] [0x30] [DATA_LEN] [DATA...] [CHECKSUM]
```

- **起始字节**：始终为 `0xFC`
- **命令字节**：标识数据包类型（见下表）
- **字节 2-3**：始终为 `0x01 0x30`（设备地址/类型）
- **数据长度**：数据负载的长度（字节索引 4）
- **数据**：可变长度负载
- **校验和**：`(0xFC - 前面所有字节之和) & 0xFF`

帧总长度 = 5（头部） + 数据长度 + 1（校验和）

### 2.2 命令类型 (byte[1])

| 命令 | 方向 | 描述 |
|------|------|------|
| `0x5A` | ESP -> 空调 | **连接握手**（标准/用户模式） |
| `0x5B` | ESP -> 空调 | **连接握手**（安装工模式，扩展功能） |
| `0x7A` | 空调 -> ESP | **连接确认**（用户模式） |
| `0x7B` | 空调 -> ESP | **连接确认**（安装工模式） |
| `0x41` | ESP -> 空调 | **SET 数据包**（向空调写入设置） |
| `0x42` | ESP -> 空调 | **INFO 请求包**（从空调请求数据） |
| `0x61` | 空调 -> ESP | **更新确认**（上次 SET 成功） |
| `0x62` | 空调 -> ESP | **数据响应**（包含请求的数据） |

### 2.3 连接握手

```
CONNECT 数据包: FC 5A 01 30 02 CA 01 A8
                      ^^
                   0x5A = 标准模式, 0x5B = 安装工模式

期望回复:  FC 7A ... (标准) 或 FC 7B ... (安装工)
```

握手包含自动降级机制：如果安装工模式 (0x5B) 在 10 秒内没有回复，系统会用标准模式 (0x5A) 重试。

### 2.4 信息请求码 (0x42 数据包中的 data[0])

这些是发送给空调以读取状态的"查询"命令：

| 代码 | 名称 | 返回内容 |
|------|------|----------|
| `0x02` | 设置 | 电源、模式、温度设定值、风速、导风板位置、水平导风板、iSee 状态 |
| `0x03` | 室温 | 室温、室外温度、运行时间 |
| `0x04` | 未知 | 未实现 |
| `0x05` | 定时器 | 定时器模式、开/关分钟数（未实现） |
| `0x06` | 状态 | 运行状态、压缩机频率、输入功率 (W)、能耗 (kWh) |
| `0x09` | 待机/功率 | 阶段 (IDLE/LOW/GENTLE/MEDIUM/MODERATE/HIGH/DIFFUSE)、子模式 (NORMAL/WARMUP/DEFROST/PREHEAT/STANDBY)、自动子模式 |
| `0x20` | 功能码 Part 1 | 硬件功能码（安装工设置） |
| `0x22` | 功能码 Part 2 | 硬件功能码续 |
| `0x42` | HVAC 选项 | 空气净化器、夜间模式、循环器状态 |

### 2.5 SET 数据包结构 (0x41 命令)

SET 数据包使用头部 `FC 41 01 30 10 01 00 00`，后跟数据字段：

```
字节  6: 控制标志 1（位掩码，指示正在设置哪些字段）
字节  7: 控制标志 2（扩展控制标志）
字节  8: 电源 (0x00=关, 0x01=开)
字节  9: 模式 (0x01=制热, 0x02=除湿, 0x03=制冷, 0x07=送风, 0x08=自动)
字节 10: 温度（映射值，0x00=31°C 递减到 0x0F=16°C）
字节 11: 风速 (0x00=自动, 0x01=静音, 0x02=1档, 0x03=2档, 0x05=3档, 0x06=4档)
字节 12: 垂直导风板 (0x00=自动, 0x01-0x05=各位置, 0x07=摆动)
字节 18: 水平导风板 | 0x80 调整位
字节 19: 温度（高分辨率模式：value*2+128）
字节 21: 校验和
```

控制标志（字节 6）位掩码：`{0x01=电源, 0x02=模式, 0x04=温度, 0x08=风速, 0x10=导风板}`

### 2.6 特殊 SET 命令

| data[5] | 用途 |
|---------|------|
| `0x07` | **远程温度** —— 告诉空调使用外部温度传感器 |
| `0x08` | **运行状态** —— 设置空气净化器、夜间模式、循环器、气流控制 |
| `0x1F` | **功能码 SET Part 1** —— 写入硬件功能码 |
| `0x21` | **功能码 SET Part 2** |

### 2.7 值映射表（关键查找表）

```cpp
// 电源
POWER:     {0x00, 0x01}        -> {"OFF", "ON"}

// 模式
MODE:      {0x01, 0x02, 0x03, 0x07, 0x08} -> {"HEAT", "DRY", "COOL", "FAN", "AUTO"}

// 温度（标准模式）
TEMP:      {0x00..0x0F}        -> {31, 30, 29, ..., 16} (摄氏度)
// 温度（高精度模式）: (byte - 128) / 2.0

// 风速
FAN:       {0x00, 0x01, 0x02, 0x03, 0x05, 0x06} -> {"AUTO", "QUIET", "1", "2", "3", "4"}

// 垂直导风板
VANE:      {0x00..0x05, 0x07}  -> {"AUTO", "↑↑", "↑", "—", "↓", "↓↓", "SWING"}

// 水平导风板
WIDEVANE:  {0x01..0x05, 0x08, 0x0C, 0x00} -> {"←←", "←", "|", "→", "→→", "←→", "SWING", "AIRFLOW CONTROL"}

// 室温（标准模式）
ROOM_TEMP: {0x00..0x1F}        -> {10..41} (摄氏度)
// 室温（高精度模式）: (byte - 128) / 2.0

// 运行阶段
STAGE:     {0x00..0x06}        -> {"IDLE", "LOW", "GENTLE", "MEDIUM", "MODERATE", "HIGH", "DIFFUSE"}

// 子模式
SUB_MODE:  {0x00, 0x01, 0x02, 0x04, 0x08} -> {"NORMAL", "WARMUP", "DEFROST", "PREHEAT", "STANDBY"}

// 气流控制
AIRFLOW_CONTROL: {0x00, 0x01, 0x02} -> {"EVEN", "INDIRECT", "DIRECT"}
```

### 2.8 响应解析 (0x62 数据包)

0x62 响应中的 data[0] 字节指示返回了哪种信息：

**设置 (0x02)：**
- data[3]: 电源字节
- data[4]: 模式字节（若 > 0x08，iSee 激活，需减去 0x08）
- data[5]: 温度（标准模式）
- data[6]: 风速
- data[7]: 垂直导风板位置
- data[10]: 水平导风板（低半字节 = 位置，高位 0x80 = 调整标志）
- data[11]: 温度（高精度模式，若 != 0：(value-128)/2）
- data[14]: 气流控制（当 wideVane = 0x80 且 iSee 激活时）

**室温 (0x03)：**
- data[3]: 室温（标准查找表）
- data[5]: 室外温度（(value-128)/2，若 <= 1 则为 NAN）
- data[6]: 室温高精度（(value-128)/2，若 != 0）
- data[11-13]: 运行分钟数（24位大端序，/60 = 小时）

**状态 (0x06)：**
- data[3]: 压缩机频率 (Hz)
- data[4]: 运行标志（1=运行, 0=待机）
- data[5-6]: 输入功率（16位大端序，瓦特）
- data[7-8]: 能耗（16位大端序，/10 = kWh）

**待机/功率 (0x09)：**
- data[3]: 子模式
- data[4]: 阶段
- data[5]: 自动子模式

**HVAC 选项 (0x42)：**
- data[1]: 空气净化器 (0/1)
- data[2]: 夜间模式 (0/1)
- data[3]: 循环器 (0/1)

---

## 3. ESP32 特定 API 使用（硬件层）

以下是绕过 ESPHome 抽象、直接使用的 ESP32 (ESP-IDF) API：

### 3.1 UART 底层重初始化 (`force_low_level_uart_reinit()`)

在重连时用于强制在驱动层重新配置 UART：

```cpp
#include <driver/uart.h>
#include <driver/gpio.h>

// GPIO 复位以确保干净的重初始化
gpio_reset_pin((gpio_num_t)tx_pin);
gpio_reset_pin((gpio_num_t)rx_pin);

// UART 配置：2400 波特率, 8E1
uart_config_t cfg = {};
cfg.baud_rate = 2400;
cfg.data_bits = UART_DATA_8_BITS;
cfg.parity = UART_PARITY_EVEN;
cfg.stop_bits = UART_STOP_BITS_1;
cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
uart_param_config(port, &cfg);

// 引脚分配
uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

// RX 上拉，适用于低波特率 8E1
gpio_set_pull_mode(rx_pin, GPIO_PULLUP_ONLY);

// 时钟源和波特率
uart_set_sclk(port, UART_SCLK_APB);  // 或 UART_SCLK_XTAL
uart_set_baudrate(port, 2400);

// 缓冲区刷新
uart_flush_input(port);
```

### 3.2 互斥锁（线程安全）

```cpp
#include <mutex>
std::mutex wantedSettingsMutex;
std::lock_guard<std::mutex> guard(wantedSettingsMutex);
```

ESP8266 使用基于布尔值的"模拟互斥锁"作为替代方案。

### 3.3 FreeRTOS（间接使用）

```cpp
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
pdMS_TO_TICKS(20)  // 用于 uart_wait_tx_done
```

---

## 4. ESPHome 框架 API 使用（集成层）

以下是将三菱协议接入 Home Assistant 生态系统的 ESPHome API。

### 4.1 核心组件生命周期

```cpp
class CN105Climate : public climate::Climate,     // HA climate 实体
                     public Component,             // ESPHome 生命周期
                     public uart::UARTDevice       // UART 访问
{
    float get_setup_priority() const override;  // 返回 AFTER_WIFI
    void setup() override;                       // 组件初始化
    void loop() override;                        // 主循环（约每 16ms 调用一次）
};
```

### 4.2 Climate API（最关键的 ESPHome 依赖）

```cpp
// Climate traits（向 HA 公布的能力）
climate::ClimateTraits traits_;
traits_.add_supported_mode(climate::CLIMATE_MODE_HEAT);
traits_.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO);
traits_.add_supported_swing_mode(climate::CLIMATE_SWING_VERTICAL);
traits_.add_feature_flags(climate::CLIMATE_SUPPORTS_ACTION | 
                          climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
traits_.set_visual_min_temperature(16);
traits_.set_visual_max_temperature(26);
traits_.set_visual_temperature_step(0.5);

// control 方法（用户通过 HA 更改设置时调用）
void control(const climate::ClimateCall& call) override;
// 访问调用值：
call.get_mode()                    // optional<ClimateMode>
call.get_target_temperature()      // optional<float>
call.get_fan_mode()                // optional<ClimateFanMode>
call.get_swing_mode()              // optional<ClimateSwingMode>

// 状态发布（推送状态到 HA）
this->mode = climate::CLIMATE_MODE_COOL;
this->fan_mode = climate::CLIMATE_FAN_AUTO;
this->swing_mode = climate::CLIMATE_SWING_OFF;
this->target_temperature = 24.0;
this->current_temperature = 23.5;
this->action = climate::CLIMATE_ACTION_COOLING;
this->publish_state();              // 推送所有状态到 HA
```

### 4.3 UART API

```cpp
// 通过 UARTDevice 基类
this->get_hw_serial_()->write_byte(byte);    // 发送字节
this->get_hw_serial_()->available();          // 检查 RX 缓冲区
this->get_hw_serial_()->read_byte(&byte);    // 读取字节

// UART 组件配置（通过 Python 代码生成设置）
uart_var.set_data_bits(8);
uart_var.set_parity(UART_CONFIG_PARITY_EVEN);
uart_var.set_stop_bits(1);
```

### 4.4 定时与调度

```cpp
// 来自 Component 基类
this->set_timeout("name", delay_ms, callback);     // 一次性定时器
this->cancel_timeout("name");
this->set_interval("name", interval_ms, callback);  // 重复定时器
this->cancel_interval("name");

// 时间
CUSTOM_MILLIS  // = esphome::millis()
CUSTOM_DELAY(x) // = esphome::delay(x)
```

### 4.5 传感器与实体 API

```cpp
// 向 HA 发布传感器值
sensor::Sensor* compressor_frequency_sensor_;
compressor_frequency_sensor_->publish_state(42.0);

binary_sensor::BinarySensor* iSee_sensor_;
iSee_sensor_->publish_state(true);

text_sensor::TextSensor* stage_sensor_;
stage_sensor_->publish_state("MEDIUM");

select::Select* vertical_vane_select_;
vertical_vane_select_->publish_state("SWING");

switch_::Switch* air_purifier_switch_;
air_purifier_switch_->publish_state(true);
```

### 4.6 日志

```cpp
ESP_LOGI(TAG, "format", ...);  // 信息
ESP_LOGD(TAG, "format", ...);  // 调试
ESP_LOGW(TAG, "format", ...);  // 警告
ESP_LOGE(TAG, "format", ...);  // 错误
ESP_LOGV(TAG, "format", ...);  // 详细
```

### 4.7 Python 代码生成 (climate.py)

`climate.py` 文件是 ESPHome 的 YAML 到 C++ 代码生成器：
- 定义 YAML 配置模式 (`CONFIG_SCHEMA`)
- 将 YAML 配置映射到 C++ 构造函数调用
- 自动加载依赖项：climate, sensor, select, binary_sensor, button, switch, text_sensor, uart, uptime, number
- 通过 `cg.register_component()` 和 `climate.register_climate()` 注册组件

---

## 5. 数据流总结

### 5.1 从空调读取（轮询周期）

```
loop()
  -> maybe_start_connection_()          # 引导 UART + 握手
  -> processInput()                     # 从 UART RX 读取字节
     -> parse(byte)                     # 状态机：查找 0xFC 起始，解析头部，累积数据
     -> checkHeader()                   # 在 byte[4] 提取命令字节和数据长度
     -> processDataPacket()             # 验证校验和，分发处理
        -> processCommand()
           0x7A/0x7B -> setHeatpumpConnected(true)
           0x61      -> updateSuccess()（确认）
           0x62      -> getDataFromResponsePacket()
                        -> scheduler_.process_response(code)
                           -> getSettingsFromResponsePacket()      # 0x02
                           -> getRoomTemperatureFromResponsePacket() # 0x03
                           -> getOperatingAndCompressorFreqFromResponsePacket() # 0x06
                           -> getPowerFromResponsePacket()           # 0x09
                           -> getHVACOptionsFromResponsePacket()     # 0x42
                        -> heatpumpUpdate() -> publishStateToHA() -> publish_state()
  
  （如果没有输入且轮询周期未运行）
  -> buildAndSendRequestsInfoPackets()  # 开始新的轮询周期
     -> scheduler_.send_next_after(0x00) # 发送第一个 info 请求
        -> buildAndSendInfoPacket(code)
        -> (等待响应 -> 发送下一个 -> ... -> terminateCycle())
```

### 5.2 写入空调（用户控制）

```
HA 用户更改设置
  -> control(ClimateCall)              # ESPHome 调用此方法
     -> controlDelegate()
        -> processModeChange()  -> controlMode()  -> setModeSetting() / setPowerSetting()
        -> processTemperatureChange() -> controlTemperature()
        -> processFanChange()   -> controlFan()   -> setFanSpeed()
        -> processSwingChange() -> controlSwing() -> setVaneSetting() / setWideVaneSetting()
     -> wantedSettings.hasChanged = true

loop() 检测到 wantedSettings.hasChanged
  -> checkPendingWantedSettings()
     -> sendWantedSettings()
        -> createPacket()              # 从 wantedSettings 构建 SET 数据包
        -> writePacket()               # 通过 UART 发送
        -> publishWantedSettingsStateToHA()  # 乐观更新
```

### 5.3 远程温度

```
HA 发送外部温度传感器值
  -> set_remote_temperature(float)
     -> shouldSendExternalTemperature_ = true
     -> startRemoteTempKeepAlive()     # 每 20 秒定期重发

terminateCycle()（轮询周期结束时）
  -> sendRemoteTemperature()
     -> sendRemoteTemperaturePacket()  # 构建 0x07 数据包，通过 UART 发送
     -> pingExternalTemperature()      # 重置超时看门狗
```

---

## 6. 关键设计模式

### 6.1 RequestScheduler（请求调度器）

管理循环轮询 info 请求（0x02, 0x03, 0x06, 0x09, 0x42, 0x20, 0x22）的编排器。每个请求具有：
- `canSend` —— 条件门控（例如，HVAC 选项仅在配置了开关时才发送）
- `onResponse` —— 匹配响应到达时调用的处理器
- `soft_timeout_ms` —— 空调未回复时跳到下一个请求
- `interval_ms` —— 请求之间的最小间隔（例如，硬件功能码每 24 小时查询一次）
- `maxFailures` —— 连续 N 次失败后禁用

### 6.2 周期管理

`cycleManagement` 结构体跟踪轮询周期：
- `cycleStarted()` / `cycleEnded()` —— 标记轮询回合的开始和结束
- `hasUpdateIntervalPassed()` —— 门控新周期（默认 2 秒间隔）
- `deferCycle()` —— 发送 SET 数据包后延迟下一个周期

### 6.3 期望设置 vs 当前设置

两层状态：
- `currentSettings` —— 空调上次确认的状态
- `wantedSettings` —— 用户请求但尚未发送/确认的状态
- 防抖延迟防止快速连续发送 SET 数据包
- 互斥锁保护 wantedSettings 免受并发访问（loop vs control）

---

## 7. 功能总结

| 功能 | 协议代码 | 组件类型 |
|------|----------|----------|
| 电源开/关 | SET 字节 8 | climate mode |
| 模式（制热/制冷/除湿/送风/自动） | SET 字节 9 | climate mode |
| 目标温度 | SET 字节 10/19 | climate target_temperature |
| 风速 | SET 字节 11 | climate fan_mode |
| 垂直导风板 | SET 字节 12 | climate swing + select |
| 水平导风板 | SET 字节 18 | climate swing + select |
| 远程温度 | SET 0x07 | set_remote_temperature() |
| 室温 | INFO 0x03 | climate current_temperature |
| 室外温度 | INFO 0x03 | sensor |
| 压缩机频率 | INFO 0x06 | sensor |
| 输入功率 (W) | INFO 0x06 | sensor |
| 能耗 (kWh) | INFO 0x06 | sensor |
| 运行时间 | INFO 0x03 | sensor |
| 运行状态 | INFO 0x06 | climate action |
| 运行阶段 | INFO 0x09 | text_sensor |
| 子模式 | INFO 0x09 | text_sensor |
| iSee 传感器 | INFO 0x02 | binary_sensor |
| 空气净化器 | INFO 0x42 / SET 0x08 | switch |
| 夜间模式 | INFO 0x42 / SET 0x08 | switch |
| 循环器 | INFO 0x42 / SET 0x08 | switch |
| 气流控制 | INFO 0x02 / SET 0x08 | select |
| 硬件功能码 | INFO 0x20/0x22 / SET 0x1F/0x21 | select |
| 双设定点 | YAML 配置 | climate target_temp_low/high |
| 华氏度支持 | 转换层 | climate display |
