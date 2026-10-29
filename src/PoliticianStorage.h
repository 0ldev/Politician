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
    PcapngFileLogger() : _fs(nullptr), _open(false), _autoFlush(true), _maxBytes(0), _bytesWritten(0), _fileIdx(0) {}

    ~PcapngFileLogger() { close(); }

    /**
     * @brief Enables or disables automatic SD/flash flushing after every write.
     *
     * Disabling auto-flush (`setAutoFlush(false)`) avoids synchronous FAT table
     * updates on every packet during high-rate captures. Call `flush()` or `close()`
     * periodically from the main loop to commit buffered data to storage.
     *
     * @param enable true to flush after every write (default), false to buffer in VFS.
     */
    void setAutoFlush(bool enable) { _autoFlush = enable; }

    /**
     * @brief Flushes any buffered PCAPNG data to the underlying storage medium.
     */
    void flush() {
        if (_open) _file.flush();
    }

    /**
     * @brief Opens the PCAPNG file for streaming writes.
     * Writes the global Section Header Block if the file is new or empty.
     *
     * @param fs       The filesystem (e.g., SD, LittleFS)
     * @param path     The file path (e.g., "/captures.pcapng")
     * @param maxBytes Optional maximum file size in bytes before rotating to a new file (0 = no rotation)
     * @return true if the file was opened successfully
     */
    bool open(fs::FS &fs, const char *path, uint32_t maxBytes = 0) {
        if (_open) close();
        _fs = &fs;
        _maxBytes = maxBytes;
        _bytesWritten = 0;
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
            if (check) {
                _bytesWritten = (uint32_t)check.size();
                isNew = (_bytesWritten == 0);
                check.close();
            }
        }

        _file = fs.open(path, FILE_APPEND);
        if (!_file) { _fs = nullptr; return false; }

        if (isNew) {
            uint8_t hdr[48];
            size_t hl = format::writePcapngGlobalHeader(hdr);
            if (hl > 0) {
                _file.write(hdr, hl);
                _bytesWritten += (uint32_t)hl;
            }
        }
        _open = true;
        return true;
    }

    /**
     * @brief Writes a HandshakeRecord to the open file.
     * @param rec The captured HandshakeRecord to serialize and write.
     * @return true if data was written, false if the logger is not open or serialization failed
     */
    bool write(const HandshakeRecord &rec) {
        if (!_open) return false;
        _checkRotate();
        if (!_open) return false;
        uint8_t buf[1024];
        size_t len = format::writePcapngRecord(rec, buf, sizeof(buf));
        if (len > 0) {
            _file.write(buf, len);
            _bytesWritten += (uint32_t)len;
            if (_autoFlush) _file.flush();
        }
        return len > 0;
    }

    /**
     * @brief Writes a raw 802.11 sniffer frame to the open file without large stack allocations.
     * @param payload Raw 802.11 frame bytes.
     * @param len     Length of @p payload in bytes.
     * @param rssi    Received signal strength in dBm.
     * @param channel 802.11 channel number.
     * @param ts_usec Hardware timestamp in microseconds.
     * @return true if data was written, false if the logger is not open or serialization failed
     */
    bool writePacket(const uint8_t *payload, uint16_t len, int8_t rssi, uint8_t channel, uint32_t ts_usec) {
#ifndef POLITICIAN_NO_PCAPNG
        if (!_open || !payload || len == 0) return false;
        _checkRotate();
        if (!_open) return false;

        uint16_t freq = 0;
        uint16_t chan_flags = 0;
        if (channel <= 14) {
            freq = 2407 + (channel * 5);
            if (channel == 14) freq = 2484;
            chan_flags = 0x00A0;
        } else {
            freq = 5000 + (channel * 5);
            chan_flags = 0x0140;
        }

        uint32_t rt_len = 14;
        uint32_t cap_len = (uint32_t)len + rt_len;
        uint32_t pkt_len = cap_len;
        uint32_t aligned_pkt_len = (cap_len + 3u) & ~3u;
        uint32_t pkt_padding = aligned_pkt_len - cap_len;
        uint32_t opt_len = 20;
        uint32_t block_len = 32 + aligned_pkt_len + opt_len;

        uint8_t hdr[42];
        uint32_t epb_type = 0x00000006;
        uint32_t interface_id = 0;
        uint64_t ts64 = (uint64_t)ts_usec;
        uint32_t ts_high = (uint32_t)(ts64 >> 32);
        uint32_t ts_low  = (uint32_t)(ts64 & 0xFFFFFFFFu);

        memcpy(hdr + 0,  &epb_type, 4);
        memcpy(hdr + 4,  &block_len, 4);
        memcpy(hdr + 8,  &interface_id, 4);
        memcpy(hdr + 12, &ts_high, 4);
        memcpy(hdr + 16, &ts_low, 4);
        memcpy(hdr + 20, &cap_len, 4);
        memcpy(hdr + 24, &pkt_len, 4);

        // Radiotap header (14 bytes) at hdr + 28
        hdr[28] = 0x00; hdr[29] = 0x00;
        hdr[30] = 0x0E; hdr[31] = 0x00;
        hdr[32] = 0x28; hdr[33] = 0x00; hdr[34] = 0x00; hdr[35] = 0x00;
        hdr[36] = (uint8_t)(freq & 0xFF);       hdr[37] = (uint8_t)(freq >> 8);
        hdr[38] = (uint8_t)(chan_flags & 0xFF); hdr[39] = (uint8_t)(chan_flags >> 8);
        hdr[40] = (uint8_t)rssi;
        hdr[41] = 0x00;

        uint8_t tail[28];
        memset(tail, 0, sizeof(tail));
        size_t tpos = pkt_padding;
        uint16_t opt_code = 0x8001; uint16_t opt_vlen = 1;
        memcpy(tail + tpos, &opt_code, 2); tpos += 2;
        memcpy(tail + tpos, &opt_vlen, 2); tpos += 2;
        tail[tpos++] = (uint8_t)rssi;
        tpos += 3;

        opt_code = 0x8002; opt_vlen = 1;
        memcpy(tail + tpos, &opt_code, 2); tpos += 2;
        memcpy(tail + tpos, &opt_vlen, 2); tpos += 2;
        tail[tpos++] = channel;
        tpos += 3;

        opt_code = 0x0000; opt_vlen = 0;
        memcpy(tail + tpos, &opt_code, 2); tpos += 2;
        memcpy(tail + tpos, &opt_vlen, 2); tpos += 2;

        memcpy(tail + tpos, &block_len, 4); tpos += 4;

        _file.write(hdr, sizeof(hdr));
        _file.write(payload, len);
        _file.write(tail, tpos);
        _bytesWritten += block_len;
        if (_autoFlush) _file.flush();
        return true;
#else
        (void)payload; (void)len; (void)rssi; (void)channel; (void)ts_usec;
        return false;
#endif
    }

    /** @brief Closes the underlying file handle. Safe to call multiple times. */
    void close() {
        if (_open) { _file.flush(); _file.close(); _open = false; _fs = nullptr; }
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
        if (_bytesWritten < _maxBytes) return;
        _file.flush();
        _file.close();
        _fileIdx++;
        char rotPath[80];
        const char *dot = strrchr(_basePath, '.');
        if (dot) {
            size_t base_len = (size_t)(dot - _basePath);
            snprintf(rotPath, sizeof(rotPath), "%.*s_%02u%s",
                     (int)base_len, _basePath, (unsigned)_fileIdx, dot);
        } else {
            snprintf(rotPath, sizeof(rotPath), "%s_%02u", _basePath, (unsigned)_fileIdx);
        }
        _file = _fs->open(rotPath, FILE_WRITE);
        _bytesWritten = 0;
        if (_file) {
            uint8_t hdr[48];
            size_t hl = format::writePcapngGlobalHeader(hdr);
            if (hl > 0) {
                _file.write(hdr, hl);
                _bytesWritten = (uint32_t)hl;
            }
        } else {
            _open = false;
        }
    }

    fs::FS  *_fs;
    fs::File _file;
    bool     _open;
    bool     _autoFlush;
    uint32_t _maxBytes;
    uint32_t _bytesWritten;
    char     _basePath[64] = {};
    uint16_t _fileIdx;
};

/**
 * @brief Helper for writing HandshakeRecords to an HC22000 text file.
 *
 * Supports both streaming (open/write/close) and one-shot (static append()) usage.
 */
class Hc22000FileLogger {
public:
    Hc22000FileLogger() : _open(false), _autoFlush(true) {}

    ~Hc22000FileLogger() { close(); }

    /**
     * @brief Enables or disables automatic flushing after every write.
     * @param enable true to flush after every write (default), false to buffer.
     */
    void setAutoFlush(bool enable) { _autoFlush = enable; }

    /**
     * @brief Flushes buffered HC22000 lines to storage.
     */
    void flush() {
        if (_open) _file.flush();
    }

    /**
     * @brief Opens the HC22000 file for streaming writes.
     * @param fs   The filesystem (e.g., SD, LittleFS)
     * @param path The file path (e.g., "/hashes.hc22000")
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
     * @brief Writes a HandshakeRecord to the open file without heap allocation.
     * @param rec The captured HandshakeRecord to format and write.
     * @return true if data was written
     */
    bool write(const HandshakeRecord &rec) {
        if (!_open) return false;
        char buf[768];
        size_t n = format::toHC22000(rec, buf, sizeof(buf));
        if (n > 0) {
            _file.println(buf);
            if (_autoFlush) _file.flush();
            return true;
        }
        return false;
    }

    /** @brief Closes the underlying file handle. Safe to call multiple times. */
    void close() {
        if (_open) { _file.flush(); _file.close(); _open = false; }
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
    bool     _autoFlush;
};

/**
 * @brief Helper for writing precise GPS location coordinates to a Wigle.net compatible CSV file.
 *
 * Wigle.net has a strict CSV format starting with a specific header:
 * MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type
 */
class WigleCsvLogger {
public:
    WigleCsvLogger() : _open(false), _autoFlush(true) {}

    ~WigleCsvLogger() { close(); }

    /**
     * @brief Enables or disables automatic flushing after every write.
     * @param enable true to flush after every write (default), false to buffer.
     */
    void setAutoFlush(bool enable) { _autoFlush = enable; }

    /** @brief Flushes buffered CSV rows to storage. */
    void flush() {
        if (_open) _file.flush();
    }

    /**
     * @brief Opens a Wigle CSV file for persistent streaming writes.
     * @param fs   The filesystem (e.g., SD, LittleFS)
     * @param path The path to the file (e.g., "/wardrive.csv")
     * @return true if opened successfully
     */
    bool open(fs::FS &fs, const char *path) {
        if (_open) close();
        _file = _openWithHeader(fs, path);
        _open = (bool)_file;
        return _open;
    }

    /**
     * @brief Writes a HandshakeRecord row to the currently open Wigle CSV file.
     * @param rec       The captured HandshakeRecord
     * @param lat       Current GPS Latitude
     * @param lon       Current GPS Longitude
     * @param alt       Current GPS Altitude in meters
     * @param acc       GPS Accuracy radius in meters
     * @param timestamp Optional timestamp override ("YYYY-MM-DD HH:MM:SS")
     * @return true if written successfully
     */
    bool write(const HandshakeRecord &rec, float lat, float lon, float alt = 0.0f,
               float acc = 10.0f, const char *timestamp = nullptr) {
        if (!_open) return false;
        _writeRow(_file, rec.bssid, rec.ssid, rec.enc, rec.channel, rec.rssi, lat, lon, alt, acc, timestamp);
        if (_autoFlush) _file.flush();
        return true;
    }

    /**
     * @brief Writes an ApRecord row to the currently open Wigle CSV file.
     * @param ap        The discovered ApRecord
     * @param lat       Current GPS Latitude
     * @param lon       Current GPS Longitude
     * @param alt       Current GPS Altitude in meters
     * @param acc       GPS Accuracy radius in meters
     * @param timestamp Optional timestamp override ("YYYY-MM-DD HH:MM:SS")
     * @return true if written successfully
     */
    bool writeAp(const ApRecord &ap, float lat, float lon, float alt = 0.0f,
                 float acc = 10.0f, const char *timestamp = nullptr) {
        if (!_open) return false;
        _writeRow(_file, ap.bssid, ap.ssid, ap.enc, ap.channel, ap.rssi, lat, lon, alt, acc, timestamp);
        if (_autoFlush) _file.flush();
        return true;
    }

    /** @brief Closes the open CSV file handle. */
    void close() {
        if (_open) { _file.flush(); _file.close(); _open = false; }
    }

    /** @brief Returns true if the CSV file is currently open. */
    bool isOpen() const { return _open; }

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
     * @param timestamp (Optional) Timestamp string
     * @return true if successful, false if file could not be opened
     */
    static bool append(fs::FS &fs, const char* path, const HandshakeRecord& rec,
                       float lat, float lon, float alt = 0.0, float acc = 10.0,
                       const char* timestamp = nullptr) {
        fs::File file = _openWithHeader(fs, path);
        if (!file) return false;
        _writeRow(file, rec.bssid, rec.ssid, rec.enc, rec.channel, rec.rssi, lat, lon, alt, acc, timestamp);
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
     * @param timestamp (Optional) Timestamp string
     * @return true if successful, false if file could not be opened
     */
    static bool appendAp(fs::FS &fs, const char* path, const ApRecord& ap,
                         float lat, float lon, float alt = 0.0, float acc = 10.0,
                         const char* timestamp = nullptr) {
        fs::File file = _openWithHeader(fs, path);
        if (!file) return false;
        _writeRow(file, ap.bssid, ap.ssid, ap.enc, ap.channel, ap.rssi, lat, lon, alt, acc, timestamp);
        file.flush();
        file.close();
        return true;
    }

private:
    static void _writeRow(fs::File &file, const uint8_t *bssid, const char *ssid, uint8_t enc,
                          uint8_t channel, int8_t rssi, float lat, float lon, float alt, float acc,
                          const char *timestamp) {
        char ssidEscaped[72];
        detail::escapeCsvField(ssid, ssidEscaped, sizeof(ssidEscaped));

        char line[256];
        snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X,%s,%s,%s,%d,%d,%.6f,%.6f,%.1f,%.1f,WIFI",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                 ssidEscaped, _authStr(enc), timestamp ? timestamp : detail::_timestamp(),
                 channel, rssi, lat, lon, alt, acc);
        file.println(line);
    }

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

    fs::File _file;
    bool     _open;
    bool     _autoFlush;
};

/**
 * @brief Helper for logging harvested 802.1X Enterprise Credentials.
 * It writes a Clean CSV file containing BSSID, Client, and Plaintext Identity.
 */
class EnterpriseCsvLogger {
public:
    EnterpriseCsvLogger() : _open(false), _autoFlush(true) {}

    ~EnterpriseCsvLogger() { close(); }

    /**
     * @brief Enables or disables automatic flushing after every write.
     * @param enable true to flush after every write (default), false to buffer.
     */
    void setAutoFlush(bool enable) { _autoFlush = enable; }

    /** @brief Flushes buffered CSV rows to storage. */
    void flush() {
        if (_open) _file.flush();
    }

    /**
     * @brief Opens an Enterprise CSV file for persistent streaming writes.
     * @param fs   The filesystem (e.g., SD, LittleFS)
     * @param path The path to the file (e.g., "/enterprise.csv")
     * @return true if opened successfully
     */
    bool open(fs::FS &fs, const char *path) {
        if (_open) close();
        _file = _openWithHeader(fs, path);
        _open = (bool)_file;
        return _open;
    }

    /**
     * @brief Writes an EapIdentityRecord to the open CSV file.
     * @param rec       The harvested EapIdentityRecord
     * @param timestamp Optional timestamp string
     * @return true if written successfully
     */
    bool write(const EapIdentityRecord &rec, const char *timestamp = nullptr) {
        if (!_open) return false;
        _writeRow(_file, rec, timestamp);
        if (_autoFlush) _file.flush();
        return true;
    }

    /** @brief Closes the open CSV file handle. */
    void close() {
        if (_open) { _file.flush(); _file.close(); _open = false; }
    }

    /** @brief Returns true if the CSV file is currently open. */
    bool isOpen() const { return _open; }

    /**
     * @brief Appends an EapIdentityRecord to a CSV file (opens and closes per call).
     * @param fs        The filesystem (e.g., SD, LittleFS)
     * @param path      The path to the file
     * @param rec       The harvested EapIdentityRecord
     * @param timestamp Optional timestamp string
     * @return true if successful
     */
    static bool append(fs::FS &fs, const char *path, const EapIdentityRecord &rec,
                       const char *timestamp = nullptr) {
        fs::File file = _openWithHeader(fs, path);
        if (!file) return false;
        _writeRow(file, rec, timestamp);
        file.flush();
        file.close();
        return true;
    }

private:
    static fs::File _openWithHeader(fs::FS &fs, const char *path) {
        bool isNew = !fs.exists(path);
        if (!isNew) {
            fs::File check = fs.open(path, FILE_READ);
            if (check) { isNew = (check.size() == 0); check.close(); }
        }
        fs::File file = fs.open(path, FILE_APPEND);
        if (!file) return file;
        if (isNew) {
            file.println("Enterprise BSSID,Client MAC,Plaintext Identity,EAP Method,FirstSeen,Channel,RSSI");
        }
        return file;
    }

    static void _writeRow(fs::File &file, const EapIdentityRecord &rec, const char *timestamp) {
        char identityEscaped[136]; // 64 chars worst-case doubled + 2 quotes + NUL
        detail::escapeCsvField(rec.identity, identityEscaped, sizeof(identityEscaped));

        char line[256];
        snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X,%02X:%02X:%02X:%02X:%02X:%02X,%s,%u,%s,%d,%d",
                 rec.bssid[0], rec.bssid[1], rec.bssid[2], rec.bssid[3], rec.bssid[4], rec.bssid[5],
                 rec.client[0], rec.client[1], rec.client[2], rec.client[3], rec.client[4], rec.client[5],
                 identityEscaped, rec.eap_method, timestamp ? timestamp : detail::_timestamp(),
                 rec.channel, rec.rssi);

        file.println(line);
    }

    fs::File _file;
    bool     _open;
    bool     _autoFlush;
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
    mutable portMUX_TYPE _mux;

public:
    NvsBssidCache(const char* ns = "wardrive")
        : _count(0), _dirty(false), _mux(portMUX_INITIALIZER_UNLOCKED) {
#ifndef POLITICIAN_NO_LOGGING
        if (ns && strlen(ns) > 15)
            Serial.println("[NvsBssidCache] WARNING: namespace name exceeds 15 chars and will be truncated");
#endif
        if (ns) {
            strncpy(_ns, ns, sizeof(_ns) - 1);
            _ns[sizeof(_ns) - 1] = '\0';
        } else {
            _ns[0] = '\0';
        }
        memset(_cache, 0, sizeof(_cache));
    }

    /**
     * @brief Initializes the NVS memory and loads the cached BSSIDs into RAM.
     */
    void begin() {
        _prefs.begin(_ns, false);
        uint8_t tmp[MAX_STORED][6];
        memset(tmp, 0, sizeof(tmp));
        size_t bytes = _prefs.getBytes("bssids", tmp, sizeof(tmp));
        size_t cnt = bytes / 6;
        if (cnt > MAX_STORED) cnt = MAX_STORED;
        portENTER_CRITICAL(&_mux);
        memcpy(_cache, tmp, sizeof(_cache));
        _count = cnt;
        _dirty = false;
        portEXIT_CRITICAL(&_mux);
    }

    /**
     * @brief Feeds the loaded BSSIDs into the Politician engine so it knows to ignore them.
     * @param engine Reference to your active Politician instance
     */
    void loadInto(Politician& engine) {
        uint8_t snapshot[MAX_STORED][6];
        size_t cnt = 0;
        portENTER_CRITICAL(&_mux);
        cnt = _count;
        if (cnt > MAX_STORED) cnt = MAX_STORED;
        memcpy(snapshot, _cache, cnt * 6);
        portEXIT_CRITICAL(&_mux);
        for (size_t i = 0; i < cnt; i++) {
            engine.markCaptured(snapshot[i]);
        }
    }

    /**
     * @brief Adds a newly captured BSSID to the in-RAM cache.
     * The change is not written to NVS until flush() is called.
     * @param bssid The 6-byte BSSID to save.
     * @return true if added, false if it already exists or the cache is full.
     */
    bool add(const uint8_t* bssid) {
        if (!bssid) return false;
        bool added = false;
        portENTER_CRITICAL(&_mux);
        for (size_t i = 0; i < _count; i++) {
            if (memcmp(_cache[i], bssid, 6) == 0) {
                portEXIT_CRITICAL(&_mux);
                return false; // Already cached
            }
        }
        if (_count < MAX_STORED) {
            memcpy(_cache[_count], bssid, 6);
            _count++;
            _dirty = true;
            added = true;
        }
        portEXIT_CRITICAL(&_mux);
        return added;
    }

    /**
     * @brief Writes any pending in-RAM changes to NVS.
     * Call this periodically (e.g., in end() or on a timer) to batch flash writes.
     * @return true if data was written, false if there were no pending changes.
     */
    bool flush() {
        uint8_t snapshot[MAX_STORED][6];
        size_t cnt = 0;
        portENTER_CRITICAL(&_mux);
        if (!_dirty) {
            portEXIT_CRITICAL(&_mux);
            return false;
        }
        cnt = _count;
        memcpy(snapshot, _cache, cnt * 6);
        _dirty = false;
        portEXIT_CRITICAL(&_mux);

        _prefs.putBytes("bssids", snapshot, cnt * 6);
        return true;
    }

    /** @brief Returns true if there are in-RAM changes not yet written to NVS. */
    bool isDirty() const {
        portENTER_CRITICAL(&_mux);
        bool d = _dirty;
        portEXIT_CRITICAL(&_mux);
        return d;
    }

    /**
     * @brief Returns the number of BSSIDs currently stored.
     */
    size_t count() const {
        portENTER_CRITICAL(&_mux);
        size_t c = _count;
        portEXIT_CRITICAL(&_mux);
        return c;
    }

    /**
     * @brief Clears the entire cache from both RAM and NVS immediately.
     */
    void clear() {
        portENTER_CRITICAL(&_mux);
        _count = 0;
        _dirty = false;
        portEXIT_CRITICAL(&_mux);
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
        uint8_t buf[1024];
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
        uint8_t buf[1024];
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
