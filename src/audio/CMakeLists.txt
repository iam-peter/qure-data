# qure_audio: audio device drivers feeding the engine.
add_library(miniaudio_impl STATIC miniaudio_impl.c)
target_include_directories(miniaudio_impl SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
target_compile_definitions(miniaudio_impl PUBLIC
    MA_NO_ENCODING MA_NO_DECODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE)
set_target_properties(miniaudio_impl PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(miniaudio_impl PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

add_library(qure_audio STATIC
    AudioDriver.cpp
)
add_library(qure::audio ALIAS qure_audio)
target_include_directories(qure_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qure_audio PUBLIC qure::core PRIVATE miniaudio_impl)
