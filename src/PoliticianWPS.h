#pragma once
/**
 * @file PoliticianWPS.h
 * @brief Opt-in WPS M1 capture utilities for the Politician engine.
 *
 * Include this header alongside Politician.h to enable WPS M1 device fingerprinting.
 * The engine passively captures the first unencrypted EAP-WSC message (M1) sent by
 * a WPS Enrollee, exposing device attributes without any active interaction.
 *
 * Usage:
 *   #include "Politician.h"
 *   #include "PoliticianWPS.h"   // Add this line
 *
 *   engine.setWpsCallback([](const WpsRecord &rec) {
 *       Serial.printf("[WPS] %s by %s (%s)\n",
 *           rec.device_name, rec.manufacturer, PoliticianWPS::configMethodsStr(rec.config_methods));
 *       PoliticianWPS::print(Serial, rec);
 *   });
 *
 * Notes:
 *  - Only M1 (Enrollee → AP) is unencrypted and parseable passively.
 *  - Subsequent WPS messages (M2-M8) are encrypted; PIN cracking is not possible passively.
 *  - Enable by calling engine.setWpsCallback() — zero overhead if callback is null.
 *  - WPS exchange occurs during association; channel hopping must be active for discovery.
 */

#include "PoliticianTypes.h"
#include <Arduino.h>

namespace politician {

namespace PoliticianWPS {

// ─── Device Type Category Strings ────────────────────────────────────────────
// Primary Device Type category codes (WPS spec Table E-3)
inline const char* devTypeCatStr(uint16_t cat) {
    switch (cat) {
        case 1:  return "Computer";
        case 2:  return "Input Device";
        case 3:  return "Printer/Scanner/Fax/Copier";
        case 4:  return "Camera";
        case 5:  return "Storage";
        case 6:  return "Network Infrastructure";
        case 7:  return "Displays";
        case 8:  return "Multimedia Device";
        case 9:  return "Gaming";
        case 10: return "Telephone";
        case 11: return "Audio";
        case 12: return "Docking Device";
        case 255: return "Other";
        default: return "Unknown";
    }
}

// ─── Config Methods Bitmask Helpers ──────────────────────────────────────────
// Config Methods field bits (WPS spec Table E-4, 0x1012 attribute)
static const uint16_t CFG_USBA          = 0x0001;
static const uint16_t CFG_ETHERNET      = 0x0002;
static const uint16_t CFG_LABEL         = 0x0004;
static const uint16_t CFG_DISPLAY       = 0x0008;
static const uint16_t CFG_EXT_NFC       = 0x0010;
static const uint16_t CFG_INT_NFC       = 0x0020;
static const uint16_t CFG_NFC_IFACE     = 0x0040;
static const uint16_t CFG_PUSH_BUTTON   = 0x0080;
static const uint16_t CFG_KEYPAD        = 0x0100;
static const uint16_t CFG_VIRTUAL_PBC   = 0x0200;
static const uint16_t CFG_PHYSICAL_PBC  = 0x0400;
static const uint16_t CFG_VIRTUAL_PIN   = 0x2000;
static const uint16_t CFG_PHYSICAL_PIN  = 0x4000;

/** @brief Returns a compact human-readable string of supported WPS setup methods. */
inline const char* configMethodsStr(uint16_t methods) {
    static char buf[64];
    buf[0] = '\0';
    if (methods & CFG_PUSH_BUTTON) strncat(buf, "PBC ", sizeof(buf) - strlen(buf) - 1);
    if (methods & CFG_KEYPAD)      strncat(buf, "PIN-Keypad ", sizeof(buf) - strlen(buf) - 1);
    if (methods & CFG_LABEL)       strncat(buf, "PIN-Label ", sizeof(buf) - strlen(buf) - 1);
    if (methods & CFG_DISPLAY)     strncat(buf, "Display ", sizeof(buf) - strlen(buf) - 1);
    if (methods & CFG_NFC_IFACE)   strncat(buf, "NFC ", sizeof(buf) - strlen(buf) - 1);
    if (buf[0] == '\0') strncat(buf, "None", sizeof(buf) - strlen(buf) - 1);
    // Trim trailing space
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == ' ') buf[n - 1] = '\0';
    return buf;
}

// ─── Auth Type Flags ──────────────────────────────────────────────────────────
static const uint16_t AUTH_OPEN         = 0x0001;
static const uint16_t AUTH_WPAPSK       = 0x0002;
static const uint16_t AUTH_SHARED       = 0x0004;
static const uint16_t AUTH_WPA          = 0x0008;
static const uint16_t AUTH_WPA2         = 0x0010;
static const uint16_t AUTH_WPA2PSK      = 0x0020;

// ─── Print Helper ─────────────────────────────────────────────────────────────
/** @brief Pretty-prints a WpsRecord to any Print-compatible stream (Serial, etc.). */
template<typename TStream>
inline void print(TStream &out, const WpsRecord &rec) {
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             rec.sta[0], rec.sta[1], rec.sta[2], rec.sta[3], rec.sta[4], rec.sta[5]);
    out.printf("┌─ WPS M1 Enrollee ───────────────────────\n");
    out.printf("│ STA:          %s  ch%d  %d dBm\n", mac, rec.channel, rec.rssi);
    out.printf("│ Device Name:  %s\n", rec.device_name[0] ? rec.device_name : "(unknown)");
    out.printf("│ Manufacturer: %s\n", rec.manufacturer[0] ? rec.manufacturer : "(unknown)");
    out.printf("│ Model:        %s %s\n",
               rec.model_name[0]   ? rec.model_name   : "",
               rec.model_number[0] ? rec.model_number : "");
    out.printf("│ Serial:       %s\n", rec.serial_number[0] ? rec.serial_number : "(unknown)");
    out.printf("│ Dev Type:     %s (cat 0x%04X)\n",
               devTypeCatStr(rec.primary_dev_type_cat), rec.primary_dev_type_cat);
    out.printf("│ Config Meth:  %s (0x%04X)\n",
               configMethodsStr(rec.config_methods), rec.config_methods);
    out.printf("│ RF Bands:     %s%s\n",
               (rec.rf_bands & 0x01) ? "2.4GHz " : "",
               (rec.rf_bands & 0x02) ? "5GHz"    : "");
    out.printf("└─────────────────────────────────────────\n");
}

} // namespace PoliticianWPS

} // namespace politician
