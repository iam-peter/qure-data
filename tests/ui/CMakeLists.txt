# UI tests: the real editor and QML, driven with synthetic input.
#
# Never on the developer's desktop, never with sound: a private X server if
# available (custom shaders need a real GL context), otherwise Qt's
# offscreen platform. QURE_NO_AUDIO is belt and braces – the tests build
# their Session in silent mode anyway.
find_program(XVFB_RUN xvfb-run)

function(qure_add_ui_test name source)
    qt_add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE qure_gui qure_guiplugin qure::options Qt6::Test)
    if(XVFB_RUN)
        add_test(NAME ${name} COMMAND ${XVFB_RUN} -a -s "-screen 0 1280x900x24" $<TARGET_FILE:${name}>)
    else()
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 180
        ENVIRONMENT_MODIFICATION "QURE_NO_AUDIO=set:1;QT_LOGGING_RULES=set:qt.qpa.*=false")
endfunction()

qure_add_ui_test(qure_ui_tests test_canvas.cpp)
qure_add_ui_test(qure_ui_example_tests test_examples.cpp)
