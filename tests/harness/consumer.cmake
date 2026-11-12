# Adds the library to a project of its own (consumer/) as a consumer does, once with every target it
# requires added first and once without each of them (cmake -P):
#
#   -DLIBRARY_DIR=<root>     the library's root, where its CMakeLists.txt is
#   -DREQUIRES=<a|b>         the targets it requires, '|' between them
#   -DOUT=<folder>           where the projects are configured
#   -DGENERATOR=<generator>  and with which generator
#
# With all of them it configures; without one it stops, and says which.

string(REPLACE "|" ";" _requires "${REQUIRES}")
file(REMOVE_RECURSE "${OUT}")

set(_problems "")
foreach(_missing "" ${_requires})
    set(_adds ${_requires})
    if(_missing)
        list(REMOVE_ITEM _adds ${_missing})
    endif()
    string(REPLACE ";" "|" _adds "${_adds}")
    set(_case "without ${_missing}")
    if(NOT _missing)
        set(_case "with all")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}/consumer" -B "${OUT}/${_case}" -G "${GENERATOR}"
                "-DVKM_LIBRARY_DIR=${LIBRARY_DIR}" "-DVKM_CONSUMER_ADDS=${_adds}"
        RESULT_VARIABLE _status
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE  _output
    )
    if(NOT _missing AND NOT _status EQUAL 0)
        string(APPEND _problems "  with ${REQUIRES} added first it does not configure:\n${_output}\n")
    elseif(_missing AND _status EQUAL 0)
        string(APPEND _problems "  without ${_missing} it configures\n")
    elseif(_missing AND NOT _output MATCHES "requires the target '${_missing}'")
        string(APPEND _problems "  without ${_missing} it stops without naming it:\n${_output}\n")
    endif()
endforeach()

if(_problems)
    message(FATAL_ERROR "As a consumer's subdirectory:\n${_problems}")
endif()
file(REMOVE_RECURSE "${OUT}")
