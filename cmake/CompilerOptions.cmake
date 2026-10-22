# Warning and optimisation flags shared by every qure target.

add_library(qure_options INTERFACE)
add_library(qure::options ALIAS qure_options)

target_compile_features(qure_options INTERFACE cxx_std_23)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qure_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        -Wno-missing-field-initializers
        $<$<CONFIG:Release,RelWithDebInfo>:-O3>
        # The DSP kernels are written as plain loops over spans; let the
        # optimiser vectorise them without having to prove IEEE strictness
        # for every reduction.
        $<$<CONFIG:Release,RelWithDebInfo>:-fno-math-errno>
    )
    if(QURE_NATIVE_ARCH)
        target_compile_options(qure_options INTERFACE -march=native)
    endif()
    if(QURE_STATIC_LIBSTDCXX AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # The packages are built on the oldest supported Ubuntu with a newer
        # GCC than it ships: carry that GCC's C++ runtime along.
        target_link_options(qure_options INTERFACE -static-libstdc++ -static-libgcc)
    endif()
    if(QURE_SANITIZE)
        target_compile_options(qure_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(qure_options INTERFACE -fsanitize=address,undefined)
    endif()
elseif(MSVC)
    # /utf-8: sources contain non-ASCII text (dashes, arrows in comments and strings).
    target_compile_options(qure_options INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8)
endif()
