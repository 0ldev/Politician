/*
 * DeviceFingerprinting.ino
 *
 * This example demonstrates how to use the passive Wi-Fi fingerprinting engine
 * to identify the vendor and model of devices in the area without connecting to any network.
 * The library listens to Wi-Fi traffic in monitor mode and compares the packets
 * against its built-in database of 150+ consumer brands (smart home, appliances, AV, etc).
 */

#include <Arduino.h>
#include <Politician.h>
#include <PoliticianFingerprint.h>

using namespace politician;
using namespace politician::fingerprint;

// Core engine
Politician engine;

// The fingerprint detector attaches to the engine
Detector detector(engine);

// This callback fires every time the detector sniffs a packet from a recognized device
void onDeviceFound(const DeviceRecord &rec) {
    Serial.printf("\n[FINGERPRINT] Recognized Device Sniffed!\n");
    Serial.printf("  MAC Address : %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  rec.mac[0], rec.mac[1], rec.mac[2], rec.mac[3], rec.mac[4], rec.mac[5]);
    
    Serial.printf("  Vendor      : %s\n", rec.vendor[0] != '\0' ? rec.vendor : "Unknown");
    
    if (rec.model[0] != '\0') {
        Serial.printf("  Model/Hint  : %s\n", rec.model);
    }
    
    Serial.printf("  Confidence  : %d%%\n", rec.confidence);
    Serial.printf("  Signal(RSSI): %d dBm\n", rec.rssi);
    Serial.printf("  Channel     : %d\n", rec.channel);
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n--- Politician Device Fingerprinting Example ---");

    Config cfg;
    
    // Start the underlying engine in passive monitoring mode
    if (engine.begin(cfg) != politician::OK) {
        Serial.println("Failed to initialize Politician engine");
        while (true) delay(10);
    }

    // Configure the detector
    detector.setCallback(onDeviceFound);
    
    // Only fire the callback if we are at least 50% sure of the match
    detector.setMinConfidence(50);
    
    // Optional: Add a custom device fingerprint dynamically at runtime
    // Here we define a fake device just for demonstration. 
    // (Vendor, Model, MAC prefix (OUI), Probe SSID prefix, Confidence score, ...)
    DeviceFingerprint myCustomDevice = {
        "My Custom IoT", 
        "Sensor v2", 
        {0xAA, 0xBB, 0xCC}, 
        "MySensor_", 
        99,
        {0}, {0}, {0}, 0, 0 // IE flags zeroed out for simple MAC+SSID match
    };
    detector.addFingerprint(myCustomDevice);
    Serial.println("Added custom fingerprint for OUI AA:BB:CC.");

    // Start autonomous channel hopping, spending 500ms on each channel
    engine.startHopping(500);

    // Start capturing and processing packets
    engine.setActive(true);

    Serial.println("Passive fingerprinting started. Listening for devices in the air...");
    Serial.println("Waiting for transmissions (Probe Requests, Keep-Alives, etc.)...");
}

void loop() {
    // Process engine tasks (channel hopping, callbacks, etc)
    engine.tick();
    delay(10);
}
