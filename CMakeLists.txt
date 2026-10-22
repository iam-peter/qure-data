cmake_minimum_required(VERSION 3.25)

project(QureData
    VERSION 0.1.0
    DESCRIPTION "A modern C++23 / Qt Quick reimagining of Pure Data"
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(QURE_BUILD_APP     "Build the Qt Quick application"           ON)
option(QURE_BUILD_TESTS   "Build the unit tests"                     ON)
option(QURE_NATIVE_ARCH   "Optimise DSP code for the build machine"  OFF)
option(QURE_SANITIZE      "Build with address/UB sanitizers"         OFF)
option(QURE_STATIC_LIBSTDCXX "Link libstdc++ statically (packages for older Linux)" OFF)

# Executables in build/bin, like the installed layout.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# An installed qure finds the Qt it was built against.
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
# std::format of floating point needs to_chars, which macOS has from 13.3.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.3)
endif()

include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)

if(QURE_BUILD_APP)
    # At the top level so the GUI and its tests share Qt's targets and
    # automoc settings.
    # ShaderToolsPrivate: QShaderBaker for runtime shaders (visuals). Like
    # QRhi it is semi-public API with its own compatibility promise.
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 ShaderTools ShaderToolsPrivate Test)
    qt_standard_project_setup(REQUIRES 6.8)
endif()

add_subdirectory(src/core)
add_subdirectory(src/audio)

if(QURE_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(QURE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
