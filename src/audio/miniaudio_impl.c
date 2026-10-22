/* The single translation unit that compiles miniaudio's implementation.
   Backends (ALSA, PulseAudio, PipeWire, JACK, CoreAudio, WASAPI, ...) are
   loaded at runtime, so no backend headers are needed to build. */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
