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

Regulatory-domain behavior remains intentionally out of scope until a country
source and DFS policy can be defined safely.