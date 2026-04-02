# Politician

> **A sophisticated WiFi auditing library for ESP32 microcontrollers**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-blue.svg)](https://platformio.org/)

Politician is an embedded C++ library designed for WiFi security auditing on ESP32 platforms. It provides a clean, modern API for capturing WPA/WPA2/WPA3 handshakes and harvesting enterprise credentials using advanced 802.11 protocol techniques.

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
| `PoliticianStress` | Decoupled DoS/disruption payload delivery (opt-in) |
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
Error begin(const Config& cfg = Config());
```

Initialize the engine. Returns `OK` on success or an `Error` code on failure. Must be called before any other method.

#### Configuration Structure

```cpp
struct Config {
    uint16_t hop_dwell_ms           = 200;   // Time spent on each channel (ms)
    uint32_t m1_lock_ms             = 800;   // How long to stay on channel after seeing M1
    uint32_t fish_timeout_ms        = 2000;  // Timeout per PMKID association attempt
    uint8_t  fish_max_retries       = 2;     // PMKID retries before pivoting to CSA
    uint32_t csa_wait_ms            = 4000;  // Wait window after CSA/Deauth burst
    uint8_t  csa_beacon_count       = 8;     // Number of CSA beacons per burst
    uint8_t  deauth_burst_count     = 16;    // Frames per standalone deauth burst
    uint8_t  csa_deauth_count       = 15;    // Deauth frames appended after CSA burst
    uint16_t probe_aggr_interval_s  = 30;    // Seconds between re-attacking the same AP
    uint32_t session_timeout_ms     = 60000; // How long orphaned M1 sessions live in RAM
    bool     capture_half_handshakes = false; // Fire callback on M2-only captures and pivot to active attack
    bool     skip_immune_networks   = true;  // Skip pure WPA3 / PMF-Required networks
    uint8_t  capture_filter         = LOG_FILTER_HANDSHAKES | LOG_FILTER_PROBES;
    int8_t   min_rssi               = -100;  // Ignore APs weaker than this signal (dBm)
    uint32_t ap_expiry_ms           = 300000; // Evict APs not seen for this long (0 = never expire)
    bool     unicast_deauth         = true;  // Send deauth to known client MAC instead of broadcast
    uint32_t probe_hidden_interval_ms = 0;   // How often to probe hidden APs for SSID (0 = disabled, opt-in)
    uint8_t  deauth_reason          = 7;     // 802.11 reason code in deauth frames
    // ── Frame capture
    bool     capture_group_keys     = false; // Fire eapolCb(CAP_EAPOL_GROUP) on GTK rotation frames
    // ── Filtering
    uint8_t  min_beacon_count       = 0;     // Min times AP must be seen before attack/apFoundCb (0 = off)
    uint8_t  max_total_attempts     = 0;     // Permanently skip BSSID after N failed attacks (0 = unlimited)
    uint8_t  sta_filter[6]          = {};    // Only record EAPOL from this client MAC (zero = no filter)
    char     ssid_filter[33]        = {};    // Only cache APs matching this SSID (empty = no filter)
    bool     ssid_filter_exact      = true;  // True = exact match, false = substring match
    uint8_t  enc_filter_mask        = 0xFF;  // Bitmask of enc types to cache (bit N = enc type N, 0xFF = all)
    bool     require_active_clients = false; // Skip attack initiation if no active clients seen on AP
};
```

#### Callbacks

```cpp
void setEapolCallback(EapolCb cb);              // Handshake captured (EAPOL, PMKID, or group key)
void setApFoundCallback(ApFoundCb cb);          // New AP discovered (respects min_beacon_count)
void setIdentityCallback(IdentityCb cb);        // 802.1X EAP-Identity harvested
void setAttackResultCallback(AttackResultCb cb);// Attack exhausted without capturing
void setTargetFilter(TargetFilterCb cb);        // Early filter — return false to ignore AP
void setPacketLogger(PacketCb cb);              // Raw promiscuous-mode frames
void setProbeRequestCallback(ProbeRequestCb cb);// Probe request received (client device history)
void setDisruptCallback(DisruptCb cb);          // Deauth/Disassoc frame received
void setClientFoundCallback(ClientFoundCb cb);  // New client STA seen associated to an AP
```

#### State & Stats

```cpp
bool    isActive()    const;  // True if frame processing is enabled
bool    isAttacking() const;  // True if a PMKID/CSA attack is in progress
bool    hasTarget()   const;  // True if focused on a specific BSSID
uint8_t getChannel()  const;  // Current radio channel
int8_t  getLastRssi() const;  // RSSI of the last received frame
Stats&  getStats();           // Reference to frame counters (captures, failures, etc.)
Config& getConfig();          // Reference to the active config for runtime mutations
void    resetStats();         // Zero all counters
int     getApCount() const;   // Number of APs in the discovery cache
bool    getAp(int idx, ApRecord &out) const;                  // Read AP from cache by index
bool    getApByBssid(const uint8_t* bssid, ApRecord &out) const; // Look up AP by BSSID
```

#### Engine Control

```cpp
void setActive(bool active);  // Enable or disable frame processing without full teardown
void setLogger(LogCb cb);     // Redirect internal log output to a custom callback
```

#### Target & Channel Control

```cpp
Error setTarget(const uint8_t* bssid, uint8_t channel); // Focus on one BSSID
void  clearTarget();                                     // Resume autonomous operation
Error setChannel(uint8_t ch);                            // Tune to a specific channel
Error lockChannel(uint8_t ch);                           // Stop hopping, lock channel
void  startHopping(uint16_t dwellMs = 0);                // Start channel hopping
void  stopHopping();                                     // Stop hopping (attack state machine continues)
void  stop();                                            // Full teardown: abort attack, clear target, stop hopping, disable capture
void  setChannelList(const uint8_t* channels, uint8_t count); // Restrict hop sequence
void  setChannelBands(bool ghz24, bool ghz5);                // Hop 2.4GHz, 5GHz, or both
Error setTargetBySsid(const char* ssid);                     // Lock target by SSID (picks strongest match from cache)
void  setAutoTarget(bool enable);                            // Continuously auto-target strongest uncaptured AP
```

#### Captured List

```cpp
void markCaptured(const uint8_t* bssid);                       // Skip this BSSID forever
void clearCapturedList();                                       // Reset captured list
void setIgnoreList(const uint8_t (*bssids)[6], uint8_t count); // Permanent ignore list
```

#### Attack Control

```cpp
void setAttackMask(uint8_t mask);                                // Configure active attack vectors (bitmask)
void setAttackMaskForBssid(const uint8_t* bssid, uint8_t mask); // Per-BSSID override (up to 8 entries)
void clearAttackMaskOverrides();                                  // Remove all per-BSSID overrides
```

#### Attack Mode Constants

```cpp
#define ATTACK_PMKID        0x01  // PMKID fishing via fake association
#define ATTACK_CSA          0x02  // Channel Switch Announcement injection
#define ATTACK_PASSIVE      0x04  // Listen-only — zero transmission
#define ATTACK_DEAUTH       0x08  // Classic deauthentication (Reason 7)
#define ATTACK_STIMULATE    0x10  // QoS Null Data client stimulation
#define ATTACK_ALL          0x1F  // All attack vectors
```

#### Capture Type Constants

```cpp
#define CAP_PMKID        0x01  // PMKID extracted via fake association
#define CAP_EAPOL        0x02  // Full M1+M2 from passive capture
#define CAP_EAPOL_CSA    0x03  // Full M1+M2 triggered by CSA/Deauth
#define CAP_EAPOL_HALF   0x04  // M2-only (no anonce) — active attack pivot fired
#define CAP_EAPOL_GROUP  0x05  // Non-pairwise EAPOL-Key (GTK rotation)
```

#### Capture Filter Constants

```cpp
#define LOG_FILTER_HANDSHAKES   0x01  // EAPOLs and PMKIDs (SPI-safe)
#define LOG_FILTER_PROBES       0x02  // Probe requests and responses (SPI-safe)
#define LOG_FILTER_BEACONS      0x04  // Beacons — high volume, SDMMC only
#define LOG_FILTER_PROBE_REQ    0x08  // Probe requests as raw EPBs (SPI-safe)
#define LOG_FILTER_MGMT_DISRUPT 0x10  // Deauth/Disassoc frames as raw EPBs (SPI-safe)
#define LOG_FILTER_ALL          0xFF  // Everything — SDMMC only
```

### Data Structures

#### HandshakeRecord

```cpp
struct HandshakeRecord {
    uint8_t  type;           // CAP_PMKID / CAP_EAPOL / CAP_EAPOL_CSA / CAP_EAPOL_HALF / CAP_EAPOL_GROUP
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  bssid[6];
    uint8_t  sta[6];         // Client (station) MAC
    char     ssid[33];
    uint8_t  ssid_len;
    uint8_t  enc;            // 0=Open, 1=WEP, 2=WPA, 3=WPA2/WPA3, 4=Enterprise
    // PMKID path
    uint8_t  pmkid[16];
    // EAPOL path
    uint8_t  anonce[32];
    uint8_t  mic[16];
    uint8_t  eapol_m2[256];
    uint16_t eapol_m2_len;
    bool     has_mic;
    bool     has_anonce;
};
```

#### EapIdentityRecord

```cpp
struct EapIdentityRecord {
    uint8_t bssid[6];       // Access Point MAC
    uint8_t client[6];      // Enterprise client MAC
    char    identity[65];   // Plaintext identity / email
    uint8_t channel;
    int8_t  rssi;
};
```

#### ApRecord

```cpp
struct ApRecord {
    uint8_t bssid[6];
    char    ssid[33];
    uint8_t ssid_len;
    uint8_t channel;
    int8_t  rssi;
    uint8_t enc;           // 0=Open, 1=WEP, 2=WPA, 3=WPA2/WPA3, 4=Enterprise
    bool    wps_enabled;   // WPS IE detected in beacon/probe-response
    bool    pmf_capable;     // MFPC — AP supports Protected Management Frames
    bool    pmf_required;    // MFPR — AP mandates PMF (pure WPA3 / PMF-Required)
    uint8_t total_attempts;  // Failed attack attempts against this BSSID
    bool    captured;        // True if BSSID is on the captured or ignore list
};
```

#### AttackResultRecord

```cpp
enum AttackResult : uint8_t {
    RESULT_PMKID_EXHAUSTED = 1,  // All PMKID retries failed
    RESULT_CSA_EXPIRED     = 2,  // CSA/Deauth window closed, no EAPOL received
};

struct AttackResultRecord {
    uint8_t      bssid[6];
    char         ssid[33];
    uint8_t      ssid_len;
    AttackResult result;
};
```

### Format Utilities

```cpp
// Convert a HandshakeRecord to a Hashcat-compatible HC22000 string
String toHC22000(const HandshakeRecord& rec);

// Write PCAPNG global header (SHB + IDB) — call once at file start
size_t writePcapngGlobalHeader(uint8_t* buffer);

// Serialize a HandshakeRecord into PCAPNG Enhanced Packet Blocks
size_t writePcapngRecord(const HandshakeRecord& rec, uint8_t* buffer, size_t max_len);

// Serialize a raw 802.11 frame into a PCAPNG Enhanced Packet Block
size_t writePcapngPacket(const uint8_t* payload, size_t len,
                         int8_t rssi, uint64_t ts_usec,
                         uint8_t* buffer, size_t max_len);
```

### Stress Utilities (Opt-in)

Requires `#include <PoliticianStress.h>`. Not linked unless explicitly included.

```cpp
// Flood a WPA3 AP with SAE Commit frames to exhaust its anti-clogging token heap
stress::saeCommitFlood(const uint8_t* bssid, uint32_t count = 1000);

// Flood nearby APs with randomized Probe Requests to saturate association queues
stress::probeRequestFlood(uint32_t count = 1000);
```

### Storage Utilities (Optional)

Requires `#include <PoliticianStorage.h>`.

```cpp
// Append handshake to PCAPNG file (writes global header automatically)
PcapngFileLogger::append(fs::FS& fs, const char* path,
                         const HandshakeRecord& rec);

// Append raw 802.11 frame to PCAPNG file
PcapngFileLogger::appendPacket(fs::FS& fs, const char* path,
                               const uint8_t* payload, uint16_t len,
                               int8_t rssi, uint32_t ts_usec);

// Append handshake details to Wigle CSV
WigleCsvLogger::append(fs::FS& fs, const char* path,
                       const HandshakeRecord& rec, float lat, float lon,
                       float alt = 0.0, float acc = 10.0,
                       const char* timestamp = nullptr);  // e.g. "2024-06-01 14:30:00"

// Append any discovered AP to Wigle CSV (use with setApFoundCallback)
WigleCsvLogger::appendAp(fs::FS& fs, const char* path,
                         const ApRecord& ap, float lat, float lon,
                         float alt = 0.0, float acc = 10.0,
                         const char* timestamp = nullptr);

// Append handshake to HC22000 text file
Hc22000FileLogger::append(fs::FS& fs, const char* path,
                           const HandshakeRecord& rec);

// Append harvested enterprise identity to CSV
EnterpriseCsvLogger::append(fs::FS& fs, const char* path,
                            const EapIdentityRecord& rec);
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

### Enterprise Credential Harvesting

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

// Log every discovered AP (use appendAp for ApRecord)
void onAp(const ApRecord &ap) {
    if (gps.location.isValid()) {
        WigleCsvLogger::appendAp(SD, "/wardrive.csv", ap,
                                 gps.location.lat(),
                                 gps.location.lng());
    }
}

// Log captured handshakes with GPS context (use append for HandshakeRecord)
void onHandshake(const HandshakeRecord &rec) {
    if (gps.location.isValid()) {
        WigleCsvLogger::append(SD, "/wardrive.csv", rec,
                               gps.location.lat(),
                               gps.location.lng());
    }
}
```

## Advanced Features

### Half-Handshakes and Smart Pivot

When `cfg.capture_half_handshakes = true`, the engine fires the EAPOL callback with `type = CAP_EAPOL_HALF` on M2-only captures. These records have no `anonce` so they cannot be directly cracked, but they confirm an active client is present.

The engine immediately executes a **Smart Pivot**:
1. Marks the network as having active clients
2. Launches CSA/Deauth to force a fresh 4-way handshake
3. Captures the complete M1+M2 on reconnection

### Attack Result Callbacks

Register `setAttackResultCallback()` to be notified when an attack exhausts all options without capturing anything. Useful for logging failed targets or adjusting strategy at runtime:

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

### Hidden Network Discovery

Probe Response frames triggered by deauth bursts automatically reveal hidden SSIDs. The engine caches these with zero configuration required.

### PMF/WPA3 Detection

RSNE (Robust Security Network Element) parsing automatically identifies networks with PMF Required. These are skipped to save time, but WPA3 Transition Mode networks (PMF Capable but not Required) are still targeted.

`ApRecord` exposes `pmf_capable` and `pmf_required` so `setTargetFilter` callbacks can make finer-grained decisions than the binary `skip_immune_networks` config field — for example, to target only WPA3 Transition networks (PMF capable but not required).

## Examples

The library includes complete examples demonstrating various use cases:

| Example | Description |
|---------|-------------|
| `TargetedAuditing` | Network filtering with callbacks |
| `EnterpriseAuditing` | 802.1X identity harvesting |
| `StorageAndNVS` | SD card PCAPNG logging and NVS persistence |
| `WigleIntegration` | GPS wardriving with Wigle CSV export |
| `ExportFormats` | HC22000 and PCAPNG format conversion |
| `DynamicControl` | Runtime attack mode switching |
| `AutoEnterpriseHunter` | Automatic enterprise network targeting |
| `SerialStreaming` | Real-time packet streaming |
| `StressTest` | Performance and memory testing |

See the [`examples/`](examples/) directory for complete source code.

## Documentation

Full API documentation is available in the [`docs/`](docs/) directory. Generate fresh documentation:

```bash
doxygen Doxyfile
```

Then open `docs/html/index.html` in your browser.


## Hardware Requirements

- **Platform**: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
- **Framework**: Arduino or ESP-IDF
- **Memory**: Minimum 4MB flash recommended
- **Optional**: SD card module for persistent logging
- **Optional**: GPS module for Wigle integration

## Performance Considerations

- **Channel Hopping**: Default 200ms dwell time balances discovery speed vs. capture reliability
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

