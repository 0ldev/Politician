#pragma once
#include <Arduino.h>
#include "PoliticianTypes.h"

namespace politician {
namespace format {

/**
 * @brief Converts a captured HandshakeRecord into the Hashcat 22000 (hc22000) text format.
 * 
 * The hc22000 format is an easily copyable string compatible with Hashcat and other modern cracking tools.
 * 
 * @param rec The handshake record.
 * @return String containing the HC22000 line, or an empty string if unsupported.
 */
String toHC22000(const HandshakeRecord& rec);

/**
 * @brief Writes a PCAPNG Global Header (SHB + IDB).
 * 
 * This must be written exactly once at the beginning of a `.pcapng` file.
 * 
 * @param buffer Output buffer (must be at least 48 bytes).
 * @return Number of bytes written (always 48).
 */
size_t writePcapngGlobalHeader(uint8_t* buffer);

/**
 * @brief Serializes a HandshakeRecord into PCAPNG Enhanced Packet Blocks.
 * 
 * @param rec The handshake record.
 * @param buffer Output buffer.
 * @param max_len Maximum length of the buffer. Recommended: 1024 bytes.
 * @return Number of bytes written, or 0 if buffer is too small.
 */
size_t writePcapngRecord(const HandshakeRecord& rec, uint8_t* buffer, size_t max_len);

/**
 * @brief Serializes a Raw 802.11 Frame into a PCAPNG Enhanced Packet Block.
 * 
 * @param payload Raw 802.11 frame.
 * @param payload_len Length of the raw frame.
 * @param rssi Signal strength.
 * @param ts_usec Hardware microsecond timestamp.
 * @param buffer Output buffer.
 * @param max_len Maximum length of the buffer.
 * @return Number of bytes written, or 0 if buffer is too small.
 */
size_t writePcapngPacket(const uint8_t* payload, size_t payload_len, int8_t rssi, uint64_t ts_usec, uint8_t* buffer, size_t max_len);

} // namespace format
} // namespace politician
