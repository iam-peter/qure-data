# qure_core: the Qt-free engine – patches, objects, compiler, scheduler.
add_library(qure_core STATIC
    Atom.cpp
    Clock.cpp
    Compiler.cpp
    Engine.cpp
    Object.cpp
    ObjectRegistry.cpp
    Patch.cpp
    PdFile.cpp
    Runtime.cpp
    Symbol.cpp

    objects/Control.cpp
    objects/Filter.cpp
    objects/Gui.cpp
    objects/Io.cpp
    objects/Math.cpp
    objects/Signal.cpp
    objects/Tables.cpp
    objects/Time.cpp
    objects/Visuals.cpp
)
add_library(qure::core ALIAS qure_core)

target_include_directories(qure_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qure_core PUBLIC qure::options)
