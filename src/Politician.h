#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
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
#define MGMT_SUB_AUTH_RESP  0xB0
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
     * @brief Configures which attack techniques are enabled.
     */
    void    setAttackMask(uint8_t mask);

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

    using EapolCb          = void (*)(const HandshakeRecord &rec);
    using ApFoundCb        = void (*)(const ApRecord &ap);
    using TargetFilterCb   = bool (*)(const ApRecord &ap);
    using PacketCb         = void (*)(const uint8_t *payload, uint16_t len, int8_t rssi, uint32_t ts_usec);
    using IdentityCb       = void (*)(const EapIdentityRecord &rec);
    using AttackResultCb   = void (*)(const AttackResultRecord &rec);
    using ProbeRequestCb   = void (*)(const ProbeRequestRecord &rec);
    using DisruptCb        = void (*)(const DisruptRecord &rec);

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

private:
    static void IRAM_ATTR _promiscuousCb(void *buf, wifi_promiscuous_pkt_type_t type);
    static Politician *_instance;

    void _handleFrame(const wifi_promiscuous_pkt_t *pkt, wifi_promiscuous_pkt_type_t type);
    void _handleMgmt(const ieee80211_hdr_t *hdr, const uint8_t *payload, uint16_t len, int8_t rssi);
    void _handleData(const ieee80211_hdr_t *hdr, const uint8_t *payload, uint16_t len, int8_t rssi);
    bool _parseEapol(const uint8_t *bssid, const uint8_t *sta,
                     const uint8_t *eapol, uint16_t len, int8_t rssi);
    void _parseEapIdentity(const uint8_t *bssid, const uint8_t *sta,
                           const uint8_t *eapol, uint16_t len, int8_t rssi);
    void _parseSsid(const uint8_t *ie, uint16_t ie_len, char *out, uint8_t &out_len);
    uint8_t _classifyEnc(const uint8_t *ie, uint16_t ie_len);
    bool _detectWpa3Only(const uint8_t *ie, uint16_t ie_len);

    bool       _initialized = false;
    bool       _active;
    uint8_t    _channel;
    uint8_t    _rxChannel;
    bool       _hopping;
    uint32_t   _lastHopMs;
    int8_t     _lastRssi;
    uint8_t    _hopIndex;
    uint8_t    _attackMask;

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

    LogCb            _logCb           = nullptr;
    ApFoundCb        _apFoundCb       = nullptr;
    TargetFilterCb   _filterCb        = nullptr;
    EapolCb          _eapolCb         = nullptr;
    PacketCb         _packetCb        = nullptr;
    IdentityCb       _identityCb      = nullptr;
    AttackResultCb   _attackResultCb  = nullptr;
    ProbeRequestCb   _probeReqCb      = nullptr;
    DisruptCb        _disruptCb       = nullptr;

    void _log(const char *fmt, ...);

    static const int MAX_IGNORE = 128;
    uint8_t _ignoreList[MAX_IGNORE][6];
    uint8_t _ignoreCount;

    static const int MAX_AP_CACHE = POLITICIAN_MAX_AP_CACHE;
    struct ApCacheEntry {
        bool     active;
        uint8_t  bssid[6];
        char     ssid[33];
        uint8_t  ssid_len;
        uint8_t  enc;
        uint8_t  channel;
        int8_t   rssi;
        uint32_t last_seen_ms;
        uint32_t last_probe_ms;
        uint32_t last_stimulate_ms;
        uint32_t last_hidden_probe_ms;  // Timestamp of last directed probe for hidden SSID
        bool     has_active_clients;
        bool     is_wpa3_only;          // True if PMF is required and no WPA2 AKM offered
        bool     is_hidden;             // True if SSID is blank
        bool     wps_enabled;           // WPS IE detected
        uint8_t  known_stas[4][6];      // Up to 4 persistently tracked client MACs
        uint8_t  known_sta_count;
        uint8_t  beacon_count;          // Times this AP has been observed
        uint8_t  total_attempts;        // Total failed attack attempts against this AP
    };
    ApCacheEntry _apCache[MAX_AP_CACHE];
    int          _apCacheCount;

    void _cacheAp(const uint8_t *bssid, const char *ssid, uint8_t ssid_len,
                  uint8_t enc, uint8_t channel, int8_t rssi,
                  bool is_wpa3_only = false, bool wps = false);
    bool _lookupSsid(const uint8_t *bssid, char *out_ssid, uint8_t &out_len);
    bool _lookupEnc(const uint8_t *bssid, uint8_t &out_enc);

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
    void _sendProbeRequest(const uint8_t *bssid);
    void _recordClientForAp(const uint8_t *bssid, const uint8_t *sta);
    void _markCapturedSsidGroup(const char *ssid, uint8_t ssid_len);
    void _markCaptured(const uint8_t *bssid);

    static const int MAX_SESSIONS = POLITICIAN_MAX_SESSIONS;
    struct Session {
        bool         active;
        uint8_t      bssid[6];
        uint8_t      sta[6];
        char         ssid[33];
        uint8_t      ssid_len;
        uint8_t      channel;
        int8_t       rssi;
        uint8_t      anonce[32];
        uint8_t      m1_replay_counter[8];
        uint8_t      mic[16];
        uint8_t      eapol_m2[256];
        uint16_t     eapol_m2_len;
        bool         has_m1;
        bool         has_m2;
        uint32_t     created_ms;
    };
    Session _sessions[MAX_SESSIONS];

    Session* _findSession(const uint8_t *bssid, const uint8_t *sta);
    Session* _createSession(const uint8_t *bssid, const uint8_t *sta);
    void     _expireSessions(uint32_t timeoutMs);

    static const int MAX_CAPTURED = POLITICIAN_MAX_CAPTURED;
    struct CapturedEntry {
        bool    active;
        uint8_t bssid[6];
    };
    CapturedEntry _captured[MAX_CAPTURED];
    int           _capturedCount;

    bool _isCaptured(const uint8_t *bssid) const;

    static const uint8_t HOP_SEQ[];
    static const uint8_t HOP_COUNT;
};

} // namespace politician
