#!/bin/bash
# ==============================================================================
# Politician Library - Example Compilation Script
# ==============================================================================
# This script compiles all examples to ensure they build successfully
# Usage: ./compile_examples.sh
# ==============================================================================

set -e  # Exit on error

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
EXAMPLES_DIR="$SCRIPT_DIR/examples"
LIB_DIR="$SCRIPT_DIR/src"
BOARD="esp32dev"
PLATFORM="espressif32@~4.4.0"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "  Politician Library - Example Builder"
echo "========================================"
echo ""

# Counter for results
TOTAL=0
PASSED=0
FAILED=0
FAILED_EXAMPLES=()

# Find all .ino files in examples directory
for example_dir in "$EXAMPLES_DIR"/*/; do
    example_name=$(basename "$example_dir")
    ino_file="$example_dir/${example_name}.ino"
    
    # Skip if no matching .ino file exists
    if [ ! -f "$ino_file" ]; then
        echo -e "${YELLOW}[SKIP]${NC} No .ino file found for $example_name"
        continue
    fi
    
    TOTAL=$((TOTAL + 1))
    echo -e "${YELLOW}[BUILD]${NC} Compiling: $example_name"
    
    # Compile using pio ci
    if pio ci \
        --lib="$LIB_DIR" \
        --board="$BOARD" \
        --project-option="platform=$PLATFORM" \
        --project-option="framework=arduino" \
        "$ino_file" \
        > "/tmp/pio_build_${example_name}.log" 2>&1; then
        
        echo -e "${GREEN}[PASS]${NC} $example_name compiled successfully"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}[FAIL]${NC} $example_name failed to compile"
        echo "       Log: /tmp/pio_build_${example_name}.log"
        FAILED=$((FAILED + 1))
        FAILED_EXAMPLES+=("$example_name")
    fi
    echo ""
done

echo "========================================"
echo "  Build Summary"
echo "========================================"
echo "Total:   $TOTAL"
echo -e "${GREEN}Passed:  $PASSED${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Failed:  $FAILED${NC}"
    echo ""
    echo "Failed examples:"
    for example in "${FAILED_EXAMPLES[@]}"; do
        echo -e "  ${RED}✗${NC} $example"
        echo "    Log: /tmp/pio_build_${example}.log"
    done
    exit 1
else
    echo -e "${GREEN}All examples compiled successfully!${NC}"
    exit 0
fi
