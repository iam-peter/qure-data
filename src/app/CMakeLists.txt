# The GUI as a library (QML module "Qure"), so the executable and the UI
# tests share exactly the same code.
qt_add_library(qure_gui STATIC
    BoxLayout.cpp BoxLayout.hpp
    CanvasLayer.cpp
    CanvasView.cpp CanvasView.hpp
    ConsoleModel.cpp ConsoleModel.hpp
    FrontendRegistry.cpp FrontendRegistry.hpp
    NodeHandle.cpp NodeHandle.hpp
    PatchDocument.cpp PatchDocument.hpp
    SceneGraph.cpp SceneGraph.hpp
    Session.cpp Session.hpp
    Theme.hpp
    WaveView.cpp WaveView.hpp
)
add_library(qure::gui ALIAS qure_gui)

set(QML_DIR ${PROJECT_SOURCE_DIR}/qml)
file(GLOB QURE_QML RELATIVE ${QML_DIR} CONFIGURE_DEPENDS ${QML_DIR}/*.qml ${QML_DIR}/nodes/*.qml ${QML_DIR}/nodes/*.js)
file(GLOB QURE_QML_RESOURCES RELATIVE ${QML_DIR} CONFIGURE_DEPENDS ${QML_DIR}/nodes/*.json)
foreach(f IN LISTS QURE_QML QURE_QML_RESOURCES)
    set_source_files_properties(${QML_DIR}/${f} PROPERTIES QT_RESOURCE_ALIAS ${f})
endforeach()
list(TRANSFORM QURE_QML PREPEND ${QML_DIR}/)
list(TRANSFORM QURE_QML_RESOURCES PREPEND ${QML_DIR}/)

# Example projects ship inside the binary and are copied to the user's data
# directory when opened. Their QML nodes are plain resources: they are
# loaded from disk at runtime like any user node pack, not compiled in.
set(EXAMPLES_DIR ${PROJECT_SOURCE_DIR}/examples)
file(GLOB_RECURSE QURE_EXAMPLES RELATIVE ${EXAMPLES_DIR} CONFIGURE_DEPENDS
    ${EXAMPLES_DIR}/*.qd ${EXAMPLES_DIR}/*.qml ${EXAMPLES_DIR}/*.js ${EXAMPLES_DIR}/*.json)
foreach(f IN LISTS QURE_EXAMPLES)
    set_source_files_properties(${EXAMPLES_DIR}/${f} PROPERTIES QT_RESOURCE_ALIAS examples/${f})
    list(APPEND QURE_QML_RESOURCES ${EXAMPLES_DIR}/${f})
endforeach()

qt_add_qml_module(qure_gui
    URI Qure
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Qure
    QML_FILES ${QURE_QML}
    RESOURCES ${QURE_QML_RESOURCES}
)

qt_add_shaders(qure_gui "qure_shaders"
    PREFIX "/qure"
    FILES
        shaders/box.vert shaders/box.frag
        shaders/cord.vert shaders/cord.frag
)

target_include_directories(qure_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qure_gui
    PUBLIC qure::core qure::audio Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2
    PRIVATE qure::options)

# --- the application ---------------------------------------------------------
qt_add_executable(qure main.cpp)
target_compile_definitions(qure PRIVATE QURE_VERSION="${PROJECT_VERSION}")
target_link_libraries(qure PRIVATE qure_gui qure_guiplugin qure::options)
set_target_properties(qure PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Qure Data"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.iam-peter.qure-data"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in)

include(GNUInstallDirs)
install(TARGETS qure
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES logo/qure.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES logo/qure.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES logo/qure-data.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
endif()
