#include "vkm_platform/process/child_process.h"

#include <algorithm>
#include <chrono>
#include <system_error>

#include "vkm_platform/process/stop_event.h"
#include "vkm_platform/windows_api.h"

#if !defined(_WIN32)
    #include <cerrno>
    #include <csignal>
    #include <cstring>
    #include <optional>
    #include <fcntl.h>
    #include <poll.h>
    #include <sys/wait.h>
    #if defined(__linux__)
        #include <sys/prctl.h>
    #endif
    #include <unistd.h>
#endif

namespace Vkm::Platform {

namespace {

constexpr int ASK_MS = 50;  ///< How long the reader waits on a quiet pipe before it asks after the program

constexpr auto STOP_GRACE = std::chrono::seconds(2);  ///< How long what stop() asks has to leave

#if defined(_WIN32)

constexpr DWORD PIPE_BYTES = 1 << 20;  ///< How far a writer may run ahead of the reader waiting out ASK_MS

/**
 * @brief One argument quoted as CommandLineToArgvW reads it back.
 *
 * @param arg The argument.
 * @return It as it goes on a command line.
 */
std::wstring quoted(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t slashes   = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        // Backslashes before a quote are doubled, and the quote escaped.
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

/**
 * @brief How many processes @p job holds.
 *
 * @param job A job a program runs in.
 * @return Its count; 0 when it cannot be asked, so a wait on it ends.
 */
DWORD processesIn(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION held{};
    const BOOL asked = QueryInformationJobObject(
        job,
        JobObjectBasicAccountingInformation,
        &held,
        sizeof(held),
        nullptr
    );
    return asked ? held.ActiveProcesses : 0;
}

#else

/**
 * @brief In a forked child that cannot become the program: say why on @p status, then end.
 *
 * @param status The write end of the pipe a successful exec closes.
 */
[[noreturn]] void failStart(int status) {
    const int error = errno;
    // Nothing is left to tell if even this fails.
    const ssize_t told = write(status, &error, sizeof(error));
    static_cast<void>(told);
    _exit(127);
}

/**
 * @brief How @p pid, a child of this process, exited, leaving it unreaped.
 *
 * @param pid The child.
 * @param options WNOHANG to answer at once, or 0 to wait for it to exit.
 * @return Nothing while it runs; once it has exited, its exit code, or -1 when a signal ended it or
 *         it can no longer be asked after.
 */
std::optional<int> exitOf(pid_t pid, int options) {
    siginfo_t info{};
    int       asked = 0;
    do {
        info.si_pid = 0;
        asked       = waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOWAIT | options);
    } while (asked < 0 && errno == EINTR);
    if (asked < 0) return -1;
    if (info.si_pid != pid) return std::nullopt;
    return info.si_code == CLD_EXITED ? info.si_status : -1;
}

#endif

} // namespace

ChildProcess::~ChildProcess() {
    stop();
}

#if defined(_WIN32)

bool ChildProcess::start(
    const std::filesystem::path& program,
    const std::vector<std::string>& args,
    const std::filesystem::path& workingDir
) {
    stop();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_output.clear();
        m_exitCode = -1;
    }

    // A batch file runs through cmd.exe, which reads the line again and acts on more than a space
    // outside quotes: there every word is quoted as cmd.exe reads quotes, with no escapes, and one
    // holding what quotes leave to cmd.exe is refused. /s strips the outer quotes and runs the rest
    // as written; /v:off keeps a ! as written.
    const std::wstring ext = program.extension().wstring();
    const bool batch = _wcsicmp(ext.c_str(), L".cmd") == 0 || _wcsicmp(ext.c_str(), L".bat") == 0;
    std::vector<std::wstring> words = {program.wstring()};
    for (const std::string& arg : args) words.push_back(std::filesystem::path(arg).wstring());

    std::wstring command;
    for (const std::wstring& word : words) {
        if (batch && word.find_first_of(L"\"%\r\n") != std::wstring::npos) {
            const std::string what = std::filesystem::path(word).string();
            const std::string why  = ": cmd.exe, which runs it, would act on the quote, % or line break in ";
            m_output = "could not start " + program.string() + why + what + "\n";
            return false;
        }
        command += (command.empty() ? L"" : L" ") + (batch ? L"\"" + word + L"\"" : quoted(word));
    }
    std::wstring line = command;
    if (batch) {
        wchar_t comspec[MAX_PATH];
        const DWORD length = GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
        const std::wstring shell = length > 0 && length < MAX_PATH ? comspec : L"cmd.exe";
        line = L"\"" + shell + L"\" /d /v:off /s /c \"" + command + L"\"";
    }

    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, PIPE_BYTES)) {
        const std::string why = std::system_category().message(static_cast<int>(GetLastError()));
        m_output = "could not open a pipe to the program: " + why + "\n";
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nothing = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &inherit, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW startup{};
    startup.cb         = sizeof(startup);
    startup.dwFlags    = STARTF_USESTDHANDLES;
    startup.hStdInput  = nothing;
    startup.hStdOutput = writeEnd;
    startup.hStdError  = writeEnd;

    PROCESS_INFORMATION info{};
    const std::wstring dir = workingDir.wstring();

    // Created suspended, so it is in the job before it can start anything of its own; one the job
    // does not hold is ended, as stop() could not reach what it starts.
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    const HANDLE job     = CreateJobObjectW(nullptr, nullptr);
    const bool   limited = job && SetInformationJobObject(
        job,
        JobObjectExtendedLimitInformation,
        &limits,
        sizeof(limits)
    );
    const bool   created = limited && CreateProcessW(
        nullptr,
        line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        dir.empty() ? nullptr : dir.c_str(),
        &startup,
        &info
    );
    const bool  held    = created && AssignProcessToJobObject(job, info.hProcess);
    const bool  resumed = held && ResumeThread(info.hThread) != static_cast<DWORD>(-1);
    const DWORD error   = GetLastError();
    CloseHandle(writeEnd);
    if (nothing != INVALID_HANDLE_VALUE) CloseHandle(nothing);
    if (created) CloseHandle(info.hThread);
    if (!resumed) {
        if (created) {
            TerminateProcess(info.hProcess, 1);
            CloseHandle(info.hProcess);
        }
        CloseHandle(readEnd);
        if (job) CloseHandle(job);
        const std::string path  = program.string();
        const std::string where = dir.empty() ? path : path + " in " + workingDir.string();
        const std::string why   = std::system_category().message(static_cast<int>(error));
        m_output = "could not start " + where + ": " + why + "\n";
        return false;
    }

    m_process = info.hProcess;
    m_pid     = info.dwProcessId;
    m_job     = job;
    m_pipe    = readEnd;
    m_running = true;
    m_reader  = std::thread(&ChildProcess::readUntilExit, this);
    return true;
}

void ChildProcess::readUntilExit() {
    // Peeked, never read blocking: what the program started may hold the pipe open, and quiet, long
    // after the program exited.
    const HANDLE pipe    = static_cast<HANDLE>(m_pipe);
    const HANDLE process = static_cast<HANDLE>(m_process);
    char  buffer[4096];
    DWORD waiting = 0;
    bool  exited  = false;
    while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &waiting, nullptr)) {
        if (waiting > 0) {
            DWORD got = 0;
            if (!ReadFile(pipe, buffer, std::min<DWORD>(waiting, sizeof(buffer)), &got, nullptr)) break;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_output.append(buffer, got);
            continue;
        }
        if (exited) break;
        exited = WaitForSingleObject(process, ASK_MS) == WAIT_OBJECT_0;
    }
    WaitForSingleObject(process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process, &code);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_exitCode = static_cast<int>(code);
    m_running  = false;
}

void ChildProcess::stop() {
    if (!m_reader.joinable()) return;
    const bool ending = running();
    if (ending) {
        // Asked first, as SIGTERM asks on POSIX: a program that asks whether it should stop holds
        // the event named for it, and its job is given two seconds to empty. Whatever of the job is
        // left then is ended, and so is a program that never asked, at once. While the process
        // handle is open, its id names no other process.
        const std::wstring name = Detail::stopEventName(m_pid);
        if (const HANDLE asked = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str())) {
            SetEvent(asked);
            CloseHandle(asked);
            const auto deadline = std::chrono::steady_clock::now() + STOP_GRACE;
            while (processesIn(m_job) > 0 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        TerminateJobObject(static_cast<HANDLE>(m_job), 1);
    }
    m_reader.join();
    if (ending) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_exitCode = -1;
    }
    CloseHandle(static_cast<HANDLE>(m_pipe));
    CloseHandle(static_cast<HANDLE>(m_process));
    CloseHandle(static_cast<HANDLE>(m_job));
    m_pipe = m_process = m_job = nullptr;
    m_pid  = 0;
}

#else

bool ChildProcess::start(
    const std::filesystem::path& program,
    const std::vector<std::string>& args,
    const std::filesystem::path& workingDir
) {
    stop();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_output.clear();
        m_exitCode = -1;
    }

    // Everything the child needs is made here: between fork and exec it may not allocate.
    const std::string path = program.string();
    const std::string dir = workingDir.string();
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(path.c_str()));
    for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        const int error = errno;
        m_output = std::string("could not open a pipe to the program: ") + std::strerror(error) + "\n";
        return false;
    }
    // Closed by the exec that starts the program; one that fails writes its errno first.
    int status[2];
    if (pipe2(status, O_CLOEXEC) != 0) {
        const int error = errno;
        close(fds[0]);
        close(fds[1]);
        m_output = std::string("could not open a pipe to the program: ") + std::strerror(error) + "\n";
        return false;
    }

    const pid_t parent  = getpid();
    const pid_t pid     = fork();
    const int forkError = pid < 0 ? errno : 0;
    if (pid == 0) {
        // A group of its own, so stop() reaches everything it starts.
        setpgid(0, 0);
#if defined(__linux__)
        // Ended with this process, as Windows' job ends it, should this one die first.
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent) _exit(127);
#endif
        // No input, as on Windows: this process's may be a terminal, which stops a reader of another
        // group, or a pipe that never ends.
        const int nothing = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (nothing < 0) failStart(status[1]);
        dup2(nothing, STDIN_FILENO);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        if (!dir.empty() && chdir(dir.c_str()) != 0) failStart(status[1]);
        execv(path.c_str(), argv.data());
        failStart(status[1]);
    }
    close(fds[1]);
    close(status[1]);
    if (pid < 0) {
        close(fds[0]);
        close(status[0]);
        m_output = "could not start " + path + ": " + std::strerror(forkError) + "\n";
        return false;
    }
    // Said by both sides: a stop() before the child runs must still find its group.
    setpgid(pid, pid);

    // Nothing to read once the program has started; an errno when it could not.
    int     error = 0;
    ssize_t told  = 0;
    do {
        told = read(status[0], &error, sizeof(error));
    } while (told < 0 && errno == EINTR);
    close(status[0]);
    if (told == static_cast<ssize_t>(sizeof(error))) {
        int ended = 0;
        while (waitpid(pid, &ended, 0) < 0 && errno == EINTR) {}
        close(fds[0]);
        const std::string where = dir.empty() ? path : path + " in " + dir;
        m_output = "could not start " + where + ": " + std::strerror(error) + "\n";
        return false;
    }

    m_pid     = pid;
    m_pipeFd  = fds[0];
    m_running = true;
    m_reader  = std::thread(&ChildProcess::readUntilExit, this);
    return true;
}

void ChildProcess::readUntilExit() {
    // The pipe ends when every holder closes it, which a daemon the program left behind never
    // does: so the program is asked after each read and each quiet wait, and once it has
    // exited, what is left in the pipe is read and the rest is not its output. It is left for
    // stop() to reap.
    char buffer[4096];
    std::optional<int> code;
    for (;;) {
        pollfd waiting{m_pipeFd, POLLIN, 0};
        const int ready = poll(&waiting, 1, code ? 0 : ASK_MS);
        if (ready < 0 && errno == EINTR) continue;
        if (ready > 0) {
            const ssize_t got = read(m_pipeFd, buffer, sizeof(buffer));
            if (got > 0) {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_output.append(buffer, static_cast<size_t>(got));
                }
                if (!code) code = exitOf(m_pid, WNOHANG);
                continue;
            }
            if (got < 0 && errno == EINTR) continue;
            break;
        }
        if (ready < 0 || code) break;
        code = exitOf(m_pid, WNOHANG);
    }
    if (!code) code = exitOf(m_pid, 0);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_exitCode = *code;
    m_running  = false;
}

void ChildProcess::stop() {
    if (!m_reader.joinable()) return;
    const bool ending = running();
    // Asked first, though the program may have exited, as what it started may not have; whatever of
    // its group still runs two seconds on is ended. The program is reaped here alone: until then the
    // group's id is its own, and after, what is left of the group holds it.
    kill(-m_pid, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + STOP_GRACE;
    bool reaped = false;
    bool held   = true;
    for (;;) {
        reaped = reaped || waitpid(m_pid, nullptr, WNOHANG) != 0;
        held   = !reaped || kill(-m_pid, 0) == 0;
        if (!held || std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (held) kill(-m_pid, SIGKILL);
    m_reader.join();
    while (!reaped && waitpid(m_pid, nullptr, 0) < 0 && errno == EINTR) {}
    if (ending) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_exitCode = -1;
    }
    close(m_pipeFd);
    m_pipeFd = -1;
    m_pid    = 0;
}

#endif

std::string ChildProcess::takeOutput() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string out;
    out.swap(m_output);
    return out;
}

bool ChildProcess::running() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_running;
}

int ChildProcess::exitCode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_exitCode;
}

} // namespace Vkm::Platform
