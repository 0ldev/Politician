# Security Policy

## Supported versions

| Version | Supported |
|---------|-----------|
| Latest (`main`) | ✅ Yes |
| Older tagged releases | ❌ No — please update |

## Reporting a vulnerability

**Do not open a public GitHub issue for security vulnerabilities.**

This is a WiFi security research library. A vulnerability in the library itself (e.g. a buffer overflow in packet parsing, an ISR race condition that corrupts memory, or a flaw in the PCAPNG writer that could affect the host machine processing capture files) should be disclosed privately so it can be fixed before becoming public knowledge.

### How to report

Open a [GitHub Security Advisory](https://github.com/0ldev/Politician/security/advisories/new) — this creates a private, encrypted channel between you and the maintainer.

Please include:

- A description of the vulnerability and its potential impact
- The affected file(s) and line numbers if known
- A minimal reproduction case (sketch, packet trace, or pseudocode)
- Your suggested fix if you have one

### What to expect

- **Acknowledgement** within 72 hours
- **Status update** (confirmed / not reproducible / out of scope) within 7 days
- **Fix and coordinated disclosure** within 30 days for confirmed issues, with credit to the reporter in the release notes unless you prefer to remain anonymous

## Scope

### In scope
- Buffer overflows or memory corruption in packet parsing (`Politician.cpp`, `PoliticianFormat.cpp`, etc.)
- Race conditions in the ISR dispatcher or state machine
- Logic flaws in the EAPOL / PMKID capture path that produce silently incorrect results
- Issues in `PoliticianStorage.h` that could corrupt or expose data on the host machine

### Out of scope
- The inherent capabilities of the library (capturing EAPOL handshakes is the documented purpose)
- Vulnerabilities in third-party tools used to process output (Hashcat, hcxtools, etc.)
- Issues in example sketches that only affect the ESP32 device running them
- Social engineering
