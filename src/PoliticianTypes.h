#pragma once
#include <stdint.h>
#include <Arduino.h>

namespace politician {

// ─── Capture Types ────────────────────────────────────────────────────────────
#define CAP_PMKID           0x01  // PMKID fishing (fake association)
#define CAP_EAPOL           0x02  // Passive EAPOL (natural client reconnection)
#define CAP_EAPOL_CSA       0x03  // EAPOL triggered by CSA beacon injection
#define CAP_EAPOL_HALF      0x04  // M2-only capture (no anonce) — active attack pivot triggered

// ─── Attack Selection Bits ────────────────────────────────────────────────────
#define ATTACK_PMKID         0x01  // PMKID fishing
#define ATTACK_CSA           0x02  // CSA beacon injection
#define ATTACK_PASSIVE       0x04  // Passive EAPOL capture
#define ATTACK_DEAUTH        0x08  // Classic Reason 7 Deauthentication
#define ATTACK_STIMULATE     0x10  // Zero-delay QoS Null Client Stimulation
#define ATTACK_ALL           0x1F

// ─── Capture Filters ──────────────────────────────────────────────────────────
// NOTE: Logging High-Frequency Intel (like Beacons) via standard SPI (SD.h) will
// create massive blocking delays (20-50ms per flush) that destroy the hopper's 
// attack loop. If you enable LOG_FILTER_BEACONS or LOG_FILTER_ALL, you MUST 
// use a board wired for SDMMC (4-bit DMA) for non-blocking background writes.
#define LOG_FILTER_HANDSHAKES  0x01 // EAPOLs, PMKIDs (Crackable info, SPI Safe)
#define LOG_FILTER_PROBES      0x02 // Probe Requests & Responses (Scouting, SPI Safe)
#define LOG_FILTER_BEACONS     0x04 // Beacons (Network Mapping, SDMMC ONLY!)
#define LOG_FILTER_ALL         0xFF // Everything (SDMMC ONLY!)

// ─── Logging Callback ─────────────────────────────────────────────────────────
typedef void (*LogCb)(const char *msg);

// ─── Callbacks ────────────────────────────────────────────────────────────────
struct ApRecord;
struct HandshakeRecord;
struct EapIdentityRecord;

typedef void (*ApFoundCb)(const ApRecord &ap);
typedef void (*PacketCb)(const uint8_t *payload, uint16_t len, int8_t rssi, uint32_t ts_usec);
typedef void (*EapolCb)(const HandshakeRecord &rec);
typedef void (*IdentityCb)(const EapIdentityRecord &rec);

// ─── Error Codes ──────────────────────────────────────────────────────────────
enum Error {
    OK = 0,
    ERR_WIFI_INIT = 1,
    ERR_INVALID_CH = 2,
    ERR_NOT_ACTIVE = 3,
    ERR_ALREADY_CAPTURED = 4,
    ERR_NOT_FOUND = 5
};

/**
 * @brief Configuration for the Politician engine.
 */
struct Config {
    uint16_t hop_dwell_ms        = 200;  // Time per channel
    uint32_t m1_lock_ms          = 800;  // How long to stay on channel after seeing M1
    uint32_t fish_timeout_ms     = 2000; // Time for PMKID association
    uint8_t  fish_max_retries    = 2;    // PMKID retries before giving up or CSA
    uint32_t csa_wait_ms         = 4000; // How long to wait for reconnect after CSA
    uint8_t  csa_beacon_count    = 8;    // Number of CSA beacons to burst
    uint8_t  deauth_burst_count  = 16;   // Number of classic Deauth frames to send
    uint16_t probe_aggr_interval_s = 30; // Seconds to wait between attacking same AP
    uint32_t session_timeout_ms  = 60000; // How long orphaned handshakes live in RAM
    bool     capture_half_handshakes = false; // Save M2-only captures and pivot to active attack
    bool     skip_immune_networks = true; // Ignore Pure WPA3 / PMF Required networks
    uint8_t  csa_deauth_count    = 15;   // Number of standard deauths to append
    uint8_t  capture_filter      = LOG_FILTER_HANDSHAKES | LOG_FILTER_PROBES; // Exclude Beacons by default to save SD storage
    int8_t   min_rssi            = -100; // Ignore APs with signal weaker than this (dBm)
};

// ─── AP Record ────────────────────────────────────────────────────────────────
struct ApRecord {
    uint8_t  bssid[6];
    char     ssid[33];
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  enc;       // 0=open, 1=WEP, 2=WPA, 3=WPA2/WPA3, 4=Enterprise
};

// ─── Frame Stats ──────────────────────────────────────────────────────────────
struct Stats {
    uint32_t total;
    uint32_t mgmt;
    uint32_t ctrl;
    uint32_t data;
    uint32_t eapol;
    uint32_t pmkid_found;
    uint32_t beacons;
    uint32_t captures;
    uint32_t failed_pmkid; // PMKID retries exhausted without capture
    uint32_t failed_csa;   // CSA/Deauth wait expired without EAPOL
};

// ─── Handshake Record ─────────────────────────────────────────────────────────
struct HandshakeRecord {
    uint8_t  type;          // CAP_PMKID / CAP_EAPOL / ...
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  bssid[6];
    uint8_t  sta[6];
    char     ssid[33];
    uint8_t  ssid_len;
    uint8_t  enc;           // 0=open, 1=WEP, 2=WPA, 3=WPA2/WPA3, 4=Enterprise
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

// ─── Attack Result ────────────────────────────────────────────────────────────
enum AttackResult : uint8_t {
    RESULT_PMKID_EXHAUSTED = 1, // All PMKID retries failed, no PMKID captured
    RESULT_CSA_EXPIRED     = 2, // CSA/Deauth wait window closed, no EAPOL captured
};

struct AttackResultRecord {
    uint8_t      bssid[6];
    char         ssid[33];
    uint8_t      ssid_len;
    AttackResult result;
};

typedef void (*AttackResultCb)(const AttackResultRecord &rec);

// ─── 802.1X Enterprise Identity Record ─────────────────────────────────────────
struct EapIdentityRecord {
    uint8_t  bssid[6];      // Access Point MAC
    uint8_t  client[6];     // Enterprise Client MAC
    char     identity[65];  // The Plaintext Identity / Email Address
    uint8_t  channel;
    int8_t   rssi;
};

} // namespace politician
