/*
 * WpsCapture.ino
 *
 * Demonstrates passive WPS M1 frame capture using the Politician library.
 *
 * When a WPS client (phone, laptop, printer) starts the WPS PIN/PBC exchange,
 * it sends a WSC_MSG M1 frame containing its device identity: model name,
 * manufacturer, UUID, and supported config methods (PBC, PIN, Display, etc.).
 *
 * This example:
 *   1. Hops channels listening for beacon WPS IEs to identify WPS-enabled APs.
 *   2. Registers a WPS callback — fired automatically when an M1 is sniffed.
 *   3. Uses PoliticianWPS.h to pretty-print the captured record.
 *
 * No active injection is needed; WPS M1 is broadcast in the clear.
 *
 * Optional: include PoliticianWPS.h for the print<> helper and bitmask constants.
 */

#include <Arduino.h>
#include <Politician.h>
#include <PoliticianWPS.h>  // Optional: print helpers, devTypeCatStr, configMethodsStr

using namespace politician;

Politician engine;

// ─── WPS M1 Callback ─────────────────────────────────────────────────────────

void onWpsM1(const WpsRecord &rec) {
    Serial.println("\n╔══════════════════════════════════════════╗");
    Serial.println("║          WPS M1 Frame Captured           ║");
    Serial.println("╚══════════════════════════════════════════╝");

    // AP that hosted the WPS exchange
    Serial.printf("  AP BSSID   : %02X:%02X:%02X:%02X:%02X:%02X\n",
        rec.bssid[0], rec.bssid[1], rec.bssid[2],
        rec.bssid[3], rec.bssid[4], rec.bssid[5]);

    // Device attributes from the M1 TLVs
    Serial.printf("  Manufacturer: %s\n", rec.manufacturer);
    Serial.printf("  Model Name  : %s\n", rec.model_name);
    Serial.printf("  Model Number: %s\n", rec.model_number);
    Serial.printf("  Device Name : %s\n", rec.device_name);
    Serial.printf("  Serial No.  : %s\n", rec.serial_number);

    // Device type category/subcategory (human-readable via PoliticianWPS.h)
    Serial.printf("  Device Type : 0x%04X / %s\n",
        rec.primary_dev_type_cat,
        PoliticianWPS::devTypeCatStr(rec.primary_dev_type_cat));

    // Config methods bitmask (human-readable via PoliticianWPS.h)
    Serial.printf("  Config Methods: 0x%04X (%s)\n",
        rec.config_methods,
        PoliticianWPS::configMethodsStr(rec.config_methods));

    // Print full record via PoliticianWPS.h template
    Serial.println("\n  [Full Record]");
    PoliticianWPS::print(Serial, rec);

    Serial.println("──────────────────────────────────────────────\n");
}

// ─── AP Found Callback (optional: log WPS-enabled APs as discovered) ─────────

void onApFound(const ApRecord &ap) {
    if (!ap.wps_enabled) return;
    Serial.printf("[WPS] WPS-enabled AP found: \"%.*s\" %02X:%02X:%02X:%02X:%02X:%02X ch%d RSSI=%d\n",
        ap.ssid_len, ap.ssid,
        ap.bssid[0], ap.bssid[1], ap.bssid[2],
        ap.bssid[3], ap.bssid[4], ap.bssid[5],
        ap.channel, ap.rssi);
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- Politician: WPS M1 Capture Example ---");
    Serial.println("Listening for WPS enrollee exchanges...\n");

    Config cfg;
    cfg.smart_hopping          = true;  // Follow traffic — WPS is usually on a specific channel
    cfg.probe_hidden_interval_ms = 0;   // No need to probe hidden APs for this task

    if (engine.begin(cfg) != politician::OK) {
        Serial.println("[ERROR] WiFi init failed!");
        while (1) delay(100);
    }

    engine.setApFoundCallback(onApFound);
    engine.setWpsCallback(onWpsM1);

    // Passive-only: no active attacks needed
    engine.setAttackMask(ATTACK_PASSIVE);
    engine.startHopping();

    Serial.println("Hopper started. Waiting for WPS M1 frames...");
}

// ─── Loop ────────────────────────────────────────────────────────────────────

void loop() {
    engine.tick();
}
