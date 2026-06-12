/*
 * PassiveSensing.ino
 *
 * Demonstrates PoliticianSense: passive human presence and motion detection
 * using RSSI variance from a fixed anchor AP — no extra hardware, no mode
 * switching, runs alongside the core Politician engine.
 *
 * How it works:
 *   A human body walking between the anchor AP and the ESP32 absorbs and
 *   scatters 2.4GHz WiFi, producing measurable RSSI fluctuations in the
 *   beacon stream. PoliticianSense tracks variance over a sliding window
 *   and fires a callback when the space transitions between STILL and MOTION.
 *
 * Setup:
 *   1. Set ANCHOR_SSID to an AP that is always on and close to the area
 *      you want to monitor (your home router, an IoT hub, etc.).
 *   2. Flash to an ESP32 and open the Serial monitor at 115200 baud.
 *   3. Walk through the monitored space and observe motion events.
 *
 * Tuning:
 *   - If you get false triggers in an empty room, raise SENSE_THRESHOLD.
 *   - If motion is not detected reliably, lower SENSE_THRESHOLD or increase
 *     SENSE_WINDOW to smooth out noise.
 *   - For walls or longer distances, lower the threshold (signal changes are subtler).
 */

#include <Arduino.h>
#include <Politician.h>
#include <PoliticianSense.h>

using namespace politician;

// ── Configuration ──────────────────────────────────────────────────────────────

// SSID of the fixed anchor AP (your router, an IoT hub, etc.)
// The engine scans while hopping; once the SSID is found its BSSID and
// channel are resolved and the radio is locked to that channel.
static const char *ANCHOR_SSID = "YourAP";

// Variance threshold (dBm²). A human body typically causes 3–15 dBm² of variance
// depending on distance, walls, and body orientation.
static const float SENSE_THRESHOLD = 6.0f;

// Sliding window in samples. At ~10 beacons/sec this is ~3 seconds of history.
static const uint8_t SENSE_WINDOW = 32;

// How long (ms) MOTION state is held after the last variance spike.
static const uint32_t SENSE_DEBOUNCE = 2500;

// ── Globals ────────────────────────────────────────────────────────────────────

Politician    engine;
PoliticianSense sense;

bool senseStarted    = false;
uint32_t lastScanMs  = 0;
uint32_t lastPrintMs = 0;

// ── Callbacks ──────────────────────────────────────────────────────────────────

void onSenseEvent(SenseEvent event, float variance) {
    if (event == SENSE_MOTION) {
        Serial.printf("\n[SENSE] *** MOTION DETECTED ***  variance=%.2f dBm²\n", variance);
    } else {
        Serial.printf("\n[SENSE]     Area is still.       variance=%.2f dBm²\n", variance);
    }
}

void onApFound(const ApRecord &ap) {
    Serial.printf("[SCAN] Found: %-32s  ch=%2u  rssi=%ddBm\n",
                  ap.ssid, ap.channel, ap.rssi);
}

// ── Setup ──────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n--- Politician: Passive Sensing Example ---");

    Config cfg;
    cfg.smart_hopping    = true;
    cfg.hop_dwell_ms     = 150;
    cfg.hop_min_dwell_ms = 80;
    cfg.hop_max_dwell_ms = 300;
    // LOG_FILTER_BEACONS must be set before engine.begin() — PoliticianSense
    // only samples beacon frames and receives none without this flag.
    // The "SDMMC ONLY!" warning in PoliticianTypes.h applies to SD logging;
    // in-memory callbacks like PoliticianSense are not affected.
    cfg.capture_filter |= LOG_FILTER_BEACONS;

    if (engine.begin(cfg) != politician::OK) {
        Serial.println("[!] WiFi init failed");
        while (1) delay(100);
    }

    engine.setApFoundCallback(onApFound);
    engine.startHopping();

    Serial.printf("[SENSE] Scanning for anchor AP '%s'...\n", ANCHOR_SSID);
}

// ── Loop ───────────────────────────────────────────────────────────────────────

void loop() {
    engine.tick();

    // Once the engine cache has the anchor AP, lock onto its channel and start sensing.
    if (!senseStarted && millis() - lastScanMs > 2000) {
        lastScanMs = millis();

        // Configure tuning before anchoring so setWindowSize() doesn't discard
        // the first samples that arrive immediately after begin().
        sense.setThreshold(SENSE_THRESHOLD);
        sense.setWindowSize(SENSE_WINDOW);
        sense.setDebounce(SENSE_DEBOUNCE);
        sense.setSenseCallback(onSenseEvent);

        // Find the anchor AP by SSID. Picks the strongest BSSID if multiple match.
        // Also enables LOG_FILTER_BEACONS so beacons reach the packet logger.
        if (sense.beginBySSID(engine, ANCHOR_SSID)) {
            // Lock to the exact channel of the BSSID chosen by beginBySSID.
            // Using getApByBssid(sense.getAnchor()) is required — iterating by
            // SSID string can pick a different BSSID on multi-AP networks.
            ApRecord anchorAp;
            if (engine.getApByBssid(sense.getAnchor(), anchorAp)) {
                engine.lockChannel(anchorAp.channel);
                Serial.printf("[SENSE] Locked to ch%u | anchor: %02X:%02X:%02X:%02X:%02X:%02X\n",
                              anchorAp.channel,
                              sense.getAnchor()[0], sense.getAnchor()[1], sense.getAnchor()[2],
                              sense.getAnchor()[3], sense.getAnchor()[4], sense.getAnchor()[5]);
            }

            senseStarted = true;
            Serial.println("[SENSE] Sensing active. Walk through the monitored area.");
        }
    }

    if (senseStarted) {
        sense.tick();

        // Print live stats every 2 seconds
        if (millis() - lastPrintMs > 2000) {
            lastPrintMs = millis();
            Serial.printf("[SENSE] state=%s  variance=%5.2f dBm²  mean=%5.1f dBm  samples=%lu\n",
                          sense.getState() == SENSE_MOTION ? "MOTION" : "STILL ",
                          sense.getVariance(),
                          sense.getMeanRssi(),
                          (unsigned long)sense.getTotalSamples());
        }
    }
}
