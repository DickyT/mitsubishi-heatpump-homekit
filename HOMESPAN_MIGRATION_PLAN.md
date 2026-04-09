# 迁移计划：MitsubishiCN105ESPHome -> ESP HomeSpan

## 目标

用 **HomeSpan**（Arduino-ESP32 HomeKit 库）替代 ESPHome + Home Assistant 依赖，实现通过 CN105 连接器直接用 Apple HomeKit 控制三菱热泵——无需中枢。

**范围限定**：不追求支持全部三菱指令。目标是把空调接入 HomeKit，能控制开关、模式、温度、风速、摆风即可。对于 HomeKit 不支持的模式（如除湿、纯送风），**绝不主动覆写**遥控器的设置，只在 HomeKit 侧报告一个兼容的状态。

> HomeSpan 仓库：https://github.com/HomeSpan/HomeSpan

---

## 1. HomeSpan API 详解

### 1.1 核心生命周期

HomeSpan 的运行模型非常简单——一个 Arduino sketch：

```cpp
#include "HomeSpan.h"

void setup() {
    Serial.begin(115200);
    
    // ① 配置（必须在 begin() 之前）
    homeSpan.setPairingCode("46637726");      // 8 位配对码
    homeSpan.setQRID("MIAC");                  // QR 码 4 字符 ID
    homeSpan.setStatusPin(LED_BUILTIN);        // 状态 LED
    homeSpan.enableOTA();                       // 启用 OTA 更新
    homeSpan.enableWebLog(50, "pool.ntp.org", "CST-8", "status");  // Web 日志
    homeSpan.setLogLevel(1);                   // 日志级别
    
    // ② 初始化
    homeSpan.begin(Category::AirConditioners, "三菱空调");
    
    // ③ 定义配件和服务（见后文）
    new SpanAccessory();
      // ...
}

void loop() {
    // ④ 核心轮询——处理 HomeKit 请求、按钮、WiFi 等
    homeSpan.poll();
}
```

`homeSpan.poll()` 做的事情：
- 检查 HAP（HomeKit Accessory Protocol）请求
- 调用每个 Service 的 `loop()` 方法
- 处理按钮事件
- 管理 WiFi 连接
- 处理 OTA 更新
- 处理串口 CLI 命令

**关键点**：`poll()` 是单线程的。你的所有代码（包括 UART 读写）都在这个循环里跑。不需要互斥锁。

### 1.2 配件与服务的树状结构

HomeKit 的数据模型是一棵树：

```
Bridge（桥接器）
├── Accessory 1（配件 1）
│   ├── Service::AccessoryInformation（必须）
│   │   ├── Characteristic::Identify（必须）
│   │   ├── Characteristic::Name
│   │   ├── Characteristic::Manufacturer
│   │   └── Characteristic::Model
│   └── Service::HeaterCooler（主服务）
│       ├── Characteristic::Active
│       ├── Characteristic::CurrentTemperature
│       └── ...
├── Accessory 2（配件 2）
│   └── Service::TemperatureSensor
└── Accessory 3（配件 3）
    └── Service::Switch
```

代码中用 `new` 来构建这棵树，缩进只是为了可读性，HomeSpan 通过调用顺序来确定父子关系：

```cpp
new SpanAccessory();                           // 配件 1 开始
  new Service::AccessoryInformation();         //   信息服务
    new Characteristic::Identify();
    new Characteristic::Name("空调");
  new Service::HeaterCooler();                 //   主服务
    new Characteristic::Active(0);
    new Characteristic::CurrentTemperature(20);
```

### 1.3 Service 的两个核心虚方法

自定义服务通过继承 `Service::Xxx` 并重写两个方法：

#### `boolean update()` —— HomeKit -> 设备

当用户在 Apple Home 中改了某个值，HomeSpan 调用 `update()`。在这个方法里：

```cpp
boolean update() override {
    // updated() 返回 true 表示这个特征被用户改了
    if (active->updated()) {
        int newVal = active->getNewVal();   // 获取用户设定的新值
        // 执行硬件操作...
    }
    if (targetState->updated()) {
        int mode = targetState->getNewVal();
        // 0=AUTO, 1=HEAT, 2=COOL
    }
    if (coolingThreshold->updated()) {
        float temp = coolingThreshold->getNewVal<float>();  // 模板方法，可指定类型
    }
    return true;   // 返回 true 表示更新成功
}
```

**重要**：`getNewVal()` 只在 `update()` 内有效。`getVal()` 返回当前值（不含待更新值）。

#### `void loop()` —— 设备 -> HomeKit

每次 `homeSpan.poll()` 都会调用每个 Service 的 `loop()`。在这里读传感器、推状态：

```cpp
void loop() override {
    // 读传感器，有变化就推送到 HomeKit
    float newTemp = readSensor();
    if (newTemp != currentTemp->getVal<float>()) {
        currentTemp->setVal(newTemp);   // 推送到 HomeKit，所有已连接的 iOS 设备会收到通知
    }
}
```

**`setVal()` 的行为**：
- 立即更新 HomeSpan 内部状态
- 通过 HAP event notification 推送到所有已订阅的 HomeKit 控制器
- 如果 `nvsStore=true`（在 Characteristic 构造时设置），还会写入 NVS 持久化

### 1.4 SpanCharacteristic 核心方法速查

| 方法 | 用途 | 在哪调用 |
|------|------|----------|
| `getVal<T>()` | 获取当前值 | `loop()` 或 `update()` |
| `getNewVal<T>()` | 获取 HomeKit 发来的新值 | 仅 `update()` |
| `setVal(value)` | 推送值到 HomeKit | 仅 `loop()`（不可在 `update()` 中调用） |
| `updated()` | 该特征是否被 HomeKit 更新了 | 仅 `update()` |
| `timeVal()` | 距上次更新的毫秒数 | 任意 |
| `setRange(min, max, step)` | 覆盖默认范围 | 构造函数中 |
| `setValidValues(n, v1, v2, ...)` | 限制枚举值 | 构造函数中 |

### 1.5 HeaterCooler 服务的特征定义

```
Service::HeaterCooler (UUID: BC)
│
├── 必需特征：
│   ├── Active              uint8   PW+PR+EV   0=INACTIVE, 1=ACTIVE
│   ├── CurrentTemperature   float   PR+EV      0-100°C
│   ├── CurrentHeaterCoolerState  uint8  PR+EV  0=INACTIVE, 1=IDLE, 2=HEATING, 3=COOLING
│   └── TargetHeaterCoolerState   uint8  PW+PR+EV  0=AUTO, 1=HEAT, 2=COOL
│
└── 可选特征：
    ├── CoolingThresholdTemperature  float  PW+PR+EV  10-35°C, step 0.1
    ├── HeatingThresholdTemperature  float  PW+PR+EV  0-25°C, step 0.1
    ├── RotationSpeed                float  PW+PR+EV  0-100%
    ├── SwingMode                    uint8  PW+PR+EV  0=DISABLED, 1=ENABLED
    ├── TemperatureDisplayUnits      uint8  PW+PR+EV  0=CELSIUS, 1=FAHRENHEIT
    └── LockPhysicalControls         uint8  PW+PR+EV  0=DISABLED, 1=ENABLED
```

### 1.6 定时器与调度

HomeSpan 没有内置的 `set_timeout` / `set_interval`。你需要用 Arduino 标准方式：

```cpp
// 在 Service 子类中：
uint32_t lastPoll_ = 0;
uint32_t pollInterval_ = 2000;  // 2 秒

void loop() override {
    uint32_t now = millis();
    if (now - lastPoll_ >= pollInterval_) {
        lastPoll_ = now;
        // 做轮询操作...
    }
}
```

对于一次性定时器：

```cpp
uint32_t timerStart_ = 0;
bool timerActive_ = false;

void startTimer(uint32_t ms) {
    timerStart_ = millis();
    timerActive_ = true;
    timerDuration_ = ms;
}

void loop() override {
    if (timerActive_ && (millis() - timerStart_ >= timerDuration_)) {
        timerActive_ = false;
        onTimerExpired();
    }
}
```

### 1.7 Web 日志与自定义页面

```cpp
// 启用 Web 日志：最多 50 条，NTP 时间服务器，时区 CST-8，访问路径 /status
homeSpan.enableWebLog(50, "pool.ntp.org", "CST-8", "status");

// 自定义 CSS
homeSpan.setWebLogCSS(
    ".bod1 { background-color: #1a1a2e; color: #eee; }"
    ".tab1 { border: 1px solid #555; }"
    ".tab2 { border: 1px solid #555; }"
);

// 注入自定义 HTML 到状态页
homeSpan.setWebLogCallback([](String &html) {
    html += "<tr><td>压缩机频率</td><td>" + String(compressorFreq) + " Hz</td></tr>";
    html += "<tr><td>输入功率</td><td>" + String(inputPower) + " W</td></tr>";
});

// 在代码中写日志
WEBLOG("室温: %.1f°C, 模式: %s", roomTemp, mode);
```

### 1.8 配对码与 QR 码

```cpp
// 设置 8 位配对码（格式 XXXYYYXX，Apple 要求某些模式）
homeSpan.setPairingCode("46637726");

// 设置 QR 码的 4 字符 setup ID
homeSpan.setQRID("MIAC");

// 配对码会显示在：
// 1. 串口启动日志中
// 2. Web 状态页中
// 3. 可以通过 CLI 命令 'S' 查看
```

---

## 2. CN105 协议引擎当前用法

### 2.1 ESPHome 中的工作流程

当前代码以 ESPHome `Component` 的 `loop()` 为核心，每次循环：

```
loop() 被 ESPHome 调用（约每 16ms）
  │
  ├── processInput()              // 读 UART，解析数据包
  │   └── parse(byte)             // 状态机：找 0xFC → 解析头 → 累积数据 → 校验和
  │       └── processDataPacket() → processCommand()
  │           ├── 0x7A/0x7B: 连接成功
  │           ├── 0x61: SET 确认
  │           └── 0x62: 数据响应 → scheduler_.process_response(code)
  │               ├── 0x02: getSettingsFromResponsePacket()     // 设置
  │               ├── 0x03: getRoomTemperatureFromResponsePacket() // 室温
  │               ├── 0x06: getOperatingAndCompressorFreqFromResponsePacket() // 运行状态
  │               ├── 0x09: getPowerFromResponsePacket()        // 阶段/子模式
  │               └── 0x42: getHVACOptionsFromResponsePacket()  // HVAC 选项
  │
  ├── (如果 wantedSettings.hasChanged 且无轮询周期在跑)
  │   └── sendWantedSettings() → createPacket() → writePacket()
  │
  └── (如果距上次轮询已过 update_interval)
      └── buildAndSendRequestsInfoPackets()
          └── scheduler_.send_next_after(0x00)  // 依次发 0x02→0x03→0x06→0x09→...
```

### 2.2 需要替换的 ESPHome API

| 原 ESPHome API | 用途 | HomeSpan 替代 |
|----------------|------|--------------|
| `Component::setup()` | 组件初始化 | Arduino `setup()` 或 Service 构造函数 |
| `Component::loop()` | 主循环 | `Service::loop()` override |
| `climate::Climate::control(ClimateCall)` | 接收 HA 控制命令 | `Service::update()` override |
| `climate::Climate::publish_state()` | 推送状态到 HA | `SpanCharacteristic::setVal()` |
| `Component::set_timeout(name, ms, cb)` | 命名一次性定时器 | 手动 `millis()` 检查 |
| `Component::set_interval(name, ms, cb)` | 命名重复定时器 | 手动 `millis()` 检查 |
| `Component::cancel_timeout(name)` | 取消定时器 | 设置 `active = false` |
| `UARTDevice::get_hw_serial_()->write_byte(b)` | 发送字节 | `HardwareSerial::write(b)` |
| `UARTDevice::get_hw_serial_()->available()` | 检查可读 | `HardwareSerial::available()` |
| `UARTDevice::get_hw_serial_()->read_byte(&b)` | 读取字节 | `HardwareSerial::read()` |
| `ESP_LOGx(TAG, fmt, ...)` | 日志 | `Serial.printf()` + `WEBLOG()` |
| `sensor::Sensor::publish_state(v)` | 推送传感器值 | `SpanCharacteristic::setVal(v)` |
| `std::mutex` / 模拟互斥锁 | 线程安全 | **删除**（单线程） |

### 2.3 可直接复用的代码（零修改或极小修改）

| 文件 | 内容 | 是否需要修改 |
|------|------|-------------|
| `cn105_types.h` | 所有结构体、常量、查找表 | **无需修改** |
| `heatpumpFunctions.h/.cpp` | 功能码处理 | **无需修改** |
| `cycle_management.h/.cpp` | 轮询周期管理 | **无需修改** |
| `hp_readings.cpp` 中的解析逻辑 | `parse()`, `checkHeader()`, `checkSum()`, 各种 `getXxxFromResponsePacket()` | 小幅修改：移除 `publish_state()` 调用，改为设置标志位 |
| `hp_writings.cpp` 中的数据包构建 | `createPacket()`, `createInfoPacket()`, `sendRemoteTemperaturePacket()` | 小幅修改：`write_byte()` → `Serial.write()` |
| `request_scheduler.h/.cpp` | 请求调度器 | 中等修改：替换 `TimeoutCallback` |

---

## 3. API 匹配：CN105 协议 ↔ HomeKit 特征

### 3.1 模式映射与"不覆写"策略

核心原则：**ESP32 绝不主动发送用户未通过 HomeKit 请求的模式变更**。

```
三菱模式       HomeKit Active  HomeKit TargetState  CurrentState        策略
─────────────────────────────────────────────────────────────────────
OFF            0 (INACTIVE)    (保持不变)            0 (INACTIVE)       ✅ 完全对应
HEAT           1 (ACTIVE)      1 (HEAT)             2 (HEATING)/1(IDLE) ✅ 完全对应
COOL           1 (ACTIVE)      2 (COOL)             3 (COOLING)/1(IDLE) ✅ 完全对应
AUTO           1 (ACTIVE)      0 (AUTO)             根据运行状态判断    ✅ 完全对应
─── 以下模式 HomeKit 无法设置，但遥控器可以设置 ───
DRY            1 (ACTIVE)      0 (AUTO)*            根据运行状态判断    ⚠️ 报告为 AUTO
FAN            1 (ACTIVE)      0 (AUTO)*            1 (IDLE)           ⚠️ 报告为 AUTO
```

`*` 标记的模式是遥控器设置的，ESP32 只是**读到了**这个状态然后**被动报告**。

#### 不覆写的实现方式

```cpp
// 内部追踪三菱真实模式
const char* actualMitsuMode_ = "AUTO";  // 从 0x02 响应读取

boolean update() override {
    if (targetState->updated()) {
        int hkMode = targetState->getNewVal();
        // 只在用户主动操作时，才发 SET 指令给空调
        switch (hkMode) {
            case 0: proto_->setMode("AUTO"); break;
            case 1: proto_->setMode("HEAT"); break;
            case 2: proto_->setMode("COOL"); break;
        }
        proto_->setPower("ON");
        proto_->commitSettings();  // 发送 SET 包
    }
    // ...
    return true;
}

void loop() override {
    if (proto_->hasNewSettings()) {
        auto& s = proto_->getCurrentSettings();
        actualMitsuMode_ = s.mode;  // 记录三菱真实模式
        
        // 映射到 HomeKit —— 只是报告，不发 SET 指令
        if (strcmp(s.power, "OFF") == 0) {
            active->setVal(0);
        } else {
            active->setVal(1);
            
            if (strcmp(s.mode, "HEAT") == 0) {
                targetState->setVal(1);
            } else if (strcmp(s.mode, "COOL") == 0) {
                targetState->setVal(2);
            } else {
                // AUTO, DRY, FAN 都报告为 AUTO
                // 但我们不会因此发 SET 指令给空调
                // 空调实际仍在 DRY 或 FAN 模式运行
                targetState->setVal(0);
            }
        }
    }
}
```

**关键**：`setVal()` 只是告诉 HomeKit "当前状态是这样"，它**不会**触发 `update()` 回调。所以不存在"HomeKit 收到 AUTO 后又发个 AUTO 命令回来覆盖 DRY"的问题。`update()` 只在**用户主动操作** Home App 时触发。

### 3.2 温度映射

```cpp
// 构造函数中设置范围：
coolingThreshold = new Characteristic::CoolingThresholdTemperature(26);
heatingThreshold = new Characteristic::HeatingThresholdTemperature(20);

// 覆盖默认范围以匹配三菱 16-31°C
coolingThreshold->setRange(16, 31, 0.5);
heatingThreshold->setRange(16, 31, 0.5);
```

在 `update()` 中读取 HomeKit 设定温度并发送到空调：

```cpp
if (coolingThreshold->updated()) {
    float temp = coolingThreshold->getNewVal<float>();
    proto_->setTemperature(temp);
    proto_->commitSettings();
}
```

在 `loop()` 中，空调报告的温度回推到 HomeKit：

```cpp
// 室温（每 2 秒轮询一次从 0x03 响应获取）
if (status.roomTemperature != currentTemp->getVal<float>()) {
    currentTemp->setVal(status.roomTemperature);
}

// 目标温度（从 0x02 响应获取，可能被遥控器改了）
if (settings.temperature != coolingThreshold->getVal<float>()) {
    coolingThreshold->setVal(settings.temperature);
    heatingThreshold->setVal(settings.temperature);
}
```

### 3.3 风速映射

三菱有 6 档（AUTO, QUIET, 1, 2, 3, 4），HomeKit 的 `RotationSpeed` 是 0-100 的连续值。

```cpp
// 三菱 → HomeKit
float fanToPercent(const char* fan) {
    if (strcmp(fan, "AUTO") == 0)  return 0;    // 特殊：0 = AUTO
    if (strcmp(fan, "QUIET") == 0) return 14;
    if (strcmp(fan, "1") == 0)     return 28;
    if (strcmp(fan, "2") == 0)     return 42;
    if (strcmp(fan, "3") == 0)     return 71;
    if (strcmp(fan, "4") == 0)     return 100;
    return 0;
}

// HomeKit → 三菱
const char* percentToFan(float pct) {
    if (pct <= 0)  return "AUTO";
    if (pct <= 20) return "QUIET";
    if (pct <= 35) return "1";
    if (pct <= 55) return "2";
    if (pct <= 80) return "3";
    return "4";
}
```

在 `update()` 和 `loop()` 中：

```cpp
// update(): HomeKit → 空调
if (rotationSpeed->updated()) {
    float pct = rotationSpeed->getNewVal<float>();
    proto_->setFan(percentToFan(pct));
    proto_->commitSettings();
}

// loop(): 空调 → HomeKit
float reportedPct = fanToPercent(settings.fan);
if (reportedPct != rotationSpeed->getVal<float>()) {
    rotationSpeed->setVal(reportedPct);
}
```

### 3.4 摆风映射

HomeKit `SwingMode` 是简单的 0/1：

```cpp
// update(): HomeKit → 空调
if (swingMode->updated()) {
    int sw = swingMode->getNewVal();
    proto_->setVane(sw ? "SWING" : "AUTO");
    // 水平导风板也设置
    if (wideVaneSupported_) {
        proto_->setWideVane(sw ? "SWING" : "|");
    }
    proto_->commitSettings();
}

// loop(): 空调 → HomeKit
bool isSwinging = (strcmp(settings.vane, "SWING") == 0) ||
                  (settings.wideVane && strcmp(settings.wideVane, "SWING") == 0);
if (isSwinging != (bool)swingMode->getVal()) {
    swingMode->setVal(isSwinging ? 1 : 0);
}
```

### 3.5 运行状态映射

```cpp
void updateCurrentState() {
    auto& s = proto_->getCurrentSettings();
    auto& st = proto_->getCurrentStatus();
    
    if (strcmp(s.power, "OFF") == 0) {
        currentState->setVal(0);  // INACTIVE
        return;
    }
    
    if (!st.operating) {
        currentState->setVal(1);  // IDLE
        return;
    }
    
    // 根据三菱真实模式判断
    if (strcmp(s.mode, "HEAT") == 0) {
        currentState->setVal(2);  // HEATING
    } else if (strcmp(s.mode, "COOL") == 0 || strcmp(s.mode, "DRY") == 0) {
        currentState->setVal(3);  // COOLING（DRY 本质上是低功率制冷）
    } else if (strcmp(s.mode, "AUTO") == 0) {
        // AUTO 模式根据室温与设定温度判断
        if (st.roomTemperature < s.temperature) {
            currentState->setVal(2);  // HEATING
        } else {
            currentState->setVal(3);  // COOLING
        }
    } else {
        // FAN 模式：压缩机在跑但不制冷制热
        currentState->setVal(1);  // IDLE
    }
}
```

---

## 4. 2 秒同步机制详解

### 4.1 问题

空调不会主动推送状态变化。ESP32 必须定期轮询（发 INFO 请求），默认每 2 秒一轮。一轮包含多个请求（0x02, 0x03, 0x06, 0x09...），顺序发送，每个等待响应后再发下一个。

### 4.2 在 HomeSpan 中的实现

```cpp
class HKHeaterCooler : public Service::HeaterCooler {
    MitsubishiProtocol* proto_;
    
    // 轮询定时
    uint32_t lastPollMs_ = 0;
    static const uint32_t POLL_INTERVAL_MS = 2000;
    
    // 发送后的保护窗口（防止刚发了 SET 就被轮询结果覆盖）
    uint32_t lastCommandMs_ = 0;
    static const uint32_t COMMAND_GRACE_MS = 3000;

    void loop() override {
        // ① 始终处理 UART 输入（不受定时器控制）
        //    因为空调可能随时发回复包
        proto_->processInput();
        
        // ② 定时发起轮询周期
        uint32_t now = millis();
        if (now - lastPollMs_ >= POLL_INTERVAL_MS) {
            lastPollMs_ = now;
            if (proto_->isConnected() && !proto_->isCycleRunning()) {
                proto_->startPollCycle();  // 发送第一个 INFO 请求
            }
        }
        
        // ③ 轮询周期中的请求链管理
        //    RequestScheduler 在收到响应后自动发下一个请求
        //    这里不需要额外逻辑
        
        // ④ 有新数据时同步到 HomeKit
        if (proto_->hasNewData()) {
            // 保护窗口：刚发了 HomeKit 命令后，忽略空调返回的旧设置
            // 避免用户改了温度但空调还没来得及更新就被旧值覆盖
            bool inGracePeriod = (now - lastCommandMs_) < COMMAND_GRACE_MS;
            
            syncSettingsToHomeKit(inGracePeriod);
            syncStatusToHomeKit();
        }
        
        // ⑤ 连接丢失检测与重连
        if (!proto_->isConnected()) {
            proto_->reconnect();
        }
    }
    
    void syncSettingsToHomeKit(bool skipSettingsSync) {
        auto& s = proto_->getCurrentSettings();
        
        // 电源状态始终同步
        int powerVal = (strcmp(s.power, "ON") == 0) ? 1 : 0;
        if (powerVal != active->getVal()) {
            active->setVal(powerVal);
        }
        
        if (skipSettingsSync) return;  // 保护窗口内不同步设置
        
        // 模式同步（不覆写策略）
        int hkTarget;
        if (strcmp(s.mode, "HEAT") == 0)      hkTarget = 1;
        else if (strcmp(s.mode, "COOL") == 0) hkTarget = 2;
        else                                   hkTarget = 0;  // AUTO/DRY/FAN → AUTO
        
        if (hkTarget != targetState->getVal()) {
            targetState->setVal(hkTarget);
        }
        
        // 温度同步
        if (s.temperature != coolingThreshold->getVal<float>()) {
            coolingThreshold->setVal(s.temperature);
            heatingThreshold->setVal(s.temperature);
        }
        
        // 风速同步
        float pct = fanToPercent(s.fan);
        if (pct != rotationSpeed->getVal<float>()) {
            rotationSpeed->setVal(pct);
        }
        
        // 摆风同步
        bool sw = (strcmp(s.vane, "SWING") == 0);
        if (sw != (bool)swingMode->getVal()) {
            swingMode->setVal(sw ? 1 : 0);
        }
    }
    
    void syncStatusToHomeKit() {
        auto& st = proto_->getCurrentStatus();
        auto& s = proto_->getCurrentSettings();
        
        // 室温
        if (!isnan(st.roomTemperature) && st.roomTemperature != currentTemp->getVal<float>()) {
            currentTemp->setVal(st.roomTemperature);
        }
        
        // 运行状态
        updateCurrentState();
    }
    
    boolean update() override {
        lastCommandMs_ = millis();  // 标记：用户刚发了命令
        
        // ... 处理 HomeKit 命令（见 3.1-3.4 节）...
        
        proto_->commitSettings();
        return true;
    }
};
```

### 4.3 时序图

```
时间线（毫秒）:
0     100   200   ...  2000  2100  2200  ...  4000
│      │     │          │     │     │          │
│  processInput()       │ startPollCycle()     │ startPollCycle()
│  (每次 loop 都跑)     │  → send 0x02        │  → send 0x02
│                        │  ← recv settings    │  ← recv settings
│                        │  → send 0x03        │  → send 0x03
│                        │  ← recv room temp   │  ← recv room temp
│                        │  → send 0x06        │  → send 0x06
│                        │  ← recv status      │  ← recv status
│                        │  → send 0x09        │
│                        │  ← recv stage       │
│                        │  endCycle()          │
│                        │  syncToHomeKit()     │
```

用户通过遥控器改了温度的场景：

```
t=0:     用户按遥控器改温度 25→22
t=0~2s:  ESP32 还不知道，HomeKit 显示 25°C
t=2s:    ESP32 发 0x02 请求
t=2.1s:  空调回复 settings，温度=22°C
t=2.1s:  syncSettingsToHomeKit() 检测到 22≠25
t=2.1s:  coolingThreshold->setVal(22) → HomeKit 显示更新为 22°C
```

---

## 5. 完整代码架构

### 5.1 协议引擎提取

将原项目的协议相关代码提取为一个独立类，不依赖任何框架：

```cpp
// MitsubishiProtocol.h
#pragma once
#include "MitsubishiTypes.h"      // 原 cn105_types.h
#include "heatpumpFunctions.h"    // 原 heatpumpFunctions.h

class MitsubishiProtocol {
public:
    MitsubishiProtocol(HardwareSerial* serial);
    
    // 连接管理
    void connect();                // 发送握手包
    void reconnect();              // 重连
    bool isConnected() const;
    
    // 主循环调用
    void processInput();           // 读取并解析 UART 输入
    bool isCycleRunning() const;
    void startPollCycle();         // 开始一轮 INFO 请求
    
    // 状态查询
    bool hasNewData();             // 自上次调用后是否有新数据
    const heatpumpSettings& getCurrentSettings() const;
    const heatpumpStatus& getCurrentStatus() const;
    
    // 控制设置
    void setPower(const char* power);     // "ON" / "OFF"
    void setMode(const char* mode);       // "HEAT" / "COOL" / "AUTO" / "DRY" / "FAN"
    void setTemperature(float temp);
    void setFan(const char* fan);         // "AUTO" / "QUIET" / "1" / "2" / "3" / "4"
    void setVane(const char* vane);       // "AUTO" / "SWING" / "1"..."5"
    void setWideVane(const char* wv);
    void commitSettings();                // 构建并发送 SET 数据包
    
    // 远程温度
    void setRemoteTemperature(float temp);
    
private:
    HardwareSerial* serial_;
    heatpumpSettings currentSettings_{};
    heatpumpSettings wantedSettings_{};
    heatpumpStatus currentStatus_{};
    bool connected_ = false;
    bool newData_ = false;
    
    // 原有的解析状态机
    uint8_t storedInputData_[64];
    bool foundStart_ = false;
    int bytesRead_ = 0;
    int dataLength_ = -1;
    uint8_t command_ = 0;
    uint8_t* data_;
    
    // 包构建与发送
    void writePacket(uint8_t* packet, int length);
    uint8_t checkSum(uint8_t* bytes, int len);
    void parse(uint8_t byte);
    void processDataPacket();
    void processCommand();
    // ... 其余原有方法 ...
};
```

### 5.2 完整 HomeSpan Service 实现

```cpp
// HKHeaterCooler.h
#pragma once
#include "HomeSpan.h"
#include "MitsubishiProtocol.h"

class HKHeaterCooler : public Service::HeaterCooler {
    MitsubishiProtocol* proto_;
    
    SpanCharacteristic* active;
    SpanCharacteristic* currentTemp;
    SpanCharacteristic* currentState;
    SpanCharacteristic* targetState;
    SpanCharacteristic* coolingThreshold;
    SpanCharacteristic* heatingThreshold;
    SpanCharacteristic* rotationSpeed;
    SpanCharacteristic* swingMode;
    
    uint32_t lastPollMs_ = 0;
    uint32_t lastCommandMs_ = 0;
    
    static const uint32_t POLL_INTERVAL = 2000;
    static const uint32_t GRACE_PERIOD = 3000;

public:
    HKHeaterCooler(HardwareSerial* serial) : Service::HeaterCooler() {
        proto_ = new MitsubishiProtocol(serial);
        
        active            = new Characteristic::Active(0, true);  // true = NVS 持久化
        currentTemp       = new Characteristic::CurrentTemperature(20);
        currentState      = new Characteristic::CurrentHeaterCoolerState(0);
        targetState       = new Characteristic::TargetHeaterCoolerState(0, true);
        coolingThreshold  = new Characteristic::CoolingThresholdTemperature(26, true);
        heatingThreshold  = new Characteristic::HeatingThresholdTemperature(20, true);
        rotationSpeed     = new Characteristic::RotationSpeed(0, true);
        swingMode         = new Characteristic::SwingMode(0, true);
        
        coolingThreshold->setRange(16, 31, 0.5);
        heatingThreshold->setRange(16, 31, 0.5);
        
        // 限制 TargetState 的可选值：0=AUTO, 1=HEAT, 2=COOL
        targetState->setValidValues(3, 0, 1, 2);
    }
    
    // 在 setup 完成后、poll 开始前连接空调
    void loop() override {
        static bool firstRun = true;
        if (firstRun) {
            firstRun = false;
            proto_->connect();
        }
        
        // 始终处理 UART
        proto_->processInput();
        
        // 定时轮询
        uint32_t now = millis();
        if (now - lastPollMs_ >= POLL_INTERVAL && proto_->isConnected() && !proto_->isCycleRunning()) {
            lastPollMs_ = now;
            proto_->startPollCycle();
        }
        
        // 同步状态到 HomeKit
        if (proto_->hasNewData()) {
            bool grace = (now - lastCommandMs_) < GRACE_PERIOD;
            syncToHomeKit(grace);
        }
        
        // 断线重连
        if (!proto_->isConnected() && now - lastPollMs_ > 10000) {
            proto_->reconnect();
        }
    }
    
    boolean update() override {
        lastCommandMs_ = millis();
        bool needSend = false;
        
        if (active->updated()) {
            if (active->getNewVal() == 0) {
                proto_->setPower("OFF");
            } else {
                proto_->setPower("ON");
            }
            needSend = true;
        }
        
        if (targetState->updated()) {
            proto_->setPower("ON");  // 切模式时自动开机
            switch (targetState->getNewVal()) {
                case 0: proto_->setMode("AUTO"); break;
                case 1: proto_->setMode("HEAT"); break;
                case 2: proto_->setMode("COOL"); break;
            }
            needSend = true;
        }
        
        if (coolingThreshold->updated()) {
            proto_->setTemperature(coolingThreshold->getNewVal<float>());
            needSend = true;
        }
        if (heatingThreshold->updated()) {
            proto_->setTemperature(heatingThreshold->getNewVal<float>());
            needSend = true;
        }
        
        if (rotationSpeed->updated()) {
            proto_->setFan(percentToFan(rotationSpeed->getNewVal<float>()));
            needSend = true;
        }
        
        if (swingMode->updated()) {
            proto_->setVane(swingMode->getNewVal() ? "SWING" : "AUTO");
            needSend = true;
        }
        
        if (needSend) {
            proto_->commitSettings();
        }
        return true;
    }

private:
    void syncToHomeKit(bool skipSettings) {
        auto& st = proto_->getCurrentStatus();
        auto& s = proto_->getCurrentSettings();
        
        // 室温始终同步
        if (!isnan(st.roomTemperature) && st.roomTemperature != currentTemp->getVal<float>()) {
            currentTemp->setVal(st.roomTemperature);
        }
        
        // 电源始终同步
        int pwr = (strcmp(s.power, "ON") == 0) ? 1 : 0;
        if (pwr != active->getVal()) active->setVal(pwr);
        
        // 运行状态始终同步
        int cs = calcCurrentState(s, st);
        if (cs != currentState->getVal()) currentState->setVal(cs);
        
        if (skipSettings) return;
        
        // 模式（不覆写策略）
        int ts = modeToTarget(s.mode);
        if (ts != targetState->getVal()) targetState->setVal(ts);
        
        // 温度
        if (s.temperature > 0 && s.temperature != coolingThreshold->getVal<float>()) {
            coolingThreshold->setVal(s.temperature);
            heatingThreshold->setVal(s.temperature);
        }
        
        // 风速
        float fp = fanToPercent(s.fan);
        if (fp != rotationSpeed->getVal<float>()) rotationSpeed->setVal(fp);
        
        // 摆风
        int sw = (strcmp(s.vane, "SWING") == 0) ? 1 : 0;
        if (sw != swingMode->getVal()) swingMode->setVal(sw);
    }
    
    int modeToTarget(const char* mode) {
        if (strcmp(mode, "HEAT") == 0) return 1;
        if (strcmp(mode, "COOL") == 0) return 2;
        return 0;  // AUTO, DRY, FAN 都映射为 AUTO
    }
    
    int calcCurrentState(const heatpumpSettings& s, const heatpumpStatus& st) {
        if (strcmp(s.power, "OFF") == 0) return 0;  // INACTIVE
        if (!st.operating) return 1;                 // IDLE
        if (strcmp(s.mode, "HEAT") == 0) return 2;   // HEATING
        if (strcmp(s.mode, "COOL") == 0 || strcmp(s.mode, "DRY") == 0) return 3;  // COOLING
        if (strcmp(s.mode, "AUTO") == 0) {
            return (st.roomTemperature < s.temperature) ? 2 : 3;
        }
        return 1;  // FAN → IDLE
    }
    
    float fanToPercent(const char* fan) {
        if (!fan) return 0;
        if (strcmp(fan, "QUIET") == 0) return 14;
        if (strcmp(fan, "1") == 0) return 28;
        if (strcmp(fan, "2") == 0) return 42;
        if (strcmp(fan, "3") == 0) return 71;
        if (strcmp(fan, "4") == 0) return 100;
        return 0;  // AUTO
    }
    
    const char* percentToFan(float pct) {
        if (pct <= 0) return "AUTO";
        if (pct <= 20) return "QUIET";
        if (pct <= 35) return "1";
        if (pct <= 55) return "2";
        if (pct <= 80) return "3";
        return "4";
    }
};
```

### 5.3 完整主 Sketch

```cpp
// MitsubishiCN105HomeKit.ino
#include "HomeSpan.h"
#include "HKHeaterCooler.h"

// ===== 配置 =====
#define TX_PIN       1        // CN105 TX
#define RX_PIN       2        // CN105 RX
#define STATUS_LED   LED_BUILTIN
#define PAIRING_CODE "46637726"

HardwareSerial CN105Serial(1);

// 全局指针，供 Web UI 回调使用
MitsubishiProtocol* g_proto = nullptr;

void setup() {
    Serial.begin(115200);
    CN105Serial.begin(2400, SERIAL_8E1, RX_PIN, TX_PIN);
    
    // HomeSpan 配置
    homeSpan.setStatusPin(STATUS_LED);
    homeSpan.setPairingCode(PAIRING_CODE);
    homeSpan.setQRID("MIAC");
    homeSpan.enableOTA();
    homeSpan.enableWebLog(100, "pool.ntp.org", "CST-8", "status");
    homeSpan.setLogLevel(1);
    
    // 注入自定义状态信息到 Web 页面
    homeSpan.setWebLogCallback(webStatusCallback);
    
    homeSpan.begin(Category::AirConditioners, "三菱空调");
    
    // ---- 配件定义 ----
    
    // Bridge
    new SpanAccessory();
      new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name("三菱 CN105 桥接");
        new Characteristic::Manufacturer("DIY");
        new Characteristic::Model("CN105-HomeKit");
        new Characteristic::FirmwareRevision("1.0.0");
    
    // 空调
    new SpanAccessory();
      new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name("空调");
      auto* hc = new HKHeaterCooler(&CN105Serial);
      g_proto = hc->getProtocol();  // 保存全局指针供 Web UI 用
    
    // 室外温度传感器（可选）
    new SpanAccessory();
      new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name("室外温度");
      new HKOutdoorTemp(g_proto);
}

void loop() {
    homeSpan.poll();
}
```

---

## 6. Web UI 实现方案

### 6.1 架构

HomeSpan 提供了一个内置的 Web 日志页面（通过 `enableWebLog()` 启用），但它功能有限——只能显示状态表格和滚动日志。要实现全功能 Web UI，我们需要**在 HomeSpan 的 HAP 服务器旁边**运行一个独立的 HTTP 服务器。

方案：使用 ESP32 的 `WebServer` 库在另一个端口（如 8080）上运行完整的 Web UI，HomeSpan 的 HAP 协议继续使用端口 80。

```cpp
#include <WebServer.h>
WebServer webServer(8080);
```

### 6.2 Web UI 功能清单

| 功能 | 页面 | 数据源 |
|------|------|--------|
| 空调状态总览 | `/` | 所有 INFO 响应 |
| 温度信息 | `/` | 0x03 响应：室温、室外温度 |
| 运行状态 | `/` | 0x06 响应：压缩机频率、功率、能耗 |
| 设置详情 | `/` | 0x02 响应：模式、温度、风速、导风板 |
| 运行阶段 | `/` | 0x09 响应：stage, sub_mode |
| HomeKit 配对码 | `/` | 编译时常量 |
| HomeKit 配对状态 | `/` | HomeSpan 内部状态 |
| 连接诊断 | `/diag` | 连接时间、轮询周期计数 |
| 日志 | `/log` | WEBLOG 条目 |
| JSON API | `/api/status` | 结构化数据 |

### 6.3 完整 Web UI 实现

```cpp
// WebUI.h
#pragma once
#include <WebServer.h>
#include "MitsubishiProtocol.h"

class WebUI {
    WebServer server_;
    MitsubishiProtocol* proto_;
    const char* pairingCode_;
    uint32_t bootTime_;

public:
    WebUI(int port, MitsubishiProtocol* proto, const char* pairingCode)
        : server_(port), proto_(proto), pairingCode_(pairingCode) {
        bootTime_ = millis();
    }
    
    void begin() {
        server_.on("/", HTTP_GET, [this]() { handleRoot(); });
        server_.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });
        server_.on("/diag", HTTP_GET, [this]() { handleDiag(); });
        server_.onNotFound([this]() { server_.send(404, "text/plain", "Not Found"); });
        server_.begin();
        Serial.printf("Web UI: http://<ip>:%d/\n", 8080);
    }
    
    void loop() {
        server_.handleClient();
    }

private:
    String uptimeString() {
        uint32_t sec = (millis() - bootTime_) / 1000;
        uint32_t d = sec / 86400; sec %= 86400;
        uint32_t h = sec / 3600;  sec %= 3600;
        uint32_t m = sec / 60;    sec %= 60;
        char buf[32];
        snprintf(buf, sizeof(buf), "%ud %02u:%02u:%02u", d, h, m, sec);
        return String(buf);
    }

    void handleRoot() {
        auto& s = proto_->getCurrentSettings();
        auto& st = proto_->getCurrentStatus();
        
        String html = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>三菱空调 CN105</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: -apple-system, 'Helvetica Neue', Arial, sans-serif;
         background: #0f0f23; color: #ccc; padding: 16px; }
  h1 { color: #fff; margin-bottom: 16px; font-size: 1.5em; }
  h2 { color: #7ec8e3; margin: 20px 0 10px; font-size: 1.1em; }
  .card { background: #1a1a3e; border-radius: 12px; padding: 16px; margin-bottom: 12px; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 12px; }
  .stat { text-align: center; }
  .stat .value { font-size: 2em; color: #fff; font-weight: 700; }
  .stat .label { font-size: 0.8em; color: #888; margin-top: 4px; }
  .row { display: flex; justify-content: space-between; padding: 8px 0;
         border-bottom: 1px solid #2a2a4e; }
  .row:last-child { border: none; }
  .row .key { color: #888; }
  .row .val { color: #fff; font-weight: 500; }
  .pairing { background: #1e3a5f; border: 2px dashed #4a90d9; text-align: center; }
  .pairing .code { font-size: 2.5em; font-family: monospace; color: #fff;
                    letter-spacing: 0.15em; margin: 8px 0; }
  .pairing .hint { font-size: 0.8em; color: #8ab4f8; }
  .badge { display: inline-block; padding: 2px 8px; border-radius: 4px;
           font-size: 0.85em; font-weight: 600; }
  .badge-on { background: #2d5a2d; color: #6fcf6f; }
  .badge-off { background: #5a2d2d; color: #cf6f6f; }
  .badge-mode { background: #2d4a5a; color: #6fb8cf; }
  footer { text-align: center; color: #555; font-size: 0.75em; margin-top: 20px; }
</style>
</head>
<body>
<h1>&#x2744; 三菱空调控制面板</h1>
)rawhtml";

        // HomeKit 配对码
        html += "<div class='card pairing'>";
        html += "<div style='color:#8ab4f8;font-size:0.9em;'>HomeKit 配对码</div>";
        html += "<div class='code'>";
        // 格式化为 XXX-YY-ZZZ
        String code = String(pairingCode_);
        html += code.substring(0,3) + "-" + code.substring(3,5) + "-" + code.substring(5,8);
        html += "</div>";
        html += "<div class='hint'>在 Apple Home 中添加配件时输入此代码</div>";
        html += "</div>";

        // 温度卡片
        html += "<div class='card'><h2>温度</h2><div class='grid'>";
        html += statBlock("室温", st.roomTemperature, "°C");
        html += statBlock("设定温度", s.temperature, "°C");
        if (!isnan(st.outsideAirTemperature)) {
            html += statBlock("室外温度", st.outsideAirTemperature, "°C");
        }
        html += "</div></div>";

        // 运行状态卡片
        html += "<div class='card'><h2>运行状态</h2>";
        html += row("电源", s.power ? s.power : "N/A",
                     (s.power && strcmp(s.power,"ON")==0) ? "badge-on" : "badge-off");
        html += row("模式", s.mode ? s.mode : "N/A", "badge-mode");
        html += row("风速", s.fan ? s.fan : "N/A", "");
        html += row("垂直导风板", s.vane ? s.vane : "N/A", "");
        if (s.wideVane) {
            html += row("水平导风板", s.wideVane, "");
        }
        html += row("压缩机运行", st.operating ? "是" : "否",
                     st.operating ? "badge-on" : "badge-off");
        html += "</div>";

        // 能耗卡片
        html += "<div class='card'><h2>能耗信息</h2><div class='grid'>";
        if (!isnan(st.compressorFrequency)) {
            html += statBlock("压缩机频率", st.compressorFrequency, " Hz");
        }
        if (!isnan(st.inputPower)) {
            html += statBlock("输入功率", st.inputPower, " W");
        }
        if (!isnan(st.kWh)) {
            html += statBlock("累计能耗", st.kWh, " kWh");
        }
        if (!isnan(st.runtimeHours)) {
            html += statBlock("运行时间", st.runtimeHours, " h");
        }
        html += "</div></div>";
        
        // 运行阶段
        if (s.stage || s.sub_mode) {
            html += "<div class='card'><h2>运行阶段</h2>";
            if (s.stage) html += row("阶段", s.stage, "badge-mode");
            if (s.sub_mode) html += row("子模式", s.sub_mode, "");
            if (s.auto_sub_mode) html += row("自动子模式", s.auto_sub_mode, "");
            html += "</div>";
        }

        // 连接信息
        html += "<div class='card'><h2>连接信息</h2>";
        html += row("运行时间", uptimeString().c_str(), "");
        html += row("CN105 连接", proto_->isConnected() ? "已连接" : "断开",
                     proto_->isConnected() ? "badge-on" : "badge-off");
        html += "</div>";

        html += "<footer>自动刷新: <a href='/' style='color:#4a90d9;'>手动</a>"
                " | <a href='/api/status' style='color:#4a90d9;'>JSON API</a>"
                " | <a href='/diag' style='color:#4a90d9;'>诊断</a></footer>";
        html += "<script>setTimeout(()=>location.reload(), 5000);</script>";  // 5秒自动刷新
        html += "</body></html>";
        
        server_.send(200, "text/html; charset=utf-8", html);
    }
    
    void handleApiStatus() {
        auto& s = proto_->getCurrentSettings();
        auto& st = proto_->getCurrentStatus();
        
        String json = "{";
        json += "\"power\":\"" + String(s.power ? s.power : "N/A") + "\",";
        json += "\"mode\":\"" + String(s.mode ? s.mode : "N/A") + "\",";
        json += "\"temperature\":" + String(s.temperature, 1) + ",";
        json += "\"fan\":\"" + String(s.fan ? s.fan : "N/A") + "\",";
        json += "\"vane\":\"" + String(s.vane ? s.vane : "N/A") + "\",";
        json += "\"roomTemperature\":" + String(st.roomTemperature, 1) + ",";
        json += "\"outsideTemperature\":" + (isnan(st.outsideAirTemperature) ? "null" : String(st.outsideAirTemperature, 1)) + ",";
        json += "\"operating\":" + String(st.operating ? "true" : "false") + ",";
        json += "\"compressorFrequency\":" + (isnan(st.compressorFrequency) ? "null" : String(st.compressorFrequency, 1)) + ",";
        json += "\"inputPower\":" + (isnan(st.inputPower) ? "null" : String(st.inputPower, 0)) + ",";
        json += "\"kWh\":" + (isnan(st.kWh) ? "null" : String(st.kWh, 1)) + ",";
        json += "\"connected\":" + String(proto_->isConnected() ? "true" : "false") + ",";
        json += "\"uptime\":" + String((millis() - bootTime_) / 1000);
        json += "}";
        
        server_.send(200, "application/json", json);
    }
    
    void handleDiag() {
        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                       "<title>诊断</title></head><body><pre>";
        html += "运行时间: " + uptimeString() + "\n";
        html += "CN105 连接: " + String(proto_->isConnected() ? "是" : "否") + "\n";
        html += "可用堆内存: " + String(ESP.getFreeHeap()) + " bytes\n";
        html += "最小可用堆: " + String(ESP.getMinFreeHeap()) + " bytes\n";
        html += "CPU 频率: " + String(ESP.getCpuFreqMHz()) + " MHz\n";
        html += "Flash 大小: " + String(ESP.getFlashChipSize() / 1024) + " KB\n";
        html += "</pre></body></html>";
        
        server_.send(200, "text/html; charset=utf-8", html);
    }
    
    // 辅助方法
    String statBlock(const char* label, float value, const char* unit) {
        if (isnan(value)) return "<div class='stat'><div class='value'>--</div>"
                                  "<div class='label'>" + String(label) + "</div></div>";
        char buf[16];
        if (value == (int)value) snprintf(buf, sizeof(buf), "%d", (int)value);
        else snprintf(buf, sizeof(buf), "%.1f", value);
        return "<div class='stat'><div class='value'>" + String(buf) + String(unit) +
               "</div><div class='label'>" + String(label) + "</div></div>";
    }
    
    String row(const char* key, const char* val, const char* badgeClass) {
        String v = (badgeClass && strlen(badgeClass) > 0)
            ? "<span class='badge " + String(badgeClass) + "'>" + String(val) + "</span>"
            : String(val);
        return "<div class='row'><span class='key'>" + String(key) +
               "</span><span class='val'>" + v + "</span></div>";
    }
};
```

### 6.4 集成到主 Sketch

```cpp
#include "WebUI.h"

WebUI* webUI = nullptr;

void setup() {
    // ... HomeSpan 配置和配件定义 ...
    
    // Web UI（端口 8080）
    webUI = new WebUI(8080, g_proto, PAIRING_CODE);
    webUI->begin();
}

void loop() {
    homeSpan.poll();      // HomeKit（端口 80）
    webUI->loop();        // Web UI（端口 8080）
}
```

### 6.5 HomeSpan 内置状态页增强

除了独立 Web UI，还可以利用 HomeSpan 内置的 Web 日志页面注入空调状态：

```cpp
void webStatusCallback(String& html) {
    if (!g_proto) return;
    auto& s = g_proto->getCurrentSettings();
    auto& st = g_proto->getCurrentStatus();
    
    html += "<tr><td colspan=2 style='background:#1a1a3e;color:#7ec8e3;"
            "font-weight:bold;text-align:center'>三菱空调状态</td></tr>";
    html += "<tr><td>电源</td><td>" + String(s.power ? s.power : "--") + "</td></tr>";
    html += "<tr><td>模式</td><td>" + String(s.mode ? s.mode : "--") + "</td></tr>";
    html += "<tr><td>室温</td><td>" + String(st.roomTemperature, 1) + " °C</td></tr>";
    html += "<tr><td>设定温度</td><td>" + String(s.temperature, 1) + " °C</td></tr>";
    html += "<tr><td>风速</td><td>" + String(s.fan ? s.fan : "--") + "</td></tr>";
    html += "<tr><td>压缩机</td><td>" + (st.operating ? String("运行中") : String("待机")) + "</td></tr>";
    if (!isnan(st.compressorFrequency)) {
        html += "<tr><td>压缩机频率</td><td>" + String(st.compressorFrequency, 1) + " Hz</td></tr>";
    }
    if (!isnan(st.inputPower)) {
        html += "<tr><td>输入功率</td><td>" + String(st.inputPower, 0) + " W</td></tr>";
    }
}
```

这样在 `http://<ip>/status`（HomeSpan 内置页面）也能看到空调状态。

---

## 7. 项目结构

```
MitsubishiCN105HomeKit/
├── MitsubishiCN105HomeKit.ino      # 主 Sketch
├── src/
│   ├── MitsubishiTypes.h           # 协议常量和结构体（from cn105_types.h，无修改）
│   ├── MitsubishiProtocol.h        # 协议引擎头文件
│   ├── MitsubishiProtocol.cpp      # 协议引擎实现（from hp_readings + hp_writings + cn105）
│   ├── heatpumpFunctions.h         # 功能码处理（无修改）
│   ├── heatpumpFunctions.cpp       # （无修改）
│   ├── cycle_management.h          # 周期管理（无修改）
│   ├── cycle_management.cpp        # （无修改）
│   ├── HKHeaterCooler.h            # HomeKit HeaterCooler 服务
│   ├── HKOutdoorTemp.h             # 室外温度传感器
│   └── WebUI.h                     # Web 控制面板
├── platformio.ini
└── README.md
```

### platformio.ini

```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = HomeSpan/HomeSpan
monitor_speed = 115200
board_build.partitions = min_spiffs.csv
```

---

## 8. 分步实施计划

| 阶段 | 工作量 | 内容 |
|------|--------|------|
| 1. 协议提取 | ~2 天 | 从原项目提取协议引擎，替换 ESPHome API 为 Arduino 标准 API，删除互斥锁 |
| 2. 验证 | ~1 天 | 最小 sketch 连接空调，Serial 输出设置/状态，确认协议引擎工作正常 |
| 3. HomeKit 集成 | ~2 天 | 实现 HKHeaterCooler，模式/温度/风速/摆风映射，2 秒轮询同步 |
| 4. Web UI | ~1 天 | 实现状态面板，配对码显示，JSON API |
| 5. 测试 | ~2 天 | 全模式测试、遥控器不覆写验证、重连测试、长期稳定性 |

---

## 9. 依赖项

```
HomeSpan >= 1.9.0
Arduino-ESP32 >= 2.0.0
WebServer（ESP32 内置）
```

无 Python，无 YAML，无 Home Assistant，无云服务。
