cmake_minimum_required(VERSION 3.25.0)

set(PROJECT_NAME vkm_platform)
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 0)

project(
  ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message("[VKM_PLATFORM] Building vkm_platform v${PROJECT_VERSION}")

# vkm_log is the consuming project's to provide: the consumer's own lines go through the same logger,
# so a copy in here would be a second one. Fail with the reason rather than with whatever error a
# missing target produces three files later.
if(NOT TARGET vkm_log)
    message(FATAL_ERROR
        "vkm_platform requires the target 'vkm_log', which the consuming project must add before "
        "add_subdirectory(vkmPlatform); see README.md.")
endif()

# Debug features follow flags, each off unless the consumer turns it on.
option(VKM_PLATFORM_ASSERTS  "Keep the library's VKM_ASSERTs on whatever the build type" OFF)
option(VKM_PLATFORM_PROFILER "Its two frame zones as Tracy zones; needs the consumer's TracyClient target" OFF)

if(VKM_PLATFORM_PROFILER AND NOT TARGET TracyClient)
    message(FATAL_ERROR
        "VKM_PLATFORM_PROFILER needs the target 'TracyClient', which the consuming project must add "
        "before add_subdirectory(vkmPlatform); see README.md.")
endif()

# GLFW, which is this library's alone.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/modules)

# Enumerated as one sweep rather than as a list: a forgotten entry in a list is a missing symbol
# somewhere else.
file(GLOB_RECURSE VKM_PLATFORM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Static and position-independent: a consumer links it whole into a shared library of its own,
# as the engine does into libvkm_core, so it ships no binary of its own.
add_library(${PROJECT_NAME} STATIC ${VKM_PLATFORM_SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The root above vkm_platform/, so an include names the library: "vkm_platform/window/window_manager.h".
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    vkm_log
)

# Private: a public header names a GLFW type by its declaration alone, so nothing of the consumer's
# compiles against GLFW unless it links glfw itself (glfw_api.h). A static library's private link
# still reaches whatever links it.
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    Threads::Threads    # the pool, and a child process's reader
    ${CMAKE_DL_LIBS}    # a dynamic library, on Linux
)

# winmm, for the frame limiter's 1 ms scheduler tick; nothing else the library links pulls it in.
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
endif()

# The engine's warning set, so a file reads the same on either side of the boundary. Clang reads the
# doc blocks: a @param naming no parameter is an error.
set(_vkm_platform_warnings
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wshadow
    -Wunused-variable
    -Wunused-value
    -Wformat=2
    -Wnon-virtual-dtor
    -Woverloaded-virtual
)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:${_vkm_platform_warnings}>
    $<$<CXX_COMPILER_ID:Clang>:-Werror=documentation>
)

# VKM_ASSERT keys on NDEBUG, which a RelWithDebInfo or Release build defines: on, the library's own
# sources compile without it, and so does any assert() in a header they include.
if(VKM_PLATFORM_ASSERTS)
    target_compile_options(${PROJECT_NAME} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
endif()

# Its log lines say whose they are.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VKM_LOG_SUFFIX="VKM-PLATFORM"
)

# The zones reach the consumer's one Tracy client, linked into whatever the library is linked into
# (src/vkm_platform/profile.h).
if(VKM_PLATFORM_PROFILER)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VKM_PLATFORM_PROFILER=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE TracyClient)
endif()

# The suite (tests/), built when the consumer asks for it.
option(VKM_PLATFORM_BUILD_TESTS "Build the vkm_platform test suite" OFF)
if(VKM_PLATFORM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
