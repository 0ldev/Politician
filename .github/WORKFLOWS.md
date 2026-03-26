# GitHub Workflows

This directory contains automated workflows for the Politician library.

## documentation.yml

Automatically builds and deploys Doxygen documentation to GitHub Pages.

**Triggers:**
- Push to `main` or `master` branch
- Manual workflow dispatch

**Actions:**
1. Installs Doxygen and Graphviz
2. Generates HTML documentation
3. Deploys to GitHub Pages

**Setup Required:**
1. Go to repository Settings → Pages
2. Set Source to "GitHub Actions"
3. The workflow will automatically deploy on next push

## build-examples.yml

Validates that all examples compile successfully across different ESP32 boards.

**Triggers:**
- Push to `main` or `master` branch  
- Pull requests

**Actions:**
- Compiles all examples using PlatformIO
- Tests on ESP32, ESP32-S2, ESP32-S3, and ESP32-C3 boards
- Fails if any example doesn't compile

This ensures code quality and prevents breaking changes.
