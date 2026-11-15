#pragma once

#include <filesystem>

namespace Vkm::Platform {

/**
 * @brief The folder the running executable is in.
 *
 * @return Its absolute path, or empty, logged, when the system does not say: on Linux, past a
 *         page's length.
 */
std::filesystem::path executableDir();

/**
 * @brief Where the system keeps a user's configuration: `%APPDATA%` on Windows; `$XDG_CONFIG_HOME`,
 *        else `~/.config`, on Linux.
 *
 * Read from the environment on each call; nothing is created.
 *
 * @return The folder, or empty when the environment names none.
 */
std::filesystem::path userConfigDir();

/**
 * @brief Where the system keeps a user's state, apart from their configuration: `%LOCALAPPDATA%` on
 *        Windows; `$XDG_STATE_HOME`, else `~/.local/state`, on Linux.
 *
 * Read from the environment on each call; nothing is created.
 *
 * @return The folder, or empty when the environment names none.
 */
std::filesystem::path userStateDir();

/**
 * @brief Write what this process prints to the console it was started from, if it opened none of its
 *        own: a Windows GUI executable opens no terminal, so started from one it would print nowhere.
 *
 * A process with a console already is left as it is, and so is a stream its starter sent to a file
 * or a pipe. Nothing on Linux, where a process prints where it was started.
 */
void attachParentConsole();

/**
 * @brief Whether the system has asked this process to stop since the first call: SIGINT (Ctrl+C) or
 *        SIGTERM; on Windows Ctrl+C, Ctrl+Break, its console closing, or ChildProcess::stop.
 *
 * The first call installs the handlers, and from then on none of these ends the process: it is the
 * caller's to ask each frame and leave through its normal exit. A process that never asks keeps the
 * system's default, which ends it. Each handler sets one lock-free flag; the one for a Windows
 * console closing then holds, as the process ends once it returns, until the process leaves or the
 * system's few seconds run out.
 *
 * Windows has no SIGTERM: in its place the first call opens an event named for the process, which
 * ChildProcess::stop sets; one that will not open is logged. Its name is in the session's own
 * namespace, so what can ask is what SIGTERM lets on POSIX: the user's own processes, and no other
 * session's.
 *
 * @return True once any of them has asked.
 */
bool stopRequested();

} // namespace Vkm::Platform
