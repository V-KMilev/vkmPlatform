// Processes: a child program, a dynamic library and its copy, a file's claim, this process's folders
// and its stop.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #include "vkm_platform/windows_api.h"
#else
    #include <signal.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include "vkm_platform/process/child_process.h"
#include "vkm_platform/process/discrete_gpu.h"
#include "vkm_platform/process/dynamic_library.h"
#include "vkm_platform/process/file_lock.h"
#include "vkm_platform/process/stop_event.h"
#include "vkm_platform/process/this_process.h"

#include "harness/check.h"
#include "harness/scratch.h"

// As every executable that draws writes it: nothing here, the two exports on Windows.
VKM_PLATFORM_PREFER_DISCRETE_GPU()

namespace {

namespace fs = std::filesystem;

using Vkm::Platform::ChildProcess;
using Vkm::Platform::DynamicLibrary;
using Vkm::Platform::FileLock;
using Vkm::Test::scratch;

using Answer = int (*)();

/// The suite's library, copied into this test's folder so what is done to it is the test's own.
fs::path builtLibrary() {
    const fs::path built = scratch() / "bin" / VKM_TEST_MODULE;
    fs::create_directories(built.parent_path());
    fs::copy_file(
        Vkm::Platform::executableDir() / VKM_TEST_MODULE,
        built,
        fs::copy_options::overwrite_existing
    );
    return built;
}

int answerOf(const DynamicLibrary& library) {
    const auto answer = reinterpret_cast<Answer>(library.symbol("vkmFixtureAnswer"));
    return answer ? answer() : -1;
}

/// The files in @p folder, by name.
std::vector<std::string> namesIn(const fs::path& folder) {
    std::vector<std::string> names;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder)) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

/// What @p child's program wrote until it wrote @p text or ended, five seconds at most.
std::string outputUntil(ChildProcess& child, const std::string& text) {
    std::string output;
    for (int i = 0; i < 5000 && child.running() && output.find(text) == std::string::npos; ++i) {
        output += child.takeOutput();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return output + child.takeOutput();
}

#if !defined(_WIN32)
/// Run @p child's program until it exits, five seconds at most, collecting what it wrote. Only the POSIX tests start a
/// program to hear, so a Windows build has no caller.
std::string runToExit(ChildProcess& child) {
    std::string output;
    for (int i = 0; i < 5000 && child.running(); ++i) {
        output += child.takeOutput();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return output + child.takeOutput();
}
#endif

} // namespace

TEST("a child program is heard, reports its exit code and can be stopped, and a missing program does not start") {
#if defined(_WIN32)
    Vkm::Test::skip("the program run here is the POSIX shell");
#else
    ChildProcess child;
    CHECK(child.exitCode() == -1);
    REQUIRE(child.start("/bin/sh", {"-c", "echo out; echo err >&2; exit 3"}));
    const std::string output = runToExit(child);
    CHECK(!child.running());
    CHECK(output.find("out\n") != std::string::npos);
    CHECK(output.find("err\n") != std::string::npos);
    CHECK(child.exitCode() == 3);

    // One that would run a minute, stopped with what it started.
    const fs::path started   = scratch() / "started";
    const std::string script = "sleep 60 & echo $! > \"$0\"; sleep 60; echo never";
    REQUIRE(child.start("/bin/sh", {"-c", script, started.string()}));
    CHECK(child.running());
    for (int i = 0; i < 5000 && Vkm::Test::readText(started).empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const pid_t below = static_cast<pid_t>(std::atoi(Vkm::Test::readText(started).c_str()));
    REQUIRE(below > 0);
    const auto before = std::chrono::steady_clock::now();
    child.stop();
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(10));
    CHECK(!child.running());
    CHECK(child.exitCode() == -1);
    CHECK(child.takeOutput().find("never") == std::string::npos);
    bool gone = false;
    for (int i = 0; i < 5000 && !(gone = ::kill(below, 0) != 0); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(gone);

    ChildProcess missing;
    CHECK(!missing.start(scratch() / "no such program", {}));
    CHECK(!missing.running());
    CHECK(!missing.takeOutput().empty());
#endif
}

TEST("stop ends what a child started that outlives its asking, though the program itself ended at once") {
#if defined(_WIN32)
    Vkm::Test::skip("the program run here is the POSIX shell");
#else
    // The program ends when asked; what it started does not hear the asking.
    ChildProcess child;
    const fs::path started   = scratch() / "started";
    const std::string script = "(trap '' TERM; exec sleep 60) & echo $! > \"$0\"; wait";
    REQUIRE(child.start("/bin/sh", {"-c", script, started.string()}));
    for (int i = 0; i < 5000 && Vkm::Test::readText(started).empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const pid_t below = static_cast<pid_t>(std::atoi(Vkm::Test::readText(started).c_str()));
    REQUIRE(below > 0);
    child.stop();
    CHECK(!child.running());
    bool gone = false;
    for (int i = 0; i < 5000 && !(gone = ::kill(below, 0) != 0); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(gone);
    if (!gone) ::kill(below, SIGKILL);
#endif
}

TEST("stop asks a program that asks whether it should stop before it ends it, and ends one that never asks at once") {
    // The listener (fixture/listener.cpp) says when it has started and when it is asked.
    const fs::path listener = Vkm::Platform::executableDir() / VKM_TEST_LISTENER;
    ChildProcess child;
    REQUIRE(child.start(listener, {}));
    std::string output = outputUntil(child, "started");
    REQUIRE(output.find("started") != std::string::npos);
    auto before = std::chrono::steady_clock::now();
    child.stop();
    output += child.takeOutput();
    CHECK(output.find("asked to stop") != std::string::npos);
    // Asked, so it ended before stop's two seconds ran out.
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(2));
    CHECK(!child.running());

    REQUIRE(child.start(listener, {"deaf"}));
    REQUIRE(outputUntil(child, "started").find("started") != std::string::npos);
    before = std::chrono::steady_clock::now();
    child.stop();
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(1));
    CHECK(!child.running());
    CHECK(child.takeOutput().find("asked to stop") == std::string::npos);
}

TEST("a child program runs in the folder it is given, each argument whole") {
#if defined(_WIN32)
    Vkm::Test::skip("the program run here is the POSIX shell");
#else
    fs::create_directories(scratch() / "work");
    ChildProcess child;
    const std::vector<std::string> args = {"-c", "pwd; printf '[%s]' \"$0\" \"$1\"", "two words", "*"};
    REQUIRE(child.start("/bin/sh", args, scratch() / "work"));
    const std::string output = runToExit(child);
    CHECK(output.find(fs::weakly_canonical(scratch() / "work").string() + "\n") != std::string::npos);
    CHECK(output.find("[two words][*]") != std::string::npos);
    CHECK(child.exitCode() == 0);
#endif
}

TEST("a dynamic library loads, gives its symbol, and unloads, and platformName names its file") {
    const fs::path built = builtLibrary();
    CHECK(built.filename().string() == DynamicLibrary::platformName("vkm_platform_test_module"));

    DynamicLibrary library;
    CHECK(!library.isLoaded());
    CHECK(library.symbol("vkmFixtureAnswer") == nullptr);
    REQUIRE(library.load(built));
    CHECK(library.isLoaded());
    CHECK(answerOf(library) == 42);
    CHECK(library.symbol("noSuchSymbol") == nullptr);
    library.unload();
    library.unload();
    CHECK(!library.isLoaded());
    CHECK(library.symbol("vkmFixtureAnswer") == nullptr);

    DynamicLibrary missing;
    CHECK(!missing.load(scratch() / "missing.so"));
    CHECK(Vkm::Test::logText().find("missing.so") != std::string::npos);
}

TEST("a library loaded through a copy leaves its original free to be rewritten while the copy runs") {
    const fs::path built = builtLibrary();
    DynamicLibrary library;
    const DynamicLibrary::Outcome outcome = library.loadCopy(built);
    CHECK(outcome.how == DynamicLibrary::Loaded::Copied);
    CHECK(!outcome.error);
    REQUIRE(library.isLoaded());
    CHECK(answerOf(library) == 42);

    // The build writes over the original, as a rebuild does.
    Vkm::Test::writeText(built, "not a library any more");
    CHECK(Vkm::Test::readText(built) == "not a library any more");
    CHECK(answerOf(library) == 42);
#if defined(_WIN32)
    CHECK(fs::remove(built));
#endif
}

TEST("two copies of one library take two names, and unload removes its own copy and leaves the original") {
    const fs::path built   = builtLibrary();
    const std::string stem = built.stem().string();
    DynamicLibrary first;
    DynamicLibrary second;
    REQUIRE(first.loadCopy(built).how == DynamicLibrary::Loaded::Copied);
    REQUIRE(second.loadCopy(built).how == DynamicLibrary::Loaded::Copied);
    CHECK(answerOf(first) == 42);
    CHECK(answerOf(second) == 42);

    std::vector<std::string> names = namesIn(built.parent_path());
    CHECK(names.size() == 3u);
    for (const std::string& name : names) {
        CHECK((name == built.filename().string() || name.rfind(stem + ".loaded.", 0) == 0));
    }

    first.unload();
    CHECK(namesIn(built.parent_path()).size() == 2u);
    CHECK(answerOf(second) == 42);
    second.unload();
    CHECK(namesIn(built.parent_path()) == std::vector<std::string>{built.filename().string()});
}

TEST("from a folder that takes no copy the library loads in place, and any other copy failure loads nothing") {
    // A missing library: the copy fails for a reason that is no folder's, and nothing is tried.
    DynamicLibrary refused;
    const DynamicLibrary::Outcome missing = refused.loadCopy(scratch() / "bin" / "libnothing.so");
    CHECK(missing.how == DynamicLibrary::Loaded::Refused);
    CHECK(static_cast<bool>(missing.error));
    CHECK(!refused.isLoaded());

#if defined(_WIN32)
    Vkm::Test::skip("a folder is made read-only here by its POSIX mode");
#else
    const fs::path built = builtLibrary();
    REQUIRE(::chmod(built.parent_path().c_str(), 0555) == 0);
    const bool writable = ::access(built.parent_path().c_str(), W_OK) == 0;
    DynamicLibrary library;
    const DynamicLibrary::Outcome outcome = library.loadCopy(built);
    ::chmod(built.parent_path().c_str(), 0755);
    if (writable) Vkm::Test::skip("this user writes into a read-only folder");
    CHECK(outcome.how == DynamicLibrary::Loaded::InPlace);
    CHECK(static_cast<bool>(outcome.error));
    CHECK(library.isLoaded());
    CHECK(answerOf(library) == 42);
    library.unload();
    CHECK(fs::exists(built));
#endif
}

TEST("sweepCopies removes the copies a process left behind, and nothing else beside them") {
    const fs::path built  = builtLibrary();
    const fs::path folder = built.parent_path();
    const std::string stem      = built.stem().string();
    const std::string extension = built.extension().string();
    for (const std::string& name : {stem + ".loaded.0" + extension, stem + ".loaded.17" + extension}) {
        fs::copy_file(built, folder / name);
    }
    Vkm::Test::writeText(folder / (stem + ".loaded.notes.txt"), "kept");
    Vkm::Test::writeText(folder / ("other.loaded.3" + extension), "kept");
    Vkm::Test::writeText(folder / (stem + extension + ".bak"), "kept");

    DynamicLibrary::sweepCopies(built);
    const std::vector<std::string> expected = [&] {
        std::vector<std::string> names = {
            built.filename().string(),
            stem + ".loaded.notes.txt",
            "other.loaded.3" + extension,
            stem + extension + ".bak",
        };
        std::sort(names.begin(), names.end());
        return names;
    }();
    CHECK(namesIn(folder) == expected);
}

TEST("a copy that cannot be deleted is said so, by unload and by sweepCopies") {
    const fs::path built  = builtLibrary();
    const fs::path folder = built.parent_path();
    DynamicLibrary library;
    REQUIRE(library.loadCopy(built).how == DynamicLibrary::Loaded::Copied);
    std::string copy;
    for (const std::string& name : namesIn(folder)) {
        if (name != built.filename().string()) copy = name;
    }
    REQUIRE(!copy.empty());
#if defined(_WIN32)
    // Loaded twice, so the first unload leaves it mapped, and Windows deletes no mapped file.
    DynamicLibrary again;
    REQUIRE(again.load(folder / copy));
#else
    REQUIRE(::chmod(folder.c_str(), 0555) == 0);
    const bool writable = ::access(folder.c_str(), W_OK) == 0;
#endif
    library.unload();
    const std::string unloaded = Vkm::Test::logText();
    DynamicLibrary::sweepCopies(built);
    const std::string swept = Vkm::Test::logText().substr(unloaded.size());
#if !defined(_WIN32)
    ::chmod(folder.c_str(), 0755);
    if (writable) Vkm::Test::skip("this user writes into a read-only folder");
#endif
    CHECK(fs::exists(folder / copy));
    CHECK(unloaded.find(copy) != std::string::npos);
    CHECK(swept.find(copy) != std::string::npos);
}

TEST("sweepCopies says so of a folder it cannot list, and nothing of one that is not there") {
    DynamicLibrary::sweepCopies(scratch() / "missing" / "game");
    CHECK(Vkm::Test::logText().empty());

    // A file where the folder would be.
    const fs::path file = scratch() / "file";
    Vkm::Test::writeText(file, "");
    DynamicLibrary::sweepCopies(file / "game");
    CHECK(Vkm::Test::logText().find(file.string()) != std::string::npos);
}

TEST("the executable's folder is the suite's, and the user's folders follow the environment") {
    // Wherever the build puts the suite: a consumer's build gathers executables in a folder of its own.
    REQUIRE(!Vkm::Platform::executableDir().empty());
    CHECK(fs::is_regular_file(Vkm::Platform::executableDir() / VKM_TEST_EXECUTABLE));
#if defined(_WIN32)
    _putenv_s("APPDATA", "C:\\config here");
    _putenv_s("LOCALAPPDATA", "C:\\state here");
    CHECK(Vkm::Platform::userConfigDir() == fs::path("C:\\config here"));
    CHECK(Vkm::Platform::userStateDir() == fs::path("C:\\state here"));
#else
    setenv("XDG_CONFIG_HOME", "/config here", 1);
    setenv("XDG_STATE_HOME", "/state here", 1);
    setenv("HOME", "/home/someone", 1);
    CHECK(Vkm::Platform::userConfigDir() == fs::path("/config here"));
    CHECK(Vkm::Platform::userStateDir() == fs::path("/state here"));

    // Unset or blank, the home's own.
    unsetenv("XDG_CONFIG_HOME");
    setenv("XDG_STATE_HOME", "", 1);
    CHECK(Vkm::Platform::userConfigDir() == fs::path("/home/someone/.config"));
    CHECK(Vkm::Platform::userStateDir() == fs::path("/home/someone/.local/state"));

    unsetenv("XDG_STATE_HOME");
    unsetenv("HOME");
    CHECK(Vkm::Platform::userConfigDir().empty());
    CHECK(Vkm::Platform::userStateDir().empty());
#endif
}

TEST("a file's claim is refused to a second, from this process too, and taken once the first is gone") {
    const fs::path path = scratch() / "claim";
    {
        FileLock first(path);
        REQUIRE(first.held());
        CHECK(fs::exists(path));
        const FileLock second(path);
        CHECK(!second.held());
        const FileLock moved(std::move(first));
        CHECK(moved.held());
        CHECK(!FileLock(path).held());
    }
    const FileLock again(path);
    CHECK(again.held());
    // Refused for its holder, which is no failure.
    CHECK(Vkm::Test::logText().empty());
}

TEST("a claimed file is read and written through another handle, this process's own too") {
    // As a log is: claimed, then written by a logger that opens it itself, its first line at byte 0.
    const fs::path path = scratch() / "claim";
    const FileLock held(path);
    REQUIRE(held.held());
    Vkm::Test::writeText(path, "a line\n");
    CHECK(Vkm::Test::readText(path) == "a line\n");
}

TEST("a claim refused for any reason but its holder says why") {
#if defined(_WIN32)
    // The null device opens as a file does, and takes no lock.
    const FileLock device(fs::path("NUL"));
    if (device.held()) Vkm::Test::skip("this system's null device takes a lock");
    CHECK(Vkm::Test::logText().find("'NUL'") != std::string::npos);
#else
    Vkm::Test::skip("every file Linux opens here takes a flock, or is refused it for its holder");
#endif
}

TEST("a file a claim makes takes the mode any file the process writes takes") {
#if defined(_WIN32)
    Vkm::Test::skip("a mode is POSIX's");
#else
    // No mask, so a mode chosen by the claim shows as itself rather than through the user's umask.
    const mode_t mask = ::umask(0);
    const fs::path claimed = scratch() / "claim";
    const fs::path written = scratch() / "written";
    const FileLock held(claimed);
    Vkm::Test::writeText(written, "");
    ::umask(mask);
    REQUIRE(held.held());
    CHECK(fs::status(claimed).permissions() == fs::status(written).permissions());
#endif
}

TEST("a file's claim ends with its process, even one that aborts") {
    const fs::path path = scratch() / "claim";
    const Vkm::Test::Ended ended = Vkm::Test::inChild([&] {
        const FileLock held(path);
        if (held.held()) std::abort();
    });
    CHECK(ended == Vkm::Test::Ended::Stopped);
    CHECK(FileLock(path).held());
}

TEST("a process that asks whether it should stop is not killed by SIGINT or SIGTERM, and is told") {
#if defined(_WIN32)
    Vkm::Test::skip("the signals are raised here as POSIX raises them");
#else
    for (const int stop : {SIGINT, SIGTERM}) {
        const fs::path told = scratch() / ("told" + std::to_string(stop));
        const Vkm::Test::Ended ended = Vkm::Test::inChild([&] {
            if (Vkm::Platform::stopRequested()) return;
            std::raise(stop);
            if (Vkm::Platform::stopRequested()) Vkm::Test::writeText(told, "yes");
        });
        CHECK(ended == Vkm::Test::Ended::Returned);
        CHECK(Vkm::Test::readText(told) == "yes");
    }
#endif
}

TEST("a process that never asks keeps the system's default, and SIGINT ends it") {
#if defined(_WIN32)
    Vkm::Test::skip("the signals are raised here as POSIX raises them");
#else
    std::fflush(nullptr);
    const pid_t child = fork();
    if (child == 0) {
        std::raise(SIGINT);
        _exit(0);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGINT);
#endif
}

TEST("a process that cannot hear a parent's stop says so") {
#if defined(_WIN32)
    // Its event's name taken by an object of another kind, so the event cannot be opened.
    const std::wstring name = Vkm::Platform::Detail::stopEventName(GetCurrentProcessId());
    const HANDLE taken = CreateMutexW(nullptr, FALSE, name.c_str());
    REQUIRE(taken != nullptr);
    CHECK(!Vkm::Platform::stopRequested());
    CloseHandle(taken);
    CHECK(Vkm::Test::logText().find("ChildProcess::stop") != std::string::npos);
#else
    Vkm::Test::skip("a parent's stop is SIGTERM here, which nothing keeps from a process");
#endif
}

TEST("an executable that asks for the discrete GPU exports both drivers' requests, and elsewhere builds as it is") {
#if defined(_WIN32)
    const HMODULE self   = GetModuleHandleW(nullptr);
    const FARPROC nvidia = GetProcAddress(self, "NvOptimusEnablement");
    const FARPROC amd    = GetProcAddress(self, "AmdPowerXpressRequestHighPerformance");
    REQUIRE(nvidia != nullptr);
    REQUIRE(amd != nullptr);
    CHECK(*reinterpret_cast<const unsigned long*>(nvidia) == 1ul);
    CHECK(*reinterpret_cast<const int*>(amd) == 1);
#else
    Vkm::Test::skip("the exports are read on Windows alone; here the macro is nothing, and this file built");
#endif
}
