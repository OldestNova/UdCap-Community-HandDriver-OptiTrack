#include <OptiTrackWire.h>
#include <cassert>
#include <cmath>
#include <cstring>

int main() {
    using namespace udcap::optitrack;
    Frame source{};
    source.hand = 1;
    std::strcpy(source.serial, "UDXST400049L");
    source.joints[0] = {0.1f, 0.2f, 0.3f, 0.9f};
    std::array<std::byte, sizeof(Frame)> bytes{};
    std::memcpy(bytes.data(), &source, sizeof(source));
    Frame decoded{};
    std::memcpy(&decoded, bytes.data(), bytes.size());
    assert(decoded.frameMagic == magic && decoded.frameVersion == version);
    assert(decoded.hand == 1 && std::strcmp(decoded.serial, "UDXST400049L") == 0);
    assert(std::abs(decoded.joints[0].z - 0.3f) < 0.00001f);
}
