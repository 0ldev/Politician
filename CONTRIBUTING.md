# Contributing to Politician

Thanks for your interest in contributing! Politician is a WiFi security research library for ESP32 — contributions that improve correctness, performance, coverage, or documentation are very welcome.

Please read this guide before opening a PR. It will save everyone time.

---

## Table of Contents

- [What we accept](#what-we-accept)
- [What we don't accept](#what-we-dont-accept)
- [Getting started](#getting-started)
- [Code style](#code-style)
- [Adding an example](#adding-an-example)
- [CI requirements](#ci-requirements)
- [Workflow & CI changes](#workflow--ci-changes)
- [Security disclosures](#security-disclosures)
- [License](#license)

---

## What we accept

- **Bug fixes** — correctness issues in the engine, packet parsing, state machine, or storage helpers
- **New attack/capture primitives** — additions to the 802.11 attack surface with a matching example
- **Platform support** — verified support for new ESP32 variants (ESP32-C6, ESP32-H2, etc.)
- **Performance improvements** — memory reduction, CPU efficiency, ISR safety
- **Documentation fixes** — typos, inaccurate API descriptions, broken links
- **Translations** — translated READMEs (e.g. `README.fr.md`). Must be accurate; we will spot-check technical sections.
- **New examples** — demonstrating real use cases not already covered

## What we don't accept

- Features that require breaking the public API without a strong reason
- Examples that are purely destructive with no research or audit justification
- Dependencies on closed-source or platform-specific external libraries
- CI/workflow changes that pin actions to mutable tags (see [Workflow & CI changes](#workflow--ci-changes))
- Code that introduces undefined behaviour, memory leaks, or raw pointer mismanagement

---

## Getting started

1. **Fork** the repository and clone it locally
2. Create a **feature branch** off `develop` (not `main`):
   ```bash
   git checkout develop
   git checkout -b feat/your-feature-name
   ```
3. Make your changes
4. Test locally with PlatformIO before pushing:
   ```bash
   cd examples/YourExample
   pio run
   ```
5. Open a PR targeting `develop`

> **Note:** `main` is a protected release branch. All contributions go through `develop` first.

---

## Code style

This is a C++ embedded library. Keep it lean.

- **Standard:** C++11 compatible — avoid C++14/17 features; ESP-IDF and Arduino toolchains vary
- **Naming:**
  - Types / classes: `PascalCase` (e.g. `ApRecord`, `HandshakeRecord`)
  - Functions / methods: `camelCase` (e.g. `begin()`, `setTargetFilter()`)
  - Constants / macros: `UPPER_SNAKE_CASE` (e.g. `ATTACK_ALL`, `LOG_FILTER_BEACONS`)
  - Private members: trailing underscore (`config_`, `dispatcher_`)
- **No heap churn in hot paths** — avoid `new`/`delete` or `std::vector` in ISR context or packet callbacks
- **No `Serial.print` in library core** — use the `DEBUG_POLITICIAN` guard macro if you need debug output
- **Header guards:** use `#pragma once`
- **Keep ISR handlers short** — dispatch to the state machine, don't do work in the ISR itself

---

## Adding an example

Every new capture primitive or major feature must ship with a working example.

1. Create a new directory under `examples/` with a `PascalCase` name
2. Include a `platformio.ini` that inherits from the root config:
   ```ini
   [platformio]
   src_dir = .

   [env:esp32dev]
   extends = ../../platformio.ini:env:esp32dev
   lib_deps =
       symlink://../..
   ```
3. Write a `main.cpp` with a clear `setup()` / `loop()` and inline comments explaining what the example demonstrates
4. Add the example name to the matrix in `.github/workflows/build-examples.yml`
5. Add a one-line entry to the examples table in `README.md`

---

## CI requirements

All PRs must pass the full build matrix before they can be merged:

- All **16 example builds** must succeed on `esp32dev`
- If you added a new example, add it to the CI matrix — PRs that add an example but skip CI will be asked to fix this before merge

CI is run automatically on every push to your PR branch. Check the **Actions** tab on your PR for results.

---

## Workflow & CI changes

PRs that modify `.github/workflows/` receive extra scrutiny. This is a security-sensitive library and supply chain hygiene matters.

**Required for any workflow PR:**

- Actions must be pinned to an **immutable commit SHA**, not a floating tag:
  ```yaml
  # ❌ Not acceptable
  uses: actions/upload-artifact@v4

  # ✅ Required
  uses: actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08  # v4.6.2
  ```
- Artifact upload steps must include `if: github.event_name == 'push'` to prevent untrusted PR builds from producing published firmware artifacts
- No new `pull_request_target:` triggers without explicit discussion — this grants secret access to untrusted branches

PRs that don't meet these requirements will be asked to fix them before review.

---

## Security disclosures

If you find a security vulnerability in this library, **do not open a public issue**. Please follow the process in [`SECURITY.md`](SECURITY.md).

---

## License

By contributing, you agree that your contributions will be licensed under the [MIT License](LICENSE) that covers this project.
