#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Vkm::Platform {

/**
 * @brief A program run in the background, its output collected as it arrives.
 *
 * Standard output and error share one pipe, read on a thread of the process's own, so a caller
 * polls takeOutput() and running() from its frame; standard input is empty. stop() asks the
 * program to stop, then ends it and everything it started - its children included - so nothing
 * outlives it. Should this process die without stopping it, the program is ended too: on Windows
 * with all it started, its job's; on Linux it alone is sent SIGTERM, and what it started is its
 * own to end. Linux sends it when the thread that started the program ends, so start one from a
 * thread that lives as long as it should.
 */
class ChildProcess {
    public:
        ChildProcess() = default;
        ~ChildProcess();

        ChildProcess(const ChildProcess& other) = delete;
        ChildProcess& operator=(const ChildProcess& other) = delete;

        ChildProcess(ChildProcess && other) = delete;
        ChildProcess& operator=(ChildProcess && other) = delete;

    public:
        /**
         * @brief Start @p program with @p args in @p workingDir, ending any program started before.
         *
         * @param program The executable, by path.
         * @param args Its arguments, each passed as one argument. A batch file's (.bat, .cmd) are read by
         *             cmd.exe too, so one holding a quote, a % or a line break is refused.
         * @param workingDir Where it runs; empty keeps this process's.
         * @return Whether it started; a failure is in takeOutput().
         */
        bool start(
            const std::filesystem::path& program,
            const std::vector<std::string>& args,
            const std::filesystem::path& workingDir = {}
        );

        /**
         * @brief Ask the program to stop, then end it and everything it started, and wait for it.
         *
         * The asking is SIGTERM to its process group on POSIX, and on Windows the event a program
         * that calls stopRequested() holds. What is left of the group or the job two seconds on is
         * ended. On Windows, with no program to ask, one that never called it or one that has
         * exited, the job is ended at once.
         */
        void stop();

        /**
         * @brief Take the output that arrived since the last call.
         *
         * @return Text as the program wrote it, possibly ending mid-line.
         */
        std::string takeOutput();

        /**
         * @brief Whether a program was started and has not yet exited.
         *
         * @return True while it runs.
         */
        bool running() const;

        /**
         * @brief How the last program exited.
         *
         * @return Its exit code; -1 while it runs, when none started, or when it was stopped.
         */
        int exitCode() const;

    private:
        /**
         * @brief Collects the output until the program exits, then keeps its exit code.
         *
         * It asks after the program after each read and each quiet wait and, once it has exited,
         * reads what is left in the pipe, which what the program started may hold open. On Linux it
         * leaves the program for stop() to reap.
         */
        void readUntilExit();

    private:
        mutable std::mutex m_mutex;
        std::string        m_output;
        int                m_exitCode = -1;
        bool               m_running  = false;
        std::thread        m_reader;

#if defined(_WIN32)
        void*         m_process = nullptr;
        unsigned long m_pid     = 0;        ///< Names its stop event (stopRequested)
        void*         m_job     = nullptr;  ///< Holds the program and all it starts, so stop() ends them together
        void*         m_pipe    = nullptr;
#else
        int m_pid    = 0;   ///< Also its group's id, which holds all it starts; stop() alone reaps it
        int m_pipeFd = -1;
#endif
};

} // namespace Vkm::Platform
