#pragma once

#include <memory>
#include <string>
#include <vector>

namespace qure {

class Engine;

struct AudioDeviceInfo {
    std::string name;
    int index = -1; ///< position in the enumeration (stable while the system is unchanged)
    bool isDefault = false;
};

struct AudioSettings {
    int playbackDevice = -1; ///< -1: system default
    int captureDevice = -1;
    bool enableInput = false; ///< open the capture device at start (see setInputEnabled)
    int bufferFrames = 256;  ///< device period; the engine still runs 64-sample blocks
};

/// Something that calls Engine::process()/tick() from exactly one thread.
/// Only one driver may run at a time.
class AudioDriver {
public:
    virtual ~AudioDriver() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool running() const = 0;
    [[nodiscard]] virtual std::string description() const = 0;
    [[nodiscard]] virtual std::string lastError() const { return {}; }
    /// Opens or closes audio input while running; playback goes on
    /// undisturbed. Returns false if the input could not be opened.
    virtual bool setInputEnabled(bool) { return true; }
    /// Fraction of the available time spent computing audio (0..1+),
    /// smoothed. 1 means the engine just keeps up.
    [[nodiscard]] virtual double load() const { return 0.0; }
};

/// Runs the engine from a timer thread in real time with silent I/O. Used
/// when no audio device is available, so control objects ([metro],
/// [delay], ...) keep working with DSP off – as in Pd.
std::unique_ptr<AudioDriver> makeTimerDriver(Engine& engine);

/// Runs the engine from a real audio device through miniaudio (ALSA,
/// PulseAudio, PipeWire, JACK, CoreAudio, WASAPI – chosen at runtime).
std::unique_ptr<AudioDriver> makeDeviceDriver(Engine& engine, const AudioSettings& settings);

std::vector<AudioDeviceInfo> listPlaybackDevices();
std::vector<AudioDeviceInfo> listCaptureDevices();

} // namespace qure
