#pragma once

/**
 * @brief The one way the library's sources reach a Windows system header.
 *
 * NOGDI keeps GDI out: nothing here draws with it, and its macros (ERROR among them) take names a
 * constant is spelled with. The defines are guarded because MinGW libstdc++ already defines
 * NOMINMAX. windef.h's empty `far`, `near` and `pascal` macros are undefined after. mmsystem.h,
 * which a lean windows.h leaves out, precedes the undefs, as it spells FAR and NEAR.
 * Safe to include unconditionally: everything is inside a _WIN32 guard.
 */
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef NOGDI
        #define NOGDI
    #endif

    #include <windows.h>
    #include <mmsystem.h>

    #undef far
    #undef near
    #undef pascal
#endif
