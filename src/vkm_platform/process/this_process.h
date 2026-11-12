#pragma once

#include <filesystem>

namespace Vkm::Platform {

/**
 * @brief The folder the running executable is in.
 *
 * @return Its absolute path, or empty when the system does not say.
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
 * @brief Whether the system has asked this process to stop: SIGINT (Ctrl+C) or SIGTERM since the
 *        first call.
 *
 * The first call installs the handlers, and from then on neither signal ends the process: it is the
 * caller's to ask each frame and leave through its normal exit. A process that never asks keeps the
 * system's default, which ends it. The handlers set one lock-free flag and touch nothing else.
 *
 * @return True once either signal has arrived.
 */
bool stopRequested();

} // namespace Vkm::Platform
