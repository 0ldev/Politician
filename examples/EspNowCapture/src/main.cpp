#include <Arduino.h>
#include <Politician.h>

using namespace politician;

Politician engine;

#ifndef ESP_NOW_DIAGNOSTICS
#define ESP_NOW_DIAGNOSTICS 0
#endif

static void printMaskedMac(const uint8_t mac[6]) {
    Serial.printf("XX:XX:XX:%02x:%02x:%02x", mac[3], mac[4], mac[5]);
}

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
}
