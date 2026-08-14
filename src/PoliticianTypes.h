#pragma once
#include <stdint.h>
#include "politician_compat.h"

#ifndef POLITICIAN_NO_STD_FUNCTION
#include <functional>
#endif

namespace politician {

// ─── Compile-Time Feature Gates ──────────────────────────────────────────────
// Define these before including Politician.h or via build flags (e.g. -DNAME)
// #define POLITICIAN_NO_DB              // Strip 14KB OUI Database (Vendor lookups)
// #define POLITICIAN_NO_PCAPNG          // Strip PCAPNG serialization logic
// #define POLITICIAN_NO_HC22000         // Strip Hashcat mode 22000 formatter
// #define POLITICIAN_NO_LOGGING         // Strip all internal Serial _log() output
// #define POLITICIAN_NO_STD_FUNCTION    // Use raw fn pointers instead of std::function (saves ~2KB, no lambda captures)
// #define POLITICIAN_NO_MSCHAPV2        // Strip bare EAP-MSCHAPv2 capture (MsChapRecord, MsChapCb, challenge table)
// #define POLITICIAN_NO_KARMA           // Strip KARMA rogue AP responder (KarmaRecord, KarmaCb, probe-response injection)

// ─── Capture Types ────────────────────────────────────────────────────────────
#define CAP_PMKID           0x01  // PMKID fishing (fake association)
#define CAP_EAPOL           0x02  // Passive EAPOL (natural client reconnection)
#define CAP_EAPOL_CSA       0x03  // EAPOL triggered by CSA beacon injection
#define CAP_EAPOL_HALF      0x04  // M2-only capture (no anonce) — active attack pivot triggered
#define CAP_EAPOL_GROUP     0x05  // Non-pairwise EAPOL-Key (GTK rotation)
#define CAP_SAE             0x06  // WPA3 SAE (Simultaneous Authentication of Equals) Commit/Confirm frame

// ─── Encryption Type Constants ────────────────────────────────────────────────
// Used in ApRecord.enc and Config.enc_filter_mask (bit N = enc value N)
#define ENC_OPEN    0  // Open network (no encryption)
#define ENC_WEP     1  // WEP (Privacy bit set, no RSN/WPA IE)
#define ENC_WPA     2  // WPA (vendor IE 00:50:F2:01)
#define ENC_WPA2    3  // WPA2/WPA3-Transition (RSN IE with PSK or SAE AKM)
#define ENC_ENT     4  // 802.1X Enterprise (RSN IE with 802.1X AKM suite 1)
#define ENC_OWE     5  // OWE — Opportunistic Wireless Encryption (AKM suite 18); no PSK, no PMKID

// ─── Attack Selection Bits ────────────────────────────────────────────────────
#define ATTACK_PMKID         0x01  // PMKID fishing
#define ATTACK_CSA           0x02  // CSA beacon injection
#define ATTACK_PASSIVE       0x04  // Passive EAPOL capture
#define ATTACK_DEAUTH        0x08  // Classic Reason 7 Deauthentication
#define ATTACK_STIMULATE     0x10  // Zero-delay QoS Null Client Stimulation
#define ATTACK_BTM           0x20  // 802.11v BSS Transition Management Request (polite client steering)
#define ATTACK_ALL           0x3F

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
#ifndef POLITICIAN_NO_STD_FUNCTION
using LogCb = std::function<void(const char *msg)>;
#else
typedef void (*LogCb)(const char *msg);
#endif

// ─── Callbacks ────────────────────────────────────────────────────────────────
struct ApRecord;
struct HandshakeRecord;
struct EapIdentityRecord;
struct ProbeRequestRecord;
struct DisruptRecord;
struct WpsRecord;
#ifndef POLITICIAN_NO_MSCHAPV2
struct MsChapRecord;
#endif

typedef void (*ApFoundCb)(const ApRecord &ap);
typedef int  (*TargetScoreCb)(const ApRecord &ap, const char *vendor); // Returns a priority score for autoTarget
typedef void (*PacketCb)(const uint8_t *payload, uint16_t len, int8_t rssi, uint8_t channel, uint32_t ts_usec);
typedef void (*EapolCb)(const HandshakeRecord &rec);
typedef void (*IdentityCb)(const EapIdentityRecord &rec);
typedef void (*ProbeRequestCb)(const ProbeRequestRecord &rec);
typedef void (*DisruptCb)(const DisruptRecord &rec);
typedef void (*WpsCb)(const WpsRecord &rec);
#ifndef POLITICIAN_NO_MSCHAPV2
typedef void (*MsChapCb)(const MsChapRecord &rec);
#endif

#ifndef POLITICIAN_NO_ESPNOW
/** @brief Captured ESP-NOW vendor-specific action frame. */
struct EspNowRecord {
    uint8_t src[6];
    uint8_t dst[6];
    uint8_t channel;
    int8_t rssi;
    uint32_t ts_usec;
    const uint8_t *payload;
    uint16_t length;
};

#ifndef POLITICIAN_NO_STD_FUNCTION
using EspNowCb = std::function<void(const EspNowRecord &rec)>;
#else
typedef void (*EspNowCb)(const EspNowRecord &rec);
#endif
#endif

// ─── Error Codes ──────────────────────────────────────────────────────────────
enum Error {
    OK = 0,
    ERR_WIFI_INIT = 1,
    ERR_INVALID_CH = 2,
    ERR_NOT_ACTIVE = 3,
    ERR_ALREADY_CAPTURED = 4,
    ERR_NOT_FOUND = 5,
    /** Returned by begin() when all POLITICIAN_MAX_INSTANCES slots are occupied. */
    ERR_MAX_INSTANCES = 6,
    ERR_INVALID_ARG = 7,
    ERR_QUEUE_FULL = 8
};

/**
 * @brief Configuration for the Politician engine.
 */
struct Config {
    uint16_t hop_dwell_ms        = 200;  // Time per channel
    bool     smart_hopping       = true; // Dynamic channel dwell time based on traffic
    uint16_t hop_min_dwell_ms    = 50;   // Minimum dwell if no traffic is seen
    uint16_t hop_max_dwell_ms    = 400;  // Maximum dwell if traffic is active
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
    bool     deauth_reason_cycling = true; // Cycle through effective reason codes during burst (fuzzing)
    bool     capture_group_keys  = false; // Fire eapolCb with CAP_EAPOL_GROUP on GTK rotation frames
    uint8_t  min_beacon_count    = 0;    // Min times AP must be seen before attack/apFoundCb (0 = no minimum)
    uint8_t  max_total_attempts  = 0;    // Permanently skip BSSID after this many failed attacks (0 = unlimited)
    uint8_t  sta_filter[6]       = {};   // Only record EAPOL sessions from this client MAC (zero = no filter)
    char     ssid_filter[33]     = {};   // Only cache APs matching this SSID (empty = no filter)
    bool     ssid_filter_exact   = true; // True = exact SSID match, false = substring match
    uint8_t  enc_filter_mask          = 0xFF;  // Bitmask of enc types to cache: bit0=Open,bit1=WEP,bit2=WPA,bit3=WPA2,bit4=Ent,bit5=OWE
    bool     require_active_clients = false; // Skip attack initiation if no active clients seen on AP
    const char* soft_ap_ssid       = nullptr; // Custom SSID for the engine's soft AP (nullptr = use default hidden AP)
    uint8_t  btm_burst_count       = 8;    // Number of BTM Request frames per client per trigger
    uint16_t btm_disassoc_timer    = 3;    // Disassociation Timer in TBTTs (~100ms each); 0 = immediate
#ifndef POLITICIAN_NO_KARMA
    bool     karma_enabled         = false; // Enable KARMA rogue AP responder
    bool     karma_open_only       = true;  // Only respond to probes for open/unknown networks (skip known WPA APs)
    uint8_t  karma_max_ssids       = 16;    // Max unique SSIDs to track for dedup (circular eviction)
#endif
};

/**
 * @brief Validates a Config struct and returns human-readable warning strings
 * for values that will be silently clamped or that may cause unexpected behavior.
 *
 * Call this before engine.begin(cfg) to surface misconfigurations early.
 *
 * @param cfg     The Config to validate.
 * @param out     Output array; each element is set to a static warning string.
 * @param maxOut  Capacity of @p out.
 * @return        Number of warnings written (0 = configuration looks clean).
 *
 * Example:
 * @code
 * const char *warnings[8];
 * int n = politician::validateConfig(cfg, warnings, 8);
 * for (int i = 0; i < n; i++) Serial.println(warnings[i]);
 * @endcode
 */
inline int validateConfig(const Config &cfg, const char **out, uint8_t maxOut) {
    int n = 0;
    auto w = [&](const char *msg) { if (n < maxOut) out[n++] = msg; };
    if (cfg.smart_hopping && cfg.hop_min_dwell_ms >= cfg.hop_max_dwell_ms)
        w("hop_min_dwell_ms >= hop_max_dwell_ms — max will be clamped to min+50ms");
    if (cfg.smart_hopping && cfg.hop_min_dwell_ms > UINT16_MAX - 50)
        w("hop_min_dwell_ms is too large for the min+50ms clamp");
    if (cfg.fish_timeout_ms < 500)
        w("fish_timeout_ms < 500 — will be clamped to 500ms");
    if (cfg.csa_wait_ms < 1000)
        w("csa_wait_ms < 1000 — will be clamped to 1000ms");
    if (cfg.hop_dwell_ms == 0)
        w("hop_dwell_ms = 0 — hopper will spin with no delay");
    if (cfg.deauth_burst_count == 0)
        w("deauth_burst_count = 0 — deauth attacks send zero frames");
    if (cfg.csa_beacon_count == 0)
        w("csa_beacon_count = 0 — CSA attacks send zero beacons");
    if (cfg.probe_aggr_interval_s == 0)
        w("probe_aggr_interval_s = 0 — APs attacked every beacon (very aggressive)");
    if (cfg.min_rssi < -100 || cfg.min_rssi > -20)
        w("min_rssi out of useful range [-100, -20] dBm");
    return n;
}

// ─── AP Record ────────────────────────────────────────────────────────────────
/** @brief Snapshot of a discovered Access Point from the internal cache. Populated by getAp(), getApByBssid(), and the ApFoundCb callback. */
struct ApRecord {
    uint8_t  bssid[6];
    char     ssid[33];
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  enc;           // ENC_OPEN=0 ENC_WEP=1 ENC_WPA=2 ENC_WPA2=3 ENC_ENT=4 ENC_OWE=5
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
    uint16_t sta_count;        // Connected client count from BSS Load IE (if present)
    uint8_t  chan_util;        // Channel utilization from BSS Load IE (0-255)
    uint8_t  venue_group;      // 802.11u Venue Group (e.g., 2=Education, 10=Residential)
    uint8_t  venue_type;       // 802.11u Venue Type (e.g., 8=University, 1=Coffee Shop)
    uint8_t  network_type;     // 802.11u Access Network Type (1=Free Public, 2=Chargeable, etc.)
    bool     is_vht;           // 802.11ac (VHT / Wi-Fi 5) capable
    bool     is_he;            // 802.11ax (HE / Wi-Fi 6) capable
    uint8_t  chan_width;       // Max channel width: 0=20MHz 1=40MHz 2=80MHz 3=160MHz 4=80+80MHz
    uint16_t beacon_count;     ///< Number of beacons observed from this AP in the current session
    uint8_t  capture_count;    ///< Number of successful handshake/PMKID captures for this BSSID
    uint32_t last_attack_ms;   ///< millis() of the most recent attack initiation (0 = never attacked)
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
    uint32_t sae_found;
    uint32_t beacons;
    uint32_t captures;
    uint32_t failed_pmkid;      // PMKID retries exhausted without capture
    uint32_t failed_csa;        // CSA/Deauth wait expired without EAPOL
    volatile uint32_t dropped;  // Frames dropped due to ringbuffer overflow
    uint32_t rb_max;            // Max observed ringbuffer usage (bytes)
    uint16_t channel_frames[200]; // Frames received per channel, indexed by channel number (e.g. ch1=index1, ch36=index36). Index 0 unused.
};

// ─── Handshake Record ─────────────────────────────────────────────────────────
// Pairwise cipher suite constants for HandshakeRecord.cipher
static const uint8_t CIPHER_UNKNOWN = 0;
static const uint8_t CIPHER_TKIP    = 1;  ///< TKIP (00-0F-AC:2) — legacy, crackable offline
static const uint8_t CIPHER_CCMP    = 2;  ///< CCMP/AES (00-0F-AC:4) — current standard

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
    uint8_t  cipher;        ///< Pairwise cipher suite: CIPHER_TKIP / CIPHER_CCMP / CIPHER_UNKNOWN
    // PMKID path
    uint8_t  pmkid[16];
    // EAPOL path
    uint8_t  anonce[32];
    uint8_t  snonce[32];
    uint8_t  mic[16];
    union {
        uint8_t  eapol_m2[256];
        uint8_t  sae_data[256]; 
    };
    uint8_t  eapol_m3[256];
    uint8_t  eapol_m4[256];
    union {
        uint16_t eapol_m2_len;
        uint16_t sae_len;
    };
    uint16_t eapol_m3_len;
    uint16_t eapol_m4_len;
    bool     has_mic;
    bool     has_anonce;
    bool     has_snonce;
    bool     has_m3;
    bool     has_m4;
    bool     is_full;       // True if this is a complete 4-way sequence or full SAE exchange
    uint8_t  sae_seq;       // SAE Auth Sequence (1=Commit, 2=Confirm)
};

// ─── Disconnection Strategy ───────────────────────────────────────────────────
enum DisconnectStrategy : uint8_t {
    STRATEGY_AUTO_FALLBACK = 0, // CSA first, fallback to Deauth halfway through wait window
    STRATEGY_SIMULTANEOUS  = 1, // CSA and Deauth simultaneously (Legacy behavior)
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

/**
 * @brief Snapshot of a client station observed associated with an AP.
 * Delivered to the ClientFoundCb callback and enriched with vendor lookup,
 * timing, and MAC-randomization detection.
 */
struct ClientRecord {
    uint8_t  bssid[6];        ///< BSSID of the AP this client is associated with
    uint8_t  sta[6];          ///< Client (station) MAC address
    int8_t   rssi;            ///< Signal strength at time of observation (dBm)
    uint32_t first_seen_ms;   ///< millis() when this client was first seen on this BSSID
    uint32_t last_seen_ms;    ///< millis() of the most recent frame from this client
    bool     rand_mac;        ///< True if the locally administered bit is set (MAC randomization)
    char     vendor[32];      ///< OUI vendor string; empty if POLITICIAN_NO_DB is defined
};

typedef void (*ClientFoundCb)(const ClientRecord &rec);

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

#ifndef POLITICIAN_NO_STD_FUNCTION
using RogueApCb = std::function<void(const RogueApRecord &rec)>; // Fired when an evil twin / rogue AP is detected
#else
typedef void (*RogueApCb)(const RogueApRecord &rec); // Fired when an evil twin / rogue AP is detected
#endif

// ─── KARMA Record ─────────────────────────────────────────────────────────────
#ifndef POLITICIAN_NO_KARMA
/**
 * @brief Delivered to the KarmaCb callback when the KARMA responder replies to a
 * named probe request. Contains the client that probed and the SSID that was echoed.
 *
 * The engine injects a probe response and one beacon spoofed as an AP with
 * that exact SSID and an open authentication mode, enticing the client to
 * auto-associate.
 */
struct KarmaRecord {
    uint8_t client[6];   // Probing client MAC
    char    ssid[33];    // SSID that was requested and echoed
    uint8_t ssid_len;
    uint8_t channel;
    int8_t  rssi;        // RSSI of the incoming probe request
    uint8_t ap_mac[6];   // Spoofed AP MAC used in the probe response
};

#ifndef POLITICIAN_NO_STD_FUNCTION
using KarmaCb = std::function<void(const KarmaRecord &rec)>;
#else
typedef void (*KarmaCb)(const KarmaRecord &rec);
#endif
#endif // POLITICIAN_NO_KARMA

// ─── 802.1X Enterprise Identity Record ─────────────────────────────────────────
// EAP method constants (RFC 3748 / RFC 5281)
static const uint8_t EAP_METHOD_IDENTITY  = 0x01;  ///< EAP Identity (always 0x01 for harvested records)
static const uint8_t EAP_METHOD_TLS       = 0x0D;  ///< EAP-TLS (RFC 5216) — mutual cert auth
static const uint8_t EAP_METHOD_TTLS      = 0x15;  ///< EAP-TTLS (RFC 5281) — outer tunnel, inner MSCHAPv2
static const uint8_t EAP_METHOD_PEAP      = 0x19;  ///< PEAP (draft-josefsson-pppext-eap-tls-eap) — outer tunnel
static const uint8_t EAP_METHOD_MSCHAPV2  = 0x1A;  ///< Bare EAP-MSCHAPv2 (no tunnel — crackable)

/** @brief A harvested 802.1X Enterprise plaintext identity, delivered to the IdentityCb callback. */
struct EapIdentityRecord {
    uint8_t  bssid[6];      // Access Point MAC
    uint8_t  client[6];     // Enterprise Client MAC
    char     identity[65];  // The Plaintext Identity / Email Address
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  eap_method;    ///< EAP method negotiated by the AP (EAP_METHOD_* constant); 0 if not yet observed
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

// ─── WPS Record ───────────────────────────────────────────────────────────────
/**
 * @brief WPS M1 device attributes harvested from an EAP-WSC exchange.
 * Delivered to the WpsCb callback when a WPS Enrollee sends its M1 message.
 * Only M1 (Enrollee → AP) is unencrypted; subsequent messages cannot be parsed passively.
 */
struct WpsRecord {
    uint8_t  bssid[6];           // Access Point MAC
    uint8_t  sta[6];             // WPS Enrollee (client) MAC
    uint8_t  channel;
    int8_t   rssi;
    char     device_name[33];    // Device Name attribute (0x1011)
    char     manufacturer[65];   // Manufacturer attribute (0x1021)
    char     model_name[33];     // Model Name attribute (0x1023)
    char     model_number[33];   // Model Number attribute (0x1024)
    char     serial_number[33];  // Serial Number attribute (0x1042)
    uint16_t auth_type_flags;    // Auth Type Flags (0x1004): bit0=Open,bit1=WPA-PSK,bit2=WPA-Ent,bit5=WPA2-PSK
    uint16_t config_methods;     // Config Methods (0x1008): bit2=NFC,bit3=PushButton,bit6=PIN
    uint8_t  rf_bands;           // RF Bands (0x103C): bit0=2.4GHz, bit1=5GHz
    uint16_t primary_dev_type_cat; // Primary Device Type category (0x1054, bytes 0-1)
};

// ─── EAP-MSCHAPv2 Record ──────────────────────────────────────────────────────
#ifndef POLITICIAN_NO_MSCHAPV2
/**
 * @brief Bare EAP-MSCHAPv2 challenge/response pair harvested passively.
 * Only available when the AP serves MSCHAPv2 without a TLS tunnel (no PEAP/TTLS).
 * The nt_response can be cracked offline with tools like asleap or hashcat (-m 5500).
 *
 * Crack with hashcat:
 *   echo "username::::peer_challenge_hex:nt_response_hex:server_challenge_hex" | hashcat -m 5500
 */
struct MsChapRecord {
    uint8_t  bssid[6];          // Access Point MAC
    uint8_t  sta[6];            // Client MAC
    uint8_t  channel;
    int8_t   rssi;
    char     username[65];      // Plaintext username from MSCHAPv2 Response
    uint8_t  server_challenge[16]; // Server challenge from MSCHAPv2 Challenge frame
    uint8_t  peer_challenge[16];   // Peer challenge from MSCHAPv2 Response frame
    uint8_t  nt_response[24];      // NT-Hash response (offline crackable)
};
#endif

// ─── Device Fingerprint ───────────────────────────────────────────────────────

// match_flags bits (reported in DeviceRecord)
#define FP_MATCH_OUI        0x01
#define FP_MATCH_PROBE_SSID 0x02
#define FP_MATCH_HT_CAP     0x04
#define FP_MATCH_RATES      0x08
#define FP_MATCH_IE_FLAGS   0x10

// ie_flags / ie_flags_mask bits (in DeviceFingerprint)
#define FP_IEF_NO_HT        0x01  // IE 45 (HT Capabilities) absent
#define FP_IEF_NO_EXT_CAP   0x02  // IE 127 (Extended Capabilities) absent
#define FP_IEF_HAS_WMM      0x04  // WMM vendor IE (00:50:F2:01) present
#define FP_IEF_HAS_WPS      0x08  // WPS vendor IE (00:50:F2:04) present

/** @brief One fingerprint entry in the built-in or user-defined database. */
struct DeviceFingerprint {
    const char* vendor;
    const char* model;
    uint8_t     oui[3];
    const char* probeSsid;
    uint8_t     confidence;
    // IE-based signals — zero values mean "don't check this signal"
    uint8_t     ht_cap_info[2];  // expected HT Capabilities Info bytes 0–1
    uint8_t     ht_cap_mask[2];  // bitmask: which bits of ht_cap_info to compare
    uint8_t     rate_sig[4];     // first 4 bytes of Supported Rates IE
    uint8_t     ie_flags;        // expected IE presence flags (FP_IEF_*)
    uint8_t     ie_flags_mask;   // which ie_flags bits to check
};

/** @brief A matched device, delivered to the DeviceFoundCb callback. */
struct DeviceRecord {
    uint8_t  mac[6];
    char     vendor[32];
    char     model[32];
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  confidence;
    uint8_t  match_flags;
};

} // namespace politician
