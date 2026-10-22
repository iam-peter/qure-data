add_executable(qure_tests
    Harness.hpp
    test_atoms.cpp
    test_engine.cpp
    test_dsp.cpp
    test_pdfile.cpp
    test_objects.cpp
    test_math_time.cpp
    test_filters_tables.cpp
    test_examples.cpp
)
target_link_libraries(qure_tests PRIVATE qure::core Catch2::Catch2WithMain)
target_compile_definitions(qure_tests PRIVATE QURE_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")

include(Catch)
catch_discover_tests(qure_tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES TIMEOUT 120)

if(QURE_BUILD_APP)
    add_subdirectory(ui)
endif()
