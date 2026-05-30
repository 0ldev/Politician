#pragma once

/**
 * @file PoliticianProbe.h
 * @brief Opt-in SSID wordlist for hidden network discovery via directed probe requests.
 *
 * Include this header and pass the wordlist to the engine:
 * @code
 *   #include <PoliticianProbe.h>
 *   using namespace politician::probe;
 *
 *   // Enable timed probing and load wordlist
 *   cfg.probe_hidden_interval_ms = 5000;
 *   engine.begin(cfg);
 *   engine.setProbeWordlist(WORDLIST, WORDLIST_COUNT);
 * @endcode
 *
 * When a hidden AP is seen, the engine cycles one SSID from the wordlist per interval
 * (per AP). Each AP keeps its own position, so no word is ever skipped.
 * SSIDs are stored in flash (.rodata) and add ~1–2KB to binary size.
 */

#ifndef ARDUINO
#error "PoliticianProbe.h requires the Arduino framework."
#endif

#include <Arduino.h>

namespace politician {
namespace probe {

static const char * const WORDLIST[] PROGMEM = {
    // ── Router defaults ──────────────────────────────────────────────────────
    "linksys",
    "NETGEAR",
    "default",
    "home",
    "network",
    "wireless",
    "wifi",
    "WIFI",
    "router",
    "gateway",
    "TP-LINK",
    "ASUS",
    "dlink",
    "D-Link",
    "Belkin",
    "belkin.setup",
    "ATT",
    "xfinitywifi",
    "Xfinity",
    "DIRECT",
    "AndroidAP",
    "iPhone",
    // ── Common homelab / IoT SSIDs ───────────────────────────────────────────
    "HomeNetwork",
    "Home Network",
    "MyNetwork",
    "private",
    "hidden",
    "secret",
    "internal",
    "office",
    "corp",
    "corporate",
    "management",
    "mgmt",
    "admin",
    "guest",
    "iot",
    "IoT",
    "lab",
    "testnet",
    "test",
    "dev",
    "staging",
    "prod",
    "backup",
    "secure",
    "staff",
    "employee",
    "CORP",
    "OFFICE",
    "GUEST",
    // ── ISP CPE defaults ─────────────────────────────────────────────────────
    "VODAFONE",
    "Vodafone",
    "BTHub",
    "SkyHub",
    "Virgin Media",
    "Movistar",
    "Claro",
    "Vivo",
    "TIM",
    "OI",
    "NET",
    "Speedy",
    "Technicolor",
    "HomeHub",
    "FiOS",
    "Optimum",
    "Spectrum",
    "Cox",
};

static const uint8_t WORDLIST_COUNT = (uint8_t)(sizeof(WORDLIST) / sizeof(WORDLIST[0]));

} // namespace probe
} // namespace politician
