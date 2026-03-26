#pragma once
#include <Arduino.h>

namespace politician {

/**
 * @brief PoliticianStress: Decoupled DoS / Disruption Payload Delivery System
 * 
 * Includes raw 802.11 framing mechanisms capable of flooding access points
 * with Management frames. If this header is not explicitly included in the user's
 * sketch, the C++ Linker will completely omit these offensive payloads from memory.
 */
namespace stress {

    /**
     * @brief Blasts a massive SAE (Simultaneous Authentication of Equals) Commit flood.
     *        Forces WPA3 routers to rapidly consume heap memory parsing anti-clogging tokens.
     * 
     * @param bssid Target router's MAC address
     * @param count Number of frames to fire natively
     */
    void saeCommitFlood(const uint8_t* bssid, uint32_t count = 1000);

    /**
     * @brief Blasts out massive strings of randomized Probe Requests to overwhelm
     *        local Access Points with client association processing queues.
     * 
     * @param count Number of frames to fire natively
     */
    void probeRequestFlood(uint32_t count = 1000);

}
}
