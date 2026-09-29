#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace udcap::motive {

inline constexpr char batteryProperty[] = "Battery";
inline constexpr char signalStrengthProperty[] = "Signal Strength";

inline std::string deviceName(std::string_view serial) {
    return "UdCapCD_" + std::string(serial);
}

// Motive's glove example exposes battery as a percentage and signal strength
// as the magnitude of a signed RSSI reading. -1 means unavailable.
inline int batteryPercent(std::uint8_t level) {
    return level >= 1 && level <= 5 ? static_cast<int>(level) * 20 : -1;
}

inline int signalStrength(std::int16_t rssiDbm) {
    if (rssiDbm == INT16_MIN || rssiDbm == 0) return -1;
    return std::clamp(std::abs(static_cast<int>(rssiDbm)), 1, 128);
}

} // namespace udcap::motive
