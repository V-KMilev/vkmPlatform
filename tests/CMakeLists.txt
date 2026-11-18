# vkm_platform's own tests: TEST("what works") in tests/<topic>.cpp (harness/check.h). Each runs in a
# process of its own and is a ctest of its own, named "vkm_platform: <what works>" and labeled
# vkm_platform, so `ctest -L vkm_platform` runs the suite in the tree of a consumer that calls
# enable_testing() too, with -C <config> under a multi-config generator. Beside them, checks of the
# library as a consumer builds it: its includes, its CMake, its debug flags.
#
# Nothing here opens a window or needs a display: what does - creating one, its callbacks, cursor
# modes, fullscreen, monitors, the icon - is walked by hand.

file(GLOB VKM_PLATFORM_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(vkm_platform_tests ${CMAKE_CURRENT_SOURCE_DIR}/harness/main.cpp ${VKM_PLATFORM_TEST_SOURCES})
# glfw for its key codes alone, which the map is checked against.
target_link_libraries(vkm_platform_tests PRIVATE ${PROJECT_NAME} glfw)
# Beside vkm_log, where Windows looks for the DLL a program needs.
set_target_properties(vkm_platform_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:vkm_log>)
target_include_directories(vkm_platform_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# The library's asserts flag, which check.h's VKM_TEST_LIBRARY_ASSERTS reads beside the file's NDEBUG.
target_compile_definitions(vkm_platform_tests PRIVATE
    VKM_TEST_SUITE="vkm_platform"
    VKM_TEST_LOG
    VKM_TEST_MODULE="$<TARGET_FILE_NAME:vkm_platform_test_module>"
    VKM_TEST_LISTENER="$<TARGET_FILE_NAME:vkm_platform_test_listener>"
    VKM_TEST_EXECUTABLE="$<TARGET_FILE_NAME:vkm_platform_tests>"
    VKM_TEST_ASSERTS_FLAG=$<BOOL:${VKM_PLATFORM_ASSERTS}>
)
target_compile_options(vkm_platform_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:${_vkm_platform_warnings}>
    $<$<CXX_COMPILER_ID:Clang>:-Werror=documentation>
)

# The library the suite loads, built as a project builds one, beside the suite: there the suite finds it
# as it runs, by its name alone.
add_library(vkm_platform_test_module MODULE ${CMAKE_CURRENT_SOURCE_DIR}/fixture/module.cpp)
set_target_properties(vkm_platform_test_module PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:vkm_platform_tests>
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:vkm_platform_tests>
)
if(WIN32)
    set_target_properties(vkm_platform_test_module PROPERTIES PREFIX "")
endif()
add_dependencies(vkm_platform_tests vkm_platform_test_module)

# A program the suite starts and stops, beside the suite: there it finds the libraries the suite does.
add_executable(vkm_platform_test_listener ${CMAKE_CURRENT_SOURCE_DIR}/fixture/listener.cpp)
target_link_libraries(vkm_platform_test_listener PRIVATE ${PROJECT_NAME})
set_target_properties(vkm_platform_test_listener PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:vkm_platform_tests>
)
add_dependencies(vkm_platform_tests vkm_platform_test_listener)

# Every public header with the library's include path alone: the suite does not build when one
# reaches GLFW's.
add_library(vkm_platform_public_headers OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/fixture/public_headers.cpp)
target_link_libraries(vkm_platform_public_headers PRIVATE ${PROJECT_NAME})
add_dependencies(vkm_platform_tests vkm_platform_public_headers)

# Every test the binary lists becomes a ctest when ctest runs: a test added is a test run, with no
# list to keep.
include(${CMAKE_CURRENT_SOURCE_DIR}/harness/discover.cmake)
vkm_discover_tests(vkm_platform_tests vkm_platform)

# The library as a consumer builds it.
set(_vkm_platform_checks
    "vkm_platform: every include is its own, the standard library's or vkmLog's, GLFW only through glfw_api.h, the system only through windows_api.h"
    "vkm_platform: every public header is in the fixture that compiles each with the library's include path alone"
    "vkm_platform: a consumer adds it after vkm_log, and without it it stops and says so"
)
add_test(NAME "vkm_platform: every include is its own, the standard library's or vkmLog's, GLFW only through glfw_api.h, the system only through windows_api.h"
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src -P ${CMAKE_CURRENT_SOURCE_DIR}/includes.cmake
)
add_test(NAME "vkm_platform: every public header is in the fixture that compiles each with the library's include path alone"
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src
        -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixture/public_headers.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/public_headers.cmake
)
add_test(NAME "vkm_platform: a consumer adds it after vkm_log, and without it it stops and says so"
    COMMAND ${CMAKE_COMMAND} -DLIBRARY_DIR=${PROJECT_SOURCE_DIR} -DREQUIRES=vkm_log
        -DOUT=${CMAKE_CURRENT_BINARY_DIR}/consumer -DGENERATOR=${CMAKE_GENERATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/harness/consumer.cmake
)
if(CMAKE_NM)
    # The library's compile commands, which the check reads, whether or not the consumer exports its own.
    set_property(TARGET ${PROJECT_NAME} PROPERTY EXPORT_COMPILE_COMMANDS ON)
    list(APPEND _vkm_platform_checks "vkm_platform: its asserts and its profiler follow their flags whatever the build type")
    add_test(NAME "vkm_platform: its asserts and its profiler follow their flags whatever the build type"
        COMMAND ${CMAKE_COMMAND} -DLIBRARY=${PROJECT_NAME} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src
            -DARCHIVE=$<TARGET_FILE:${PROJECT_NAME}> -DNM=${CMAKE_NM} -DASSERTS=${VKM_PLATFORM_ASSERTS}
            -DPROFILER=${VKM_PLATFORM_PROFILER} -DCONFIG=$<CONFIG>
            -DCOMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/harness/flags.cmake
    )
endif()
set_tests_properties(${_vkm_platform_checks} PROPERTIES LABELS vkm_platform)
