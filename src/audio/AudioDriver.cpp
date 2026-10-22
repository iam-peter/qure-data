#include "AudioDriver.hpp"

#include "Engine.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

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

// Playback and capture are separate devices. miniaudio's duplex mode would
// be simpler, but over PulseAudio (and PipeWire's PulseAudio server) it
// stalls the callback for up to 100 ms and audio crackles. Captured audio
// reaches the playback callback, which drives the engine, through a
// lock-free ring; the two devices run on their own clocks, so the ring
// also absorbs their drift.
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

        const auto& cfg = engine_.config();
        ma_device_config dc = deviceConfig(ma_device_type_playback);
        dc.playback.format = ma_format_f32;
        dc.playback.channels = static_cast<ma_uint32>(cfg.outputChannels);
        dc.playback.pDeviceID = deviceId(true, settings_.playbackDevice);
        dc.noPreSilencedOutputBuffer = MA_TRUE; // the engine writes every sample
        dc.dataCallback = &DeviceDriver::playbackCallback;
        if (ma_device_init(&context_, &dc, &playback_) != MA_SUCCESS) {
            error_ = "could not open an audio device";
            stop();
            return false;
        }
        playbackOk_ = true;
        if (cfg.inputChannels > 0) {
            if (ma_pcm_rb_init(ma_format_f32, static_cast<ma_uint32>(cfg.inputChannels), kRingFrames, nullptr,
                               nullptr, &ring_) != MA_SUCCESS) {
                error_ = "out of memory";
                stop();
                return false;
            }
            ringOk_ = true;
            input_.assign(static_cast<std::size_t>(kChunkFrames * cfg.inputChannels), 0.0f);
        }
        if (ma_device_start(&playback_) != MA_SUCCESS) {
            error_ = "could not start the audio device";
            stop();
            return false;
        }
        running_ = true;
        name_ = playback_.playback.name;
        if (settings_.enableInput)
            setInputEnabled(true); // without input, playback still runs
        return true;
    }

    void stop() override {
        closeCapture();
        if (playbackOk_) {
            ma_device_uninit(&playback_); // blocks until the callback has returned
            playbackOk_ = false;
        }
        if (ringOk_) {
            ma_pcm_rb_uninit(&ring_);
            ringOk_ = false;
        }
        if (contextOk_) {
            ma_context_uninit(&context_);
            contextOk_ = false;
        }
        running_ = false;
    }

    bool setInputEnabled(bool on) override {
        settings_.enableInput = on;
        if (!running_ || on == captureOk_)
            return true;
        if (!on) {
            closeCapture();
            return true;
        }
        if (!ringOk_) {
            error_ = "the engine has no input channels";
            return false;
        }
        ma_device_config dc = deviceConfig(ma_device_type_capture);
        dc.capture.format = ma_format_f32;
        dc.capture.channels = static_cast<ma_uint32>(engine_.config().inputChannels);
        dc.capture.pDeviceID = deviceId(false, settings_.captureDevice);
        dc.dataCallback = &DeviceDriver::captureCallback;
        if (ma_device_init(&context_, &dc, &capture_) != MA_SUCCESS) {
            error_ = "could not open the audio input";
            return false;
        }
        captureOk_ = true;
        if (ma_device_start(&capture_) != MA_SUCCESS) {
            error_ = "could not start the audio input";
            closeCapture();
            return false;
        }
        inputLive_.store(true, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool running() const override { return running_; }
    [[nodiscard]] std::string description() const override {
        if (!running_)
            return "audio stopped";
        std::string d = std::string(ma_get_backend_name(context_.backend)) + ": " + name_;
        if (captureOk_)
            return d + " (in/out)";
        return d + " (out)";
    }
    [[nodiscard]] std::string lastError() const override { return error_; }
    [[nodiscard]] double load() const override { return load_.load(std::memory_order_relaxed); }

private:
    static constexpr ma_uint32 kRingFrames = 16384; ///< about 1/3 s at 48 kHz
    static constexpr ma_uint32 kChunkFrames = 4096; ///< engine calls per callback slice

    ma_device_config deviceConfig(ma_device_type type) {
        ma_device_config dc = ma_device_config_init(type);
        dc.sampleRate = static_cast<ma_uint32>(engine_.config().sampleRate);
        dc.periodSizeInFrames = static_cast<ma_uint32>(settings_.bufferFrames);
        dc.performanceProfile = ma_performance_profile_low_latency;
        dc.pUserData = this;
        return dc;
    }

    /// The id of device `index`, or null for the system default.
    const ma_device_id* deviceId(bool playback, int index) {
        ma_device_info* playbackInfos = nullptr;
        ma_device_info* captureInfos = nullptr;
        ma_uint32 nPlayback = 0, nCapture = 0;
        if (index < 0 || ma_context_get_devices(&context_, &playbackInfos, &nPlayback, &captureInfos, &nCapture) !=
                             MA_SUCCESS)
            return nullptr;
        if (playback)
            return static_cast<ma_uint32>(index) < nPlayback ? &playbackInfos[index].id : nullptr;
        return static_cast<ma_uint32>(index) < nCapture ? &captureInfos[index].id : nullptr;
    }

    void closeCapture() {
        if (!captureOk_)
            return;
        inputLive_.store(false, std::memory_order_release);
        ma_device_uninit(&capture_);
        captureOk_ = false;
    }

    static void captureCallback(ma_device* device, void*, const void* input, ma_uint32 frames) {
        auto* self = static_cast<DeviceDriver*>(device->pUserData);
        const auto channels = device->capture.channels;
        const auto* in = static_cast<const float*>(input);
        // When the ring is full the rest is dropped; the reader then skips
        // ahead anyway.
        while (frames > 0) {
            ma_uint32 n = frames;
            void* dst = nullptr;
            if (ma_pcm_rb_acquire_write(&self->ring_, &n, &dst) != MA_SUCCESS || n == 0)
                return;
            std::memcpy(dst, in, n * channels * sizeof(float));
            ma_pcm_rb_commit_write(&self->ring_, n);
            in += n * channels;
            frames -= n;
        }
    }

    static void playbackCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto* self = static_cast<DeviceDriver*>(device->pUserData);
        const auto t0 = std::chrono::steady_clock::now();
        self->render(static_cast<float*>(output), frames);
        const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double available = frames / static_cast<double>(device->sampleRate);
        const double l = self->load_.load(std::memory_order_relaxed);
        self->load_.store(l + 0.05 * (spent / available - l), std::memory_order_relaxed);
    }

    void render(float* output, ma_uint32 frames) {
        const auto nout = static_cast<ma_uint32>(engine_.config().outputChannels);
        const bool live = ringOk_ && inputLive_.load(std::memory_order_acquire);
        int slip = ringOk_ ? balanceInput(live, frames) : 0;
        while (frames > 0) {
            const ma_uint32 n = std::min(frames, kChunkFrames);
            engine_.process(live ? readInput(n, std::exchange(slip, 0)) : nullptr, output, static_cast<int>(n));
            output += n * nout;
            frames -= n;
        }
    }

    /// Keeps about two device periods of input queued: one period of jitter
    /// between the callbacks is common (PulseAudio), and the second adds
    /// only a few milliseconds of latency. The two clocks drift apart by
    /// around 0.1 %, so the queue is steered back by repeating (-1) or
    /// dropping (+1) a single frame per callback, which is inaudible.
    int balanceInput(bool live, ma_uint32 frames) {
        const ma_uint32 queued = ma_pcm_rb_available_read(&ring_);
        const double period = settings_.bufferFrames;
        const double target = frames + 2 * period;
        if (!live) {
            ma_pcm_rb_seek_read(&ring_, queued);
            primed_ = false;
            return 0;
        }
        if (!primed_) {
            primed_ = queued >= target; // play silence until then
            level_ = queued;
            return 0;
        }
        if (queued > target + 8 * period) { // a device stalled; catch up at once
            ma_pcm_rb_seek_read(&ring_, queued - static_cast<ma_uint32>(target));
            level_ = target;
            return 0;
        }
        // The queue level jumps by whole capture periods; steer by its mean.
        level_ += 0.01 * (queued - level_);
        if (level_ < target)
            return -1;
        if (level_ > target + period)
            return 1;
        return 0;
    }

    /// The next `frames` captured frames, interleaved; zeros where the ring
    /// ran dry, after which it fills up again.
    const float* readInput(ma_uint32 frames, int slip) {
        const auto channels = static_cast<ma_uint32>(engine_.config().inputChannels);
        if (slip > 0)
            ma_pcm_rb_seek_read(&ring_, 1);
        const ma_uint32 want = slip < 0 ? frames - 1 : frames;
        ma_uint32 got = 0;
        while (primed_ && got < want) {
            ma_uint32 n = want - got;
            void* src = nullptr;
            if (ma_pcm_rb_acquire_read(&ring_, &n, &src) != MA_SUCCESS || n == 0) {
                primed_ = false;
                break;
            }
            std::memcpy(input_.data() + got * channels, src, n * channels * sizeof(float));
            ma_pcm_rb_commit_read(&ring_, n);
            got += n;
        }
        if (slip < 0 && got == want && got > 0) {
            std::copy_n(input_.begin() + (got - 1) * channels, channels, input_.begin() + got * channels);
            ++got;
        }
        std::fill(input_.begin() + got * channels, input_.begin() + frames * channels, 0.0f);
        return input_.data();
    }

    Engine& engine_;
    AudioSettings settings_;
    ma_context context_{};
    ma_device playback_{}, capture_{};
    ma_pcm_rb ring_{};
    std::vector<float> input_; ///< interleaved, kChunkFrames long
    bool contextOk_ = false, playbackOk_ = false, captureOk_ = false, ringOk_ = false, running_ = false;
    bool primed_ = false; ///< audio thread only, as is level_
    double level_ = 0;    ///< mean of the input queue length, frames
    std::atomic<bool> inputLive_{false};
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
