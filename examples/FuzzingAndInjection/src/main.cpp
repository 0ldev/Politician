/*
 * FuzzingAndInjection.ino
 *
 * This example demonstrates the custom 802.11 frame injection and fuzzing 
 * capabilities of the Politician library. It shows how to inject frames 
 * both synchronously (immediate) and asynchronously (hopper-synchronized).
 */

#include <Arduino.h>
#include <Politician.h>

using namespace politician;

Politician engine;

// A malformed Beacon frame for fuzzing. 
// Note: In a real scenario, you would craft this more carefully.
uint8_t fuzzedBeacon[] = {
    0x80, 0x00, 0x00, 0x00,               // FC: Beacon
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // DA: Broadcast
    0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED,   // SA: Spoofed
    0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED,   // BSSID: Spoofed
    0x00, 0x00,                           // Seq
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
    0x64, 0x00, 0x31, 0x04,               // Params
    0x00, 0x04, 'F', 'U', 'Z', 'Z'        // SSID: FUZZ
};

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- Politician: Fuzzing & Injection Example ---");

    Config cfg;
    if (engine.begin(cfg) != politician::OK) {
        Serial.println("WiFi Init Failed!");
        while(1) delay(100);
    }

    engine.startHopping();
    Serial.println("Engine started. Press 's' for sync inject, 'a' for async inject.");
}

void loop() {
    engine.tick();

    if (Serial.available()) {
        char cmd = Serial.read();
        
        if (cmd == 's') {
            Serial.println("[!] Sync Inject: Jumping to ch 1 and blasting...");
            // Immediately jump to channel 1, fire, and lock the hopper for 1 second.
            engine.injectCustomFrame(fuzzedBeacon, sizeof(fuzzedBeacon), 1, 1000);
        } 
        else if (cmd == 'a') {
            Serial.println("[!] Async Inject: Queuing for ch 11...");
            // Queue the frame. The engine will fire it only when the hopper lands on ch 11.
            engine.injectCustomFrame(fuzzedBeacon, sizeof(fuzzedBeacon), 11, 0, true);
        }
    }
}
