# The library's debug features follow its flags, read off what the build made of them (cmake -P):
#
#   -DLIBRARY=<name>         its target, which is its sources' folder under SOURCE_DIR too
#   -DSOURCE_DIR=<src>       its sources' root
#   -DARCHIVE=<file>         the library the build made
#   -DNM=<nm>                the toolchain's nm
#   -DASSERTS=<bool>         VKM_<NAME>_ASSERTS
#   -DPROFILER=<bool>        VKM_<NAME>_PROFILER, OFF for a library without one
#   -DCONFIG=<config>        the build type
#   -DCOMPILE_COMMANDS=<file> compile_commands.json, read when the build wrote one
#
# Asserts on: every source compiles with -UNDEBUG, so VKM_ASSERT is in whatever the build type.
# Off, where the sources compile with NDEBUG, as their compile commands say or, with none written, as
# CMake's own flags for the build type do: none undoes it, and the archive calls no assert.
# The profiler off: the archive calls nothing of Tracy's.

set(_problems "")
set(_ndebug FALSE)

if(EXISTS "${COMPILE_COMMANDS}")
    file(READ "${COMPILE_COMMANDS}" _json)
    string(JSON _count LENGTH "${_json}")
    math(EXPR _last "${_count} - 1")
    # The library's entries. A multi-config generator lists one per build type, and an object's path
    # names its type in the folder after the library's: then only the type ctest runs is read.
    set(_entries "")
    set(_per_config FALSE)
    foreach(_index RANGE ${_last})
        string(JSON _file GET "${_json}" ${_index} file)
        file(TO_CMAKE_PATH "${_file}" _file)
        string(FIND "${_file}" "${SOURCE_DIR}/${LIBRARY}/" _at)
        if(NOT _at EQUAL 0)
            continue()
        endif()
        list(APPEND _entries ${_index})
        string(JSON _output ERROR_VARIABLE _none GET "${_json}" ${_index} output)
        file(TO_CMAKE_PATH "/${_output}" _output)
        if(_output MATCHES "/${LIBRARY}\\.dir/${CONFIG}/")
            set(_per_config TRUE)
        endif()
    endforeach()
    set(_sources 0)
    foreach(_index IN LISTS _entries)
        string(JSON _file GET "${_json}" ${_index} file)
        string(JSON _output ERROR_VARIABLE _none GET "${_json}" ${_index} output)
        file(TO_CMAKE_PATH "/${_output}" _output)
        if(_per_config AND NOT _output MATCHES "/${LIBRARY}\\.dir/${CONFIG}/")
            continue()
        endif()
        math(EXPR _sources "${_sources} + 1")
        string(JSON _command ERROR_VARIABLE _none GET "${_json}" ${_index} command)
        if(_command MATCHES "[-/]DNDEBUG")
            set(_ndebug TRUE)
        endif()
        if(ASSERTS AND NOT _command MATCHES "[-/]UNDEBUG")
            string(APPEND _problems "  ${_file} compiles without -UNDEBUG, though its asserts are on\n")
        elseif(NOT ASSERTS AND _command MATCHES "[-/]UNDEBUG")
            string(APPEND _problems "  ${_file} compiles with -UNDEBUG, though its asserts are off\n")
        endif()
    endforeach()
    if(_sources EQUAL 0)
        string(APPEND _problems "  ${COMPILE_COMMANDS} holds none of ${SOURCE_DIR}/${LIBRARY}\n")
    endif()
elseif(CONFIG MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    # No commands to read: the build types that define NDEBUG by CMake's own flags.
    set(_ndebug TRUE)
endif()

execute_process(COMMAND "${NM}" -u "${ARCHIVE}" OUTPUT_VARIABLE _undefined RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "${NM} -u ${ARCHIVE} failed")
endif()

if(NOT ASSERTS AND _ndebug)
    foreach(_symbol assertFail __assert_fail)
        if(_undefined MATCHES "${_symbol}")
            string(APPEND _problems "  ${ARCHIVE} calls ${_symbol}, though its asserts are off\n")
        endif()
    endforeach()
endif()

if(NOT PROFILER AND _undefined MATCHES "[Tt]racy")
    string(APPEND _problems "  ${ARCHIVE} calls Tracy, though its profiler is off\n")
endif()

if(_problems)
    message(FATAL_ERROR "The debug features do not follow the flags (asserts ${ASSERTS}, profiler ${PROFILER}, "
                        "${CONFIG}):\n${_problems}")
endif()
# A generator that writes no compile_commands.json leaves the sources' flags unread: said, not hidden.
set(_unread "")
if(NOT EXISTS "${COMPILE_COMMANDS}")
    set(_unread " (the sources' flags unread: no ${COMPILE_COMMANDS})")
endif()
message(STATUS "asserts ${ASSERTS}, profiler ${PROFILER}, ${CONFIG}: as the flags say${_unread}")
