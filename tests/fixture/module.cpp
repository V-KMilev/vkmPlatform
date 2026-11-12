// A dynamic library for the suite to load: one symbol, and nothing of the platform.

#if defined(_WIN32)
    #define VKM_FIXTURE_EXPORT extern "C" __declspec(dllexport)
#else
    #define VKM_FIXTURE_EXPORT extern "C"
#endif

VKM_FIXTURE_EXPORT int vkmFixtureAnswer() { return 42; }
