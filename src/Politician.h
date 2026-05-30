#pragma once
#include "politician_compat.h"
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include "PoliticianTypes.h"

namespace politician {

#ifndef POLITICIAN_MAX_AP_CACHE
#define POLITICIAN_MAX_AP_CACHE 48
#endif

#ifndef POLITICIAN_MAX_SESSIONS
#define POLITICIAN_MAX_SESSIONS 8
#endif

#ifndef POLITICIAN_MAX_CAPTURED
#define POLITICIAN_MAX_CAPTURED 128
#endif

#ifndef POLITICIAN_MAX_CHANNELS
#define POLITICIAN_MAX_CHANNELS 50
#endif

/**
 * Maximum number of concurrent Politician instances that can be active at once.
 * Each instance gets its own ring buffer and worker task, but they share the
 * single Wi-Fi radio — channel changes made by one instance affect all others.
 * On standard ESP32/ESP32-S2/ESP32-C3 the practical limit is 1; on ESP32-S3
 * with an external SPI radio a second instance can operate on that interface.
 * Define before including Politician.h to override the default of 2.
 */
#ifndef POLITICIAN_MAX_INSTANCES
#define POLITICIAN_MAX_INSTANCES 2
#endif

// ─── 802.11 Frame Structures ──────────────────────────────────────────────────

typedef struct {
    uint16_t frame_ctrl;
    uint16_t duration;
    uint8_t  addr1[6];
    uint8_t  addr2[6];
    uint8_t  addr3[6];
    uint16_t seq_ctrl;
} __attribute__((packed)) ieee80211_hdr_t;

typedef struct {
    ieee80211_hdr_t hdr;
    uint8_t         payload[0];
} __attribute__((packed)) ieee80211_frame_t;

// ─── Frame Control Masks ──────────────────────────────────────────────────────
#define FC_TYPE_MASK        0x000C
#define FC_SUBTYPE_MASK     0x00F0
#define FC_TODS_MASK        0x0100
#define FC_FROMDS_MASK      0x0200
#define FC_TYPE_MGMT        0x0000
#define FC_TYPE_CTRL        0x0004
#define FC_TYPE_DATA        0x0008
#define FC_ORDER_MASK       0x8000

#define MGMT_SUB_ASSOC_REQ  0x00
#define MGMT_SUB_ASSOC_RESP 0x10
#define MGMT_SUB_PROBE_REQ  0x40
#define MGMT_SUB_PROBE_RESP 0x50
#define MGMT_SUB_BEACON     0x80
#define MGMT_SUB_AUTH       0xB0
#define MGMT_SUB_DISASSOC   0xA0
#define MGMT_SUB_DEAUTH     0xC0

// ─── EAPOL ────────────────────────────────────────────────────────────────────
#define EAPOL_LLC_OFFSET    0
#define EAPOL_ETHERTYPE_HI  0x88
#define EAPOL_ETHERTYPE_LO  0x8E
#define EAPOL_LLC_SIZE      8
#define EAPOL_MIN_FRAME_LEN (EAPOL_LLC_SIZE + 4)

#define EAPOL_KEY_DESC_TYPE     0
#define EAPOL_KEY_INFO          1
#define EAPOL_REPLAY_COUNTER    5
#define EAPOL_KEY_NONCE        13
#define EAPOL_KEY_MIC          77
#define EAPOL_KEY_DATA_LEN     93
#define EAPOL_KEY_DATA         95

#define KEYINFO_TYPE_MASK   0x0007
#define KEYINFO_PAIRWISE    0x0008
#define KEYINFO_ACK         0x0080
#define KEYINFO_MIC         0x0100
#define KEYINFO_SECURE      0x0200
#define KEYINFO_INSTALL     0x0040

// ─── Politician (The Handshaker) ──────────────────────────────────────────────

/**
 * @brief The core WiFi handshake capturing engine.
 */
class Politician {
public:
    Politician();

    /**
     * @brief Initializes the WiFi driver in promiscuous mode.
     * @param cfg Optional configuration struct.
     * @return OK on success, or an error code.
     */
    Error   begin(const Config &cfg = Config());

    /**
     * @brief Sets a custom logging callback to intercept library output.
     */
    void    setLogger(LogCb cb) { _logCb = cb; }

    /**
     * @brief Manually adds a BSSID to the "already captured" list to skip it.
     */
    void    markCaptured(const uint8_t *bssid);

    /**
     * @brief Clears the captured BSSID list.
     */
    void    clearCapturedList();

    /**
     * @brief Sets a list of BSSIDs that should always be ignored by the engine.
     */
    void    setIgnoreList(const uint8_t (*bssids)[6], uint8_t count);

    /**
     * @brief Enables or disables frame processing.
     */
    void    setActive(bool active);

    /**
     * @brief Manually sets the WiFi radio to a specific channel.
     * @param ch Channel number (2.4GHz: 1-14, 5GHz: 36-165)
     * @return OK on success, ERR_INVALID_CH if ch is invalid.
     */
    Error   setChannel(uint8_t ch);

    /**
     * @brief Starts autonomous channel hopping.
     * @param dwellMs Time in milliseconds to stay on each channel (0 = use config).
     */
    void    startHopping(uint16_t dwellMs = 0);

    /**
     * @brief Stops autonomous channel hopping and goes idle.
     */
    void    stopHopping();

    /**
     * @brief Full engine teardown. Aborts any in-progress attack, clears the
     *        target, stops hopping, and disables frame processing in one call.
     *        Use this instead of combining stopHopping() + clearTarget() + setActive(false).
     */
    void    stop();

    /**
     * @brief Stops hopping and locks the radio to a specific channel.
     * @return OK on success, or an error code.
     */
    Error   lockChannel(uint8_t ch);

    /**
     * @brief Restricts hopping to a specific list of channels.
     * @param channels Array of channel numbers (2.4GHz: 1-14, 5GHz: 36-165)
     * @param count Number of channels in array
     */
    void    setChannelList(const uint8_t *channels, uint8_t count);

    /**
     * @brief Restricts hopping to 2.4GHz, 5GHz, or both bands.
     * @param ghz24 Include 2.4GHz channels (1-13)
     * @param ghz5  Include 5GHz common channels (36-165)
     */
    void    setChannelBands(bool ghz24, bool ghz5);

    /**
     * @brief Searches the AP cache by SSID and locks onto the strongest match.
     * Equivalent to calling setTarget() on the best matching AP.
     * @param ssid Null-terminated SSID string to search for.
     * @return OK on success, ERR_NOT_FOUND if SSID is not in cache,
     *         ERR_ALREADY_CAPTURED if BSSID is already captured, ERR_NOT_ACTIVE if not initialized.
     */
    Error   setTargetBySsid(const char *ssid);

    /**
     * @brief Main worker method. Must be called frequently from loop().
     */
    void    tick();

    /**
     * @brief Configures which attack techniques are enabled globally.
     */
    void    setAttackMask(uint8_t mask);

    /**
     * @brief Overrides the attack mask for a specific BSSID.
     * When the engine targets this BSSID the override mask is used instead of the global mask.
     * The override table holds up to 8 entries; oldest is evicted if full.
     */
    void    setAttackMaskForBssid(const uint8_t *bssid, uint8_t mask);

    /**
     * @brief Sets an attack mask for all APs whose SSID matches the given string.
     *
     * @param ssid      The SSID string to match
     * @param mask      Attack mask to apply (ATTACK_PASSIVE, ATTACK_PMKID, etc.)
     * @param substring If true, matches any AP whose SSID contains @p ssid as a
     *                  substring. If false (default), requires an exact match.
     *
     * Overrides are applied after per-BSSID overrides and before the global mask.
     * If an AP matches both a BSSID override and an SSID override, the BSSID
     * override takes precedence. Up to MAX_SSID_OVERRIDES entries are stored;
     * subsequent calls overwrite the oldest entry on overflow.
     */
    void    setAttackMaskForSsid(const char *ssid, uint8_t mask, bool substring = false);

    /**
     * @brief Clears all per-BSSID attack mask overrides.
     */
    void    clearAttackMaskOverrides();

    /**
     * @brief Configures how the engine handles disconnection when both CSA and Deauth are enabled.
     * @param strategy STRATEGY_AUTO_FALLBACK (default) or STRATEGY_SIMULTANEOUS.
     */
    void    setDisconnectionStrategy(DisconnectStrategy strategy) { _disconnectStrategy = strategy; }

    /**
     * @brief Focuses the engine on a single BSSID.
     * @return OK on success, ERR_ALREADY_CAPTURED if BSSID is on the captured/ignore list.
     */
    Error   setTarget(const uint8_t *bssid, uint8_t channel);

    /**
     * @brief Clears the specific target and resumes autonomous wardriving.
     */
    void    clearTarget();

    /** @return True if currently focusing on a specific target BSSID. */
    bool    hasTarget()    const { return _hasTarget; }

    /** @return True if an active attack (PMKID fishing or CSA/Deauth) is in progress. */
    bool    isAttacking()  const { return _fishState != FISH_IDLE; }

    /**
     * @brief Continuously locks onto the strongest uncaptured AP in the cache.
     * After each attack attempt (success or failure), automatically moves to the next best target.
     * @param enable True to enable, false to disable and resume normal hopping.
     */
    void    setAutoTarget(bool enable);

    /** @brief Resets all frame and capture statistics to zero. */
    void    resetStats()         { memset(&_stats, 0, sizeof(_stats)); }

    /** @return The current operating channel. */
    uint8_t         getChannel()  const { return _channel; }

    /** @return True if the engine is currently processing frames. */
    bool            isActive()    const { return _active; }

    /** @return Signal strength (RSSI) of the last received frame. */
    int8_t          getLastRssi() const { return _lastRssi; }

    /** @return Reference to the internal statistics counter. */
    Stats&          getStats()    { return _stats; }

    /** @return Reference to the internal configuration struct for runtime mutations. */
    Config&         getConfig()   { return _cfg; }

    /** @return Number of unique APs currently in the discovery cache. */
    int             getApCount()  const;

    /**
     * @brief Reads an AP from the discovery cache by index.
     * @param idx Zero-based index (0 to getApCount()-1).
     * @param out Populated with the AP's details on success.
     * @return True if idx is valid, false otherwise.
     */
    bool            getAp(int idx, ApRecord &out) const;

    /**
     * @brief Looks up an AP in the discovery cache by BSSID.
     * @param bssid 6-byte BSSID to search for.
     * @param out Populated with the AP's details on success.
     * @return True if found, false if the BSSID is not in cache.
     */
    bool            getApByBssid(const uint8_t *bssid, ApRecord &out) const;

    /**
     * @brief Returns the N most active channels sorted by descending frame count.
     * Uses the per-channel frame counters accumulated since the last resetStats() call.
     * @param out   Output array to receive sorted channel numbers
     * @param count Maximum number of channels to return
     * @return      Actual number of channels written (≤ count)
     */
    uint8_t getChannelsSortedByActivity(uint8_t *out, uint8_t count) const;

    /**
     * @brief Feeds the top-N most active channels (from getChannelsSortedByActivity)
     * into setChannelList(), replacing the current hop sequence.
     * Call this periodically (e.g., every 60s) to adapt the hopper to live traffic.
     * @param topN  Number of top channels to keep (clamped to POLITICIAN_MAX_CHANNELS)
     * @return      Number of channels in the new hop list
     */
    uint8_t setAutoChannelList(uint8_t topN = 13);

    /**
     * @brief Iterates all active APs in the cache, calling @p cb for each one.
     *
     * The internal mutex is held for the entire iteration, making this safe on a
     * multi-core FreeRTOS system. Do not call any blocking Politician API from
     * within the callback.
     *
     * @param cb   Callback invoked with each AP snapshot
     * @param ctx  Opaque user pointer passed unchanged to @p cb (may be nullptr)
     */
    void forEachAp(void (*cb)(const ApRecord &ap, void *ctx), void *ctx = nullptr) const;

#ifndef POLITICIAN_NO_STD_FUNCTION
    /**
     * @brief std::function overload of forEachAp() — supports lambda captures.
     * Gated by POLITICIAN_NO_STD_FUNCTION.
     */
    void forEachAp(std::function<void(const ApRecord &ap)> cb) const;
#endif

    /**
     * @brief Returns the number of unique clients seen associated to a given AP.
     * @param bssid 6-byte BSSID of the AP.
     * @return Client count (0-4), or 0 if BSSID is not in cache.
     */
    int             getClientCount(const uint8_t *bssid) const;

    /**
     * @brief Reads a client MAC from the per-AP client table.
     * @param bssid   6-byte BSSID of the AP.
     * @param idx     Zero-based client index (0 to getClientCount()-1).
     * @param out_sta Output buffer for the 6-byte client MAC.
     * @return True if idx is valid, false otherwise.
     */
    bool            getClient(const uint8_t *bssid, int idx, uint8_t out_sta[6]) const;

    using _FpHookCb        = void (*)(const uint8_t *mac, const char *ssid, uint8_t ssid_len, uint8_t ch, int8_t rssi, const uint8_t *ie, uint16_t ie_len);
    void _setFingerprintHook(_FpHookCb cb) { _fpHook = cb; }

#ifndef POLITICIAN_NO_STD_FUNCTION
    using EapolCb          = std::function<void(const HandshakeRecord &rec)>;
    using ApFoundCb        = std::function<void(const ApRecord &ap)>;
    using TargetFilterCb   = std::function<bool(const ApRecord &ap)>;
    using TargetScoreCb    = std::function<int(const ApRecord &ap, const char *vendor)>;
    using PacketCb         = std::function<void(const uint8_t *payload, uint16_t len, int8_t rssi, uint8_t channel, uint32_t ts_usec)>;
    using IdentityCb       = std::function<void(const EapIdentityRecord &rec)>;
    using AttackResultCb   = std::function<void(const AttackResultRecord &rec)>;
    using ProbeRequestCb   = std::function<void(const ProbeRequestRecord &rec)>;
    using DisruptCb        = std::function<void(const DisruptRecord &rec)>;
    using ClientFoundCb    = std::function<void(const ClientRecord &rec)>;
    using WpsCb            = std::function<void(const WpsRecord &rec)>;
#ifndef POLITICIAN_NO_MSCHAPV2
    using MsChapCb         = std::function<void(const MsChapRecord &rec)>;
#endif
#else
    using EapolCb          = void (*)(const HandshakeRecord &rec);
    using ApFoundCb        = void (*)(const ApRecord &ap);
    using TargetFilterCb   = bool (*)(const ApRecord &ap);
    using TargetScoreCb    = int  (*)(const ApRecord &ap, const char *vendor);
    using PacketCb         = void (*)(const uint8_t *payload, uint16_t len, int8_t rssi, uint8_t channel, uint32_t ts_usec);
    using IdentityCb       = void (*)(const EapIdentityRecord &rec);
    using AttackResultCb   = void (*)(const AttackResultRecord &rec);
    using ProbeRequestCb   = void (*)(const ProbeRequestRecord &rec);
    using DisruptCb        = void (*)(const DisruptRecord &rec);
    using ClientFoundCb    = void (*)(const ClientRecord &rec);
    using WpsCb            = void (*)(const WpsRecord &rec);
#ifndef POLITICIAN_NO_MSCHAPV2
    using MsChapCb         = void (*)(const MsChapRecord &rec);
#endif
#endif

    /**
     * @brief Looks up the vendor name for a given MAC address (OUI).
     * @param mac 6-byte MAC address.
     * @return The vendor string (e.g., "Apple") or an empty string if unknown.
     */
    static const char* getVendor(const uint8_t *mac);

    /**
     * @brief Sets the callback for calculating a custom priority score during autoTarget.
     */
    void setTargetScoreCallback(TargetScoreCb cb) { _targetScoreCb = cb; }

    /**
     * @brief Injects a custom 802.11 frame.
     * @param payload The raw 802.11 frame bytes.
     * @param len Length of the frame.
     * @param channel The 2.4GHz or 5GHz channel to transmit on.
     * @param lock_ms Optional. If > 0, the engine disables hopping and stays on the channel for this duration.
     * @param wait_for_channel If true, the frame is queued until the hopper naturally reaches the channel (stealth). If false, the engine immediately switches to the channel and fires.
     * @return OK on success, or an error code if the queue is full or engine is not initialized.
     */
    Error   injectCustomFrame(const uint8_t *payload, size_t len, uint8_t channel, uint32_t lock_ms = 0, bool wait_for_channel = false);

    /**
     * @brief Sets the callback for when a handshake (EAPOL or PMKID) is captured.
     */
    void setEapolCallback(EapolCb cb)     { _eapolCb = cb; }

    /**
     * @brief Sets the callback for when a new Access Point is discovered.
     */
    void setApFoundCallback(ApFoundCb cb)   { _apFoundCb = cb; }

    /**
     * @brief Sets an early filter callback. If it returns false, the AP is ignored completely.
     */
    void setTargetFilter(TargetFilterCb cb) { _filterCb = cb; }

    /**
     * @brief Sets the callback for raw promiscuous mode packets.
     */
    void setPacketLogger(PacketCb cb)       { _packetCb = cb; }

    /**
     * @brief Sets the callback for passive 802.1X Enterprise Identity harvesting.
     */
    void setIdentityCallback(IdentityCb cb) { _identityCb = cb; }

    /**
     * @brief Sets the callback fired when an attack attempt exhausts all options without capturing.
     * Useful for logging failed targets or adjusting strategy at runtime.
     */
    void setAttackResultCallback(AttackResultCb cb) { _attackResultCb = cb; }

    /**
     * @brief Sets the callback fired on every probe request frame.
     * Exposes the probing client MAC and requested SSID for device history reconstruction.
     */
    void setProbeRequestCallback(ProbeRequestCb cb) { _probeReqCb = cb; }

    /**
     * @brief Sets the callback fired on deauthentication and disassociation frames.
     * Exposes source, destination, BSSID, reason code, and direction for attack/roaming detection.
     */
    void setDisruptCallback(DisruptCb cb)           { _disruptCb = cb; }

    /**
     * @brief Sets the callback fired when a new client (STA) is first seen associated to an AP.
     * Fired at most once per unique BSSID+STA pair (tracked per AP cache entry, up to 4 clients).
     */
    void setClientFoundCallback(ClientFoundCb cb)   { _clientFoundCb = cb; }

    /**
     * @brief Sets the callback fired when a WPS Enrollee's M1 message is captured.
     * The M1 message is the first EAP-WSC frame sent by the Enrollee and is unencrypted,
     * revealing device attributes (name, manufacturer, model, auth/config capabilities).
     * Only fired if the callback is set — has zero overhead otherwise.
     */
    void setWpsCallback(WpsCb cb)               { _wpsCb = cb; }

#ifndef POLITICIAN_NO_MSCHAPV2
    /**
     * @brief Sets the callback fired on a bare EAP-MSCHAPv2 challenge/response exchange.
     * Only fires when an AP serves MSCHAPv2 directly without a TLS tunnel (no PEAP/TTLS).
     * The captured MsChapRecord contains everything needed for offline cracking with
     * asleap or hashcat mode 5500. Zero overhead if not set.
     */
    void setMsChapCallback(MsChapCb cb)         { _msChapCb = cb; }
#endif

    /**
     * @brief Sets the callback fired when a potential evil twin or rogue AP is detected.
     * Triggered when a newly observed BSSID advertises the same SSID as an already-cached AP on the same channel.
     */
    void setRogueApCallback(RogueApCb cb)           { _rogueApCb = cb; }

#ifndef POLITICIAN_NO_KARMA
    /**
     * @brief Enable or disable the KARMA rogue AP responder at runtime.
     *
     * When enabled, the engine intercepts named probe requests (SSIDs != wildcard)
     * and immediately injects a matching probe response + beacon, spoofing an open
     * AP with that SSID. Clients configured to auto-join known networks will then
     * associate with the engine's soft AP.
     *
     * @note Only effective when `cfg.karma_enabled = true` was set in `begin()`, or
     *       when called after `begin()` to toggle dynamically.
     * @param en  true to enable, false to disable
     */
    void enableKarma(bool en) { _karmaEnabled = en; }

    /**
     * @brief Sets the callback fired each time the KARMA responder echoes a probe.
     * The record contains the client MAC, requested SSID, channel, and spoofed AP MAC.
     * Zero overhead if not set.
     */
    void setKarmaCallback(KarmaCb cb) { _karmaCb = cb; }
#endif

    /**
     * @brief Sets an SSID wordlist for directed probes against hidden access points.
     * When set and probe_hidden_interval_ms > 0, the engine cycles through each SSID
     * per hidden AP instead of sending a wildcard probe. Each hidden AP maintains its
     * own position in the wordlist so no word is skipped.
     *
     * The array must remain valid for the lifetime of the engine (use PROGMEM / static storage).
     * Pass nullptr to revert to wildcard-only probing.
     *
     * @param wordlist  Array of null-terminated SSID strings (max 32 chars each)
     * @param count     Number of entries in the array
     */
    void setProbeWordlist(const char * const *wordlist, uint8_t count) {
        _probeWordlist    = wordlist;
        _probeWordlistLen = count;
    }

private:
    static void IRAM_ATTR _promiscuousCb(void *buf, wifi_promiscuous_pkt_type_t type);
    static void _workerTask(void *pvParameters);
    /** Per-process instance registry; populated by begin(), cleared by stop(). */
    static Politician *_instances[POLITICIAN_MAX_INSTANCES];
    /** Set to true after the first successful begin() so subsequent calls skip WiFi driver init. */
    static bool        _wifiInitialized;

    RingbufHandle_t _rb = nullptr;
    TaskHandle_t    _task = nullptr;
    SemaphoreHandle_t _lock = nullptr;

    void _handleFrame(const wifi_promiscuous_pkt_t *pkt, wifi_promiscuous_pkt_type_t type);
    void _handleMgmt(const ieee80211_hdr_t *hdr, const uint8_t *payload, uint16_t len, int8_t rssi);
    void _handleData(const ieee80211_hdr_t *hdr, const uint8_t *payload, uint16_t len, int8_t rssi);
    bool _parseEapol(const uint8_t *bssid, const uint8_t *sta,
                     const uint8_t *eapol, uint16_t len, int8_t rssi);
    void _parseEapIdentity(const uint8_t *bssid, const uint8_t *sta,
                           const uint8_t *eapol, uint16_t len, int8_t rssi);
    void _parseWpsFrame(const uint8_t *bssid, const uint8_t *sta,
                        const uint8_t *eapol, uint16_t len, int8_t rssi);
#ifndef POLITICIAN_NO_MSCHAPV2
    void _parseEapMsChap(const uint8_t *bssid, const uint8_t *sta,
                         const uint8_t *eapol, uint16_t len, int8_t rssi);
#endif
    void _parseSsid(const uint8_t *ie, uint16_t ie_len, char *out, uint8_t &out_len);
    uint8_t _classifyEnc(const uint8_t *ie, uint16_t ie_len);
    uint8_t _classifyPairwiseCipher(const uint8_t *ie, uint16_t ie_len);
    bool _detectWpa3Only(const uint8_t *ie, uint16_t ie_len);
    void _detectPmfFlags(const uint8_t *ie, uint16_t ie_len, bool &pmf_capable, bool &pmf_required);
    bool _detectFt(const uint8_t *ie, uint16_t ie_len);
#ifndef POLITICIAN_NO_KARMA
    void _sendKarmaResponse(const uint8_t *client, const char *ssid, uint8_t ssid_len, uint8_t channel);
#endif

    bool       _initialized = false;
    volatile bool _active;
    uint8_t    _channel;
    uint8_t    _rxChannel;
    bool       _hopping;
    volatile bool _channelTrafficSeen;
    uint32_t   _lastHopMs;
    int8_t     _lastRssi;
    uint8_t    _hopIndex;
    uint8_t    _attackMask;
    DisconnectStrategy _disconnectStrategy;
    uint32_t   _csaFallbackMs;

    static const int MAX_INJECT_QUEUE = 4;
    struct InjectFrame {
        bool     active;
        uint8_t  payload[256];
        uint16_t len;
        uint8_t  channel;
        uint32_t lock_ms;
    };
    InjectFrame _injectQueue[MAX_INJECT_QUEUE];

    static const int MAX_ATTACK_OVERRIDES = 8;
    struct AttackOverride { bool active; uint8_t bssid[6]; uint8_t mask; };
    AttackOverride _attackOverrides[MAX_ATTACK_OVERRIDES];
    struct SsidOverride { bool active; char ssid[33]; uint8_t ssid_len; uint8_t mask; bool substring; };
    static const int MAX_SSID_OVERRIDES = 8;
    SsidOverride _ssidOverrides[MAX_SSID_OVERRIDES];
    struct EapMethodSeen { uint8_t bssid[6]; uint8_t method; };
    static const uint8_t MAX_EAP_METHODS = 8;
    EapMethodSeen _eapMethods[MAX_EAP_METHODS];
    uint8_t       _eapMethodIdx = 0;
    uint8_t _getAttackMask(const uint8_t *bssid) const;

    bool       _hasTarget;
    uint8_t    _targetBssid[6];
    uint8_t    _targetChannel;

    bool       _m1Locked;
    uint32_t   _m1LockEndMs;

    bool       _probeLocked;
    uint32_t   _probeLockEndMs;

    uint8_t    _customChannels[POLITICIAN_MAX_CHANNELS];
    uint8_t    _customChannelCount;
    Config     _cfg;
    Stats      _stats;

    bool             _autoTarget       = false;
    bool             _autoTargetActive = false;

    uint8_t          _lastCapBssid[6]  = {};
    uint8_t          _lastCapSta[6]    = {};
    uint32_t         _lastCapMs        = 0;

    TargetScoreCb    _targetScoreCb   = nullptr;
    LogCb            _logCb           = nullptr;
    ApFoundCb        _apFoundCb       = nullptr;
    TargetFilterCb   _filterCb        = nullptr;
    EapolCb          _eapolCb         = nullptr;
    PacketCb         _packetCb        = nullptr;
    IdentityCb       _identityCb      = nullptr;
    AttackResultCb   _attackResultCb  = nullptr;
    ProbeRequestCb   _probeReqCb      = nullptr;
    DisruptCb        _disruptCb       = nullptr;
    ClientFoundCb    _clientFoundCb   = nullptr;
    RogueApCb        _rogueApCb       = nullptr;
    _FpHookCb        _fpHook          = nullptr;
    WpsCb            _wpsCb           = nullptr;
#ifndef POLITICIAN_NO_KARMA
    KarmaCb          _karmaCb         = nullptr;
    bool             _karmaEnabled    = false;
    // Circular SSID dedup table — avoid spamming one client with repeated responses
    static const int MAX_KARMA_SEEN   = 16;
    struct KarmaSeen { uint8_t client[6]; char ssid[33]; uint32_t last_ms; };
    KarmaSeen        _karmaSeen[MAX_KARMA_SEEN];
    uint8_t          _karmaSeenIdx    = 0;
#endif
#ifndef POLITICIAN_NO_MSCHAPV2
    MsChapCb         _msChapCb        = nullptr;
    // Challenge session table: correlates server challenge (from AP) with response (from client)
    static const int MAX_MSCHAP_SESSIONS = 4;
    struct MsChapSession {
        bool    active;
        uint8_t bssid[6];
        uint8_t ms_id;           // MS-CHAPv2 identifier — ties challenge to response
        uint8_t challenge[16];   // Server challenge from MSCHAPv2 Challenge frame
    };
    MsChapSession _msChapSessions[MAX_MSCHAP_SESSIONS];
#endif

    const char * const * _probeWordlist    = nullptr;
    uint8_t              _probeWordlistLen  = 0;

    void _log(const char *fmt, ...);

    static const int MAX_IGNORE = 128;
    uint8_t _ignoreList[MAX_IGNORE][6];
    uint8_t _ignoreCount;

    static const int MAX_AP_CACHE = POLITICIAN_MAX_AP_CACHE;
    struct ApCacheEntry {
        uint8_t  bssid[6];
        char     ssid[33];
        uint8_t  ssid_len;
        uint8_t  enc;
        uint8_t  channel;
        int8_t   rssi;
        uint32_t first_seen_ms;
        uint32_t last_seen_ms;
        uint32_t last_probe_ms;
        uint32_t last_stimulate_ms;
        uint32_t last_hidden_probe_ms;  // Timestamp of last directed probe for hidden SSID
        uint8_t  known_stas[4][6];      // Up to 4 persistently tracked client MACs
        uint8_t  known_sta_count;
        uint16_t beacon_count;          // Times this AP has been observed
        uint8_t  total_attempts;        // Total failed attack attempts against this AP
        char     country[3];            // IE 7 country code (e.g. "US"), empty if absent
        uint16_t beacon_interval;       // Beacon interval in TUs from fixed fields
        uint8_t  max_rate_mbps;         // Highest rate from Supported Rates IE (Mbps)
        uint16_t sta_count;             // Connected client count from BSS Load
        uint8_t  chan_util;             // Channel utilization (0-255)
        uint8_t  venue_group;           // 802.11u Venue Group
        uint8_t  venue_type;            // 802.11u Venue Type
        uint8_t  network_type;          // 802.11u Access Network Type
        struct {
            uint16_t active             : 1;
            uint16_t has_active_clients : 1;
            uint16_t is_wpa3_only       : 1;
            uint16_t is_hidden          : 1;
            uint16_t wps_enabled        : 1;
            uint16_t pmf_capable        : 1;
            uint16_t pmf_required       : 1;
            uint16_t ft_capable         : 1;
            uint16_t is_vht             : 1;  // 802.11ac capable
            uint16_t is_he              : 1;  // 802.11ax capable
        } flags;
        uint8_t  chan_width;            // Max channel width: 0=20 1=40 2=80 3=160 4=80+80 (MHz)
        uint8_t  probe_word_idx;        // Next wordlist index to probe for this hidden AP
        uint32_t last_attack_ms;        // millis() when the most recent attack was initiated
        uint8_t  capture_count;         // Number of successful captures for this BSSID
        uint8_t  pairwise_cipher;       // CIPHER_TKIP/CIPHER_CCMP/CIPHER_UNKNOWN from RSN IE
    };
    ApCacheEntry _apCache[MAX_AP_CACHE];

    ApCacheEntry* _cacheAp(const uint8_t *bssid, const char *ssid, uint8_t ssid_len,
                  uint8_t enc, uint8_t channel, int8_t rssi,
                  bool is_wpa3_only = false, bool wps = false,
                  bool pmf_capable = false, bool pmf_required = false,
                  bool ft_capable = false, uint16_t sta_count = 0, uint8_t chan_util = 0,
                  uint8_t venue_group = 0, uint8_t venue_type = 0, uint8_t network_type = 0);
    bool _lookupSsid(const uint8_t *bssid, char *out_ssid, uint8_t &out_len) const;
    bool _lookupEnc(const uint8_t *bssid, uint8_t &out_enc) const;
    bool _lookupCipher(const uint8_t *bssid, uint8_t &out_cipher) const;

    enum FishState : uint8_t { FISH_IDLE = 0, FISH_CONNECTING = 1, FISH_CSA_WAIT = 2 };
    FishState _fishState;
    uint32_t  _fishStartMs;
    uint8_t   _fishBssid[6];
    uint8_t   _fishRetry;
    char      _fishSsid[33];
    uint8_t   _fishSsidLen;
    uint8_t   _fishChannel;
    uint8_t   _fishSta[6];     // Known client MAC for unicast deauth (zeros = unknown)
    uint8_t   _ownStaMac[6];
    bool      _fishAuthLogged;
    bool      _fishAssocLogged;
    bool      _csaSecondBurstSent;

    void _startFishing(const uint8_t *bssid, const char *ssid,
                       uint8_t ssid_len, uint8_t channel);
    void _processFishing();
    void _randomizeMac();
    void _sendCsaBurst();
    void _sendDeauthBurst(uint8_t count, const uint8_t *sta = nullptr);
    void _sendBtmRequest(const uint8_t *bssid, const uint8_t *sta);
    void _sendProbeRequest(const uint8_t *bssid, const char *ssid = nullptr, uint8_t ssid_len = 0);
    void _recordClientForAp(const uint8_t *bssid, const uint8_t *sta, int8_t rssi = 0);
    void _markCapturedSsidGroup(const char *ssid, uint8_t ssid_len);
    void _markCaptured(const uint8_t *bssid);
    void _incCaptureCount(const uint8_t *bssid);

    static const int MAX_SESSIONS = POLITICIAN_MAX_SESSIONS;
    struct Session {
        uint8_t      bssid[6];
        uint8_t      sta[6];
        char         ssid[33];
        uint8_t      ssid_len;
        uint8_t      channel;
        int8_t       rssi;
        uint8_t      anonce[32];
        uint8_t      snonce[32];
        uint8_t      m1_replay_counter[8];
        uint8_t      mic[16];
        uint32_t     created_ms;

        // Dynamic EAPOL Buffer (M2, M3, M4 packed)
        uint8_t      eapol_buffer[400];
        uint16_t     m2_off, m2_len;
        uint16_t     m3_off, m3_len;
        uint16_t     m4_off, m4_len;

        struct {
            uint8_t active : 1;
            uint8_t has_m1 : 1;
            uint8_t has_m2 : 1;
            uint8_t has_m3 : 1;
            uint8_t has_m4 : 1;
        } flags;
    };
    Session _sessions[MAX_SESSIONS];

    Session* _findSession(const uint8_t *bssid, const uint8_t *sta);
    Session* _createSession(const uint8_t *bssid, const uint8_t *sta);
    void     _expireSessions(uint32_t timeoutMs);

    static const int MAX_CAPTURED = POLITICIAN_MAX_CAPTURED;
    uint8_t _captured[MAX_CAPTURED][6];
    int     _capturedCount;

    bool _isCaptured(const uint8_t *bssid) const;

    static const uint8_t HOP_SEQ[];
    static const uint8_t HOP_COUNT;
};

} // namespace politician
