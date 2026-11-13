#pragma once

#include <string>

namespace Vkm::Platform::Detail {

/**
 * @brief On Windows, the event a process that asks whether it should stop (stopRequested) waits on,
 *        and ChildProcess::stop sets: Windows' SIGTERM.
 *
 * In the session's Local\ namespace, so a process of another session cannot reach it: the reach
 * SIGTERM has on POSIX, a user's own processes.
 *
 * @param pid The process the event is for.
 * @return Its name.
 */
inline std::wstring stopEventName(unsigned long pid) { return L"Local\\vkm-stop-" + std::to_wstring(pid); }

} // namespace Vkm::Platform::Detail
