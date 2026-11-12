#pragma once

// The platform's Tracy zones, for its own sources only. On when it is built with
// VKM_PLATFORM_PROFILER, which links TracyClient; off, each is a free no-op.
//
//   VKM_PLATFORM_ZONE("SwapBuffers");  // a string literal

#ifndef VKM_PLATFORM_PROFILER
    #define VKM_PLATFORM_PROFILER 0
#endif

#if VKM_PLATFORM_PROFILER

// Tracy calls vsnprintf without including <cstdio>.
#include <cstdio>

#include <tracy/Tracy.hpp>

// __LINE__ keeps zone variable names unique, so several zones in one block don't collide.
#define VKM_PLATFORM_CONCAT_(a, b) a##b
#define VKM_PLATFORM_CONCAT(a, b)  VKM_PLATFORM_CONCAT_(a, b)

#define VKM_PLATFORM_ZONE(name_literal) \
    ZoneNamedN(VKM_PLATFORM_CONCAT(vkmPlatformZone_, __LINE__), name_literal, true)

#else

#define VKM_PLATFORM_ZONE(name_literal) ((void)sizeof(name_literal))

#endif
