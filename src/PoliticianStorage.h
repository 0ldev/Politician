#pragma once

#ifndef ARDUINO
#error "PoliticianStorage.h requires the Arduino framework. Use ESP-IDF VFS and nvs_flash APIs directly."
#endif

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <string>
#ifndef POLITICIAN_NO_NETWORK_LOGGER
#ifdef ARDUINO
#include <WiFiClient.h>
#if __has_include(<WiFiUdp.h>)
#include <WiFiUdp.h>
#elif __has_include(<WiFiUDP.h>)
#include <WiFiUDP.h>
#endif
#endif
#endif
#include "Politician.h"
#include "PoliticianFormat.h"

namespace politician {
namespace storage {

namespace detail {
#ifndef POLITICIAN_NO_STD_FUNCTION
using TimestampCb = std::function<const char *()>;
#else
using TimestampCb = const char *(*)();
#endif

inline TimestampCb &_timestampCb() {
    static TimestampCb cb;
    return cb;
}
inline const char *_timestamp() {
    auto &cb = _timestampCb();
    const char *ts = cb ? cb() : nullptr;
    return ts ? ts : "1970-01-01 00:00:00";
}

/**
 * @brief Writes a CSV-safe quoted field into output (RFC 4180).
 * Wraps in double-quotes and escapes embedded double-quotes by doubling them.
 */
inline void escapeCsvField(const char *input, char *output, size_t maxLen) {
    size_t out = 0;
    if (out < maxLen - 1) output[out++] = '"';
    for (size_t i = 0; input[i] && out < maxLen - 2; i++) {
        if (input[i] == '"' && out < maxLen - 2) output[out++] = '"';
        output[out++] = input[i];
    }
    if (out < maxLen - 1) output[out++] = '"';
    output[out] = '\0';
}
} // namespace detail

/**
 * @brief Sets a global timestamp provider called by all CSV loggers when no
 * explicit timestamp string is supplied.
 *
 * The callback should return a pointer to a static or long-lived buffer with a
 * datetime string in Wigle CSV format: "YYYY-MM-DD HH:MM:SS". It is called
 * once per log entry.
 *
 * @code
 * // NTP example
 * storage::setTimestampProvider([]() -> const char* {
 *     static char buf[20];
 *     time_t now = time(nullptr);
 *     strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
 *     return buf;
 * });
 * @endcode
 */
inline void setTimestampProvider(detail::TimestampCb cb) {
    detail::_timestampCb() = cb;
}

/**
 * @brief Helper for writing HandshakeRecords and raw packets to a standard PCAPNG file.
 *
 * Two usage modes:
 *  - **Streaming (recommended for continuous capture):** call open() once, write()/writePacket()
 *    repeatedly, then close(). The file handle stays open across writes, avoiding repeated
 *    open/close overhead and SD wear.
 *  - **One-shot (legacy):** use the static append() / appendPacket() helpers which open and
 *    close the file on every call.
 */
class PcapngFileLogger {
public:
    PcapngFileLogger() : _fs(nullptr), _open(false) {}

    ~PcapngFileLogger() { close(); }

    /**
     * @brief Opens the PCAPNG file for streaming writes.
     * Writes the global Section Header Block if the file is new or empty.
     *
     * @param fs   The filesystem (e.g., SD, LittleFS)
     * @param path The file path (e.g., "/captures.pcapng")
     * @return true if the file was opened successfully
     */
    bool open(fs::FS &fs, const char *path, uint32_t maxBytes = 0) {
        if (_open) close();
        _fs = &fs;
        _maxBytes = maxBytes;
        _fileIdx = 0;
        if (path) {
            strncpy(_basePath, path, sizeof(_basePath) - 1);
            _basePath[sizeof(_basePath) - 1] = '\0';
        } else {
            _basePath[0] = '\0';
        }

        bool isNew = !fs.exists(path);
        if (!isNew) {
            fs::File check = fs.open(path, FILE_READ);
            if (check) { isNew = (check.size() == 0); check.close(); }
        }

        _file = fs.open(path, FILE_APPEND);
        if (!_file) { _fs = nullptr; return false; }

        if (isNew) {
            uint8_t hdr[48];
            size_t hl = format::writePcapngGlobalHeader(hdr);
            _file.write(hdr, hl);
        }
        _open = true;
        return true;
    }

    /**
     * @brief Writes a HandshakeRecord to the open file.
     * @return true if data was written, false if the logger is not open or serialization failed
     */
    bool write(const HandshakeRecord &rec) {
        if (!_open) return false;
        _checkRotate();
        if (!_open) return false;
        uint8_t buf[512];
        size_t len = format::writePcapngRecord(rec, buf, sizeof(buf));
        if (len > 0) { _file.write(buf, len); _file.flush(); }
        return len > 0;
    }

    /**
     * @brief Writes a raw 802.11 sniffer frame to the open file.
     * @return true if data was written, false if the logger is not open or serialization failed
     */
    bool writePacket(const uint8_t *payload, uint16_t len, int8_t rssi, uint8_t channel, uint32_t ts_usec) {
        if (!_open) return false;
        _checkRotate();
        if (!_open) return false;
        uint8_t buf[2500]; // Max 802.11 frame is 2346 bytes
        size_t wlen = format::writePcapngPacket(payload, len, rssi, channel, ts_usec, buf, sizeof(buf));
        if (wlen > 0) { _file.write(buf, wlen); _file.flush(); }
        return wlen > 0;
    }

    /** @brief Closes the underlying file handle. Safe to call multiple times. */
    void close() {
        if (_open) { _file.close(); _open = false; _fs = nullptr; }
    }

    /** @brief Returns true if the file is currently open for streaming. */
    bool isOpen() const { return _open; }

    // ── Static one-shot helpers (backward compatible) ──────────────────────────

    /**
     * @brief Appends a HandshakeRecord to a file as PCAPNG (opens and closes per call).
     * Prefer the streaming API (open/write/close) for continuous capture.
     */
    static bool append(fs::FS &fs, const char *path, const HandshakeRecord &rec) {
        PcapngFileLogger logger;
        return logger.open(fs, path) && logger.write(rec);
    }

    /**
     * @brief Appends a raw 802.11 sniffer frame to a PCAPNG file (opens and closes per call).
     * Prefer the streaming API (open/writePacket/close) for continuous capture.
     */
    static bool appendPacket(fs::FS &fs, const char *path, const uint8_t *payload, uint16_t len,
                             int8_t rssi, uint8_t channel, uint32_t ts_usec) {
        PcapngFileLogger logger;
        return logger.open(fs, path) && logger.writePacket(payload, len, rssi, channel, ts_usec);
    }

private:
    void _checkRotate() {
        if (!_open || _maxBytes == 0) return;
        if ((uint32_t)_file.size() < _maxBytes) return;
        _file.close();
        _fileIdx++;
        char rotPath[80];
        const char *dot = strrchr(_basePath, '.');
        if (dot) {
            size_t base_len = (size_t)(dot - _basePath);
            snprintf(rotPath, sizeof(rotPath), "%.*s_%02u%s",
                     (int)base_len, _basePath, _fileIdx, dot);
        } else {
            snprintf(rotPath, sizeof(rotPath), "%s_%02u", _basePath, _fileIdx);
        }
        _file = _fs->open(rotPath, FILE_WRITE);
        if (_file) {
            uint8_t hdr[48];
            size_t hl = format::writePcapngGlobalHeader(hdr);
            _file.write(hdr, hl);
        } else {
            _open = false;
        }
    }

    fs::FS  *_fs;
    fs::File _file;
    bool     _open;
    uint32_t _maxBytes = 0;
    char     _basePath[64] = {};
    uint8_t  _fileIdx = 0;
};

/**
 * @brief Helper for writing HandshakeRecords to an HC22000 text file.
 *
 * Supports both streaming (open/write/close) and one-shot (static append()) usage.
 */
class Hc22000FileLogger {
public:
    Hc22000FileLogger() : _open(false) {}

    ~Hc22000FileLogger() { close(); }

    /**
     * @brief Opens the HC22000 file for streaming writes.
     * @return true if successful
     */
    bool open(fs::FS &fs, const char *path) {
        if (_open) close();
        _file = fs.open(path, FILE_APPEND);
        if (!_file) return false;
        _open = true;
        return true;
    }

    /**
     * @brief Writes a HandshakeRecord to the open file.
     * @return true if data was written
     */
    bool write(const HandshakeRecord &rec) {
        if (!_open) return false;
        std::string str = format::toHC22000(rec);
        if (!str.empty()) { _file.println(str.c_str()); _file.flush(); }
        return !str.empty();
    }

    /** @brief Closes the underlying file handle. Safe to call multiple times. */
    void close() {
        if (_open) { _file.close(); _open = false; }
    }

    /** @brief Returns true if the file is currently open for streaming. */
    bool isOpen() const { return _open; }

    /**
     * @brief Appends a HandshakeRecord to a file as an HC22000 string (opens and closes per call).
     * Prefer the streaming API (open/write/close) for continuous capture.
     */
    static bool append(fs::FS &fs, const char *path, const HandshakeRecord &rec) {
        Hc22000FileLogger logger;
        return logger.open(fs, path) && logger.write(rec);
    }

private:
    fs::File _file;
    bool     _open;
};

/**
 * @brief Helper for writing precise GPS location coordinates to a Wigle.net compatible CSV file.
 *
 * Wigle.net has a strict CSV format starting with a specific header:
 * MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type
 */
class WigleCsvLogger {
public:
    /**
     * @brief Appends a HandshakeRecord's details alongside GPS coordinates to a Wigle CSV.
     *
     * @param fs   The filesystem (e.g., SD, LittleFS)
     * @param path The path to the file (e.g., "/wardrive.csv")
     * @param rec  The captured HandshakeRecord
     * @param lat  Current GPS Latitude
     * @param lon  Current GPS Longitude
     * @param alt  (Optional) Current GPS Altitude in meters
     * @param acc  (Optional) GPS Accuracy radius in meters
     * @return true if successful, false if file could not be opened
     */
    static bool append(fs::FS &fs, const char* path, const HandshakeRecord& rec,
                       float lat, float lon, float alt = 0.0, float acc = 10.0,
                       const char* timestamp = nullptr) {
        fs::File file = _openWithHeader(fs, path);
        if (!file) return false;

        char ssidEscaped[72]; // 32 chars worst-case doubled + 2 quotes + NUL
        detail::escapeCsvField(rec.ssid, ssidEscaped, sizeof(ssidEscaped));

        char line[256];
        snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X,%s,%s,%s,%d,%d,%.6f,%.6f,%.1f,%.1f,WIFI",
                 rec.bssid[0], rec.bssid[1], rec.bssid[2], rec.bssid[3], rec.bssid[4], rec.bssid[5],
                 ssidEscaped, _authStr(rec.enc), timestamp ? timestamp : detail::_timestamp(),
                 rec.channel, rec.rssi, lat, lon, alt, acc);

        file.println(line);
        file.flush();
        file.close();
        return true;
    }

    /**
     * @brief Appends any discovered ApRecord alongside GPS coordinates to a Wigle CSV.
     * Use this with setApFoundCallback() to log all networks, not just captured ones.
     *
     * @param fs   The filesystem (e.g., SD, LittleFS)
     * @param path The path to the file (e.g., "/wardrive.csv")
     * @param ap   The discovered ApRecord
     * @param lat  Current GPS Latitude
     * @param lon  Current GPS Longitude
     * @param alt  (Optional) Current GPS Altitude in meters
     * @param acc  (Optional) GPS Accuracy radius in meters
     * @return true if successful, false if file could not be opened
     */
    static bool appendAp(fs::FS &fs, const char* path, const ApRecord& ap,
                         float lat, float lon, float alt = 0.0, float acc = 10.0,
                         const char* timestamp = nullptr) {
        fs::File file = _openWithHeader(fs, path);
        if (!file) return false;

        char ssidEscaped[72];
        detail::escapeCsvField(ap.ssid, ssidEscaped, sizeof(ssidEscaped));

        char line[256];
        snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X,%s,%s,%s,%d,%d,%.6f,%.6f,%.1f,%.1f,WIFI",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
                 ssidEscaped, _authStr(ap.enc), timestamp ? timestamp : detail::_timestamp(),
                 ap.channel, ap.rssi, lat, lon, alt, acc);

        file.println(line);
        file.flush();
        file.close();
        return true;
    }

private:
    static const char* _authStr(uint8_t enc) {
        switch (enc) {
            case 1:  return "[WEP][ESS]";
            case 2:  return "[WPA-PSK-TKIP][ESS]";
            case 3:  return "[WPA2-PSK-CCMP][ESS]";
            case 4:  return "[WPA2-EAP-CCMP][ESS]";
            default: return "[ESS]";
        }
    }

    static fs::File _openWithHeader(fs::FS &fs, const char* path) {
        bool isNew = !fs.exists(path);
        if (!isNew) {
            fs::File check = fs.open(path, FILE_READ);
            if (check) { isNew = (check.size() == 0); check.close(); }
        }
        fs::File file = fs.open(path, FILE_APPEND);
        if (!file) return file;
        if (isNew) {
            file.println("WigleWifi-1.4,appRelease=1.0,model=Politician,release=1.0,device=ESP32,display=1.0,board=ESP32,brand=Espressif");
            file.println("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type");
        }
        return file;
    }
};

/**
 * @brief Helper for logging harvested 802.1X Enterprise Credentials.
 * It writes a Clean CSV file containing BSSID, Client, and Plaintext Identity.
 */
class EnterpriseCsvLogger {
public:
    static bool append(fs::FS &fs, const char *path, const EapIdentityRecord &rec,
                       const char *timestamp = nullptr) {
        bool isNew = !fs.exists(path);
        
        fs::File file = fs.open(path, FILE_APPEND);
        if (!file) return false;

        if (isNew) {
            file.println("Enterprise BSSID,Client MAC,Plaintext Identity,EAP Method,FirstSeen,Channel,RSSI");
        }

        char identityEscaped[136]; // 64 chars worst-case doubled + 2 quotes + NUL
        detail::escapeCsvField(rec.identity, identityEscaped, sizeof(identityEscaped));

        char line[256];
        snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X,%02X:%02X:%02X:%02X:%02X:%02X,%s,%u,%s,%d,%d",
                 rec.bssid[0], rec.bssid[1], rec.bssid[2], rec.bssid[3], rec.bssid[4], rec.bssid[5],
                 rec.client[0], rec.client[1], rec.client[2], rec.client[3], rec.client[4], rec.client[5],
                 identityEscaped, rec.eap_method, timestamp ? timestamp : detail::_timestamp(),
                 rec.channel, rec.rssi);

        file.println(line);
        file.flush();
        file.close();
        return true;
    }
};

/**
 * @brief Helper for persistently storing captured BSSIDs in NVS memory.
 * This ensures that previously captured networks aren't attacked again after a reboot.
 */
class NvsBssidCache {
private:
    Preferences _prefs;
    char _ns[16];
    static const int MAX_STORED = 128;
    uint8_t _cache[MAX_STORED][6];
    size_t _count;
    bool   _dirty;

public:
    NvsBssidCache(const char* ns = "wardrive") : _count(0), _dirty(false) {
        if (strlen(ns) > 15)
            Serial.println("[NvsBssidCache] WARNING: namespace name exceeds 15 chars and will be truncated");
        strncpy(_ns, ns, sizeof(_ns) - 1);
        _ns[sizeof(_ns) - 1] = '\0';
        memset(_cache, 0, sizeof(_cache));
    }

    /**
     * @brief Initializes the NVS memory and loads the cached BSSIDs into RAM.
     */
    void begin() {
        _prefs.begin(_ns, false);
        size_t bytes = _prefs.getBytes("bssids", _cache, sizeof(_cache));
        _count = bytes / 6;
        if (_count > MAX_STORED) _count = MAX_STORED; // Safety parameter
        _dirty = false;
    }

    /**
     * @brief Feeds the loaded BSSIDs into the Politician engine so it knows to ignore them.
     * @param engine Reference to your active Politician instance
     */
    void loadInto(Politician& engine) {
        for (size_t i = 0; i < _count; i++) {
            engine.markCaptured(_cache[i]);
        }
    }

    /**
     * @brief Adds a newly captured BSSID to the in-RAM cache.
     * The change is not written to NVS until flush() is called.
     * @param bssid The 6-byte BSSID to save.
     * @return true if added, false if it already exists or the cache is full.
     */
    bool add(const uint8_t* bssid) {
        for (size_t i = 0; i < _count; i++) {
            if (memcmp(_cache[i], bssid, 6) == 0) return false; // Already cached
        }
        if (_count >= MAX_STORED) return false; // Cache full

        memcpy(_cache[_count], bssid, 6);
        _count++;
        _dirty = true;
        return true;
    }

    /**
     * @brief Writes any pending in-RAM changes to NVS.
     * Call this periodically (e.g., in end() or on a timer) to batch flash writes.
     * @return true if data was written, false if there were no pending changes.
     */
    bool flush() {
        if (!_dirty) return false;
        _prefs.putBytes("bssids", _cache, _count * 6);
        _dirty = false;
        return true;
    }

    /** @brief Returns true if there are in-RAM changes not yet written to NVS. */
    bool isDirty() const { return _dirty; }

    /**
     * @brief Returns the number of BSSIDs currently stored.
     */
    size_t count() const { return _count; }

    /**
     * @brief Clears the entire cache from both RAM and NVS immediately.
     */
    void clear() {
        _count = 0;
        _dirty = false;
        _prefs.remove("bssids");
    }
};

// Gate the network loggers — they require WiFi.h which may not be available in all environments
#ifndef POLITICIAN_NO_NETWORK_LOGGER
#ifdef ARDUINO

/**
 * @brief Streams HandshakeRecords as raw PCAPNG data over an existing TCP connection.
 *
 * Designed for wardrive deployments where SD is unavailable but a laptop on the
 * same network can run Wireshark with `-i TCP@<ip>:<port>` or a raw receiver.
 */
class TcpStreamLogger {
public:
    TcpStreamLogger() : _client(nullptr), _open(false) {}

    bool connect(WiFiClient &client) {
        if (!client.connected()) return false;
        _client = &client;
        uint8_t hdr[48];
        size_t hl = format::writePcapngGlobalHeader(hdr);
        _client->write(hdr, hl);
        _open = true;
        return true;
    }

    bool write(const HandshakeRecord &rec) {
        if (!_open || !_client || !_client->connected()) { _open = false; return false; }
        uint8_t buf[512];
        size_t len = format::writePcapngRecord(rec, buf, sizeof(buf));
        if (len > 0) { _client->write(buf, len); return true; }
        return false;
    }

    void close() {
        if (_open && _client) { _client->stop(); }
        _open = false;
        _client = nullptr;
    }

    bool isConnected() const { return _open && _client && _client->connected(); }

private:
    WiFiClient *_client;
    bool        _open;
};

/**
 * @brief Sends HandshakeRecords as PCAPNG Enhanced Packet Blocks over UDP.
 *
 * UDP datagrams have a 1472-byte payload limit on typical networks. Records that
 * exceed this limit are silently dropped. Suitable for LAN-local collection servers
 * where packet loss is acceptable and TCP session management is undesirable.
 */
class UdpStreamLogger {
public:
    UdpStreamLogger() : _udp(nullptr), _host(nullptr), _port(0), _open(false) {}

    bool begin(WiFiUDP &udp, const char *host, uint16_t port) {
        _udp = &udp;
        _host = host;
        _port = port;
        _open = true;
        return true;
    }

    bool write(const HandshakeRecord &rec) {
        if (!_open || !_udp) return false;
        uint8_t buf[512];
        size_t len = format::writePcapngRecord(rec, buf, sizeof(buf));
        if (len == 0 || len > 1472) return false;
        _udp->beginPacket(_host, _port);
        _udp->write(buf, len);
        return _udp->endPacket() != 0;
    }

    void close() {
        _open = false;
        _udp = nullptr;
    }

    bool isOpen() const { return _open; }

private:
    WiFiUDP    *_udp;
    const char *_host;
    uint16_t    _port;
    bool        _open;
};

#endif // ARDUINO
#endif // POLITICIAN_NO_NETWORK_LOGGER

} // namespace storage
} // namespace politician
