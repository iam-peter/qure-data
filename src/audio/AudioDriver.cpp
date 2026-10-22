#include "AudioDriver.hpp"

#include "Engine.hpp"

#include <miniaudio.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace qure {
namespace {

// ---------------------------------------------------------------------------

class TimerDriver final : public AudioDriver {
public:
    explicit TimerDriver(Engine& engine) : engine_(engine) {}
    ~TimerDriver() override { stop(); }

    bool start() override {
        if (thread_.joinable())
            return true;
        // A plain thread and flag: Apple's libc++ has no std::jthread yet.
        stop_.store(false);
        thread_ = std::thread([this] { run(); });
        return true;
    }
    void stop() override {
        if (thread_.joinable()) {
            stop_.store(true);
            thread_.join();
        }
    }
    [[nodiscard]] bool running() const override { return thread_.joinable(); }
    [[nodiscard]] std::string description() const override { return "no audio device (timer)"; }

private:
    void run() {
        using clock = std::chrono::steady_clock;
        const auto& cfg = engine_.config();
        const auto block = std::chrono::duration<double>(cfg.blockSize / cfg.sampleRate);
        // Wake about every 5 ms and catch up on all due blocks.
        const auto wake = std::chrono::milliseconds(5);
        auto next = clock::now();
        while (!stop_.load()) {
            const auto now = clock::now();
            if (now - next > std::chrono::milliseconds(200))
                next = now; // we were suspended; don't try to catch up
            while (next <= now) {
                engine_.tick();
                next += std::chrono::duration_cast<clock::duration>(block);
            }
            std::this_thread::sleep_for(wake);
        }
    }

    Engine& engine_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
};

// ---------------------------------------------------------------------------

class DeviceDriver final : public AudioDriver {
public:
    DeviceDriver(Engine& engine, const AudioSettings& settings) : engine_(engine), settings_(settings) {}
    ~DeviceDriver() override { stop(); }

    bool start() override {
        if (running_)
            return true;
        if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS) {
            error_ = "could not initialise any audio backend";
            return false;
        }
        contextOk_ = true;

        ma_device_info* playback = nullptr;
        ma_device_info* capture = nullptr;
        ma_uint32 nPlayback = 0, nCapture = 0;
        ma_context_get_devices(&context_, &playback, &nPlayback, &capture, &nCapture);

        const auto& cfg = engine_.config();
        auto tryInit = [&](ma_device_type type) {
            ma_device_config dc = ma_device_config_init(type);
            dc.sampleRate = static_cast<ma_uint32>(cfg.sampleRate);
            dc.periodSizeInFrames = static_cast<ma_uint32>(settings_.bufferFrames);
            dc.performanceProfile = ma_performance_profile_low_latency;
            dc.playback.format = ma_format_f32;
            dc.playback.channels = static_cast<ma_uint32>(cfg.outputChannels);
            if (settings_.playbackDevice >= 0 && static_cast<ma_uint32>(settings_.playbackDevice) < nPlayback)
                dc.playback.pDeviceID = &playback[settings_.playbackDevice].id;
            dc.capture.format = ma_format_f32;
            dc.capture.channels = static_cast<ma_uint32>(cfg.inputChannels);
            if (settings_.captureDevice >= 0 && static_cast<ma_uint32>(settings_.captureDevice) < nCapture)
                dc.capture.pDeviceID = &capture[settings_.captureDevice].id;
            dc.noPreSilencedOutputBuffer = MA_TRUE; // the engine writes every sample
            dc.dataCallback = &DeviceDriver::callback;
            dc.pUserData = this;
            duplex_ = type == ma_device_type_duplex;
            return ma_device_init(&context_, &dc, &device_) == MA_SUCCESS;
        };

        // Prefer full duplex; fall back to output only (no microphone, or
        // the user disabled input).
        bool ok = settings_.enableInput && cfg.inputChannels > 0 && tryInit(ma_device_type_duplex);
        if (!ok)
            ok = tryInit(ma_device_type_playback);
        if (!ok) {
            error_ = "could not open an audio device";
            shutdownContext();
            return false;
        }
        deviceOk_ = true;
        if (ma_device_start(&device_) != MA_SUCCESS) {
            error_ = "could not start the audio device";
            stop();
            return false;
        }
        running_ = true;
        name_ = device_.playback.name;
        return true;
    }

    void stop() override {
        if (deviceOk_) {
            ma_device_uninit(&device_); // blocks until the callback has returned
            deviceOk_ = false;
        }
        shutdownContext();
        running_ = false;
    }

    [[nodiscard]] bool running() const override { return running_; }
    [[nodiscard]] std::string description() const override {
        if (!running_)
            return "audio stopped";
        return std::string(ma_get_backend_name(context_.backend)) + ": " + name_ + (duplex_ ? " (in/out)" : " (out)");
    }
    [[nodiscard]] std::string lastError() const override { return error_; }
    [[nodiscard]] double load() const override { return load_.load(std::memory_order_relaxed); }

private:
    static void callback(ma_device* device, void* output, const void* input, ma_uint32 frames) {
        auto* self = static_cast<DeviceDriver*>(device->pUserData);
        const auto t0 = std::chrono::steady_clock::now();
        self->engine_.process(static_cast<const float*>(input), static_cast<float*>(output),
                              static_cast<int>(frames));
        const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double available = frames / static_cast<double>(device->sampleRate);
        const double l = self->load_.load(std::memory_order_relaxed);
        self->load_.store(l + 0.05 * (spent / available - l), std::memory_order_relaxed);
    }

    void shutdownContext() {
        if (contextOk_) {
            ma_context_uninit(&context_);
            contextOk_ = false;
        }
    }

    Engine& engine_;
    AudioSettings settings_;
    ma_context context_{};
    ma_device device_{};
    bool contextOk_ = false, deviceOk_ = false, running_ = false, duplex_ = false;
    std::string name_, error_;
    std::atomic<double> load_{0.0};
};

std::vector<AudioDeviceInfo> listDevices(bool playbackDevices) {
    std::vector<AudioDeviceInfo> out;
    ma_context ctx;
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS)
        return out;
    ma_device_info* playback = nullptr;
    ma_device_info* capture = nullptr;
    ma_uint32 nPlayback = 0, nCapture = 0;
    if (ma_context_get_devices(&ctx, &playback, &nPlayback, &capture, &nCapture) == MA_SUCCESS) {
        auto* list = playbackDevices ? playback : capture;
        const auto n = playbackDevices ? nPlayback : nCapture;
        for (ma_uint32 i = 0; i < n; ++i)
            out.push_back({list[i].name, static_cast<int>(i), list[i].isDefault != MA_FALSE});
    }
    ma_context_uninit(&ctx);
    return out;
}

} // namespace

std::unique_ptr<AudioDriver> makeTimerDriver(Engine& engine) { return std::make_unique<TimerDriver>(engine); }

std::unique_ptr<AudioDriver> makeDeviceDriver(Engine& engine, const AudioSettings& settings) {
    return std::make_unique<DeviceDriver>(engine, settings);
}

std::vector<AudioDeviceInfo> listPlaybackDevices() { return listDevices(true); }
std::vector<AudioDeviceInfo> listCaptureDevices() { return listDevices(false); }

} // namespace qure
