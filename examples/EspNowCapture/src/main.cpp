#include <Arduino.h>
#include <Politician.h>

using namespace politician;

Politician engine;

#ifndef ESP_NOW_DIAGNOSTICS
#define ESP_NOW_DIAGNOSTICS 0
#endif

static void printMac(const uint8_t mac[6]) {
#if ESP_NOW_DIAGNOSTICS
    Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#else
    Serial.printf("XX:XX:XX:%02x:%02x:%02x", mac[3], mac[4], mac[5]);
#endif
}

void setup() {
    Serial.begin(115200);

    Config cfg;
    cfg.capture_filter = LOG_FILTER_BEACONS | LOG_FILTER_PROBES;
    if (engine.begin(cfg) != OK) {
        while (true) delay(100);
    }

    engine.setEspNowCallback([](const EspNowRecord &rec) {
        Serial.print("[ESP-NOW] src=");
        printMac(rec.src);
        Serial.print(" dst=");
        printMac(rec.dst);
        Serial.printf(" ch=%u rssi=%d len=%u", rec.channel, rec.rssi, rec.length);
#if ESP_NOW_DIAGNOSTICS
        Serial.print(" payload=");
        for (uint16_t i = 0; i < rec.length; ++i) {
            Serial.printf("%02x", rec.payload[i]);
        }
#endif
        Serial.println();
    });
    engine.startHopping();
}

void loop() {
    engine.tick();
}
