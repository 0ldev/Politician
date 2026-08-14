# Change Summary

## Changes

- Added `EspNowCapture` to the example build matrix and restored its example.
- Added the ESP-NOW callback record/parser contract.
- Added deterministic host-side `Config` validation tests.
- Added idempotent stop/rebegin cleanup for per-instance task, ring buffer, and
  mutex resources.
- Made `begin()` transactional after instance registration, including Wi-Fi
  driver teardown when that attempt initialized Wi-Fi.
- Added an ISR-safe shutdown barrier: registry removal and shutdown marking are
  serialized, in-flight callbacks drain, and the worker exits before its
  ringbuffer and mutex are destroyed.
- Removed the duplicate `[env:esp32]` section from `EspNowCapture`.
- Masked ESP-NOW MACs and disabled payload printing by default in the example;
  complete payload diagnostics are compile-time opt-in.
- Documented lifecycle and callback contracts.

## Breaking Changes

None. Existing public methods and callback signatures remain compatible.

## Migration Scope

None. ESP-NOW is available by default and can be excluded with
`POLITICIAN_NO_ESPNOW`.

## Rollback Strategy

Revert the commits in reverse order. No database, persistent storage, or
external state migration is introduced.

## Known Limitations

- PlatformIO is not installed in the local environment, so ESP32 builds were
  not run locally.
- Regulatory-domain selection is documented as a proposal only.
- The host test covers public configuration validation, not ESP32 FreeRTOS
  scheduling or WiFi-driver behavior.
- PlatformIO is unavailable in the local environment; the native test was
  compiled and executed directly with Clang, while the ESP32 example build was
  not run locally.
- `stop()` must not be called from a Politician callback running on the worker
  task, because waiting for that same worker would deadlock. Callbacks should
  signal application state and let the main/application task perform stop.