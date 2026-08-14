# Change Summary

## Changes

- Added `EspNowCapture` to the example build matrix and restored its example.
- Added the ESP-NOW callback record/parser contract.
- Added deterministic host-side `Config` validation tests.
- Added idempotent stop/rebegin cleanup for per-instance task, ring buffer, and
  mutex resources.
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