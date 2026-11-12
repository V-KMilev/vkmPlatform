#pragma once

// The one way to GLFW's header: the Windows headers first, through the one header that knows
// how they are included, and GLFW's own GL header off, so a renderer's loader owns the API's
// declarations. It compiles only where the includer links glfw itself: the library links it
// privately, so its consumers get no GLFW include path from it.

#include "vkm_platform/windows_api.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
