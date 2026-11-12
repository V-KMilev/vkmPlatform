set(MODULES_DIR ${PROJECT_SOURCE_DIR}/modules)

# GLFW: the window, its input and the monitors. Its docs, tests, examples and install rules are off:
# a consumer installs what it ships. Whether it is static or shared is the consumer's to say, by
# GLFW_LIBRARY_TYPE before this: shared, when something else of the consumer links GLFW too. A
# consumer that already has the target keeps it.
if(NOT TARGET glfw)
    message("[VKM_PLATFORM] Modules:")
    message("\t- glfw | Path: ${MODULES_DIR}/glfw (https://github.com/glfw/glfw)")

    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

    # SYSTEM: its headers reach this library as system headers, so its warning set is not applied to
    # code it does not own.
    add_subdirectory(glfw SYSTEM)
endif()
