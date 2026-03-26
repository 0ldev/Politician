# Politician

> **A sophisticated WiFi auditing library for ESP32 microcontrollers**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-blue.svg)](https://platformio.org/)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue)](https://0ldev.github.io/Politician/)

Politician is an embedded C++ library designed for WiFi security auditing on ESP32 platforms. It provides a clean, modern API for capturing WPA/WPA2/WPA3 handshakes and harvesting enterprise credentials using advanced 802.11 protocol techniques.

**📚 [Full API Documentation](https://0ldev.github.io/Politician/)**

## Key Capabilities

- **PMKID Capture**: Extract PMKIDs from association responses without client disconnection
- **CSA (Channel Switch Announcement) Injection**: Modern alternative to deauthentication attacks
- **Enterprise Credential Harvesting**: Capture EAP-Identity frames from 802.1X networks  
- **Hidden Network Discovery**: Automatic SSID decloaking via probe response interception
- **Client Stimulation**: Wake sleeping mobile devices using QoS Null Data frames
- **WPA3/PMF Detection**: Intelligent filtering to skip Protected Management Frame-enabled networks
- **Export Formats**: PCAPNG and Hashcat HC22000 output support

## Architecture

The library is built around a non-blocking state machine that manages channel hopping, target selection, attack execution, and capture processing. All operations are contained within the `politician` namespace.

### Core Components

| Component | Description |
|-----------|-------------|
| `Politician` | Main engine class managing the audit lifecycle |
| `PoliticianFormat` | PCAPNG and Hashcat export utilities |
| `PoliticianStorage` | Optional SD card logging and NVS persistence |
| `PoliticianTypes` | Core data structures and enumerations |

### Attack Modes

Traditional deauthentication attacks are ineffective against modern WPA3 and WPA2 networks with Protected Management Frames (PMF/802.11w). Politician implements modern alternatives:

| Mode | Description | Effectiveness |
|------|-------------|---------------|
| `ATTACK_PMKID` | Extract PMKID via dummy authentication | Works on all WPA2/WPA3-Transition |
| `ATTACK_CSA` | Channel Switch Announcement injection | Bypasses PMF protections |
| `ATTACK_DEAUTH` | Legacy deauthentication (Reason 7) | WPA2 without PMF only |
| `ATTACK_STIMULATE` | QoS Null Data for sleeping clients | Non-intrusive client wake-up |
| `ATTACK_PASSIVE` | Listen-only mode | Zero transmission |
| `ATTACK_ALL` | Enable all active attack vectors | Maximum aggression |

## Installation

### PlatformIO

Add to your `platformio.ini`:

```ini
[env:myboard]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
    Politician
```

Or clone directly into your project's `lib/` directory:

```bash
cd lib/
git clone https://github.com/0ldev/Politician.git
```

### Arduino IDE

1. Download the library as a ZIP file
2. In Arduino IDE: **Sketch** → **Include Library** → **Add .ZIP Library**
3. Select the downloaded ZIP file

## Quick Start

### Basic Handshake Capture

```cpp
#include <Arduino.h>
#include <Politician.h>
#include <PoliticianFormat.h>

using namespace politician;
using namespace politician::format;

Politician engine;

void onHandshake(const HandshakeRecord &rec) {
    Serial.printf("\n[✓] Captured handshake: %s\n", rec.ssid);
    Serial.printf("HC22000: %s\n", toHC22000(rec).c_str());
}

void setup() {
    Serial.begin(115200);
    
    engine.setEapolCallback(onHandshake);
    
    Config cfg;
    cfg.capture_filter = LOG_FILTER_HANDSHAKES | LOG_FILTER_PROBES;
    engine.begin(cfg);
    
    engine.setAttackMask(ATTACK_ALL);
}

void loop() {
    engine.tick();
}
```

## API Reference

### Politician Class

The main engine class. Must call `tick()` in your main loop.

#### Initialization

```cpp
void begin(const Config& cfg = Config());
```

Initialize the engine with configuration options.

#### Configuration Structure

```cpp
struct Config {
    uint16_t hop_dwell_ms;          // Time spent on each channel (default: 300ms)
    uint8_t  hop_interval;          // Channels between hops (default: 1)
    uint8_t  attack_retries;        // Attack attempts per target (default: 3)
    uint16_t pmkid_timeout_ms;      // PMKID wait time (default: 500ms)
    bool     capture_half_handshakes; // Save M2-only frames (default: true)
    uint8_t  capture_filter;        // Packet logging bitmask (default: 0)
};
```

#### Callbacks

```cpp
void setEapolCallback(EapolCb callback);        // Handshake capture events
void setIdentityCallback(IdentityCb callback);  // EAP-Identity events  
void setApCallback(ApCb callback);              // AP discovery events
void setTargetFilter(TargetFilterCb filter);    // Network selection filter
void setPacketLogger(PacketCb logger);          // Raw packet logging
```

#### Attack Control

```cpp
void setAttackMask(uint8_t mask);  // Configure attack modes (bitmask)
void addIgnoreBssid(const uint8_t* bssid);  // Skip specific networks
```

#### Attack Mode Constants

```cpp
#define ATTACK_PASSIVE      0x00  // Listen only
#define ATTACK_PMKID        0x01  // PMKID extraction
#define ATTACK_CSA          0x02  // Channel Switch Announcement
#define ATTACK_DEAUTH       0x04  // Deauthentication frames
#define ATTACK_STIMULATE    0x08  // QoS Null Data stimulation
#define ATTACK_ALL          0xFF  // All attack vectors
```

#### Capture Filter Constants

```cpp
#define LOG_FILTER_NONE       0x00
#define LOG_FILTER_HANDSHAKES 0x01
#define LOG_FILTER_PROBES     0x02
#define LOG_FILTER_BEACONS    0x04
#define LOG_FILTER_ALL        0xFF
```

### Data Structures

#### HandshakeRecord

```cpp
struct HandshakeRecord {
    uint8_t  bssid[6];
    uint8_t  client[6];
    char     ssid[33];
    uint8_t  enc;           // Encryption type
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  eapol_m1[256]; // EAPOL Message 1
    uint8_t  eapol_m2[256]; // EAPOL Message 2
    uint16_t m1_len;
    uint16_t m2_len;
    bool     is_complete;   // false = M2-only
};
```

#### IdentityRecord

```cpp
struct IdentityRecord {
    uint8_t bssid[6];
    uint8_t client[6];
    char    ssid[33];
    char    identity[128];  // Plaintext username/email
    int8_t  rssi;
};
```

#### ApRecord

```cpp
struct ApRecord {
    uint8_t bssid[6];
    char    ssid[33];
    uint8_t enc;       // 0=Open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3
    uint8_t channel;
    int8_t  rssi;
    bool    pmf_required;
};
```

### Format Utilities

```cpp
String toHC22000(const HandshakeRecord& rec);  // Hashcat format
String toPcapng(const HandshakeRecord& rec);   // PCAPNG format
```

### Storage Utilities (Optional)

Requires `#include <PoliticianStorage.h>`.

```cpp
// Append handshake to PCAPNG file
PcapngFileLogger::append(fs::FS& fs, const char* path, 
                        const HandshakeRecord& rec);

// Append packet to PCAPNG file  
PcapngFileLogger::appendPacket(fs::FS& fs, const char* path,
                              const uint8_t* payload, uint16_t len,
                              int8_t rssi, uint32_t timestamp);

// Append to Wigle CSV
WigleCsvLogger::append(fs::FS& fs, const char* path,
                      const ApRecord& ap, float lat, float lon);

// Append enterprise identity to CSV
EnterpriseCsvLogger::append(fs::FS& fs, const char* path,
                           const IdentityRecord& rec);
```

## Usage Examples

### Targeted Network Auditing

Use callbacks to filter networks by signal strength, encryption type, or SSID pattern:

```cpp
engine.setTargetFilter([](const politician::ApRecord &ap) {
    // Only audit strong signals
    if (ap.rssi < -70) return false;
    
    // Skip Open/WEP networks
    if (ap.enc < 3) return false;
    
    // Skip corporate networks  
    if (strstr(ap.ssid, "CORP-") != nullptr) return false;
    
    return true;
});
```

### Selective Attack Modes

```cpp
// Modern CSA-only (bypasses PMF)
engine.setAttackMask(ATTACK_CSA);

// Classic deauth for legacy networks
engine.setAttackMask(ATTACK_DEAUTH);

// Passive monitoring with client stimulation
engine.setAttackMask(ATTACK_PASSIVE | ATTACK_STIMULATE);

// Full aggression
engine.setAttackMask(ATTACK_ALL);
```

### Custom Channel Selection (Optional)

By default, the library hops through all standard channels. You can optionally restrict to specific channels:

```cpp
// Scan only primary 2.4GHz channels for faster hopping
const uint8_t channels_24[] = {1, 6, 11};
engine.setChannelList(channels_24, 3);

// On ESP32-C6: Scan only 5GHz channels (common non-DFS)
const uint8_t channels_5ghz[] = {36, 40, 44, 48, 149, 153, 157, 161, 165};
engine.setChannelList(channels_5ghz, 9);

// On ESP32-C6: Mix 2.4GHz and 5GHz for dual-band coverage
const uint8_t channels_dual[] = {1, 6, 11, 36, 40, 44, 149, 153, 157};
engine.setChannelList(channels_dual, 9);

// Clear custom list to restore default (all channels)
engine.setChannelList(nullptr, 0);
```

### Enterprise Credential Harvesting

```cpp
void onIdentity(const IdentityRecord &rec) {
    Serial.printf("[802.1X] %s → %s\n", rec.ssid, rec.identity);
    // Save to CSV for analysis
    EnterpriseCsvLogger::append(SD, "/identities.csv", rec);
}

void setup() {
    engine.setIdentityCallback(onIdentity);
    
    Config cfg;
    cfg.hop_dwell_ms = 800;  // Longer dwell for EAP exchanges
    engine.begin(cfg);
}
```

### Persistent Storage

The core library is decoupled from filesystem dependencies. Optionally include `PoliticianStorage.h` for SD card logging:

```cpp
#include <PoliticianStorage.h>
#include <SD.h>

using namespace politician::storage;

void onHandshake(const HandshakeRecord &rec) {
    // Append to PCAPNG file (creates headers automatically)
    PcapngFileLogger::append(SD, "/captures.pcapng", rec);
}

void onPacket(const uint8_t* payload, uint16_t len, int8_t rssi, uint32_t ts) {
    // Log raw 802.11 frames
    PcapngFileLogger::appendPacket(SD, "/intel.pcapng", payload, len, rssi, ts);
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

**⚠️ Logging Performance Warning**

Beacon logging (`LOG_FILTER_BEACONS`) can generate 500+ writes/second. Standard SPI SD card writes are **blocking** and will freeze the engine. For high-volume logging, use ESP32 boards with native **SDMMC** (4-bit) hardware support and DMA.

### GPS Integration (Wigle.net)

Combine with a GPS module for wardriving datasets:

```cpp
#include <TinyGPS++.h>

TinyGPSPlus gps;

void onAp(const ApRecord &ap) {
    if (gps.location.isValid()) {
        WigleCsvLogger::append(SD, "/wardrive.csv", ap, 
                              gps.location.lat(), 
                              gps.location.lng());
    }
}
```

## Advanced Features

### Half-Handshakes and Smart Pivot

When `cfg.capture_half_handshakes = true`, the engine saves M2-only frames (incomplete handshakes). These can still be cracked by modern tools like Hashcat.

When an M2-only handshake is captured, the engine automatically executes a **Smart Pivot**:
1. Marks the network as a "Hot Target" with active clients
2. Immediately launches CSA/Deauth attacks on the client
3. Captures the complete 4-way handshake on reconnection

### Hidden Network Discovery

Probe Response frames triggered by deauth bursts automatically reveal hidden SSIDs. The engine caches these with zero configuration required.

### PMF/WPA3 Detection

RSNE (Robust Security Network Element) parsing automatically identifies networks with PMF Required. These are skipped to save time, but WPA3 Transition Mode networks (PMF Capable but not Required) are still targeted.

## Examples

The library includes complete examples demonstrating various use cases:

| Example | Description |
|---------|-------------|
| `ExportFormats` | HC22000 and PCAPNG format conversion |
| `TargetedAuditing` | Network filtering with callbacks |
| `EnterpriseAuditing` | 802.1X identity harvesting |
| `StorageAndNVS` | SD card PCAPNG logging and NVS persistence |
| `WigleIntegration` | GPS wardriving with Wigle CSV export |
| `DynamicControl` | Runtime attack mode switching |
| `AutoEnterpriseHunter` | Automatic enterprise network targeting |
| `SerialStreaming` | Real-time packet streaming over USB |
| `StressTest` | Performance and memory testing |

See the [`examples/`](examples/) directory for complete source code.

## Documentation

Full API documentation is automatically generated and published to **[GitHub Pages](https://0ldev.github.io/Politician/)**.

The documentation includes:
- Complete API reference for all classes and methods
- Data structure specifications
- Usage examples and code snippets
- Architecture overview

To generate documentation locally:

```bash
doxygen Doxyfile
```

Then open `docs/html/index.html` in your browser.


## Hardware Requirements

- **Platform**: ESP32, ESP32-S2, ESP32-S3, ESP32-C3 (2.4GHz), ESP32-C6 (2.4GHz + 5GHz)
- **Framework**: Arduino or ESP-IDF
- **Memory**: Minimum 4MB flash recommended
- **WiFi Bands**: 
  - All ESP32 variants: 2.4GHz (channels 1-14)
  - ESP32-C6: Also supports 5GHz (channels 36-165)
- **Optional**: SD card module for persistent logging
- **Optional**: GPS module for Wigle integration

### 5GHz Support

On ESP32-C6, the library automatically supports 5GHz bands - no code changes required. All capture techniques (PMKID, handshakes, enterprise identities) work identically on both 2.4GHz and 5GHz.

**Note**: Not all 5GHz channels are legal worldwide. DFS channels (52-144) may have regulatory restrictions in your region.

## Performance Considerations

- **Channel Hopping**: Default 300ms dwell time balances discovery speed vs. capture reliability
- **Memory**: Core engine uses ~45KB RAM. Storage helpers are opt-in
- **CPU**: Non-blocking state machine keeps `loop()` responsive
- **Half-Handshakes**: Enable for better capture rate on fast-hopping scenarios

## Troubleshooting

**No handshakes captured:**
- Verify WiFi is enabled and promiscuous mode works
- Increase `hop_dwell_ms` for slow-reconnecting devices
- Check if target networks use PMF Required (will be auto-skipped)
- Try `ATTACK_ALL` mask for maximum aggression

**SD card writes fail:**
- Ensure SD.begin() succeeds before logging
- Check file permissions and available space
- Disable `LOG_FILTER_BEACONS` if using SPI SD cards

**Enterprise identities not captured:**
- Increase `hop_dwell_ms` to 800-1200ms for EAP exchanges
- Use `ATTACK_PASSIVE` or `ATTACK_STIMULATE` only
- Aggressive attacks may interrupt EAP authentication

## Legal & Ethical Use

This library is intended for:
- ✅ Authorized penetration testing
- ✅ Security research in controlled environments  
- ✅ Educational purposes with permission
- ✅ Auditing your own networks

**Unauthorized access to networks you do not own or have permission to test is illegal** under laws such as the Computer Fraud and Abuse Act (CFAA) in the United States and similar legislation worldwide.

The authors and contributors assume no liability for misuse of this software.

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Add tests/examples for new features
4. Submit a pull request

## License

MIT License - see [`LICENSE`](LICENSE) for details.

## Acknowledgments

Special thanks to [justcallmekoko](https://github.com/justcallmekoko) for inspiring this project and the broader hardware hacking community through the [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder) project. Years of learning from Marauder's innovative approaches to WiFi security research have been invaluable.

Built on ESP32 WiFi driver capabilities and inspired by modern WiFi security research and responsible disclosure practices.
