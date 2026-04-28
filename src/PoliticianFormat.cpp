#include "PoliticianFormat.h"
#include "politician_compat.h"

namespace politician {
namespace format {

static void appendHex(std::string& str, const uint8_t* data, size_t len) {
    char buf[3];
    for (size_t i = 0; i < len; i++) {
        sprintf(buf, "%02x", data[i]);
        str += buf;
    }
}

std::string toHC22000(const HandshakeRecord& rec) {
    if (rec.type == CAP_EAPOL_HALF) return std::string(); // No anonce — not crackable
    
    // Hashcat 22000 Format:
    // WPA*TYPE*MIC*BSSID*STA*SSID*ANONCE*EAPOL*KEYVER*KEYDATA
    
    std::string out = "WPA*";
    
    // 1. TYPE
    if (rec.type == CAP_PMKID) out += "01*";
    else if (rec.type == CAP_SAE) out += "06*";
    else out += "02*";

    // 2. MIC (Field 3)
    if (rec.type == CAP_PMKID || rec.type == CAP_SAE) {
        out += "*"; // Empty for PMKID/SAE
    } else {
        appendHex(out, rec.mic, 16);
        out += "*";
    }

    // 3. BSSID (Field 4)
    appendHex(out, rec.bssid, 6);
    out += "*";

    // 4. STA (Field 5)
    appendHex(out, rec.sta, 6);
    out += "*";

    // 5. SSID (Field 6)
    appendHex(out, (const uint8_t*)rec.ssid, rec.ssid_len);
    out += "*";

    // 6. ANONCE (Field 7)
    if (rec.type == CAP_EAPOL || rec.type == CAP_EAPOL_CSA) {
        appendHex(out, rec.anonce, 32);
    }
    out += "*";

    // 7. EAPOL/M2 (Field 8)
    if (rec.type == CAP_EAPOL || rec.type == CAP_EAPOL_CSA) {
        appendHex(out, rec.eapol_m2, rec.eapol_m2_len);
    }
    out += "*";

    // 8. KEYVER (Field 9)
    out += "02*"; // Default to WPA2/AES

    // 9. KEYDATA (Field 10) - Used for PMKID or SAE elements
    if (rec.type == CAP_PMKID) {
        appendHex(out, rec.pmkid, 16);
    } else if (rec.type == CAP_SAE) {
        appendHex(out, rec.sae_data, rec.sae_len);
    }
    
    return out;
}

size_t writePcapngGlobalHeader(uint8_t* buffer) {
    size_t offset = 0;
    
    // 1. Section Header Block (SHB)
    uint32_t shb_type = 0x0A0D0D0A;
    uint32_t shb_len = 28;
    uint32_t magic = 0x1A2B3C4D;
    uint16_t v_major = 1;
    uint16_t v_minor = 0;
    int64_t section_len = -1;
    
    memcpy(buffer + offset, &shb_type, 4); offset += 4;
    memcpy(buffer + offset, &shb_len, 4); offset += 4;
    memcpy(buffer + offset, &magic, 4); offset += 4;
    memcpy(buffer + offset, &v_major, 2); offset += 2;
    memcpy(buffer + offset, &v_minor, 2); offset += 2;
    memcpy(buffer + offset, &section_len, 8); offset += 8;
    memcpy(buffer + offset, &shb_len, 4); offset += 4;

    // 2. Interface Description Block (IDB)
    uint32_t idb_type = 0x00000001;
    uint32_t idb_len = 20;
    uint16_t link_type = 127; // IEEE 802.11 radiotap
    uint16_t reserved = 0;
    uint32_t snaplen = 65535;

    memcpy(buffer + offset, &idb_type, 4); offset += 4;
    memcpy(buffer + offset, &idb_len, 4); offset += 4;
    memcpy(buffer + offset, &link_type, 2); offset += 2;
    memcpy(buffer + offset, &reserved, 2); offset += 2;
    memcpy(buffer + offset, &snaplen, 4); offset += 4;
    memcpy(buffer + offset, &idb_len, 4); offset += 4;

    return offset;
}

size_t writePcapngPacket(const uint8_t* payload, size_t payload_len, int8_t rssi, uint8_t channel, uint64_t ts_usec, uint8_t* buffer, size_t max_len) {
    uint16_t freq = 0;
    uint16_t chan_flags = 0;
    if (channel <= 14) {
        freq = 2407 + (channel * 5);
        if (channel == 14) freq = 2484;
        chan_flags = 0x00A0; // 2GHz + Dynamic CCK/OFDM
    } else {
        freq = 5000 + (channel * 5);
        chan_flags = 0x0140; // 5GHz + OFDM
    }

    uint8_t radiotap[14] = {
        0x00, 0x00,         // Version 0, Pad 0
        0x0E, 0x00,         // Header length 14
        0x28, 0x00, 0x00, 0x00, // Present mask: Channel + RSSI
        (uint8_t)(freq & 0xFF), (uint8_t)(freq >> 8),
        (uint8_t)(chan_flags & 0xFF), (uint8_t)(chan_flags >> 8),
        (uint8_t)rssi,
        0x00                // Pad
    };

    uint32_t rt_len = 14;
    uint32_t cap_len = (uint32_t)payload_len + rt_len;
    uint32_t pkt_len = (uint32_t)payload_len + rt_len;
    uint32_t aligned_pkt_len = (cap_len + 3) & ~3;
    uint32_t pkt_padding = aligned_pkt_len - cap_len;

    // Options: RSSI (8), Channel (8), End (4) = 20 bytes
    uint32_t opt_len = 20; 
    uint32_t block_len = 32 + aligned_pkt_len + opt_len;

    if (block_len > max_len) return 0;

    size_t offset = 0;
    uint32_t epb_type = 0x00000006;
    uint32_t interface_id = 0;
    uint32_t ts_high = (uint32_t)(ts_usec >> 32);
    uint32_t ts_low  = (uint32_t)(ts_usec & 0xFFFFFFFF);
    
    memcpy(buffer + offset, &epb_type, 4); offset += 4;
    memcpy(buffer + offset, &block_len, 4); offset += 4;
    memcpy(buffer + offset, &interface_id, 4); offset += 4;
    memcpy(buffer + offset, &ts_high, 4); offset += 4;
    memcpy(buffer + offset, &ts_low, 4); offset += 4;
    memcpy(buffer + offset, &cap_len, 4); offset += 4;
    memcpy(buffer + offset, &pkt_len, 4); offset += 4;

    memcpy(buffer + offset, radiotap, rt_len); offset += rt_len;
    memcpy(buffer + offset, payload, payload_len); offset += payload_len;
    
    if (pkt_padding > 0) {
        memset(buffer + offset, 0, pkt_padding); offset += pkt_padding;
    }

    // Option TLVs
    uint16_t opt_code; uint16_t opt_vlen;
    
    // RSSI (0x8001)
    opt_code = 0x8001; opt_vlen = 1;
    memcpy(buffer + offset, &opt_code, 2); offset += 2;
    memcpy(buffer + offset, &opt_vlen, 2); offset += 2;
    buffer[offset++] = (uint8_t)rssi;
    memset(buffer + offset, 0, 3); offset += 3;

    // Channel (0x8002)
    opt_code = 0x8002; opt_vlen = 1;
    memcpy(buffer + offset, &opt_code, 2); offset += 2;
    memcpy(buffer + offset, &opt_vlen, 2); offset += 2;
    buffer[offset++] = channel;
    memset(buffer + offset, 0, 3); offset += 3;

    // End of Options
    opt_code = 0x0000; opt_vlen = 0;
    memcpy(buffer + offset, &opt_code, 2); offset += 2;
    memcpy(buffer + offset, &opt_vlen, 2); offset += 2;

    memcpy(buffer + offset, &block_len, 4); offset += 4;
    
    return offset;
}

size_t writePcapngRecord(const HandshakeRecord& rec, uint8_t* buffer, size_t max_len) {
    size_t offset = 0;
    uint8_t pkt[512];
    uint64_t ts = (uint64_t)millis() * 1000;
    
    // Packet 1: Beacon
    {
        size_t p = 0;
        // MAC Header (24 bytes): Frame Control(2), Dur(2), DA(6), SA(6), BSSID(6), Seq(2)
        pkt[p++] = 0x80; pkt[p++] = 0x00; pkt[p++] = 0x00; pkt[p++] = 0x00; // Type Mgmt / Subtype Beacon
        for (int i=0; i<6; i++) pkt[p++] = 0xFF; // Broadcast DA
        memcpy(pkt + p, rec.bssid, 6); p += 6;   // SA
        memcpy(pkt + p, rec.bssid, 6); p += 6;   // BSSID
        pkt[p++] = 0x00; pkt[p++] = 0x00; // Seq
        // Fixed params (12 bytes)
        memset(pkt + p, 0, 8); p += 8; // Timestamp
        pkt[p++] = 0x64; pkt[p++] = 0x00; // Beacon Interval (100)
        pkt[p++] = 0x11; pkt[p++] = 0x04; // Capabilities
        // Tagged params
        pkt[p++] = 0x00; pkt[p++] = rec.ssid_len; // SSID Tag
        memcpy(pkt + p, rec.ssid, rec.ssid_len); p += rec.ssid_len;
        pkt[p++] = 0x03; pkt[p++] = 0x01; pkt[p++] = rec.channel; // Channel Tag
        
        size_t w = writePcapngPacket(pkt, p, rec.rssi, rec.channel, ts++, buffer + offset, max_len - offset);
        if (w == 0) return offset;
        offset += w;
    }

    if (rec.type == CAP_PMKID) {
        // Packet 2: EAPOL M1 containing PMKID in Key Data
        size_t p = 0;
        pkt[p++] = 0x08; pkt[p++] = 0x02; pkt[p++] = 0x00; pkt[p++] = 0x00; // Data / QoS Data, FromDS=1
        memcpy(pkt + p, rec.sta, 6); p += 6;     // DA
        memcpy(pkt + p, rec.bssid, 6); p += 6;   // SA
        memcpy(pkt + p, rec.bssid, 6); p += 6;   // BSSID
        pkt[p++] = 0x00; pkt[p++] = 0x00; // Seq
        pkt[p++] = 0x00; pkt[p++] = 0x00; // QoS Control

        // LLC SNAP
        uint8_t snap[8] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E };
        memcpy(pkt + p, snap, 8); p += 8;

        // EAPOL Version 1, Type 3 (Key)
        pkt[p++] = 0x01; pkt[p++] = 0x03;
        
        uint16_t eapol_len = 95 + 22; // Key Frame + 22 bytes Key Data (PMKID)
        pkt[p++] = (eapol_len >> 8); pkt[p++] = (eapol_len & 0xFF);
        
        // EAPOL Key Frame (95 bytes without Key Data)
        uint8_t eapolPayload[95] = {0};
        eapolPayload[0] = 0x02; // Descriptor Type
        eapolPayload[1] = 0x00; eapolPayload[2] = 0x8A; // Key Info (Pairwise, Ack)
        eapolPayload[5] = 0x00; eapolPayload[6] = 0x00; eapolPayload[7] = 0x00; eapolPayload[8] = 0x00;
        eapolPayload[9] = 0x00; eapolPayload[10] = 0x00; eapolPayload[11] = 0x00; eapolPayload[12] = 0x01; // Replay Counter
        // Remaining is zeros
        eapolPayload[93] = 0x00; eapolPayload[94] = 0x16; // Key Data Len = 22
        
        memcpy(pkt + p, eapolPayload, 95); p += 95;
        
        // Key Data: PMKID = DD 14 00 0F AC 04 <16 bytes>
        pkt[p++] = 0xDD; pkt[p++] = 0x14; pkt[p++] = 0x00; pkt[p++] = 0x0F; pkt[p++] = 0xAC; pkt[p++] = 0x04;
        memcpy(pkt + p, rec.pmkid, 16); p += 16;
        
        size_t w = writePcapngPacket(pkt, p, rec.rssi, rec.channel, ts++, buffer + offset, max_len - offset);
        if (w == 0) return offset;
        offset += w;

    } else if (rec.type == CAP_SAE) {
        // Packet 2: SAE Authentication (Commit or Confirm)
        size_t p = 0;
        // MAC Header: Type Mgmt / Subtype Auth (0xB0)
        pkt[p++] = 0xB0; pkt[p++] = 0x00; pkt[p++] = 0x00; pkt[p++] = 0x00; 
        memcpy(pkt + p, rec.bssid, 6); p += 6;   // DA (APs BSSID)
        memcpy(pkt + p, rec.sta, 6); p += 6;     // SA (Client MAC)
        memcpy(pkt + p, rec.bssid, 6); p += 6;   // BSSID
        pkt[p++] = 0x00; pkt[p++] = 0x00; // Seq

        // SAE Fixed Header (6 bytes)
        pkt[p++] = 0x03; pkt[p++] = 0x00; // Algorithm: SAE (3)
        pkt[p++] = rec.sae_seq; pkt[p++] = 0x00; // Sequence
        pkt[p++] = 0x00; pkt[p++] = 0x00; // Status: Success (0)
        
        // SAE Body
        if (rec.sae_len > 0) {
            memcpy(pkt + p, rec.sae_data, rec.sae_len);
            p += rec.sae_len;
        }
        
        size_t w = writePcapngPacket(pkt, p, rec.rssi, rec.channel, ts++, buffer + offset, max_len - offset);
        if (w == 0) return offset;
        offset += w;

    } else {
        // CAP_EAPOL / EAPOL_CSA / EAPOL_HALF
        // Packet 2: EAPOL M1 (AP -> STA)
        if (rec.has_anonce) {
            size_t p = 0;
            pkt[p++] = 0x08; pkt[p++] = 0x02; pkt[p++] = 0x00; pkt[p++] = 0x00; // Data / QoS Data, FromDS=1
            memcpy(pkt + p, rec.sta, 6); p += 6;     // DA
            memcpy(pkt + p, rec.bssid, 6); p += 6;   // SA
            memcpy(pkt + p, rec.bssid, 6); p += 6;   // BSSID
            pkt[p++] = 0x00; pkt[p++] = 0x00; // Seq
            pkt[p++] = 0x00; pkt[p++] = 0x00; // QoS Control

            uint8_t snap[8] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E };
            memcpy(pkt + p, snap, 8); p += 8;

            pkt[p++] = 0x01; pkt[p++] = 0x03;
            uint16_t eapol_len = 95;
            pkt[p++] = (eapol_len >> 8); pkt[p++] = (eapol_len & 0xFF);
            
            uint8_t eapolPayload[95] = {0};
            eapolPayload[0] = 0x02; 
            eapolPayload[1] = 0x00; eapolPayload[2] = 0x8A; // Key Info
            eapolPayload[12] = 0x01; // Replay Counter
            memcpy(&eapolPayload[13], rec.anonce, 32);      // Anonce

            memcpy(pkt + p, eapolPayload, 95); p += 95;
            
            size_t w = writePcapngPacket(pkt, p, rec.rssi, rec.channel, ts++, buffer + offset, max_len - offset);
            if (w == 0) return offset;
            offset += w;
        }

        // Packet 3: EAPOL M2 (STA -> AP)
        if (rec.eapol_m2_len > 0) {
            size_t p = 0;
            pkt[p++] = 0x08; pkt[p++] = 0x01; pkt[p++] = 0x00; pkt[p++] = 0x00; // Data / QoS Data, ToDS=1
            memcpy(pkt + p, rec.bssid, 6); p += 6;   // DA
            memcpy(pkt + p, rec.sta, 6); p += 6;     // SA
            memcpy(pkt + p, rec.bssid, 6); p += 6;   // BSSID
            pkt[p++] = 0x00; pkt[p++] = 0x00; // Seq
            pkt[p++] = 0x00; pkt[p++] = 0x00; // QoS Control

            uint8_t snap[8] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E };
            memcpy(pkt + p, snap, 8); p += 8;

            memcpy(pkt + p, rec.eapol_m2, rec.eapol_m2_len); p += rec.eapol_m2_len;
            
            size_t w = writePcapngPacket(pkt, p, rec.rssi, rec.channel, ts++, buffer + offset, max_len - offset);
            if (w == 0) return offset;
            offset += w;
        }

        // Packet 4: EAPOL M3 (AP -> STA)
        if (rec.has_m3) {
            size_t p = 0;
            pkt[p++] = 0x08; pkt[p++] = 0x02; pkt[p++] = 0x00; pkt[p++] = 0x00; // FromDS=1
            memcpy(pkt + p, rec.sta, 6); p += 6;
            memcpy(pkt + p, rec.bssid, 6); p += 6;
            memcpy(pkt + p, rec.bssid, 6); p += 6;
            pkt[p++] = 0x00; pkt[p++] = 0x00;
            pkt[p++] = 0x00; pkt[p++] = 0x00;

            uint8_t snap[8] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E };
            memcpy(pkt + p, snap, 8); p += 8;

            memcpy(pkt + p, rec.eapol_m3, rec.eapol_m3_len); p += rec.eapol_m3_len;
            
            size_t w = writePcapngPacket(pkt, p, rec.rssi, rec.channel, ts++, buffer + offset, max_len - offset);
            if (w == 0) return offset;
            offset += w;
        }

        // Packet 5: EAPOL M4 (STA -> AP)
        if (rec.has_m4) {
            size_t p = 0;
            pkt[p++] = 0x08; pkt[p++] = 0x01; pkt[p++] = 0x00; pkt[p++] = 0x00; // ToDS=1
            memcpy(pkt + p, rec.bssid, 6); p += 6;
            memcpy(pkt + p, rec.sta, 6); p += 6;
            memcpy(pkt + p, rec.bssid, 6); p += 6;
            pkt[p++] = 0x00; pkt[p++] = 0x00;
            pkt[p++] = 0x00; pkt[p++] = 0x00;

            uint8_t snap[8] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E };
            memcpy(pkt + p, snap, 8); p += 8;

            memcpy(pkt + p, rec.eapol_m4, rec.eapol_m4_len); p += rec.eapol_m4_len;
            
            size_t w = writePcapngPacket(pkt, p, rec.rssi, rec.channel, ts++, buffer + offset, max_len - offset);
            if (w == 0) return offset;
            offset += w;
        }
    }

    return offset;
}

} // namespace format
} // namespace politician
