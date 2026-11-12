#pragma once

/**
 * @brief Asks a two-GPU laptop's NVIDIA or AMD driver for the discrete GPU, which each reads off
 *        an export of the executable: written once, at file scope, in the main.cpp of each
 *        executable that draws.
 *
 * Exports of a library are not read, so the executable carries them itself. On Linux the choice
 * is the environment's (`__NV_PRIME_RENDER_OFFLOAD`, `DRI_PRIME`), and this is nothing.
 */
#if defined(_WIN32)
#define VKM_PLATFORM_PREFER_DISCRETE_GPU()                                \
    extern "C" {                                                          \
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;          \
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;   \
    }
#else
#define VKM_PLATFORM_PREFER_DISCRETE_GPU()
#endif
