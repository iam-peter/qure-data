qt_add_executable(qure_ui_tests test_canvas.cpp)
target_link_libraries(qure_ui_tests PRIVATE qure_gui qure_guiplugin qure::options Qt6::Test)

# Never on the developer's desktop, never with sound: a private X server if
# available (custom shaders need a real GL context), otherwise Qt's
# offscreen platform. QURE_NO_AUDIO is belt and braces – the test builds
# its Session in silent mode anyway.
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
    add_test(NAME qure_ui_tests
             COMMAND ${XVFB_RUN} -a -s "-screen 0 1280x900x24" $<TARGET_FILE:qure_ui_tests>)
else()
    add_test(NAME qure_ui_tests COMMAND qure_ui_tests)
    set_tests_properties(qure_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
set_tests_properties(qure_ui_tests PROPERTIES
    TIMEOUT 180
    ENVIRONMENT_MODIFICATION "QURE_NO_AUDIO=set:1;QT_LOGGING_RULES=set:qt.qpa.*=false")
