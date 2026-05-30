#pragma once
#include <stdint.h>
#include <cstring>

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

    /**
     * @brief Transmits beacon frames with rotating fake SSIDs to stress-test AP
     * table management and SSID announcement handling.
     *
     * Each beacon advertises an open (no RSN IE), ESS network on the given channel.
     * Source MACs are randomized per frame. Call only from a task that can tolerate
     * the blocking delay loop (one frame every ~5ms for the duration).
     *
     * @param ssids       Array of C-string SSIDs to cycle through
     * @param ssidCount   Number of entries in @p ssids
     * @param channel     802.11 channel to advertise on (1-13 for 2.4GHz)
     * @param durationMs  Total duration of the flood in milliseconds
     */
    void beaconFlood(const char **ssids, uint8_t ssidCount,
                     uint8_t channel, uint32_t durationMs);

}
}
