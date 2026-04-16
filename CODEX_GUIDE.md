# Codex 傻瓜式迁移指南：三菱 CN105 ESPHome → HomeSpan (HomeKit)

> **本文件是写给 AI (Codex) 看的**。每一步都写得极其详细，包含完整可复制的代码。
> Codex：你只需要按顺序执行每个 TASK，不需要自己发明任何东西。

---

## PROJECT LOCAL RULES: 当前项目约定（优先于后文默认假设）

这些规则来自当前项目实际联调过程，是 **Codex 在本仓库中必须优先遵守** 的本地约定。
如果后文示例、旧草稿、或默认 Arduino/ESP32 习惯与这里冲突，**以本节为准**。

### 1. 串口分工

- 调试串口必须保持为默认 `Serial`，用于 Arduino 串口监视器输出日志
- CN105 通讯必须使用独立硬件串口，不得占用调试串口

### 2. CN105 引脚固定约定

在本项目中，ESP32 与 CN105 的 UART 连接固定按下面约定处理：

- `ESP32 GPIO26` = CN105 UART `RX`
- `ESP32 GPIO32` = CN105 UART `TX`

对应接线关系：

- `CN105 Pin4 (TX)` → `ESP32 GPIO26 (RX)`
- `CN105 Pin5 (RX)` → `ESP32 GPIO32 (TX)`
- `CN105 Pin2 (GND)` → `ESP32 GND`

### 3. 供电约定

- ESP32 默认由外部 `5V/USB` 自行供电
- 默认 **不要** 使用 CN105 的供电脚给 ESP32 供电
- 因此默认接线优先只使用 `GND + TX + RX`

### 3.1 CN105 连接器物理规格

CN105 在本项目中按下面这组物理连接器信息处理：

- 规格：`JST PA series`
- 位数：`5 pin`
- 间距：`2.00 mm`
- 常用壳体：`PAP-05V-S`
- 常用压接端子：`SPHD-002T-P0.5`

注意：

- `PA` 不是 `PH`
- 即使同为 `2.0mm` 间距，也不要默认 `PH 2.0` 可以直接替代 `PA 2.0`

### 3.2 M5Stack ATOM Lite 机身 4-pin 口规格

M5Stack ATOM Lite 机身自带的 4-pin 接口，在本项目中按下面约定理解：

- 规格：`Grove / HY2.0-4P`
- 位数：`4 pin`
- 间距：`2.0 mm`
- 不是：`JST PH`

在本项目中，默认把这组口当作 UART 口使用，并沿用下面映射：

- `Yellow` = `TX` = `GPIO32`
- `White` = `RX` = `GPIO26`
- `Red` = `5V`
- `Black` = `GND`

这与本项目的 CN105 默认 UART 引脚约定保持一致：

- `ESP32 GPIO26` = CN105 UART `RX`
- `ESP32 GPIO32` = CN105 UART `TX`

### 4. 当前开发顺序

这个项目当前采用“逐步落地”的方式推进，而不是一次性把所有 guide 内容全部写完：

- 先实现可烧录、可联网、可访问的 Web UI 最小版本
- 再逐步接入 CN105 协议层
- 最后再接 HomeSpan / HomeKit 集成

Codex 在用户没有明确要求“一次性全做完”时，应优先支持这种分阶段开发方式。

### 5. 配置管理约定

- WiFi、AP、端口、页面标题等可调参数，默认集中放到单独配置文件
- 不要把这类配置重新分散写回 `.ino` 主文件或业务逻辑文件

### 6. Web 联调约定

在协议层和 HomeKit 接入之前，Web 侧优先保持最小可验证链路：

- 页面可以发起简单请求
- ESP32 服务端返回简单响应
- 必要时可把网页输入内容打印到串口

这样做的目的，是先验证“刷机、联网、HTTP 通路、前后端交互”都没问题，再叠加更复杂逻辑。

### 7. HTTP 端口偏好

- 当前项目在联调阶段优先使用 HTTP 端口 `80`
- 原因：局域网访问更直接，避免因为遗漏 `:8080` 导致误判为服务不可用

---

## 目录

- PART 1: 背景知识 + 协议引擎（本文件）
- PART 2: HomeSpan 集成 + HomeKit 服务实现
- PART 3: Web UI + 主程序 + 项目结构

---

## TASK 0: 理解项目目标

**你要做什么：**
把一个 ESPHome 组件（控制三菱空调）迁移到 HomeSpan（Apple HomeKit），去掉 Home Assistant 依赖。

**硬件：**
- ESP32 开发板
- 三菱空调室内机的 CN105 连接器（5 针）
- 连接方式：ESP32 的 UART TX/RX ↔ CN105 的 Pin4/Pin5
- UART 参数：**2400 波特率, 8 数据位, 偶校验, 1 停止位**

**最终效果：**
- iPhone/iPad 的 Home App 可以控制空调（开关、模式、温度、风速、摆风）
- ESP32 上有一个 Web 页面显示空调状态和 HomeKit 配对码
- 遥控器设置的模式如果 HomeKit 不支持（如除湿/送风），ESP32 不会覆写

---

## TASK 1: 创建项目目录结构

在你的工作目录下创建以下结构：

```
MitsubishiCN105HomeKit/
├── MitsubishiCN105HomeKit.ino      # Arduino 主程序
├── src/
│   ├── MitsubishiTypes.h           # 协议常量（直接复制）
│   ├── MitsubishiProtocol.h        # 协议引擎头文件（新写）
│   ├── MitsubishiProtocol.cpp      # 协议引擎实现（新写）
│   ├── HKHeaterCooler.h            # HomeKit 服务（新写）
│   └── WebUI.h                     # Web 界面（新写）
└── platformio.ini                  # 构建配置
```

---

## TASK 2: 创建 platformio.ini

直接复制这个文件，不要改任何东西：

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

## TASK 3: 创建 MitsubishiTypes.h

这个文件定义了所有协议常量和数据结构。**大部分从原项目的 `cn105_types.h` 直接复制**，只删除了 ESPHome 依赖。

把下面的代码完整复制到 `src/MitsubishiTypes.h`：

```cpp
#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>

// ============================================================
// 缓冲区大小
// ============================================================
#define MAX_DATA_BYTES 64

// ============================================================
// 数据包长度常量
// ============================================================
static const int PACKET_LEN = 22;       // SET 和 INFO 数据包都是 22 字节
static const int CONNECT_LEN = 8;       // 连接握手包是 8 字节
static const int HEADER_LEN = 8;        // SET 包头部 8 字节
static const int INFOHEADER_LEN = 5;    // INFO 请求包头部 5 字节

// ============================================================
// 预定义数据包模板
// ============================================================

// 连接握手包: FC 5A 01 30 02 CA 01 A8
// byte[1] = 0x5A 表示标准模式, 0x5B 表示安装工模式
static const uint8_t CONNECT[CONNECT_LEN] = {
    0xFC, 0x5A, 0x01, 0x30, 0x02, 0xCA, 0x01, 0xA8
};

// SET 命令包头部: FC 41 01 30 10 01 00 00
// byte[1] = 0x41 表示这是 SET 命令
// byte[4] = 0x10 表示数据长度 16 字节
static const uint8_t HEADER[HEADER_LEN] = {
    0xFC, 0x41, 0x01, 0x30, 0x10, 0x01, 0x00, 0x00
};

// INFO 请求包头部: FC 42 01 30 10
// byte[1] = 0x42 表示这是 INFO 请求
static const uint8_t INFOHEADER[INFOHEADER_LEN] = {
    0xFC, 0x42, 0x01, 0x30, 0x10
};

// ============================================================
// SET 包的控制标志位
// ============================================================
// byte[6] 中的位掩码，告诉空调"我要改哪些字段"
// 如果你要改温度，就把 byte[6] |= 0x04
static const uint8_t CONTROL_PACKET_1[5] = {
    0x01,   // bit 0: 电源
    0x02,   // bit 1: 模式
    0x04,   // bit 2: 温度
    0x08,   // bit 3: 风速
    0x10    // bit 4: 垂直导风板
};
// byte[7] 中的位掩码
static const uint8_t CONTROL_PACKET_2[1] = {
    0x01    // bit 0: 水平导风板
};

// ============================================================
// 值映射表
// ============================================================
// 每个表都是 "协议字节值" 和 "人类可读字符串" 的对应关系

// --- 电源 ---
// 0x00 = OFF, 0x01 = ON
static const uint8_t POWER[2] = { 0x00, 0x01 };
static const char* POWER_MAP[2] = { "OFF", "ON" };

// --- 模式 ---
// 0x01=制热, 0x02=除湿, 0x03=制冷, 0x07=送风, 0x08=自动
static const uint8_t MODE[5] = { 0x01, 0x02, 0x03, 0x07, 0x08 };
static const char* MODE_MAP[5] = { "HEAT", "DRY", "COOL", "FAN", "AUTO" };

// --- 温度（传统模式）---
// 0x00=31°C, 0x01=30°C, ..., 0x0F=16°C
// 注意：是倒序的！0x00 对应最高温度 31°C
static const uint8_t TEMP[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};
static const int TEMP_MAP[16] = {
    31, 30, 29, 28, 27, 26, 25, 24,
    23, 22, 21, 20, 19, 18, 17, 16
};

// --- 风速 ---
// 注意：没有 0x04！从 0x03 跳到 0x05
static const uint8_t FAN[6] = { 0x00, 0x01, 0x02, 0x03, 0x05, 0x06 };
static const char* FAN_MAP[6] = { "AUTO", "QUIET", "1", "2", "3", "4" };

// --- 垂直导风板 ---
// 注意：没有 0x06！从 0x05 跳到 0x07
static const uint8_t VANE[7] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07 };
static const char* VANE_MAP[7] = { "AUTO", "1", "2", "3", "4", "5", "SWING" };

// --- 水平导风板 ---
static const uint8_t WIDEVANE[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x0C, 0x00 };
static const char* WIDEVANE_MAP[8] = { "<<", "<", "|", ">", ">>", "<>", "SWING", "AIRFLOW CONTROL" };

// --- 室温（传统模式）---
// 0x00=10°C, 0x01=11°C, ..., 0x1F=41°C
static const uint8_t ROOM_TEMP[32] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
    0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F
};
static const int ROOM_TEMP_MAP[32] = {
    10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,
    26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41
};

// --- 运行阶段 ---
static const uint8_t STAGE[7] = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06 };
static const char* STAGE_MAP[7] = { "IDLE","LOW","GENTLE","MEDIUM","MODERATE","HIGH","DIFFUSE" };

// --- 子模式 ---
static const uint8_t SUB_MODE[5] = { 0x00,0x01,0x02,0x04,0x08 };
static const char* SUB_MODE_MAP[5] = { "NORMAL","WARMUP","DEFROST","PREHEAT","STANDBY" };

// ============================================================
// 查找辅助函数
// ============================================================

// 根据字节值在映射表中查找对应的字符串
// 例如: lookupByteMapValue(POWER_MAP, POWER, 2, 0x01) 返回 "ON"
template<typename T>
static T lookupByteMapValue(const T* map, const uint8_t* bytes, int len, uint8_t value) {
    for (int i = 0; i < len; i++) {
        if (bytes[i] == value) return map[i];
    }
    return map[0]; // 找不到就返回第一个（默认值）
}

// 根据字符串在映射表中查找对应的索引
// 例如: lookupByteMapIndex(MODE_MAP, 5, "COOL") 返回 2
static int lookupByteMapIndex(const char** map, int len, const char* value) {
    for (int i = 0; i < len; i++) {
        if (strcmp(map[i], value) == 0) return i;
    }
    return -1; // 找不到
}

// ============================================================
// 数据结构
// ============================================================

// 空调当前设置（从 0x02 响应解析得到）
struct heatpumpSettings {
    const char* power;          // "ON" 或 "OFF"
    const char* mode;           // "HEAT","DRY","COOL","FAN","AUTO"
    float temperature;          // 设定温度（摄氏度），-1 表示未知
    const char* fan;            // "AUTO","QUIET","1","2","3","4"
    const char* vane;           // "AUTO","1","2","3","4","5","SWING"
    const char* wideVane;       // "<<","<","|",">",">>","<>","SWING"
    bool iSee;                  // iSee 传感器是否激活
    bool connected;             // 是否已连接

    void reset() {
        power = nullptr;
        mode = nullptr;
        temperature = -1.0f;
        fan = nullptr;
        vane = nullptr;
        wideVane = nullptr;
        iSee = false;
        connected = false;
    }
};

// 空调运行状态（从 0x03 和 0x06 响应解析得到）
struct heatpumpStatus {
    float roomTemperature;          // 室温
    float outsideAirTemperature;    // 室外温度
    bool operating;                 // 压缩机是否在运行
    float compressorFrequency;      // 压缩机频率 (Hz)
    float inputPower;               // 输入功率 (W)
    float kWh;                      // 累计能耗 (kWh)
    float runtimeHours;             // 运行时间 (小时)

    void reset() {
        roomTemperature = NAN;
        outsideAirTemperature = NAN;
        operating = false;
        compressorFrequency = 0;
        inputPower = 0;
        kWh = 0;
        runtimeHours = 0;
    }
};
```

---

## TASK 4: 理解 CN105 串口协议（必读！）

在写任何代码之前，你必须理解这个协议。这是整个项目的核心。

### 4.1 数据包格式

**每个数据包**都长这样：

```
[0xFC] [CMD] [0x01] [0x30] [DATA_LEN] [DATA...] [CHECKSUM]
  ↑      ↑     ↑      ↑       ↑          ↑          ↑
  |      |     固定    固定    数据长度   数据负载   校验和
  |      |
  起始   命令类型
  标记
```

- **总长度** = 5（头部） + DATA_LEN + 1（校验和）
- **校验和算法**：把前面所有字节加起来，然后 `(0xFC - sum) & 0xFF`

### 4.2 命令类型速查表

| CMD 字节 | 方向 | 含义 |
|----------|------|------|
| `0x5A` | ESP→空调 | 连接握手（标准模式） |
| `0x7A` | 空调→ESP | 连接成功回复 |
| `0x41` | ESP→空调 | SET 命令（改设置） |
| `0x61` | 空调→ESP | SET 成功确认 |
| `0x42` | ESP→空调 | INFO 请求（读状态） |
| `0x62` | 空调→ESP | INFO 数据回复 |

### 4.3 INFO 请求码

发 `0x42` 包时，`data[0]` 指定你要查什么：

| data[0] | 返回什么 |
|---------|----------|
| `0x02` | 当前设置（电源、模式、温度、风速、导风板） |
| `0x03` | 温度（室温、室外温度、运行时间） |
| `0x06` | 状态（压缩机频率、功率、能耗） |
| `0x09` | 阶段和子模式 |

### 4.4 SET 包的 22 字节布局

```
字节  0: 0xFC（起始）
字节  1: 0x41（SET 命令）
字节  2: 0x01（固定）
字节  3: 0x30（固定）
字节  4: 0x10（数据长度=16）
字节  5: 0x01（固定）
字节  6: 控制标志1（位掩码：0x01=电源, 0x02=模式, 0x04=温度, 0x08=风速, 0x10=导风板）
字节  7: 控制标志2（0x01=水平导风板）
字节  8: 电源（0x00=关, 0x01=开）
字节  9: 模式（0x01=制热, 0x02=除湿, 0x03=制冷, 0x07=送风, 0x08=自动）
字节 10: 温度-传统模式（0x00=31°C ... 0x0F=16°C）
字节 11: 风速（0x00=自动, 0x01=静音, 0x02=1档, 0x03=2档, 0x05=3档, 0x06=4档）
字节 12: 垂直导风板（0x00=自动 ... 0x07=摆动）
字节 13-17: 保留（填0）
字节 18: 水平导风板
字节 19: 温度-高精度模式（值 = temp * 2 + 128）
字节 20: 保留（填0）
字节 21: 校验和
```

**关键**：字节 6 是位掩码！只有你设置了对应位的字段才会被空调采纳。
比如你只想改温度：`byte[6] = 0x04`，然后只填 `byte[10]` 或 `byte[19]`。

### 4.5 响应解析（0x62 包）

空调回复 `0x62` 包时，`data[0]` 告诉你这是什么数据：

**data[0] = 0x02（设置）：**
```
data[3] = 电源字节  →  用 POWER_MAP 查找
data[4] = 模式字节  →  如果 > 0x08，说明 iSee 开了，减 0x08 后用 MODE_MAP 查找
data[5] = 温度（传统模式）→  用 TEMP_MAP 查找
data[6] = 风速    →  用 FAN_MAP 查找
data[7] = 垂直导风板  →  用 VANE_MAP 查找
data[10] = 水平导风板  →  低4位用 WIDEVANE_MAP 查找
data[11] = 温度（高精度）→  如果不为0: (value - 128) / 2.0
```

**data[0] = 0x03（温度）：**
```
data[3] = 室温（传统模式）→  用 ROOM_TEMP_MAP 查找
data[5] = 室外温度  →  (value - 128) / 2.0，如果 ≤ 1 则为 NAN
data[6] = 室温（高精度）→  如果不为0: (value - 128) / 2.0
data[11-13] = 运行分钟数（24位大端序）→  / 60 = 小时
```

**data[0] = 0x06（状态）：**
```
data[3] = 压缩机频率 (Hz)
data[4] = 运行标志（1=运行, 0=待机）
data[5-6] = 输入功率（16位大端序，瓦特）
data[7-8] = 能耗（16位大端序，/ 10 = kWh）
```
## TASK 5: 创建 MitsubishiProtocol.h

这是协议引擎的头文件。它封装了所有和空调通信的逻辑，不依赖任何框架（不依赖 ESPHome，不依赖 HomeSpan）。

把下面的代码完整复制到 `src/MitsubishiProtocol.h`：

```cpp
#pragma once
#include <Arduino.h>
#include "MitsubishiTypes.h"

// ============================================================
// MitsubishiProtocol 类
// ============================================================
// 这个类负责：
// 1. 通过 UART 和空调通信
// 2. 解析空调返回的数据包
// 3. 构建并发送控制命令
// 4. 管理轮询周期（每 2 秒轮询一次空调状态）
//
// 使用方法：
//   MitsubishiProtocol proto(&Serial1);
//   proto.connect();                    // 发送握手包
//   // 在 loop() 中：
//   proto.processInput();               // 读取 UART
//   proto.loopPollCycle();              // 管理轮询周期
//   if (proto.hasNewSettings()) { ... } // 检查是否有新数据
// ============================================================

class MitsubishiProtocol {
public:
    // ============================================================
    // 构造函数
    // ============================================================
    // serial: 连接到 CN105 的硬件串口（必须已经用 begin(2400, SERIAL_8E1) 初始化过）
    MitsubishiProtocol(HardwareSerial* serial)
        : serial_(serial)
    {
        currentSettings_.reset();
        currentStatus_.reset();
        wantedPower_ = nullptr;
        wantedMode_ = nullptr;
        wantedTemperature_ = -1;
        wantedFan_ = nullptr;
        wantedVane_ = nullptr;
        wantedWideVane_ = nullptr;
        wantedChanged_ = false;
        initParser();
    }

    // ============================================================
    // 连接管理
    // ============================================================

    // 发送握手包连接空调
    // 调用后需要等待空调回复 0x7A/0x7B
    void connect() {
        connected_ = false;
        uint8_t packet[CONNECT_LEN];
        memcpy(packet, CONNECT, CONNECT_LEN);
        // 使用标准模式 0x5A（不用安装工模式）
        packet[1] = 0x5A;
        packet[CONNECT_LEN - 1] = calcCheckSum(packet, CONNECT_LEN - 1);
        writePacket(packet, CONNECT_LEN);
        lastConnectAttemptMs_ = millis();
        Serial.println("[CN105] Connect packet sent (0x5A)");
    }

    // 是否已连接
    bool isConnected() const { return connected_; }

    // ============================================================
    // 主循环方法（必须在 loop() 中频繁调用）
    // ============================================================

    // 读取 UART 缓冲区中的所有字节，逐字节喂给解析器
    void processInput() {
        while (serial_->available()) {
            uint8_t b = serial_->read();
            parse(b);
        }
    }

    // 管理轮询周期：每 POLL_INTERVAL_MS 发一轮 INFO 请求
    // 这个方法也要在 loop() 中调用
    void loopPollCycle() {
        uint32_t now = millis();

        // 如果没连接，尝试重连
        if (!connected_) {
            if (now - lastConnectAttemptMs_ > 10000) {
                Serial.println("[CN105] Not connected, retrying...");
                connect();
            }
            return;
        }

        // 如果正在等待回复，检查超时
        if (awaitingResponse_) {
            if (now - lastRequestMs_ > 1000) {
                // 超时了，跳到下一个请求
                Serial.printf("[CN105] Timeout waiting for 0x%02X response\n", pollCodes_[currentPollIndex_]);
                awaitingResponse_ = false;
                currentPollIndex_++;
                if (currentPollIndex_ >= POLL_CODE_COUNT) {
                    // 这一轮结束了
                    cycleRunning_ = false;
                    currentPollIndex_ = 0;
                }
            }
            return;
        }

        // 如果有一个周期在跑，发下一个请求
        if (cycleRunning_) {
            sendNextPollRequest();
            return;
        }

        // 检查是否该开始新的轮询周期
        if (now - lastPollCycleMs_ >= POLL_INTERVAL_MS) {
            lastPollCycleMs_ = now;
            cycleRunning_ = true;
            currentPollIndex_ = 0;
            sendNextPollRequest();
        }
    }

    // ============================================================
    // 状态查询
    // ============================================================

    // 自上次调用后是否收到了新的设置数据
    bool hasNewSettings() {
        if (newSettings_) { newSettings_ = false; return true; }
        return false;
    }

    // 自上次调用后是否收到了新的状态数据
    bool hasNewStatus() {
        if (newStatus_) { newStatus_ = false; return true; }
        return false;
    }

    // 获取当前设置（只读引用）
    const heatpumpSettings& getCurrentSettings() const { return currentSettings_; }

    // 获取当前状态（只读引用）
    const heatpumpStatus& getCurrentStatus() const { return currentStatus_; }

    // ============================================================
    // 控制方法（设置想要的值）
    // ============================================================
    // 调用这些方法设置你想要的值，然后调用 commitSettings() 发送

    void setPower(const char* power) { wantedPower_ = power; wantedChanged_ = true; }
    void setMode(const char* mode)   { wantedMode_ = mode; wantedChanged_ = true; }
    void setTemperature(float temp)   { wantedTemperature_ = temp; wantedChanged_ = true; }
    void setFan(const char* fan)      { wantedFan_ = fan; wantedChanged_ = true; }
    void setVane(const char* vane)    { wantedVane_ = vane; wantedChanged_ = true; }
    void setWideVane(const char* wv)  { wantedWideVane_ = wv; wantedChanged_ = true; }

    // 构建 SET 数据包并发送到空调
    // 只发送你用上面的方法设置过的字段
    void commitSettings() {
        if (!wantedChanged_ || !connected_) return;

        uint8_t packet[PACKET_LEN];
        memset(packet, 0, PACKET_LEN);

        // 复制 SET 头部
        memcpy(packet, HEADER, HEADER_LEN);

        // 填充各字段
        if (wantedPower_) {
            int idx = lookupByteMapIndex(POWER_MAP, 2, wantedPower_);
            if (idx >= 0) {
                packet[8] = POWER[idx];
                packet[6] |= CONTROL_PACKET_1[0]; // 设置"电源已改"标志
            }
        }

        if (wantedMode_) {
            int idx = lookupByteMapIndex(MODE_MAP, 5, wantedMode_);
            if (idx >= 0) {
                packet[9] = MODE[idx];
                packet[6] |= CONTROL_PACKET_1[1]; // 设置"模式已改"标志
            }
        }

        if (wantedTemperature_ >= 0) {
            if (tempMode_) {
                // 高精度模式：值 = temp * 2 + 128
                packet[19] = (uint8_t)(wantedTemperature_ * 2 + 128);
            } else {
                // 传统模式：查表
                int idx = -1;
                for (int i = 0; i < 16; i++) {
                    if (TEMP_MAP[i] == (int)wantedTemperature_) { idx = i; break; }
                }
                if (idx >= 0) {
                    packet[10] = TEMP[idx];
                }
            }
            packet[6] |= CONTROL_PACKET_1[2]; // 设置"温度已改"标志
        }

        if (wantedFan_) {
            int idx = lookupByteMapIndex(FAN_MAP, 6, wantedFan_);
            if (idx >= 0) {
                packet[11] = FAN[idx];
                packet[6] |= CONTROL_PACKET_1[3]; // 设置"风速已改"标志
            }
        }

        if (wantedVane_) {
            int idx = lookupByteMapIndex(VANE_MAP, 7, wantedVane_);
            if (idx >= 0) {
                packet[12] = VANE[idx];
                packet[6] |= CONTROL_PACKET_1[4]; // 设置"导风板已改"标志
            }
        }

        if (wantedWideVane_) {
            int idx = lookupByteMapIndex(WIDEVANE_MAP, 8, wantedWideVane_);
            if (idx >= 0) {
                packet[18] = WIDEVANE[idx];
                packet[7] |= CONTROL_PACKET_2[0]; // 设置"水平导风板已改"标志
            }
        }

        // 计算校验和
        packet[21] = calcCheckSum(packet, 21);

        // 发送
        writePacket(packet, PACKET_LEN);

        // 清除 wanted 状态
        wantedPower_ = nullptr;
        wantedMode_ = nullptr;
        wantedTemperature_ = -1;
        wantedFan_ = nullptr;
        wantedVane_ = nullptr;
        wantedWideVane_ = nullptr;
        wantedChanged_ = false;

        lastCommandMs_ = millis();
        Serial.println("[CN105] SET packet sent");
    }

    // 上次发送 HomeKit 命令的时间（用于 grace period）
    uint32_t getLastCommandMs() const { return lastCommandMs_; }

private:
    HardwareSerial* serial_;

    // ============================================================
    // 连接状态
    // ============================================================
    bool connected_ = false;
    uint32_t lastConnectAttemptMs_ = 0;
    uint32_t lastCommandMs_ = 0;

    // ============================================================
    // 当前空调状态
    // ============================================================
    heatpumpSettings currentSettings_;
    heatpumpStatus currentStatus_;
    bool newSettings_ = false;
    bool newStatus_ = false;
    bool tempMode_ = false;   // true = 使用高精度温度（data[11]），false = 传统模式

    // ============================================================
    // 用户想要设置的值
    // ============================================================
    const char* wantedPower_;
    const char* wantedMode_;
    float wantedTemperature_;
    const char* wantedFan_;
    const char* wantedVane_;
    const char* wantedWideVane_;
    bool wantedChanged_;

    // ============================================================
    // 轮询周期管理
    // ============================================================
    static const uint32_t POLL_INTERVAL_MS = 2000; // 每 2 秒轮询一次
    static const int POLL_CODE_COUNT = 4;
    // 我们只轮询这 4 个关键的 INFO 代码
    const uint8_t pollCodes_[POLL_CODE_COUNT] = { 0x02, 0x03, 0x06, 0x09 };
    uint32_t lastPollCycleMs_ = 0;
    bool cycleRunning_ = false;
    int currentPollIndex_ = 0;
    bool awaitingResponse_ = false;
    uint32_t lastRequestMs_ = 0;

    // ============================================================
    // 数据包解析状态机
    // ============================================================
    uint8_t storedInputData_[MAX_DATA_BYTES];
    bool foundStart_ = false;
    int bytesRead_ = 0;
    int dataLength_ = -1;
    uint8_t command_ = 0;
    uint8_t* data_ = nullptr;   // 指向 storedInputData_[5]，即负载开始位置

    void initParser() {
        foundStart_ = false;
        bytesRead_ = 0;
        dataLength_ = -1;
        command_ = 0;
    }

    // ============================================================
    // 校验和计算
    // ============================================================
    // 把前 len 个字节加起来，返回 (0xFC - sum) & 0xFF
    static uint8_t calcCheckSum(const uint8_t* bytes, int len) {
        uint8_t sum = 0;
        for (int i = 0; i < len; i++) sum += bytes[i];
        return (0xFC - sum) & 0xFF;
    }

    // 验证收到的包的校验和
    bool verifyCheckSum() {
        uint8_t expected = storedInputData_[bytesRead_]; // 最后一个字节是校验和
        uint8_t computed = calcCheckSum(storedInputData_, dataLength_ + 5);
        return expected == computed;
    }

    // ============================================================
    // 发送数据包
    // ============================================================
    void writePacket(const uint8_t* packet, int length) {
        for (int i = 0; i < length; i++) {
            serial_->write(packet[i]);
        }
    }

    // ============================================================
    // 发送 INFO 请求包
    // ============================================================
    void sendInfoRequest(uint8_t code) {
        uint8_t packet[PACKET_LEN];
        memset(packet, 0, PACKET_LEN);
        // 复制 INFO 头部
        for (int i = 0; i < INFOHEADER_LEN; i++) {
            packet[i] = INFOHEADER[i];
        }
        packet[5] = code;  // 请求码
        // 后面 15 字节已经是 0 了（memset）
        packet[21] = calcCheckSum(packet, 21);
        writePacket(packet, PACKET_LEN);

        awaitingResponse_ = true;
        lastRequestMs_ = millis();
    }

    // ============================================================
    // 轮询周期：发送下一个 INFO 请求
    // ============================================================
    void sendNextPollRequest() {
        if (currentPollIndex_ >= POLL_CODE_COUNT) {
            cycleRunning_ = false;
            currentPollIndex_ = 0;
            return;
        }
        uint8_t code = pollCodes_[currentPollIndex_];
        sendInfoRequest(code);
        Serial.printf("[CN105] Sent INFO request 0x%02X\n", code);
    }

    // ============================================================
    // 解析状态机
    // ============================================================
    // 逐字节喂入，当收到完整数据包时自动处理
    void parse(uint8_t b) {
        if (!foundStart_) {
            // 还没找到起始字节，等待 0xFC
            if (b == 0xFC) {
                foundStart_ = true;
                bytesRead_ = 0;
                storedInputData_[bytesRead_++] = b;
            }
            return;
        }

        // 防止缓冲区溢出
        if (bytesRead_ >= MAX_DATA_BYTES - 1) {
            initParser();
            return;
        }

        storedInputData_[bytesRead_] = b;

        // 当读到第 5 个字节（index 4）时，解析头部
        if (bytesRead_ == 4) {
            command_ = storedInputData_[1];
            dataLength_ = storedInputData_[4];

            // 安全检查
            if (dataLength_ + 6 > MAX_DATA_BYTES) {
                initParser();
                return;
            }
        }

        if (dataLength_ != -1) {
            // 头部已完整，检查是否收够了所有字节
            if (bytesRead_ == dataLength_ + 5) {
                // 完整数据包！处理它
                processPacket();
                initParser();
            } else {
                bytesRead_++;
            }
        } else {
            // 头部还没完整
            bytesRead_++;
        }
    }

    // ============================================================
    // 处理完整数据包
    // ============================================================
    void processPacket() {
        data_ = &storedInputData_[5]; // 负载从 byte[5] 开始

        if (!verifyCheckSum()) {
            Serial.println("[CN105] Checksum FAILED, dropping packet");
            return;
        }

        switch (command_) {
            case 0x7A: // 标准连接成功
            case 0x7B: // 安装工连接成功
                connected_ = true;
                currentSettings_.reset();
                currentStatus_.reset();
                Serial.printf("[CN105] Connected! (0x%02X)\n", command_);
                break;

            case 0x61: // SET 确认
                Serial.println("[CN105] SET acknowledged (0x61)");
                break;

            case 0x62: // INFO 数据回复
                processInfoResponse();
                break;

            default:
                Serial.printf("[CN105] Unknown command: 0x%02X\n", command_);
                break;
        }
    }

    // ============================================================
    // 处理 0x62 INFO 响应
    // ============================================================
    void processInfoResponse() {
        uint8_t code = data_[0]; // 响应中的第一个字节是 info code

        // 标记这个请求已收到回复
        awaitingResponse_ = false;

        switch (code) {
            case 0x02: parseSettingsResponse(); break;
            case 0x03: parseRoomTempResponse(); break;
            case 0x06: parseStatusResponse(); break;
            case 0x09: parseStageResponse(); break;
            default:
                Serial.printf("[CN105] Unknown info code: 0x%02X\n", code);
                break;
        }

        // 推进到下一个轮询请求
        currentPollIndex_++;
        if (currentPollIndex_ >= POLL_CODE_COUNT) {
            cycleRunning_ = false;
            currentPollIndex_ = 0;
        }
    }

    // ============================================================
    // 解析 0x02 设置响应
    // ============================================================
    void parseSettingsResponse() {
        currentSettings_.connected = true;
        currentSettings_.power = lookupByteMapValue(POWER_MAP, POWER, 2, data_[3]);

        // iSee 检测：如果 data[4] > 0x08，说明 iSee 开了
        currentSettings_.iSee = data_[4] > 0x08;
        uint8_t modeVal = currentSettings_.iSee ? (data_[4] - 0x08) : data_[4];
        currentSettings_.mode = lookupByteMapValue(MODE_MAP, MODE, 5, modeVal);

        // 温度
        if (data_[11] != 0x00) {
            // 高精度模式
            currentSettings_.temperature = (float)(data_[11] - 128) / 2.0f;
            tempMode_ = true;
        } else {
            // 传统模式
            currentSettings_.temperature = (float)lookupByteMapValue(TEMP_MAP, TEMP, 16, data_[5]);
        }

        currentSettings_.fan = lookupByteMapValue(FAN_MAP, FAN, 6, data_[6]);
        currentSettings_.vane = lookupByteMapValue(VANE_MAP, VANE, 7, data_[7]);

        if (data_[10] != 0) {
            currentSettings_.wideVane = lookupByteMapValue(
                WIDEVANE_MAP, WIDEVANE, 8, data_[10] & 0x0F);
        }

        newSettings_ = true;
        Serial.printf("[CN105] Settings: power=%s mode=%s temp=%.1f fan=%s vane=%s\n",
            currentSettings_.power, currentSettings_.mode, currentSettings_.temperature,
            currentSettings_.fan, currentSettings_.vane);
    }

    // ============================================================
    // 解析 0x03 室温响应
    // ============================================================
    void parseRoomTempResponse() {
        // 室温
        if (data_[6] != 0x00) {
            currentStatus_.roomTemperature = (float)(data_[6] - 128) / 2.0f;
        } else {
            currentStatus_.roomTemperature =
                (float)lookupByteMapValue(ROOM_TEMP_MAP, ROOM_TEMP, 32, data_[3]);
        }

        // 室外温度
        if (data_[5] > 1) {
            currentStatus_.outsideAirTemperature = (float)(data_[5] - 128) / 2.0f;
        } else {
            currentStatus_.outsideAirTemperature = NAN;
        }

        // 运行时间
        currentStatus_.runtimeHours =
            (float)((data_[11] << 16) | (data_[12] << 8) | data_[13]) / 60.0f;

        newStatus_ = true;
        Serial.printf("[CN105] Room=%.1f°C Outside=%.1f°C Runtime=%.1fh\n",
            currentStatus_.roomTemperature,
            currentStatus_.outsideAirTemperature,
            currentStatus_.runtimeHours);
    }

    // ============================================================
    // 解析 0x06 状态响应
    // ============================================================
    void parseStatusResponse() {
        currentStatus_.compressorFrequency = (float)data_[3];
        currentStatus_.operating = data_[4] != 0;
        currentStatus_.inputPower = (float)((data_[5] << 8) | data_[6]);
        currentStatus_.kWh = (float)((data_[7] << 8) | data_[8]) / 10.0f;

        newStatus_ = true;
        Serial.printf("[CN105] Compressor=%dHz Operating=%s Power=%.0fW Energy=%.1fkWh\n",
            (int)currentStatus_.compressorFrequency,
            currentStatus_.operating ? "YES" : "NO",
            currentStatus_.inputPower, currentStatus_.kWh);
    }

    // ============================================================
    // 解析 0x09 阶段响应
    // ============================================================
    void parseStageResponse() {
        // 我们读取但不存储阶段/子模式（HomeKit 不需要）
        // 只打印日志
        const char* stage = lookupByteMapValue(STAGE_MAP, STAGE, 7, data_[4]);
        const char* subMode = lookupByteMapValue(SUB_MODE_MAP, SUB_MODE, 5, data_[3]);
        Serial.printf("[CN105] Stage=%s SubMode=%s\n", stage, subMode);
    }
};
```
## TASK 6: 理解 HomeSpan API（必读！）

HomeSpan 是一个 Arduino-ESP32 的 HomeKit 库。下面是你**必须知道**的所有 API。

### 6.1 程序结构

```cpp
#include "HomeSpan.h"

void setup() {
    Serial.begin(115200);

    // 配置（必须在 begin() 之前）
    homeSpan.setPairingCode("46637726");   // HomeKit 配对码
    homeSpan.setQRID("MIAC");              // QR 码 ID
    homeSpan.enableOTA();                   // 允许 OTA 更新
    homeSpan.setLogLevel(1);

    // 初始化
    homeSpan.begin(Category::AirConditioners, "三菱空调");

    // 定义配件和服务
    new SpanAccessory();
      new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name("空调");
      new MyHeaterCooler();  // 你的自定义服务
}

void loop() {
    homeSpan.poll();  // 这一行驱动一切
}
```

### 6.2 Service 的两个核心方法

你需要继承一个 HomeKit 服务（如 `Service::HeaterCooler`），然后重写两个方法：

#### `boolean update()` — 当用户在 iPhone 上改了设置

```cpp
boolean update() override {
    // 检查哪些特征被改了
    if (active->updated()) {
        int val = active->getNewVal();  // 获取新值
        // val == 0 表示关机，val == 1 表示开机
    }
    if (targetState->updated()) {
        int mode = targetState->getNewVal();
        // 0=AUTO, 1=HEAT, 2=COOL
    }
    if (coolingThreshold->updated()) {
        float temp = coolingThreshold->getNewVal<float>();
    }
    return true;  // 返回 true 表示成功
}
```

**关键规则：**
- `updated()` 只在 `update()` 里有效
- `getNewVal()` 只在 `update()` 里有效
- `getVal()` 返回当前值（不是新值）

#### `void loop()` — 每次 homeSpan.poll() 都会调用

```cpp
void loop() override {
    // 读传感器，推状态到 HomeKit
    float temp = readSensor();
    if (temp != currentTemp->getVal<float>()) {
        currentTemp->setVal(temp);  // 推送到 HomeKit
    }
}
```

**关键规则：**
- `setVal()` 只在 `loop()` 里调用（不能在 `update()` 里调用！）
- `setVal()` 只是告诉 HomeKit 当前状态，**不会**触发 `update()`
- 这意味着：你用 `setVal()` 报告除湿模式为 AUTO，不会导致 `update()` 被调用

### 6.3 HeaterCooler 服务的特征

```
Service::HeaterCooler
├── Active                       0=关机, 1=开机
├── CurrentTemperature           当前室温 (float, 只读)
├── CurrentHeaterCoolerState     0=INACTIVE, 1=IDLE, 2=HEATING, 3=COOLING
├── TargetHeaterCoolerState      0=AUTO, 1=HEAT, 2=COOL
├── CoolingThresholdTemperature  制冷目标温度 (float)
├── HeatingThresholdTemperature  制热目标温度 (float)
├── RotationSpeed                风速 0-100% (float)
└── SwingMode                    0=关闭, 1=开启
```

### 6.4 SpanCharacteristic 方法速查

| 方法 | 在哪用 | 干什么 |
|------|--------|--------|
| `getVal<T>()` | loop() 或 update() | 获取当前值 |
| `getNewVal<T>()` | 仅 update() | 获取用户设的新值 |
| `setVal(value)` | 仅 loop() | 推送值到 HomeKit |
| `updated()` | 仅 update() | 这个特征是否被用户改了 |
| `setRange(min,max,step)` | 构造函数 | 设置值范围 |
| `setValidValues(n,v1,v2,...)` | 构造函数 | 限制枚举值 |

---

## TASK 7: 创建 HKHeaterCooler.h

这是 HomeKit 服务的实现。它是协议引擎和 HomeKit 之间的桥梁。

把下面的代码完整复制到 `src/HKHeaterCooler.h`：

```cpp
#pragma once
#include "HomeSpan.h"
#include "MitsubishiProtocol.h"

// ============================================================
// 风速转换
// ============================================================
// 三菱有 6 档风速，HomeKit 用 0-100% 的连续值
// 我们把它分成几个区间

// 三菱风速 → HomeKit 百分比
static float fanToPercent(const char* fan) {
    if (!fan) return 0;
    if (strcmp(fan, "QUIET") == 0) return 14;
    if (strcmp(fan, "1") == 0)     return 28;
    if (strcmp(fan, "2") == 0)     return 42;
    if (strcmp(fan, "3") == 0)     return 71;
    if (strcmp(fan, "4") == 0)     return 100;
    return 0;  // AUTO → 0%
}

// HomeKit 百分比 → 三菱风速
static const char* percentToFan(float pct) {
    if (pct <= 0)  return "AUTO";
    if (pct <= 20) return "QUIET";
    if (pct <= 35) return "1";
    if (pct <= 55) return "2";
    if (pct <= 80) return "3";
    return "4";
}

// ============================================================
// HKHeaterCooler 类
// ============================================================
class HKHeaterCooler : public Service::HeaterCooler {

    // 协议引擎（负责和空调通信）
    MitsubishiProtocol* proto_;

    // HomeKit 特征（每个都是一个可读/可写的值）
    SpanCharacteristic* active_;            // 开关
    SpanCharacteristic* currentTemp_;       // 当前室温
    SpanCharacteristic* currentState_;      // 当前运行状态
    SpanCharacteristic* targetState_;       // 目标模式
    SpanCharacteristic* coolingThreshold_;  // 制冷目标温度
    SpanCharacteristic* heatingThreshold_;  // 制热目标温度
    SpanCharacteristic* rotationSpeed_;     // 风速
    SpanCharacteristic* swingMode_;         // 摆风

    // Grace period：发完 HomeKit 命令后 3 秒内，不用空调回报的旧数据覆盖设置
    static const uint32_t GRACE_PERIOD_MS = 3000;

public:
    HKHeaterCooler(MitsubishiProtocol* proto) : Service::HeaterCooler() {
        proto_ = proto;

        // 创建所有特征
        // 第二个参数 true = 断电后记住值（NVS 持久化）
        active_           = new Characteristic::Active(0, true);
        currentTemp_      = new Characteristic::CurrentTemperature(20);
        currentState_     = new Characteristic::CurrentHeaterCoolerState(0);
        targetState_      = new Characteristic::TargetHeaterCoolerState(0, true);
        coolingThreshold_ = new Characteristic::CoolingThresholdTemperature(26, true);
        heatingThreshold_ = new Characteristic::HeatingThresholdTemperature(20, true);
        rotationSpeed_    = new Characteristic::RotationSpeed(0, true);
        swingMode_        = new Characteristic::SwingMode(0, true);

        // 设置温度范围：三菱支持 16-31°C，步进 0.5°C
        coolingThreshold_->setRange(16, 31, 0.5);
        heatingThreshold_->setRange(16, 31, 0.5);

        // 限制目标模式：只有 AUTO(0), HEAT(1), COOL(2)
        targetState_->setValidValues(3, 0, 1, 2);
    }

    // ============================================================
    // update() — 用户在 iPhone 上改了设置
    // ============================================================
    boolean update() override {

        bool needSend = false;

        // --- 电源 ---
        if (active_->updated()) {
            if (active_->getNewVal() == 0) {
                proto_->setPower("OFF");
            } else {
                proto_->setPower("ON");
            }
            needSend = true;
        }

        // --- 模式 ---
        if (targetState_->updated()) {
            proto_->setPower("ON"); // 切模式时自动开机
            switch (targetState_->getNewVal()) {
                case 0: proto_->setMode("AUTO"); break;
                case 1: proto_->setMode("HEAT"); break;
                case 2: proto_->setMode("COOL"); break;
            }
            needSend = true;
        }

        // --- 温度 ---
        // HomeKit 的 HeaterCooler 有两个温度特征
        // 制冷模式用 CoolingThreshold，制热模式用 HeatingThreshold
        // 我们把两个都映射到空调的同一个温度设定
        if (coolingThreshold_->updated()) {
            proto_->setTemperature(coolingThreshold_->getNewVal<float>());
            needSend = true;
        }
        if (heatingThreshold_->updated()) {
            proto_->setTemperature(heatingThreshold_->getNewVal<float>());
            needSend = true;
        }

        // --- 风速 ---
        if (rotationSpeed_->updated()) {
            float pct = rotationSpeed_->getNewVal<float>();
            proto_->setFan(percentToFan(pct));
            needSend = true;
        }

        // --- 摆风 ---
        if (swingMode_->updated()) {
            int sw = swingMode_->getNewVal();
            proto_->setVane(sw ? "SWING" : "AUTO");
            needSend = true;
        }

        // 发送到空调
        if (needSend) {
            proto_->commitSettings();
        }

        return true;
    }

    // ============================================================
    // loop() — 每次 homeSpan.poll() 都会调用
    // ============================================================
    void loop() override {

        // ① 处理 UART 输入（始终执行，不管定时器）
        proto_->processInput();

        // ② 管理轮询周期
        proto_->loopPollCycle();

        // ③ 如果有新的设置数据，同步到 HomeKit
        if (proto_->hasNewSettings()) {
            bool inGrace = (millis() - proto_->getLastCommandMs()) < GRACE_PERIOD_MS;
            syncSettingsToHomeKit(inGrace);
        }

        // ④ 如果有新的状态数据，同步到 HomeKit
        if (proto_->hasNewStatus()) {
            syncStatusToHomeKit();
        }
    }

private:

    // ============================================================
    // 同步设置到 HomeKit
    // ============================================================
    void syncSettingsToHomeKit(bool skipSettings) {
        auto& s = proto_->getCurrentSettings();

        // 电源始终同步
        int pwr = (s.power && strcmp(s.power, "ON") == 0) ? 1 : 0;
        if (pwr != active_->getVal()) {
            active_->setVal(pwr);
        }

        if (skipSettings) return; // Grace period 内不同步其他设置

        // --- 模式（不覆写策略！）---
        // 如果空调在 DRY 或 FAN 模式（遥控器设的），我们报告为 AUTO
        // 但我们不会发 SET 命令给空调，所以不会覆写
        // setVal() 不会触发 update()，所以不会形成反馈循环
        int hkTarget;
        if (s.mode && strcmp(s.mode, "HEAT") == 0)      hkTarget = 1;
        else if (s.mode && strcmp(s.mode, "COOL") == 0) hkTarget = 2;
        else                                             hkTarget = 0; // AUTO/DRY/FAN → AUTO
        if (hkTarget != targetState_->getVal()) {
            targetState_->setVal(hkTarget);
        }

        // --- 温度 ---
        if (s.temperature > 0 && s.temperature != coolingThreshold_->getVal<float>()) {
            coolingThreshold_->setVal(s.temperature);
            heatingThreshold_->setVal(s.temperature);
        }

        // --- 风速 ---
        float fp = fanToPercent(s.fan);
        if (fp != rotationSpeed_->getVal<float>()) {
            rotationSpeed_->setVal(fp);
        }

        // --- 摆风 ---
        int sw = (s.vane && strcmp(s.vane, "SWING") == 0) ? 1 : 0;
        if (sw != swingMode_->getVal()) {
            swingMode_->setVal(sw);
        }
    }

    // ============================================================
    // 同步状态到 HomeKit
    // ============================================================
    void syncStatusToHomeKit() {
        auto& st = proto_->getCurrentStatus();
        auto& s = proto_->getCurrentSettings();

        // 室温
        if (!isnan(st.roomTemperature) &&
            st.roomTemperature != currentTemp_->getVal<float>()) {
            currentTemp_->setVal(st.roomTemperature);
        }

        // 运行状态
        int cs = calcCurrentState(s, st);
        if (cs != currentState_->getVal()) {
            currentState_->setVal(cs);
        }
    }

    // ============================================================
    // 计算当前运行状态
    // ============================================================
    // HomeKit CurrentHeaterCoolerState:
    //   0 = INACTIVE（关机）
    //   1 = IDLE（开机但不运行）
    //   2 = HEATING（正在制热）
    //   3 = COOLING（正在制冷）
    int calcCurrentState(const heatpumpSettings& s, const heatpumpStatus& st) {
        if (!s.power || strcmp(s.power, "OFF") == 0) return 0; // INACTIVE
        if (!st.operating) return 1; // IDLE

        if (s.mode && strcmp(s.mode, "HEAT") == 0) return 2; // HEATING
        if (s.mode && (strcmp(s.mode, "COOL") == 0 || strcmp(s.mode, "DRY") == 0)) return 3; // COOLING
        if (s.mode && strcmp(s.mode, "AUTO") == 0) {
            // AUTO 模式：根据室温和设定温度猜测
            if (!isnan(st.roomTemperature) && s.temperature > 0) {
                return (st.roomTemperature < s.temperature) ? 2 : 3;
            }
        }
        return 1; // FAN 或未知 → IDLE
    }
};
```

---

## TASK 8: 创建 WebUI.h

Web 界面，运行在端口 8080。显示空调状态和 HomeKit 配对码。

把下面的代码完整复制到 `src/WebUI.h`：

```cpp
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
        : server_(port), proto_(proto), pairingCode_(pairingCode)
    {
        bootTime_ = millis();
    }

    void begin() {
        server_.on("/", HTTP_GET, [this]() { handleRoot(); });
        server_.on("/api/status", HTTP_GET, [this]() { handleApi(); });
        server_.begin();
        Serial.printf("[WebUI] Started on port 8080\n");
    }

    // 必须在 loop() 中调用
    void loop() {
        server_.handleClient();
    }

private:
    void handleRoot() {
        auto& s = proto_->getCurrentSettings();
        auto& st = proto_->getCurrentStatus();

        // 格式化配对码为 XXX-YY-ZZZ
        String code = String(pairingCode_);
        String fmtCode = code.substring(0,3) + "-" + code.substring(3,5) + "-" + code.substring(5,8);

        // 运行时间
        uint32_t sec = (millis() - bootTime_) / 1000;
        char uptime[32];
        snprintf(uptime, sizeof(uptime), "%ud %02u:%02u:%02u",
            (unsigned)(sec/86400), (unsigned)(sec%86400/3600),
            (unsigned)(sec%3600/60), (unsigned)(sec%60));

        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>CN105 AC</title>"
            "<style>"
            "body{font-family:-apple-system,sans-serif;background:#0f0f23;color:#ccc;padding:16px;}"
            "h1{color:#fff;font-size:1.5em;}"
            ".card{background:#1a1a3e;border-radius:12px;padding:16px;margin:12px 0;}"
            ".pairing{background:#1e3a5f;border:2px dashed #4a90d9;text-align:center;}"
            ".code{font-size:2.5em;font-family:monospace;color:#fff;letter-spacing:0.15em;margin:8px 0;}"
            ".row{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #2a2a4e;}"
            ".row:last-child{border:none;}"
            ".key{color:#888;}.val{color:#fff;font-weight:500;}"
            "</style></head><body>";

        html += "<h1>&#x2744; Mitsubishi AC</h1>";

        // 配对码
        html += "<div class='card pairing'>";
        html += "<div style='color:#8ab4f8;font-size:0.9em;'>HomeKit Pairing Code</div>";
        html += "<div class='code'>" + fmtCode + "</div>";
        html += "</div>";

        // 状态
        html += "<div class='card'>";
        html += row("Power", s.power ? s.power : "--");
        html += row("Mode", s.mode ? s.mode : "--");
        html += row("Target Temp", s.temperature > 0 ? String(s.temperature, 1) + " C" : "--");
        html += row("Room Temp", !isnan(st.roomTemperature) ? String(st.roomTemperature, 1) + " C" : "--");
        html += row("Outside Temp", !isnan(st.outsideAirTemperature) ? String(st.outsideAirTemperature, 1) + " C" : "--");
        html += row("Fan", s.fan ? s.fan : "--");
        html += row("Vane", s.vane ? s.vane : "--");
        html += row("Compressor", st.operating ? "Running" : "Standby");
        html += row("Compressor Hz", String(st.compressorFrequency, 0));
        html += row("Power (W)", String(st.inputPower, 0));
        html += row("Energy (kWh)", String(st.kWh, 1));
        html += row("Connected", proto_->isConnected() ? "Yes" : "No");
        html += row("Uptime", String(uptime));
        html += "</div>";

        html += "<script>setTimeout(()=>location.reload(),5000);</script>";
        html += "</body></html>";

        server_.send(200, "text/html; charset=utf-8", html);
    }

    void handleApi() {
        auto& s = proto_->getCurrentSettings();
        auto& st = proto_->getCurrentStatus();

        String json = "{";
        json += "\"power\":\"" + String(s.power ? s.power : "") + "\",";
        json += "\"mode\":\"" + String(s.mode ? s.mode : "") + "\",";
        json += "\"temperature\":" + String(s.temperature, 1) + ",";
        json += "\"roomTemperature\":" + String(st.roomTemperature, 1) + ",";
        json += "\"operating\":" + String(st.operating ? "true" : "false") + ",";
        json += "\"connected\":" + String(proto_->isConnected() ? "true" : "false");
        json += "}";

        server_.send(200, "application/json", json);
    }

    String row(const char* key, String val) {
        return "<div class='row'><span class='key'>" + String(key) +
               "</span><span class='val'>" + val + "</span></div>";
    }
};
```
## TASK 9: 创建主程序 MitsubishiCN105HomeKit.ino

这是 Arduino 主程序，把所有东西连接起来。

把下面的代码完整复制到 `MitsubishiCN105HomeKit.ino`：

```cpp
// ============================================================
// MitsubishiCN105HomeKit.ino
// 三菱空调 CN105 → Apple HomeKit 桥接器
// ============================================================
#include "HomeSpan.h"
#include "src/MitsubishiProtocol.h"
#include "src/HKHeaterCooler.h"
#include "src/WebUI.h"

// ============================================================
// 配置区域 —— 根据你的硬件修改这里
// ============================================================
#define CN105_TX_PIN    17       // ESP32 TX → CN105 Pin5 (RX)
#define CN105_RX_PIN    16       // ESP32 RX → CN105 Pin4 (TX)
#define STATUS_LED      2        // 状态 LED（ESP32 DevKit 内置 LED）
#define PAIRING_CODE    "46637726"  // HomeKit 配对码（8位数字）
#define DEVICE_NAME     "Mitsubishi AC"

// ============================================================
// 全局对象
// ============================================================
HardwareSerial CN105Serial(1);    // 使用 UART1
MitsubishiProtocol* g_proto = nullptr;
WebUI* g_webUI = nullptr;

void setup() {
    // ① 初始化调试串口
    Serial.begin(115200);
    Serial.println("\n=== Mitsubishi CN105 HomeKit Bridge ===");

    // ② 初始化 CN105 串口
    // 关键参数：2400 波特率, 8 数据位, 偶校验, 1 停止位
    CN105Serial.begin(2400, SERIAL_8E1, CN105_RX_PIN, CN105_TX_PIN);
    Serial.println("[UART] CN105 serial initialized: 2400 8E1");

    // ③ 创建协议引擎
    g_proto = new MitsubishiProtocol(&CN105Serial);

    // ④ 配置 HomeSpan
    homeSpan.setStatusPin(STATUS_LED);
    homeSpan.setPairingCode(PAIRING_CODE);
    homeSpan.setQRID("MIAC");
    homeSpan.enableOTA();
    homeSpan.setLogLevel(1);
    homeSpan.enableWebLog(50, "pool.ntp.org", "CST-8", "status");

    // ⑤ 初始化 HomeSpan
    homeSpan.begin(Category::AirConditioners, DEVICE_NAME);

    // ⑥ 定义 HomeKit 配件树
    //    第一个 SpanAccessory 是桥接器信息（必须）
    new SpanAccessory();
      new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name("CN105 Bridge");
        new Characteristic::Manufacturer("DIY");
        new Characteristic::Model("CN105-HomeKit");
        new Characteristic::FirmwareRevision("1.0.0");

    //    第二个 SpanAccessory 是空调
    new SpanAccessory();
      new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name(DEVICE_NAME);
      new HKHeaterCooler(g_proto);

    // ⑦ 启动 Web UI（端口 8080）
    g_webUI = new WebUI(8080, g_proto, PAIRING_CODE);
    g_webUI->begin();

    // ⑧ 连接空调
    g_proto->connect();

    Serial.println("[SETUP] Complete! Waiting for HomeKit pairing...");
    Serial.printf("[SETUP] Pairing code: %s\n", PAIRING_CODE);
    Serial.println("[SETUP] Web UI: http://<ip>:8080/");
}

void loop() {
    homeSpan.poll();    // 驱动 HomeKit（会自动调用 HKHeaterCooler::loop()）
    g_webUI->loop();    // 驱动 Web UI
}
```

---

## TASK 10: 理解"不覆写"策略（最重要的设计决策！）

### 10.1 问题

HomeKit 的 HeaterCooler 只支持 3 种模式：AUTO, HEAT, COOL。
三菱空调有 5 种模式：AUTO, HEAT, COOL, DRY, FAN。

如果用户通过遥控器把空调设为"除湿"(DRY)模式，ESP32 该怎么办？

### 10.2 错误做法（会覆写用户设置）

```
1. 空调在 DRY 模式（遥控器设的）
2. ESP32 轮询到 DRY 模式
3. ESP32 不认识 DRY → 映射为 AUTO
4. ESP32 发 SET 命令把空调改为 AUTO  ← 这就覆写了！
```

### 10.3 正确做法（我们的策略）

```
1. 空调在 DRY 模式（遥控器设的）
2. ESP32 轮询到 DRY 模式
3. ESP32 用 setVal(0) 告诉 HomeKit"当前是 AUTO"
4. setVal() 不会触发 update()
5. ESP32 不发任何 SET 命令给空调
6. 空调继续在 DRY 模式运行 ← 没有被覆写！
```

### 10.4 为什么 setVal() 不会覆写？

```
setVal()  →  只是更新 HomeKit 显示  →  不触发 update()
update()  →  只在用户主动操作时触发  →  才会发 SET 命令

所以：
  setVal(0)  →  HomeKit 显示 AUTO  →  不会发 SET 命令  →  空调不受影响
```

### 10.5 代码中体现在哪里？

在 `HKHeaterCooler.h` 的 `syncSettingsToHomeKit()` 方法中：

```cpp
// 这行只是"报告"给 HomeKit，不会触发 update()
// 所以不会发 SET 命令给空调
if (hkTarget != targetState_->getVal()) {
    targetState_->setVal(hkTarget);  // ← 只是显示，不是命令
}
```

在 `update()` 方法中：

```cpp
// 只有用户在 iPhone 上主动操作时才会执行到这里
if (targetState_->updated()) {
    // 这里才会真正发 SET 命令给空调
    proto_->setMode("HEAT");
    proto_->commitSettings();  // ← 这才是真正的命令
}
```

---

## TASK 11: 理解 2 秒同步机制

### 11.1 为什么需要轮询？

空调**不会主动推送**状态变化。如果有人按了遥控器，ESP32 不知道。
所以 ESP32 必须每 2 秒主动问一次空调"你现在什么状态？"

### 11.2 一轮轮询包含什么？

```
时刻 0ms:     发送 INFO 0x02 (请求设置)
时刻 ~50ms:   收到回复，解析设置
时刻 ~50ms:   发送 INFO 0x03 (请求温度)
时刻 ~100ms:  收到回复，解析温度
时刻 ~100ms:  发送 INFO 0x06 (请求状态)
时刻 ~150ms:  收到回复，解析状态
时刻 ~150ms:  发送 INFO 0x09 (请求阶段)
时刻 ~200ms:  收到回复，解析阶段
时刻 ~200ms:  这一轮结束
时刻 2000ms:  开始下一轮
```

### 11.3 Grace Period（保护窗口）

当用户通过 HomeKit 改了温度后，空调需要一点时间来处理。
在这 3 秒内，如果 ESP32 轮询到了"旧温度"，不应该用旧值覆盖 HomeKit 显示。

```
t=0s:     用户在 iPhone 上把温度从 25 改为 22
t=0s:     ESP32 发 SET 命令给空调
t=0s:     ESP32 记住 lastCommandMs_ = millis()
t=1s:     ESP32 轮询到温度=25（空调还没更新）
t=1s:     但是 millis() - lastCommandMs_ < 3000
t=1s:     所以跳过设置同步（skipSettings = true）
t=3s:     ESP32 轮询到温度=22（空调已更新）
t=3s:     Grace period 过了，正常同步
```

---

## TASK 12: 编译和烧录

### 12.1 使用 PlatformIO

```bash
# 编译
pio run

# 烧录
pio run --target upload

# 查看日志
pio device monitor
```

### 12.2 使用 Arduino IDE

1. 安装 ESP32 板支持
2. 安装 HomeSpan 库（库管理器搜索 "HomeSpan"）
3. 选择板子：ESP32 Dev Module
4. 打开 `MitsubishiCN105HomeKit.ino`
5. 编译上传

### 12.3 首次配对

1. 烧录后，ESP32 会创建一个 WiFi AP（名称包含 "HomeSpan"）
2. 用手机连接这个 AP，配置 WiFi
3. 配置完成后，打开 iPhone 的 Home App
4. 点击 "+" → "添加配件"
5. 输入配对码：466-37-726
6. 完成！

---

## TASK 13: 检查清单

在提交代码之前，确认以下所有要点：

- [ ] UART 参数是 2400, 8E1（不是 8N1！）
- [ ] 校验和算法是 `(0xFC - sum) & 0xFF`
- [ ] SET 包的 byte[6] 是控制标志位掩码
- [ ] 解析器正确处理 0xFC 起始字节
- [ ] 高精度温度 = `(byte - 128) / 2.0`
- [ ] iSee 检测：`data[4] > 0x08` 时减 0x08
- [ ] DRY/FAN 模式映射为 HomeKit AUTO，不发 SET 命令
- [ ] setVal() 在 loop() 中调用，getNewVal() 在 update() 中调用
- [ ] Grace period 3 秒内不同步设置
- [ ] 风速档位没有 0x04（0x03 跳到 0x05）
- [ ] 垂直导风板没有 0x06（0x05 跳到 0x07）

---

## 附录 A: 完整项目文件清单

| 文件 | TASK | 说明 |
|------|------|------|
| `platformio.ini` | TASK 2 | PlatformIO 配置 |
| `src/MitsubishiTypes.h` | TASK 3 | 协议常量和数据结构 |
| `src/MitsubishiProtocol.h` | TASK 5 | 协议引擎（整个文件就是实现，不需要 .cpp） |
| `src/HKHeaterCooler.h` | TASK 7 | HomeKit 服务 |
| `src/WebUI.h` | TASK 8 | Web 界面 |
| `MitsubishiCN105HomeKit.ino` | TASK 9 | 主程序 |

## 附录 B: 关键字节速查表

```
数据包起始: 0xFC
SET 命令:   0x41
INFO 请求:  0x42
连接握手:   0x5A (标准) / 0x5B (安装工)
SET 确认:   0x61
INFO 回复:  0x62
连接成功:   0x7A (标准) / 0x7B (安装工)

INFO 请求码:
  0x02 = 设置（电源/模式/温度/风速/导风板）
  0x03 = 温度（室温/室外温度/运行时间）
  0x06 = 状态（压缩机/功率/能耗）
  0x09 = 阶段（stage/sub_mode）

SET 控制标志 byte[6]:
  0x01 = 电源
  0x02 = 模式
  0x04 = 温度
  0x08 = 风速
  0x10 = 垂直导风板

SET 控制标志 byte[7]:
  0x01 = 水平导风板

电源: 0x00=OFF, 0x01=ON
模式: 0x01=HEAT, 0x02=DRY, 0x03=COOL, 0x07=FAN, 0x08=AUTO
风速: 0x00=AUTO, 0x01=QUIET, 0x02=1, 0x03=2, 0x05=3, 0x06=4
导风板: 0x00=AUTO, 0x01-0x05=位置, 0x07=SWING
```
