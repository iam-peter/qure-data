# Third-party dependencies, pinned to exact releases.
#
#  * miniaudio  – audio I/O. Loads ALSA / PulseAudio / PipeWire / JACK /
#                 CoreAudio / WASAPI at runtime, so no backend development
#                 headers are needed at build time.
#  * Catch2     – unit tests for the (Qt-free) engine core.

include(FetchContent)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.25
    GIT_SHALLOW    TRUE
    # Only the single-file implementation is used; pointing SOURCE_SUBDIR at
    # a directory without a CMakeLists.txt keeps upstream's examples and
    # tools out of our build.
    SOURCE_SUBDIR  qure-not-a-cmake-project
)
FetchContent_MakeAvailable(miniaudio)

if(QURE_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.9.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()
