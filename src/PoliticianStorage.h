#pragma once

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include "Politician.h"
#include "PoliticianFormat.h"

namespace politician {
namespace storage {

/**
 * @brief Helper for writing HandshakeRecords to a standard PCAPNG file.
 */
class PcapngFileLogger {
public:
    /**
     * @brief Appends a HandshakeRecord to a file as PCAPNG.
     * If the file is empty, it automatically writes the global PCAPNG Header Block first.
     * 
     * @param fs   The filesystem (e.g., SD, LittleFS)
     * @param path The path to the file (e.g., "/captures.pcapng")
     * @param rec  The HandshakeRecord to write
     * @return true if successful, false if file could not be opened
     */
    static bool append(fs::FS &fs, const char* path, const HandshakeRecord& rec) {
        bool isNew = !fs.exists(path);
        if (!isNew) {
            fs::File check = fs.open(path, FILE_READ);
            if (check) {
                isNew = (check.size() == 0);
                check.close();
            }
        }

        fs::File file = fs.open(path, FILE_APPEND);
        if (!file) return false;

        if (isNew) {
            uint8_t hdr[48];
            size_t hl = format::writePcapngGlobalHeader(hdr);
            file.write(hdr, hl);
        }

        uint8_t buf[512];
        size_t len = format::writePcapngRecord(rec, buf, sizeof(buf));
        if (len > 0) {
            file.write(buf, len);
            file.flush();
        }
        file.close();
        return true;
    }

    /**
     * @brief Appends a raw 802.11 sniffer frame to a PCAPNG file (used for intel gathering).
     * 
     * @param fs   The filesystem (e.g., SD, LittleFS)
     * @param path The path to the file (e.g., "/intel.pcapng")
     * @param payload Raw packet data
     * @param len Packet length
     * @param rssi Signal strength
     * @param ts_usec Hardware microsecond timestamp
     * @return true if successful, false otherwise
     */
    static bool appendPacket(fs::FS &fs, const char* path, const uint8_t* payload, uint16_t len, int8_t rssi, uint32_t ts_usec) {
        bool isNew = !fs.exists(path);
        if (!isNew) {
            fs::File check = fs.open(path, FILE_READ);
            if (check) {
                isNew = (check.size() == 0);
                check.close();
            }
        }

        fs::File file = fs.open(path, FILE_APPEND);
        if (!file) return false;

        if (isNew) {
            uint8_t hdr[48];
            size_t hl = format::writePcapngGlobalHeader(hdr);
            file.write(hdr, hl);
        }

        uint8_t buf[2500]; // Max 802.11 frame is 2346 bytes
        size_t wlen = format::writePcapngPacket(payload, len, rssi, ts_usec, buf, sizeof(buf));
        if (wlen > 0) {
            file.write(buf, wlen);
            file.flush();
        }
        file.close();
        return true;
    }
};

/**
 * @brief Helper for writing HandshakeRecords to an HC22000 text file.
 */
class Hc22000FileLogger {
public:
    /**
     * @brief Appends a HandshakeRecord to a file as an HC22000 string.
     * 
     * @param fs   The filesystem (e.g., SD, LittleFS)
     * @param path The path to the file (e.g., "/captures.22000")
     * @param rec  The HandshakeRecord to write
     * @return true if successful, false if file could not be opened
     */
    static bool append(fs::FS &fs, const char* path, const HandshakeRecord& rec) {
        fs::File file = fs.open(path, FILE_APPEND);
        if (!file) return false;

        String str = format::toHC22000(rec);
        if (str.length() > 0) {
            file.println(str);
            file.flush();
        }
        file.close();
        return true;
    }
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

        char line[256];
        snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X,%s,%s,%s,%d,%d,%.6f,%.6f,%.1f,%.1f,WIFI",
                 rec.bssid[0], rec.bssid[1], rec.bssid[2], rec.bssid[3], rec.bssid[4], rec.bssid[5],
                 rec.ssid, _authStr(rec.enc), timestamp ? timestamp : "1970-01-01 00:00:00",
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

        char line[256];
        snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X,%s,%s,%s,%d,%d,%.6f,%.6f,%.1f,%.1f,WIFI",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
                 ap.ssid, _authStr(ap.enc), timestamp ? timestamp : "1970-01-01 00:00:00",
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
    static bool append(fs::FS &fs, const char *path, const EapIdentityRecord &rec) {
        bool isNew = !fs.exists(path);
        
        fs::File file = fs.open(path, FILE_APPEND);
        if (!file) return false;

        if (isNew) {
            file.println("Enterprise BSSID,Client MAC,Plaintext Identity,Channel,RSSI");
        }

        char line[128];
        snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X,%02X:%02X:%02X:%02X:%02X:%02X,%s,%d,%d",
                 rec.bssid[0], rec.bssid[1], rec.bssid[2], rec.bssid[3], rec.bssid[4], rec.bssid[5],
                 rec.client[0], rec.client[1], rec.client[2], rec.client[3], rec.client[4], rec.client[5],
                 rec.identity, rec.channel, rec.rssi);

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
    String _ns;
    static const int MAX_STORED = 128;
    uint8_t _cache[MAX_STORED][6];
    size_t _count;

public:
    NvsBssidCache(const char* ns = "wardrive") : _ns(ns), _count(0) {
        memset(_cache, 0, sizeof(_cache));
    }

    /**
     * @brief Initializes the NVS memory and loads the cached BSSIDs into RAM.
     */
    void begin() {
        _prefs.begin(_ns.c_str(), false);
        size_t bytes = _prefs.getBytes("bssids", _cache, sizeof(_cache));
        _count = bytes / 6;
        if (_count > MAX_STORED) _count = MAX_STORED; // Safety parameter
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
     * @brief Adds a newly captured BSSID to the cache and saves it to NVS.
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
        
        // Save entire updated array to NVS
        _prefs.putBytes("bssids", _cache, _count * 6);
        return true;
    }
    
    /**
     * @brief Returns the number of BSSIDs currently stored.
     */
    size_t count() const { return _count; }

    /**
     * @brief Clears the entire cache from NVS.
     */
    void clear() {
        _count = 0;
        _prefs.remove("bssids");
    }
};

} // namespace storage
} // namespace politician
