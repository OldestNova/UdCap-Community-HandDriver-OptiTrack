#include "GloveMetadata.h"

#include <cassert>
#include <string>

int main() {
    assert(udcap::motive::deviceName("UDXST400049L") == "UdCapCD_UDXST400049L");
    assert(std::string(udcap::motive::batteryProperty) == "Battery");
    assert(std::string(udcap::motive::signalStrengthProperty) == "Signal Strength");
    assert(udcap::motive::batteryPercent(0) == -1);
    assert(udcap::motive::batteryPercent(1) == 20);
    assert(udcap::motive::batteryPercent(5) == 100);
    assert(udcap::motive::batteryPercent(6) == -1);
    assert(udcap::motive::signalStrength(INT16_MIN) == -1);
    assert(udcap::motive::signalStrength(0) == -1);
    assert(udcap::motive::signalStrength(-62) == 62);
    assert(udcap::motive::signalStrength(-127) == 127);
}
