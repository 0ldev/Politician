# Implementation Plan

Base: `upstream/main`
Branch: `feat/core-reliability`

1. Add the existing ESP-NOW capture example to the PlatformIO CI matrix.
2. Add deterministic host-side coverage for public `Config` validation without
   including Arduino or ESP32 runtime headers.
3. Preserve the public API while making `begin()` validation reject an unsafe
   dwell correction and making stop/rebegin release per-instance runtime
   resources.
4. Document lifecycle, callback lifetime/threading constraints, compatibility,
   and the regulatory-domain proposal.

Security remediation completed on the current branch:

5. Make `begin()` roll back registry, Wi-Fi, mutex, ringbuffer, and worker
   resources on every post-registration failure.
6. Make `stop()` mark shutdown and unregister under an ISR-safe critical
   section, wait for in-flight callbacks, then wait for the worker before
   deleting the ringbuffer and mutex.
7. Keep the ESP-NOW example's MAC output masked and payload output disabled by
   default. Full payload diagnostics require `ESP_NOW_DIAGNOSTICS=1`.

Regulatory-domain behavior remains intentionally out of scope until a country
source and DFS policy can be defined safely.