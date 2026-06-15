#pragma once
/**
 * @file PoliticianSense.h
 * @brief Opt-in passive RSSI-based presence and motion sensing for the Politician engine.
 *
 * Hooks into the engine's promiscuous-mode packet stream and monitors beacon RSSI
 * variance from a fixed anchor AP to detect human presence and motion — device-free,
 * zero mode switching, zero conflicts with the core engine.
 *
 * A human body walking between the anchor AP and the ESP32 scatters and absorbs
 * 2.4GHz/5GHz radio waves, producing measurable fluctuations in received signal
 * strength. By tracking the statistical variance of beacon RSSI over a sliding
 * window, PoliticianSense classifies the monitored space as SENSE_STILL or SENSE_MOTION.
 *
 * Usage:
 * @code
 *   #include <Politician.h>
 *   #include <PoliticianSense.h>
 *
 *   politician::Politician   engine;
 *   politician::PoliticianSense sense;
 *
 *   // Lock the engine to the anchor AP's channel for continuous data.
 *   // During free channel hopping, samples only arrive when the radio
 *   // happens to land on the anchor's channel (coarser, but still works).
 *   uint8_t anchorBssid[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
 *
 *   // LOG_FILTER_BEACONS must be set before engine.begin().
 *   politician::Config cfg;
 *   cfg.capture_filter |= politician::LOG_FILTER_BEACONS;
 *   engine.begin(cfg);
 *   engine.lockChannel(6); // anchor AP's channel
 *
 *   sense.begin(engine, anchorBssid);
 *   sense.setSenseCallback([](politician::SenseEvent ev, float variance) {
 *       if (ev == politician::SENSE_MOTION)
 *           Serial.printf("[SENSE] Motion detected!  variance=%.2f dBm²\n", variance);
 *       else
 *           Serial.printf("[SENSE] Area is still.    variance=%.2f dBm²\n", variance);
 *   });
 *
 *   // In loop():
 *   engine.tick();
 *   sense.tick();
 * @endcode
 *
 * Anchor modes:
 *  - Pass a 6-byte BSSID to anchor to a specific AP (recommended — most stable).
 *  - Call beginBySSID() to look up the BSSID by SSID from the engine cache.
 *  - Pass nullptr to sample all visible APs (noisier, useful without a fixed AP).
 *
 * Threading:
 *  RSSI samples are written from Politician's internal worker task.
 *  tick() and all sense callbacks run on your calling task (usually loop()).
 *  A FreeRTOS spinlock protects the ring buffer across tasks.
 *
 * Packet logger chaining:
 *  PoliticianSense installs itself as the engine's packet logger.
 *  If you also need raw frame access, call sense.setPacketLogger() before begin()
 *  to register a pass-through callback that fires on every frame after sensing.
 *
 * Compile-time tuning:
 *  #define POLITICIAN_SENSE_MAX_WINDOW 128  // Enlarge maximum window (default 64)
 */

#include "Politician.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <string.h>

#ifdef POLITICIAN_NO_STD_FUNCTION
#error "PoliticianSense.h requires std::function (POLITICIAN_NO_STD_FUNCTION must not be defined)."
#endif

namespace politician {

// ─── Sense Events ─────────────────────────────────────────────────────────────

/** @brief State transition delivered to the SenseCb callback. */
enum SenseEvent : uint8_t {
    SENSE_MOTION = 0, ///< RSSI variance spiked above threshold — movement detected.
    SENSE_STILL  = 1, ///< RSSI variance returned to baseline — area quiet.
};

// ─── Callback Type ────────────────────────────────────────────────────────────

/** @brief Callback fired on SENSE_STILL ↔ SENSE_MOTION transitions. */
using SenseCb = std::function<void(SenseEvent event, float variance)>;

// ─── Compile-Time Ceiling ─────────────────────────────────────────────────────

#ifndef POLITICIAN_SENSE_MAX_WINDOW
#define POLITICIAN_SENSE_MAX_WINDOW 64 ///< Maximum ring-buffer capacity (samples).
#endif

// ─── PoliticianSense ──────────────────────────────────────────────────────────

/**
 * @brief Passive RSSI-based motion and presence detector.
 *
 * Hooks into a Politician engine via the packet logger callback and tracks
 * RSSI variance from a fixed anchor AP over a configurable sliding window.
 * Fires a SenseCb whenever the space transitions between quiet and active.
 */
class PoliticianSense {
public:
    // ── Defaults ──────────────────────────────────────────────────────────────
    static constexpr uint8_t  DEFAULT_WINDOW    = 32;    ///< Sliding window (samples).
    static constexpr float    DEFAULT_THRESHOLD  = 6.0f;  ///< Variance threshold (dBm²).
    static constexpr uint32_t DEFAULT_DEBOUNCE   = 2000;  ///< Motion hold-time (ms).
    static constexpr uint32_t DEFAULT_STALE_MS   = 10000; ///< Max gap before ignoring stale data (ms).

    PoliticianSense()
        : _engine(nullptr)
        , _anyAnchor(true)
        , _active(false)
        , _head(0), _count(0), _windowSize(DEFAULT_WINDOW)
        , _threshold(DEFAULT_THRESHOLD)
        , _debounceMs(DEFAULT_DEBOUNCE)
        , _staleMs(DEFAULT_STALE_MS)
        , _mean(0.0f), _variance(0.0f)
        , _state(SENSE_STILL)
        , _lastMotionMs(0), _lastSampleMs(0)
        , _totalSamples(0)
    {
        memset(_anchor, 0, 6);
        memset(_buf, 0, sizeof(_buf));
        _mux = portMUX_INITIALIZER_UNLOCKED;
    }

    // ── Setup ─────────────────────────────────────────────────────────────────

    /**
     * @brief Attaches the sensor to a Politician engine.
     *
     * @param engine      The running Politician instance.
     * @param anchorBssid 6-byte BSSID of the anchor AP, or nullptr to sample every AP.
     *
     * Installs an internal packet logger on the engine. If you also need raw
     * frame access, call sense.setPacketLogger() **before** begin() to chain a
     * pass-through callback. Setting it after begin() is a data race — the
     * engine worker task may be concurrently reading _userPacketCb.
     *
     * The engine must already be initialized (begin() called) before calling this.
     * For continuous sensing, lock the engine to the anchor's channel:
     *   engine.lockChannel(anchorChannel);
     *
     * @note `cfg.capture_filter` must include `LOG_FILTER_BEACONS` before
     * `engine.begin()` is called. Beacons are the primary RSSI source and
     * PoliticianSense will collect no samples without them. Set it in Config:
     * @code
     *   Config cfg;
     *   cfg.capture_filter |= LOG_FILTER_BEACONS;
     *   engine.begin(cfg);
     *   sense.begin(engine, anchorBssid);
     * @endcode
     */
    void begin(Politician &engine, const uint8_t *anchorBssid = nullptr) {
        // Detach from a previous engine if re-anchoring to a different one,
        // so the old engine does not keep a lambda that captures this object.
        if (_engine && _engine != &engine) {
            _engine->setPacketLogger(nullptr);
        }
        _engine = &engine;
        _reset_internal();

        // Update anchor under the same lock used by _onPacket, so the worker task
        // never observes a partially-written anchor during reconfiguration.
        portENTER_CRITICAL_SAFE(&_mux);
        if (anchorBssid && memcmp(anchorBssid, "\x00\x00\x00\x00\x00\x00", 6) != 0) {
            memcpy(_anchor, anchorBssid, 6);
            _anyAnchor = false;
        } else {
            memset(_anchor, 0, 6);
            _anyAnchor = true;
        }
        portEXIT_CRITICAL_SAFE(&_mux);

        _engine->setPacketLogger([this](const uint8_t *payload, uint16_t len,
                                        int8_t rssi, uint8_t ch, uint32_t ts) {
            _onPacket(payload, len, rssi, ch, ts);
        });
        _active = true;
    }

    /**
     * @brief Looks up an AP by SSID in the engine cache and anchors to its BSSID.
     *
     * @param engine The running Politician instance.
     * @param ssid   Exact SSID string to match.
     * @return true if the SSID was found and the sensor was anchored.
     *         false if the SSID is not yet in the engine's AP cache — call again
     *         after the engine has had time to scan.
     *
     * When multiple BSSIDs share the same SSID the strongest signal is chosen.
     */
    bool beginBySSID(Politician &engine, const char *ssid) {
        if (!ssid) return false;
        int best = -999;
        uint8_t bestBssid[6] = {};
        bool found = false;

        engine.forEachAp([&](const ApRecord &ap) {
            if (strncmp(ap.ssid, ssid, sizeof(ap.ssid)) == 0) {
                if (ap.rssi > best) {
                    best = ap.rssi;
                    memcpy(bestBssid, ap.bssid, 6);
                    found = true;
                }
            }
        });

        if (found) begin(engine, bestBssid);
        return found;
    }

    // ── Callbacks ─────────────────────────────────────────────────────────────

    /**
     * @brief Sets the state-change callback.
     * Fired once when the space transitions STILL→MOTION or MOTION→STILL.
     * Do not call engine.tick() or blocking operations from inside the callback.
     */
    void setSenseCallback(SenseCb cb) { _senseCb = cb; }

    /**
     * @brief Registers a pass-through raw-packet callback.
     * Called on every frame after PoliticianSense has processed it,
     * so you can use raw packet access alongside sensing.
     *
     * @note Like all Politician callbacks, this must be set before begin() or
     * after end(). Changing it while the engine is running is not thread-safe.
     */
    void setPacketLogger(Politician::PacketCb cb) { _userPacketCb = cb; }

    // ── Tuning ────────────────────────────────────────────────────────────────

    /**
     * @brief Sets the variance threshold (dBm²) that triggers SENSE_MOTION.
     * Lower values = more sensitive; higher values = less false positives.
     * Useful range: 3.0–15.0. Default: 6.0
     */
    void setThreshold(float dBm2) { _threshold = dBm2; }

    /**
     * @brief Sets the sliding window size in samples (clamped to [4, POLITICIAN_SENSE_MAX_WINDOW]).
     * At ~10 beacons/sec on a locked channel, 32 samples ≈ 3 seconds of history.
     * Smaller = faster response; larger = smoother, fewer false triggers. Default: 32
     */
    void setWindowSize(uint8_t n) {
        if (n < 4) n = 4;
        if (n > POLITICIAN_SENSE_MAX_WINDOW) n = POLITICIAN_SENSE_MAX_WINDOW;
        portENTER_CRITICAL_SAFE(&_mux);
        _windowSize = n;
        _head  = 0;
        _count = 0;
        portEXIT_CRITICAL_SAFE(&_mux);
    }

    /**
     * @brief Sets the debounce hold-time (ms).
     * MOTION state is held for this long after the last variance spike before
     * returning to STILL, preventing rapid flickering during intermittent movement.
     * Default: 2000 ms
     */
    void setDebounce(uint32_t ms) { _debounceMs = ms; }

    /**
     * @brief Sets the stale-data timeout (ms).
     * If no new RSSI samples arrive for this duration, tick() skips processing
     * to avoid acting on stale window data (e.g., when hopping away from the anchor).
     * Default: 10000 ms
     */
    void setStaleTimeout(uint32_t ms) { _staleMs = ms; }

    // ── Worker ────────────────────────────────────────────────────────────────

    /**
     * @brief Main worker — call from loop() alongside engine.tick().
     * Computes variance from the current window snapshot and fires the
     * SenseCb if a STILL↔MOTION transition is detected.
     */
    void tick() {
        if (!_engine) return;

        // Snapshot the ring buffer under the lock
        int8_t  snap[POLITICIAN_SENSE_MAX_WINDOW];
        uint8_t snapCount, snapStart, snapWindow;
        uint32_t lastSample;

        portENTER_CRITICAL_SAFE(&_mux);
        snapCount  = _count;
        snapWindow = _windowSize;
        snapStart  = (snapCount < snapWindow) ? 0 : _head;
        lastSample = _lastSampleMs;
        memcpy(snap, _buf, snapWindow);
        portEXIT_CRITICAL_SAFE(&_mux);

        // Need at least 4 samples before starting the state machine
        if (snapCount < 4) return;

        bool isStale = (_staleMs > 0 && (millis() - lastSample) > _staleMs);

        if (isStale) {
            // No fresh data: decay variance to zero so the debounce can still
            // expire and transition MOTION → STILL. Do not freeze state forever.
            _variance = 0.0f;
            _mean     = 0.0f;
        } else {
            // ── Compute mean ──
            float sum = 0.0f;
            for (uint8_t i = 0; i < snapCount; i++) {
                sum += snap[(snapStart + i) % snapWindow];
            }
            _mean = sum / snapCount;

            // ── Compute variance ──
            float varSum = 0.0f;
            for (uint8_t i = 0; i < snapCount; i++) {
                float d = snap[(snapStart + i) % snapWindow] - _mean;
                varSum += d * d;
            }
            _variance = varSum / snapCount;
        }

        // ── State machine ──
        uint32_t now    = millis();
        SenseEvent prev = _state;

        if (_variance >= _threshold) {
            _lastMotionMs = now;
            _state = SENSE_MOTION;
        } else if (_state == SENSE_MOTION) {
            if ((now - _lastMotionMs) >= _debounceMs) {
                _state = SENSE_STILL;
            }
        }

        if (_state != prev && _senseCb) {
            _senseCb(_state, _variance);
        }
    }

    // ── Accessors ─────────────────────────────────────────────────────────────

    /** @return Current RSSI variance across the window (dBm²). Updated by tick(). */
    float getVariance()        const { return _variance; }

    /** @return Mean RSSI across the window (dBm). Updated by tick(). */
    float getMeanRssi()        const { return _mean; }

    /** @return Current sense state (SENSE_STILL or SENSE_MOTION). */
    SenseEvent getState()      const { return _state; }

    /** @return Total RSSI samples collected since begin(). */
    uint32_t getTotalSamples() const {
        portENTER_CRITICAL_SAFE(&_mux);
        uint32_t n = _totalSamples;
        portEXIT_CRITICAL_SAFE(&_mux);
        return n;
    }

    /** @return True if anchored to a specific BSSID, false if sampling all APs. */
    bool hasAnchor()           const { return !_anyAnchor; }

    /** @return Pointer to the 6-byte anchor BSSID (all-zeros if any-anchor mode). */
    const uint8_t *getAnchor() const { return _anchor; }

    /**
     * @brief Clears the sample window and resets state without detaching from the engine.
     * Useful when the environment changes (furniture moved, AP relocated, etc.).
     */
    void reset() { _reset_internal(); }

    /**
     * @brief Detaches from the engine and clears its packet logger.
     *
     * Sets _active = false first so any _onPacket() invocation already in flight
     * on the engine worker task will bail out immediately without touching members.
     * Then clears the engine's packet logger slot so no further calls are dispatched.
     *
     * @warning This does NOT provide a hard synchronisation barrier. If the engine
     * worker task has already passed the `if (!_active)` guard before end() writes
     * the flag, it may still access members after end() returns. This window is
     * narrow but real on a multi-core ESP32.
     *
     * Safe usage pattern when destroying from a different core/task:
     * @code
     *   sense.end();
     *   delay(20); // > one engine tick — guarantees any in-flight call has returned
     *   // now safe to destroy or reuse
     * @endcode
     *
     * Calling end() from the same task/core as the engine worker (e.g. in loop())
     * is always safe with no delay required.
     */
    void end() {
        if (_engine) {
            // Clear the active flag first. Any _onPacket() call already dispatched
            // by the worker task will see _active=false and return immediately
            // without touching any members, narrowing the use-after-free window.
            _active = false;
            _engine->setPacketLogger(nullptr);
            _engine = nullptr;
        }
    }

    ~PoliticianSense() { end(); }

private:
    Politician *_engine;
    uint8_t     _anchor[6];
    bool        _anyAnchor;
    volatile bool _active; ///< Set true by begin(), false by end(). Guards _onPacket against use-after-free.

    // ── Ring buffer (written from Politician worker task) ─────────────────────
    mutable portMUX_TYPE _mux;
    int8_t  _buf[POLITICIAN_SENSE_MAX_WINDOW];
    uint8_t _head;       ///< Next write position
    uint8_t _count;      ///< Valid samples in window (≤ _windowSize)
    uint8_t _windowSize;

    // ── Config ────────────────────────────────────────────────────────────────
    float    _threshold;
    uint32_t _debounceMs;
    uint32_t _staleMs;

    // ── Computed by tick() ────────────────────────────────────────────────────
    float      _mean;
    float      _variance;
    SenseEvent _state;
    uint32_t   _lastMotionMs;

    // ── Diagnostics ───────────────────────────────────────────────────────────
    volatile uint32_t _lastSampleMs;
    uint32_t _totalSamples;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    SenseCb                _senseCb;
    Politician::PacketCb   _userPacketCb;

    // ── Internal helpers ──────────────────────────────────────────────────────

    void _reset_internal() {
        portENTER_CRITICAL_SAFE(&_mux);
        _head         = 0;
        _count        = 0;
        _totalSamples = 0;
        _lastSampleMs = 0;
        portEXIT_CRITICAL_SAFE(&_mux);
        _mean         = 0.0f;
        _variance     = 0.0f;
        _state        = SENSE_STILL;
        _lastMotionMs = 0;
    }

    void _onPacket(const uint8_t *payload, uint16_t len,
                   int8_t rssi, uint8_t ch, uint32_t ts) {
        if (!_active) return; // Guard against in-flight calls after end()/destructor.
        if (len >= sizeof(ieee80211_hdr_t)) {
            const auto *hdr = reinterpret_cast<const ieee80211_hdr_t *>(payload);
            uint16_t fc = hdr->frame_ctrl;
            // Accept only beacon frames. Probe responses, EAPOLs, and data frames
            // from the same BSSID have different RSSI signatures and contaminate
            // the variance stream, causing false motion detections.
            bool isBeacon = ((fc & FC_TYPE_MASK) == FC_TYPE_MGMT) &&
                            ((fc & FC_SUBTYPE_MASK) == MGMT_SUB_BEACON);
            if (isBeacon) {
                uint32_t now = millis();
                // Single critical section: anchor check and sample write are atomic,
                // preventing a torn anchor read if begin() reconfigures concurrently.
                portENTER_CRITICAL_SAFE(&_mux);
                if (_anyAnchor || memcmp(hdr->addr2, _anchor, 6) == 0) {
                    _buf[_head] = rssi;
                    _head = (_head + 1) % _windowSize;
                    if (_count < _windowSize) _count++;
                    _totalSamples++;
                    _lastSampleMs = now;
                }
                portEXIT_CRITICAL_SAFE(&_mux);
            }
        }
        if (_userPacketCb) _userPacketCb(payload, len, rssi, ch, ts);
    }
};

} // namespace politician
