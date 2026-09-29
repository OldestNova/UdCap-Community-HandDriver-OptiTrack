#include <OptiTrackWire.h>
#include "GloveMetadata.h"
#include <AnalogFrame.h>
#include <IDeviceFactory.h>
#include <IDeviceManager.h>
#include <PluginDevice.h>
#include <nng/nng.h>
#include <nng/protocol/pubsub0/sub.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using udcap::optitrack::Frame;
class Bridge;
Bridge *bridge = nullptr;
std::mutex bridgeMutex;

// The official Unity prefab's local X points opposite Motive's +X for both
// hands. Reflecting X also changes Unity handedness to Motive's right-handed
// local space: quaternion Y/Z change sign; X/W remain unchanged.
udcap::optitrack::Quaternion motiveRotation(udcap::optitrack::Quaternion q) {
    const float squared = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (!std::isfinite(squared) || squared < 0.0001f) return {0, 0, 0, 1};
    const float scale = 1.0f / std::sqrt(squared);
    return {q.x * scale, -q.y * scale, -q.z * scale, q.w * scale};
}

class GloveDevice final : public AnalogSystem::cPluginDevice<float> {
public:
    GloveDevice(std::string serial, std::string name, std::uint8_t hand)
        : serial_(std::move(serial)), name_(std::move(name)) {
        SetProperty(kNamePropName, name_.c_str());
        SetProperty(kDisplayNamePropName, name_.c_str());
        SetProperty(kModelPropName, "UdCapCD Glove");
        SetProperty(kSerialPropName, serial_.c_str());
        SetProperty(kDeviceTypePropName, static_cast<int>(AnalogSystem::DeviceType_Glove));
        SetProperty(kOrderPropName, static_cast<int>(hand));
        SetProperty(kRatePropName, 120.0);
        SetProperty(kUseDriftCorrectionPropName, true);
        SetProperty(kConnectedPropName, true);
        AddProperty(udcap::motive::batteryProperty, -1, "Settings", false);
        AddProperty(udcap::motive::signalStrengthProperty, -1, "Settings", false);
        ModifyProperty(udcap::motive::batteryProperty, true, false, false, false);
        ModifyProperty(udcap::motive::signalStrengthProperty, true, false, false, false);
        constexpr std::array<const char *, 5> finger{"T", "I", "M", "R", "P"};
        constexpr std::array<const char *, 3> joint{"MCP", "PIP", "DIP"};
        constexpr std::array<const char *, 4> axis{"W", "X", "Y", "Z"};
        for (int f = 0; f < 5; ++f)
            for (int j = 0; j < 3; ++j)
                for (int a = 0; a < 4; ++a) {
                    char label[32]{};
                    sprintf_s(label, "%s-%s %s", finger[f], joint[j], axis[a]);
                    const int channel = AddChannelDescriptor(label, AnalogSystem::ChannelType_Float);
                    ChannelDescriptor(channel)->SetProperty(AnalogSystem::ChannelProp_Enabled, true);
                }
    }
    ~GloveDevice() override { StopCapture(); }
    bool StartCapture() override {
        if (running_) return true;
        if (!AnalogSystem::cPluginDevice<float>::StartCapture()) return false;
        running_ = true;
        worker_ = std::thread([this] { collect(); });
        return true;
    }
    bool StopCapture() override {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        return AnalogSystem::cPluginDevice<float>::StopCapture();
    }
private:
    void collect();
    std::string serial_, name_;
    std::atomic_bool running_{false};
    std::thread worker_;
};

class GloveFactory final : public AnalogSystem::PluginDeviceFactory {
public:
    GloveFactory(const std::string &serial, std::uint8_t hand)
        : PluginDeviceFactory(udcap::motive::deviceName(serial).c_str()),
          serial_(serial), hand_(hand) {}
    const char *Name() const override { return "UdCapCD Glove"; }
    std::unique_ptr<AnalogSystem::IDevice> Create() const override {
        return std::make_unique<GloveDevice>(serial_, DeviceName(), hand_);
    }
private:
    std::string serial_;
    std::uint8_t hand_;
};

class Bridge {
public:
    explicit Bridge(AnalogSystem::IDeviceManager *manager) : manager_(manager) {
        if (nng_sub0_open(&socket_) != 0) return;
        open_ = true;
        if (nng_setopt(socket_, NNG_OPT_SUB_SUBSCRIBE, "", 0) != 0) return;
        nng_setopt_ms(socket_, NNG_OPT_RECVTIMEO, 250);
        if (nng_dial(socket_, udcap::optitrack::endpoint, nullptr, NNG_FLAG_NONBLOCK) != 0) return;
        running_ = true;
        worker_ = std::thread([this] { receive(); });
    }
    ~Bridge() {
        running_ = false;
        if (open_) nng_close(socket_);
        if (worker_.joinable()) worker_.join();
    }
    bool latest(const std::string &serial, Frame &out) {
        std::lock_guard lock(mutex_);
        const auto it = frames_.find(serial);
        if (it == frames_.end() ||
            Clock::now() - it->second.arrived > std::chrono::seconds(2)) return false;
        out = it->second.frame;
        return true;
    }
private:
    struct Recent { Frame frame; Clock::time_point arrived; };
    static bool valid(const Frame &f) {
        if (f.frameMagic != udcap::optitrack::magic || f.frameVersion != udcap::optitrack::version ||
            (f.hand != 1 && f.hand != 2) || f.serial[0] == 0 ||
            std::memchr(f.serial, 0, sizeof(f.serial)) == nullptr) return false;
        const std::string serial(f.serial);
        if (serial.back() != (f.hand == 1 ? 'L' : 'R') ||
            !std::all_of(serial.begin(), serial.end(), [](unsigned char c) {
                return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            })) return false;
        for (const auto &q : f.joints) {
            const float squared = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
            if (!std::isfinite(squared) || squared < 0.0001f || squared > 4.0f) return false;
        }
        return true;
    }
    void receive() {
        while (running_) {
            Frame frame{};
            size_t length = sizeof(frame);
            const int result = nng_recv(socket_, &frame, &length, 0);
            if (!running_) break;
            if (result == 0 && length == sizeof(frame) && valid(frame)) {
                const std::string serial(frame.serial);
                bool add = false;
                {
                    std::lock_guard lock(mutex_);
                    frames_[serial] = {frame, Clock::now()};
                    add = known_.emplace(serial, frame.hand).second;
                }
                if (add && !manager_->AddDevice(std::make_unique<GloveFactory>(serial, frame.hand))) {
                    std::lock_guard lock(mutex_);
                    known_.erase(serial);
                }
            }
            std::vector<std::string> stale;
            {
                std::lock_guard lock(mutex_);
                for (const auto &[serial, side] : known_) {
                    const auto it = frames_.find(serial);
                    if (it != frames_.end() && Clock::now() - it->second.arrived > std::chrono::seconds(5))
                        stale.push_back(serial);
                }
                for (const auto &serial : stale) {
                    known_.erase(serial);
                    frames_.erase(serial);
                }
            }
            for (const auto &serial : stale)
                manager_->RemoveDevice(udcap::motive::deviceName(serial).c_str());
        }
    }
    AnalogSystem::IDeviceManager *manager_;
    nng_socket socket_{};
    bool open_ = false;
    std::atomic_bool running_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::map<std::string, Recent> frames_;
    std::map<std::string, std::uint8_t> known_;
};

bool readLatest(const std::string &serial, Frame &frame) {
    std::lock_guard lock(bridgeMutex);
    return bridge && bridge->latest(serial, frame);
}

void GloveDevice::collect() {
    auto lastProperties = Clock::now();
    bool telemetryAvailable = false;
    while (running_) {
        bool enabled = true;
        GetProperty(kEnabledPropName, enabled);
        Frame frame{};
        if (enabled && readLatest(serial_, frame)) {
            const long id = FrameCounter();
            if (auto *output = BeginFrameUpdate(id, false)) {
                auto *channels = output->ChannelData();
                for (std::size_t i = 0; i < frame.joints.size(); ++i) {
                    const auto q = motiveRotation(frame.joints[i]);
                    channels[i*4 + 0] = q.w;
                    channels[i*4 + 1] = q.x;
                    channels[i*4 + 2] = q.y;
                    channels[i*4 + 3] = q.z;
                }
                output->SetID(id);
                output->SetFlag(0);
                EndFrameUpdate();
                SetFrameCounter(id + 1);
            }
            if (!telemetryAvailable || Clock::now() - lastProperties >= std::chrono::seconds(1)) {
                SetProperty(udcap::motive::batteryProperty,
                    udcap::motive::batteryPercent(frame.batteryLevel));
                SetProperty(udcap::motive::signalStrengthProperty,
                    udcap::motive::signalStrength(frame.rssiDbm));
                lastProperties = Clock::now();
                telemetryAvailable = true;
            }
        } else if (telemetryAvailable) {
            SetProperty(udcap::motive::batteryProperty, -1);
            SetProperty(udcap::motive::signalStrengthProperty, -1);
            telemetryAvailable = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

std::unique_ptr<Bridge> instance;
void stopBridge() {
    std::unique_ptr<Bridge> retired;
    {
        std::lock_guard lock(bridgeMutex);
        bridge = nullptr;
        retired = std::move(instance);
    }
    // Joining the receiver can remove devices; do it without blocking their
    // collection threads from observing the disconnected bridge.
    retired.reset();
}
} // namespace

extern "C" __declspec(dllexport) void DLLVersion(int, int, int,
        int &major, int &minor, int &revision) {
    major = 1;
    minor = 0;
    revision = 0;
}
extern "C" __declspec(dllexport) int DLLEnumerateDeviceFactories(
        AnalogSystem::IDeviceManager *manager) {
    stopBridge();
    auto next = std::make_unique<Bridge>(manager);
    std::lock_guard lock(bridgeMutex);
    instance = std::move(next);
    bridge = instance.get();
    return 0;
}
extern "C" __declspec(dllexport) int PluginDLLUnload(AnalogSystem::IDeviceManager *) {
    stopBridge();
    return 0;
}
