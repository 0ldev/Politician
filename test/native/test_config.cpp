#include <cassert>
#include <cstdint>

#define POLITICIAN_HOST_TEST
#include "PoliticianTypes.h"

using namespace politician;

int main() {
    Config defaults;
    const char *warnings[16] = {};
    assert(validateConfig(defaults, warnings, 16) == 0);

    Config invalid;
    invalid.hop_min_dwell_ms = 400;
    invalid.hop_max_dwell_ms = 200;
    invalid.fish_timeout_ms = 100;
    invalid.csa_wait_ms = 100;
    invalid.hop_dwell_ms = 0;
    invalid.deauth_burst_count = 0;
    invalid.csa_beacon_count = 0;
    invalid.probe_aggr_interval_s = 0;
    invalid.min_rssi = -10;
    assert(validateConfig(invalid, warnings, 16) == 8);

    Config overflow;
    overflow.hop_min_dwell_ms = UINT16_MAX;
    assert(validateConfig(overflow, warnings, 16) == 2);

    Config noSmartHopping;
    noSmartHopping.smart_hopping = false;
    noSmartHopping.hop_min_dwell_ms = UINT16_MAX;
    assert(validateConfig(noSmartHopping, warnings, 16) == 0);
    return 0;
}