#pragma once
#include <stdint.h>
#include "politician_compat.h"

namespace politician {

// ─── Capture Types ────────────────────────────────────────────────────────────
#define CAP_PMKID           0x01  // PMKID fishing (fake association)
#define CAP_EAPOL           0x02  // Passive EAPOL (natural client reconnection)
#define CAP_EAPOL_CSA       0x03  // EAPOL triggered by CSA beacon injection
#define CAP_EAPOL_HALF      0x04  // M2-only capture (no anonce) — active attack pivot triggered
#define CAP_EAPOL_GROUP     0x05  // Non-pairwise EAPOL-Key (GTK rotation)

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
#define LOG_FILTER_PROBE_REQ   0x08 // Probe Requests as raw EPBs (Client Device History, SPI Safe)
#define LOG_FILTER_MGMT_DISRUPT 0x10 // Deauth/Disassoc frames as raw EPBs (Attack Detection, SPI Safe)
#define LOG_FILTER_ALL         0xFF // Everything (SDMMC ONLY!)

// ─── Logging Callback ─────────────────────────────────────────────────────────
typedef void (*LogCb)(const char *msg);

// ─── Callbacks ────────────────────────────────────────────────────────────────
struct ApRecord;
struct HandshakeRecord;
struct EapIdentityRecord;
struct ProbeRequestRecord;
struct DisruptRecord;

typedef void (*ApFoundCb)(const ApRecord &ap);
typedef void (*PacketCb)(const uint8_t *payload, uint16_t len, int8_t rssi, uint32_t ts_usec);
typedef void (*EapolCb)(const HandshakeRecord &rec);
typedef void (*IdentityCb)(const EapIdentityRecord &rec);
typedef void (*ProbeRequestCb)(const ProbeRequestRecord &rec);
typedef void (*DisruptCb)(const DisruptRecord &rec);

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
    uint32_t ap_expiry_ms        = 300000; // Evict APs not seen for this long (0 = never expire)
    bool     unicast_deauth      = true;  // Send deauth to known client MAC instead of broadcast
    uint32_t probe_hidden_interval_ms = 0;     // How often to probe hidden APs for SSID (0 = disabled, opt-in)
    uint8_t  deauth_reason       = 7;    // 802.11 reason code for deauth frames (7=Class 3 from non-assoc)
    bool     capture_group_keys  = false; // Fire eapolCb with CAP_EAPOL_GROUP on GTK rotation frames
    uint8_t  min_beacon_count    = 0;    // Min times AP must be seen before attack/apFoundCb (0 = no minimum)
    uint8_t  max_total_attempts  = 0;    // Permanently skip BSSID after this many failed attacks (0 = unlimited)
    uint8_t  sta_filter[6]       = {};   // Only record EAPOL sessions from this client MAC (zero = no filter)
    char     ssid_filter[33]     = {};   // Only cache APs matching this SSID (empty = no filter)
    bool     ssid_filter_exact   = true; // True = exact SSID match, false = substring match
    uint8_t  enc_filter_mask     = 0xFF; // Bitmask of enc types to cache: bit0=open,1=WEP,2=WPA,3=WPA2/3,4=Ent
    bool     require_active_clients = false; // Skip attack initiation if no active clients seen on AP
};

// ─── AP Record ────────────────────────────────────────────────────────────────
/** @brief Snapshot of a discovered Access Point from the internal cache. Populated by getAp(), getApByBssid(), and the ApFoundCb callback. */
struct ApRecord {
    uint8_t  bssid[6];
    char     ssid[33];
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  enc;           // 0=open, 1=WEP, 2=WPA, 3=WPA2/WPA3, 4=Enterprise
    bool     wps_enabled;   // WPS IE detected in beacon/probe-response
    bool     pmf_capable;      // MFPC bit set in RSN Capabilities (PMF supported)
    bool     pmf_required;     // MFPR bit set in RSN Capabilities (PMF mandatory)
    uint8_t  total_attempts;   // Number of failed attack attempts recorded
    bool     captured;         // True if BSSID is on the captured or ignore list
    bool     ft_capable;       // 802.11r FT AKM advertised (FT-PSK or FT-EAP)
    uint32_t first_seen_ms;    // millis() timestamp when this AP was first observed
    uint32_t last_seen_ms;     // millis() timestamp of the most recent beacon or probe response
    char     country[3];       // ISO 3166-1 alpha-2 country code from IE 7 (e.g. "US"), empty if absent
    uint16_t beacon_interval;  // Advertised beacon interval in TUs (1 TU = 1024 µs), 0 if unknown
    uint8_t  max_rate_mbps;    // Highest legacy data rate from Supported Rates IEs (Mbps), 0 if unknown
    bool     is_hidden;        // True if AP broadcasts an empty SSID (hidden network)
};

// ─── Frame Stats ──────────────────────────────────────────────────────────────
/** @brief Cumulative frame and capture counters for the engine session. Accessible via getStats(), reset with resetStats(). */
struct Stats {
    uint32_t total;
    uint32_t mgmt;
    uint32_t ctrl;
    uint32_t data;
    uint32_t eapol;
    uint32_t pmkid_found;
    uint32_t beacons;
    uint32_t captures;
    uint32_t failed_pmkid;      // PMKID retries exhausted without capture
    uint32_t failed_csa;        // CSA/Deauth wait expired without EAPOL
    uint16_t channel_frames[14]; // Frames received per 2.4GHz channel (index 0 = ch1, index 13 = ch14)
};

// ─── Handshake Record ─────────────────────────────────────────────────────────
/** @brief A captured handshake or PMKID record delivered to the EapolCb callback. The @p type field identifies the capture path; fields not relevant to that path are zeroed. */
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

/** @brief Identifies the AP and failure reason for a failed attack, delivered to the AttackResultCb callback. */
struct AttackResultRecord {
    uint8_t      bssid[6];
    char         ssid[33];
    uint8_t      ssid_len;
    AttackResult result;
};

typedef void (*AttackResultCb)(const AttackResultRecord &rec);
typedef void (*ClientFoundCb)(const uint8_t *bssid, const uint8_t *sta, int8_t rssi);

/**
 * @brief Fired when a second BSSID advertising the same SSID is observed on the same channel.
 * This indicates a potential evil twin or rogue AP. Both the known AP and the newcomer are included.
 */
struct RogueApRecord {
    uint8_t known_bssid[6]; // BSSID of the first AP already cached with this SSID
    uint8_t rogue_bssid[6]; // BSSID of the newly observed AP sharing the same SSID
    char    ssid[33];       // The shared SSID
    uint8_t ssid_len;
    uint8_t channel;        // Channel on which the conflict was detected
    int8_t  rssi;           // Signal strength of the rogue AP (dBm)
};

typedef void (*RogueApCb)(const RogueApRecord &rec); // Fired when an evil twin / rogue AP is detected

// ─── 802.1X Enterprise Identity Record ─────────────────────────────────────────
/** @brief A harvested 802.1X Enterprise plaintext identity, delivered to the IdentityCb callback. */
struct EapIdentityRecord {
    uint8_t  bssid[6];      // Access Point MAC
    uint8_t  client[6];     // Enterprise Client MAC
    char     identity[65];  // The Plaintext Identity / Email Address
    uint8_t  channel;
    int8_t   rssi;
};

// ─── Probe Request Record ─────────────────────────────────────────────────────
/** @brief A probe request frame observed on the air, delivered to the ProbeRequestCb callback. */
struct ProbeRequestRecord {
    uint8_t  client[6];     // Probing device MAC
    uint8_t  channel;
    int8_t   rssi;
    char     ssid[33];      // Requested SSID (empty = wildcard probe)
    uint8_t  ssid_len;
    bool     rand_mac;      // True if locally administered bit is set (iOS/Android MAC randomization)
};

// ─── Disruption Record ────────────────────────────────────────────────────────
/** @brief A deauthentication or disassociation frame observed on the air, delivered to the DisruptCb callback. */
struct DisruptRecord {
    uint8_t  src[6];        // Frame source MAC
    uint8_t  dst[6];        // Frame destination MAC
    uint8_t  bssid[6];      // BSSID (addr3)
    uint16_t reason;        // 802.11 reason code
    uint8_t  subtype;       // MGMT_SUB_DEAUTH (0xC0) or MGMT_SUB_DISASSOC (0xA0)
    uint8_t  channel;
    int8_t   rssi;
    bool     rand_mac;      // True if source MAC has locally administered bit set (randomized)
};

} // namespace politician
