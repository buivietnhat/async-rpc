cmake_minimum_required(VERSION 3.10)

include(GoogleTest)

file(GLOB TEST_SOURCES "${PROJECT_SOURCE_DIR}/test/*/*test.cpp")

add_custom_target(build-tests COMMAND ${CMAKE_CTEST_COMMAND} --show-only)
add_custom_target(check-tests COMMAND ${CMAKE_CTEST_COMMAND} --verbose)

foreach (test_source ${TEST_SOURCES})
    # Create a human readable name.
    get_filename_component(test_filename ${test_source} NAME)
    string(REPLACE ".cpp" "" test_name ${test_filename})

    # Add the test target separately and as part of "make check-tests".
    add_executable(${test_name} EXCLUDE_FROM_ALL ${test_source}
            include/rpc/mock_socket.h)
    add_dependencies(build-tests ${test_name})
    add_dependencies(check-tests ${test_name})

    gtest_discover_tests(${test_name}
            EXTRA_ARGS
            --gtest_color=yes
            --gtest_output=xml:${CMAKE_BINARY_DIR}/test/${test_name}.xml)
    target_link_libraries(${test_name} async_rpc gtest gmock_main)

    # Set test target properties and dependencies.
    set_target_properties(${test_name}
            PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test"
            COMMAND ${test_name}
    )
endforeach (test_source ${TEST_SOURCES})