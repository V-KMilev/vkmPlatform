# Every header under src/vkm_platform is in fixture/public_headers.cpp, but the library's doors (glfw_api.h,
# windows_api.h) and the two its own sources alone include (profile.h, process/stop_event.h): a header
# left out is one nothing checks reaches no further than it should (cmake -P, -DSOURCE_DIR=<src>
# -DFIXTURE=<public_headers.cpp>).

set(_own_only glfw_api.h windows_api.h profile.h process/stop_event.h)

file(GLOB_RECURSE _headers RELATIVE "${SOURCE_DIR}/vkm_platform" "${SOURCE_DIR}/vkm_platform/*.h")
file(STRINGS "${FIXTURE}" _included REGEX "^#include \"vkm_platform/")
set(_missing "")
foreach(_header IN LISTS _headers)
    list(FIND _own_only "${_header}" _own)
    list(FIND _included "#include \"vkm_platform/${_header}\"" _listed)
    if(_own EQUAL -1 AND _listed EQUAL -1)
        string(APPEND _missing "  vkm_platform/${_header}\n")
    endif()
endforeach()

if(_missing)
    message(FATAL_ERROR "Public headers fixture/public_headers.cpp does not include:\n${_missing}")
endif()
