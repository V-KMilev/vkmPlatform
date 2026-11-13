# Every #include under src/vkm_platform is the library's own, the standard library's or vkmLog's;
# GLFW only through glfw_api.h, and that in .cpp files alone; the Windows headers only through
# windows_api.h; POSIX's in .cpp files; Tracy only in profile.h (cmake -P, -DSOURCE_DIR=<src>).

set(_posix "dlfcn.h|fcntl.h|poll.h|sched.h|unistd.h|sys/[a-z]+\\.h")

file(GLOB_RECURSE _files "${SOURCE_DIR}/vkm_platform/*.h" "${SOURCE_DIR}/vkm_platform/*.cpp")
set(_problems "")
foreach(_file IN LISTS _files)
    get_filename_component(_name "${_file}" NAME)
    file(STRINGS "${_file}" _includes REGEX "^[ \t]*#[ \t]*include")
    foreach(_line IN LISTS _includes)
        string(REGEX MATCH "[<\"]([^>\"]+)[>\"]" _ "${_line}")
        set(_header "${CMAKE_MATCH_1}")
        if(_header STREQUAL "vkm_platform/glfw_api.h")
            if(NOT _name MATCHES "\\.cpp$")
                string(APPEND _problems "  ${_file}: ${_line} (GLFW reaches whoever includes this header)\n")
            endif()
        elseif(_header MATCHES "^vkm_platform/" AND EXISTS "${SOURCE_DIR}/${_header}")
        elseif(_line MATCHES "<[a-z_]+>")
        elseif(_header MATCHES "^vkm_log/")
        elseif(_name STREQUAL "glfw_api.h" AND _header STREQUAL "GLFW/glfw3.h")
        elseif(_name STREQUAL "windows_api.h" AND _header MATCHES "^(windows|mmsystem)\\.h$")
        elseif(_name STREQUAL "profile.h" AND _header STREQUAL "tracy/Tracy.hpp")
        elseif(_name MATCHES "\\.cpp$" AND _header MATCHES "^(${_posix})$")
        else()
            string(APPEND _problems "  ${_file}: ${_line}\n")
        endif()
    endforeach()
endforeach()

if(_problems)
    message(FATAL_ERROR "Includes the library may not have:\n${_problems}")
endif()
