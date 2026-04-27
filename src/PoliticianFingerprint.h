#pragma once
#include "Politician.h"
#include <string.h>

/**
 * @brief PoliticianFingerprint: Passive WiFi Device Fingerprinting
 *
 * Identifies devices by matching observed MAC OUIs, probe request SSID
 * patterns, HT Capabilities, Supported Rates, and IE presence flags against a
 * built-in database. Fires a callback once per unique device per session
 * (seen-MAC cache). RSSI is silently refreshed on re-sightings.
 *
 * DATABASE TIERS — define before including this header, or in build_flags:
 *   -DPOLITICIAN_FP_DB=FP_DB_BUILTIN curated WiFi consumer devices [default]
 *   -DPOLITICIAN_FP_DB=FP_DB_NONE    no built-ins; user-defined entries only
 *
 * CAPACITY OVERRIDES (build_flags):
 *   -DPOLITICIAN_MAX_FP_USER=N   max user-defined fingerprints [default 16]
 *   -DPOLITICIAN_MAX_FP_SEEN=N   seen-MAC dedup cache size     [default 64]
 *
 * USAGE:
 *   #include <PoliticianFingerprint.h>
 *   fingerprint::Detector fp(engine);
 *   fp.setCallback([](const DeviceRecord& dev) {
 *       Serial.printf("[FP] %s %s conf=%d%% flags=0x%02X\n",
 *                     dev.vendor, dev.model, dev.confidence, dev.match_flags);
 *   });
 *   fp.setMinConfidence(60);
 *   fp.addFingerprint({"Acme", "Plug", {0xAA,0xBB,0xCC}, nullptr, 75});
 *
 * NOTE: Include in a single translation unit (your main sketch).
 */

// ─── Database Tier Defines ────────────────────────────────────────────────────
#define FP_DB_NONE     0
#define FP_DB_BUILTIN  1

#ifndef POLITICIAN_FP_DB
#define POLITICIAN_FP_DB FP_DB_BUILTIN
#endif

#if POLITICIAN_FP_DB > FP_DB_NONE
#include "PoliticianFingerprintDB.h"
#endif

namespace politician {
namespace fingerprint {

#ifndef POLITICIAN_MAX_FP_USER
#define POLITICIAN_MAX_FP_USER 16
#endif

#ifndef POLITICIAN_MAX_FP_SEEN
#define POLITICIAN_MAX_FP_SEEN 64
#endif

// ─── Callback Type ────────────────────────────────────────────────────────────
using DeviceFoundCb = void (*)(const DeviceRecord &rec);

// ─── Detector ─────────────────────────────────────────────────────────────────
class Detector {
public:
    explicit Detector(Politician &engine) {
        _inst        = this;
        _userFpCount = 0;
        _seenHead    = 0;
        _seenFill    = 0;
        memset(_seen,    0, sizeof(_seen));
        memset(_userFps, 0, sizeof(_userFps));
        engine._setFingerprintHook(_hook);
    }

    void setCallback(DeviceFoundCb cb)   { _cb = cb; }
    void setMinConfidence(uint8_t pct) { _minConf = pct; }

    bool addFingerprint(const DeviceFingerprint &fp) {
        if (_userFpCount >= POLITICIAN_MAX_FP_USER) return false;
        _userFps[_userFpCount++] = fp;
        return true;
    }

    void resetCache() {
        memset(_seen, 0, sizeof(_seen));
        _seenHead = 0;
        _seenFill = 0;
    }

private:
    // ── IE signal container (parsed once per frame) ───────────────────────────
    struct IeSignals {
        bool    has_ht;
        uint8_t ht_cap_info[2];
        bool    has_ext_cap;
        bool    has_wmm;
        bool    has_wps;
        uint8_t rate_sig[4];
        bool    has_rates;
    };

    static IeSignals _parseIe(const uint8_t *ie, uint16_t ie_len) {
        IeSignals s = {};
        if (!ie || !ie_len) return s;
        uint16_t pos = 0;
        while (pos + 2 <= ie_len) {
            uint8_t tag = ie[pos];
            uint8_t len = ie[pos + 1];
            if (pos + 2 + (uint16_t)len > ie_len) break;
            const uint8_t *body = ie + pos + 2;
            switch (tag) {
                case 1:   // Supported Rates
                    if (len >= 4) { s.has_rates = true; memcpy(s.rate_sig, body, 4); }
                    break;
                case 45:  // HT Capabilities
                    if (len >= 2) { s.has_ht = true; s.ht_cap_info[0] = body[0]; s.ht_cap_info[1] = body[1]; }
                    break;
                case 127: // Extended Capabilities
                    s.has_ext_cap = true;
                    break;
                case 221: // Vendor Specific
                    if (len >= 4 && body[0] == 0x00 && body[1] == 0x50 && body[2] == 0xF2) {
                        if (body[3] == 0x01) s.has_wmm = true;
                        if (body[3] == 0x04) s.has_wps = true;
                    }
                    break;
            }
            pos += 2 + len;
        }
        return s;
    }

    static void _hook(const uint8_t *mac, const char *ssid, uint8_t ssid_len,
                      uint8_t ch, int8_t rssi, const uint8_t *ie, uint16_t ie_len) {
        if (_inst) _inst->_process(mac, ssid, ssid_len, ch, rssi, ie, ie_len);
    }

    void _process(const uint8_t *mac, const char *ssid, uint8_t ssid_len,
                  uint8_t ch, int8_t rssi, const uint8_t *ie, uint16_t ie_len) {
        if (mac[0] & 0x02) return; // skip randomized MACs

        int idx = _findSeen(mac);
        if (idx >= 0) { _seen[idx].rssi = rssi; return; } // silent RSSI refresh

        IeSignals sig = _parseIe(ie, ie_len);

        const DeviceFingerprint *best  = nullptr;
        uint8_t               bestConf  = 0;
        uint8_t               bestFlags = 0;

        auto scan = [&](const DeviceFingerprint *tbl, size_t cnt) {
            for (size_t i = 0; i < cnt; i++) {
                const DeviceFingerprint &f = tbl[i];
                if (memcmp(f.oui, mac, 3) != 0) continue;

                uint8_t conf  = f.confidence;
                uint8_t flags = FP_MATCH_OUI;

                // Probe SSID prefix match → +20
                if (f.probeSsid && ssid_len > 0) {
                    size_t plen = strlen(f.probeSsid);
                    if ((size_t)ssid_len >= plen && memcmp(ssid, f.probeSsid, plen) == 0) {
                        conf = (conf + 20u > 100u) ? 100u : conf + 20u;
                        flags |= FP_MATCH_PROBE_SSID;
                    }
                }

                // HT Cap Info match → +15
                if (ie && (f.ht_cap_mask[0] || f.ht_cap_mask[1]) && sig.has_ht) {
                    if ((sig.ht_cap_info[0] & f.ht_cap_mask[0]) == (f.ht_cap_info[0] & f.ht_cap_mask[0]) &&
                        (sig.ht_cap_info[1] & f.ht_cap_mask[1]) == (f.ht_cap_info[1] & f.ht_cap_mask[1])) {
                        conf = (conf + 15u > 100u) ? 100u : conf + 15u;
                        flags |= FP_MATCH_HT_CAP;
                    }
                }

                // Supported Rates signature match → +10
                if (ie && f.rate_sig[0] && sig.has_rates && memcmp(sig.rate_sig, f.rate_sig, 4) == 0) {
                    conf = (conf + 10u > 100u) ? 100u : conf + 10u;
                    flags |= FP_MATCH_RATES;
                }

                // IE flags match → +5
                if (ie && f.ie_flags_mask) {
                    uint8_t obs = 0;
                    if (!sig.has_ht)      obs |= FP_IEF_NO_HT;
                    if (!sig.has_ext_cap) obs |= FP_IEF_NO_EXT_CAP;
                    if (sig.has_wmm)      obs |= FP_IEF_HAS_WMM;
                    if (sig.has_wps)      obs |= FP_IEF_HAS_WPS;
                    if ((obs & f.ie_flags_mask) == (f.ie_flags & f.ie_flags_mask)) {
                        conf = (conf + 5u > 100u) ? 100u : conf + 5u;
                        flags |= FP_MATCH_IE_FLAGS;
                    }
                }

                if (conf > bestConf) { bestConf = conf; bestFlags = flags; best = &f; }
            }
        };

#if POLITICIAN_FP_DB > FP_DB_NONE
        scan(_FP_BUILTIN_DB, _FP_BUILTIN_DB_COUNT);
#endif
        scan(_userFps, _userFpCount);

        if (!best || bestConf < _minConf) return;

        _markSeen(mac, rssi);

        if (!_cb) return;
        DeviceRecord rec;
        memset(&rec, 0, sizeof(rec));
        memcpy(rec.mac, mac, 6);
        strncpy(rec.vendor, best->vendor ? best->vendor : "", sizeof(rec.vendor) - 1);
        if (best->model) strncpy(rec.model, best->model, sizeof(rec.model) - 1);
        rec.channel     = ch;
        rec.rssi        = rssi;
        rec.confidence  = bestConf;
        rec.match_flags = bestFlags;
        _cb(rec);
    }

    int _findSeen(const uint8_t *mac) const {
        for (int i = 0; i < _seenFill; i++)
            if (memcmp(_seen[i].mac, mac, 6) == 0) return i;
        return -1;
    }

    void _markSeen(const uint8_t *mac, int8_t rssi) {
        _seen[_seenHead].active = true;
        memcpy(_seen[_seenHead].mac, mac, 6);
        _seen[_seenHead].rssi = rssi;
        _seenHead = (_seenHead + 1) % POLITICIAN_MAX_FP_SEEN;
        if (_seenFill < POLITICIAN_MAX_FP_SEEN) _seenFill++;
    }

    inline static Detector *_inst = nullptr;

    DeviceFoundCb _cb      = nullptr;
    uint8_t     _minConf = 50;

    DeviceFingerprint _userFps[POLITICIAN_MAX_FP_USER];
    uint8_t        _userFpCount;

    struct SeenEntry { bool active; uint8_t mac[6]; int8_t rssi; };
    SeenEntry _seen[POLITICIAN_MAX_FP_SEEN];
    uint8_t   _seenHead;
    uint8_t   _seenFill;
};

} // namespace fingerprint
} // namespace politician
