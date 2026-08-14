#include <Arduino.h>
#include <Politician.h>

using namespace politician;

Politician engine;

void setup() {
    Serial.begin(115200);

    Config cfg;
    cfg.capture_filter = LOG_FILTER_BEACONS | LOG_FILTER_PROBES;
    if (engine.begin(cfg) != OK) {
        while (true) delay(100);
    }

    engine.setEspNowCallback([](const EspNowRecord &rec) {
        Serial.printf("[ESP-NOW] ch=%u rssi=%d len=%u\n", rec.channel, rec.rssi, rec.length);
    });
    engine.startHopping();
}

void loop() {
    engine.tick();
}#include <Arduino.h>
#include <Politician.h>

using namespace politician;

Politician engine;

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n--- Politician ESP-NOW Capture Example ---");

    Config cfg;
    // We don't necessarily need to capture beacons for ESP-NOW, but it helps populate the AP cache
    cfg.capture_filter = LOG_FILTER_BEACONS | LOG_FILTER_PROBES;

    // Initialize the engine
    if (engine.begin(cfg) != politician::OK) {
        Serial.println("Failed to start Politician engine");
        while (1) delay(100);
    }

    // Set the callback to process ESP-NOW payloads
    engine.setEspNowCallback([](const EspNowRecord &rec) {
        Serial.printf("[ESP-NOW] ch=%d rssi=%d src=%02x:%02x:%02x:%02x:%02x:%02x dst=%02x:%02x:%02x:%02x:%02x:%02x len=%d payload=[",
                      rec.channel, rec.rssi,
                      rec.src[0], rec.src[1], rec.src[2], rec.src[3], rec.src[4], rec.src[5],
                      rec.dst[0], rec.dst[1], rec.dst[2], rec.dst[3], rec.dst[4], rec.dst[5],
                      rec.length);

        // Print the first few bytes of the payload in hex
        int printLen = rec.length > 16 ? 16 : rec.length;
        for (int i = 0; i < printLen; i++) {
            Serial.printf("%02x ", rec.payload[i]);
        }
        if (rec.length > 16) Serial.print("...");
        Serial.println("]");
    });

    // Start hopping to discover ESP-NOW devices on any channel,
    // or use engine.lockChannel(1) if you know their specific channel.
    engine.startHopping();
    Serial.println("Hopping started. Waiting for ESP-NOW traffic...");
}

void loop() {
    engine.tick();
}
