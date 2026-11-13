# vkmPlatform

The machine a game runs on: a window with its input - keys and buttons by names of our own, the
pointer, typed text, dropped files - cursor and window modes, the monitors' resolutions and a frame
limiter; one thread pool; a loaded dynamic library, loaded through a copy so it can be rebuilt while
it runs; a child process; where the system keeps the executable and a user's files; and the system
asking the process to stop. Windows and Linux. GLFW and the Windows headers are inside it, so its
consumer includes neither.

## Conventions

- **The window and its input are the main thread's**, as GLFW requires: create it, poll it
  (`updateInput`), and ask it from the thread that runs the frame.
- **A closed window accepts every request and performs none**, and its reporters answer zero
  (`framebufferScale` one), so a windowless program still runs and can still be told to end.
- **Keys by where they sit.** `Key::W` is the key a US layout prints W on, whatever the layout;
  `Key` is declared in GLFW's code order. Typed text comes through `InputHandle::text()`, layout
  applied.
- **The window names no graphics API.** What a renderer needs of it - hints before it exists, the
  window once it does, presenting and the swap interval - is a `WindowSurface` the renderer
  implements and hands to `createWindow`.
- **One pool, one worker fewer than the physical cores** the process may run on, SMT siblings counted
  once, as the thread that calls `parallelFor` runs a share too.
- **No clock of its own but the frame limiter's**, and nothing kept between calls that a call does
  not need.

## A window

```cpp
using namespace Vkm::Platform;

WindowManager window;
window.createWindow("Game", std::make_unique<MySurface>());  // the renderer's WindowSurface
window.setFramerate(144);                                     // 0 is uncapped

while (!stopRequested() && window.beginFrame()) {
    window.updateInput();
    const InputHandle& input = window.getInputHandle();
    if (input.wasKeyStruck(Key::Escape)) window.requestClose();
    // ... draw
    window.getInputHandle().clearStrikes();  // the frame's strikes and text are read
    window.swapBuffers();
}
ThreadPool::get().shutdown();  // before main returns
```

`docs/how-it-works.md` says what each piece does, and why.

## Requirements

CMake 3.25 or above and a C++17 compiler. On Linux it links the thread library and `dl`; on Windows
`winmm`. GLFW's own needs (X11 or Wayland headers on Linux) are GLFW's.

## Use

Add it as a submodule and `add_subdirectory` it after the one target it requires and does not
vendor: `vkm_log`, which it logs through. The build stops with the reason when it is missing.
GLFW comes with it, in `modules/glfw`, unless a `glfw` target exists already.

```
git submodule add https://github.com/V-KMilev/vkmPlatform.git modules/vkmPlatform
```

The library is `vkm_platform`, static and position-independent, for linking into a library of the
consumer's own. Headers are included by the library's folder: `#include
"vkm_platform/window/window_manager.h"`; everything is in `Vkm::Platform`. It links GLFW privately:
a public header names a GLFW type by its declaration alone, and only code that links `glfw` itself
includes `vkm_platform/glfw_api.h`, the one way to GLFW's header. GLFW is static unless the consumer
sets `GLFW_LIBRARY_TYPE` to `SHARED` first, which it must when something else of its own links GLFW
too.

| Option                     | Default | What                                                                                       |
|----------------------------|---------|--------------------------------------------------------------------------------------------|
| `VKM_PLATFORM_ASSERTS`     | `OFF`   | The library's asserts run whatever the build type (`-UNDEBUG` on its own sources)          |
| `VKM_PLATFORM_PROFILER`    | `OFF`   | `SwapBuffers` and `FrameLimiter` as Tracy zones; needs the consumer's `TracyClient` target |
| `VKM_PLATFORM_BUILD_TESTS` | `OFF`   | Builds `vkm_platform_tests`: each test a CTest of its own, labeled `vkm_platform`          |

## What it holds

| Header                      | What                                                                                                                  |
|-----------------------------|-----------------------------------------------------------------------------------------------------------------------|
| `window/window_manager.h`   | `WindowManager`, `WindowMode`, `CursorMode`, `WindowSize`, `fittingWindowSizes`, `keyFromGlfw`, `mouseButtonFromGlfw` |
| `window/window_surface.h`   | `WindowSurface`: what a renderer asks of the window                                                                   |
| `window/frame_limiter.h`    | `FrameLimiter`: a frame cap, slept then spun                                                                          |
| `input/key.h`               | `Key`, `MouseButton`                                                                                                  |
| `input/input_handle.h`      | `InputHandle`: one frame of keys, buttons, pointer, scroll and text                                                   |
| `threading/thread_pool.h`   | `ThreadPool`, `parallelFor`, `parallelGrain`                                                                          |
| `process/dynamic_library.h` | `DynamicLibrary`: load, load through a copy, symbols                                                                  |
| `process/child_process.h`   | `ChildProcess`: a program run in the background, its output collected                                                 |
| `process/this_process.h`    | `executableDir`, `userConfigDir`, `userStateDir`, `attachParentConsole`, `stopRequested`                              |
| `process/discrete_gpu.h`    | `VKM_PLATFORM_PREFER_DISCRETE_GPU()`, for an executable's `main.cpp`                                                  |
| `glfw_api.h`                | GLFW's header, for code that links `glfw` itself                                                                      |
| `windows_api.h`             | The Windows headers, for the library's own sources                                                                    |

Each is under `src/vkm_platform/`.

## Tests

`tests/` holds the library's own suite, built with `VKM_PLATFORM_BUILD_TESTS`: each test is
`TEST("what works")` in `tests/<topic>.cpp`, a CTest named `vkm_platform: <what works>` and run in a
process of its own, so `ctest -L vkm_platform` runs them in the tree of a consumer that calls
`enable_testing()`, with `-C <config>` under a multi-config generator. Beside them, CTests of the
library as a consumer builds it: its includes, its CMake, and its debug flags. Nothing there opens a
window: creating one, its callbacks, cursor modes, fullscreen, monitors and the icon are walked by hand.

## In vkmEngine

The engine pins it at `modules/vkmPlatform` and links it whole into `libvkm_core`. Its actions,
bindings and their names are the engine's, on top of this; so is the frame. A change is a commit
here, then an engine commit that moves the pin.
