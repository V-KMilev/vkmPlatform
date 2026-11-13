#define VKM_LOG_CATEGORY "THREAD"

#include "vkm_platform/threading/thread_pool.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__)
    #include <sched.h>
#endif

#include "vkm_log/assert.h"
#include "vkm_log/logger.h"
#include "vkm_platform/windows_api.h"

namespace Vkm::Platform {

namespace {
// Set inside process(); see ThreadPool::isWorkerThread.
thread_local bool t_isWorker = false;

/**
 * @brief The physical cores the process may run on, a core's SMT siblings counted once: a
 *        container or `taskset` that grants fewer gets fewer workers.
 *
 * @return The count, or 0 when the platform does not say.
 */
size_t physicalCores() {
    size_t cores = 0;
#if defined(_WIN32)
    // One record per core, each as long as its Size says; a core counts while the process's mask,
    // over its first group, holds one of its logical processors.
    using Record = SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX;
    DWORD_PTR allowed = 0;
    DWORD_PTR system  = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system)) allowed = ~DWORD_PTR(0);
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    std::vector<char> records(bytes);
    auto* first = reinterpret_cast<Record*>(records.data());
    if (bytes == 0 || !GetLogicalProcessorInformationEx(RelationProcessorCore, first, &bytes)) return 0;
    for (DWORD at = 0; at < bytes; at += reinterpret_cast<const Record*>(&records[at])->Size) {
        const GROUP_AFFINITY& group = reinterpret_cast<const Record*>(&records[at])->Processor.GroupMask[0];
        if (group.Group != 0 || (group.Mask & allowed) != 0) ++cores;
    }
#elif defined(__linux__)
    // A core is the set of logical CPUs sharing it, which each lists. Not core_id: an AMD module's
    // two cores have ids of their own and share one front end.
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    const bool masked = sched_getaffinity(0, sizeof(allowed), &allowed) == 0;
    std::set<std::string> siblingSets;
    std::error_code       error;
    for (const auto& cpu : std::filesystem::directory_iterator("/sys/devices/system/cpu", error)) {
        // cpu0, cpu1...; not cpufreq or cpuidle.
        const std::string name = cpu.path().filename().string();
        const bool numbered    = name.size() > 3 && std::isdigit(static_cast<unsigned char>(name[3]));
        if (name.compare(0, 3, "cpu") != 0 || !numbered) continue;
        const int index = std::atoi(name.c_str() + 3);
        if (masked && (index >= CPU_SETSIZE || !CPU_ISSET(index, &allowed))) continue;
        std::ifstream file(cpu.path() / "topology" / "thread_siblings_list");
        std::string   siblings;
        if (std::getline(file, siblings)) siblingSets.insert(siblings);
    }
    cores = siblingSets.size();
#endif
    return cores;
}

/// The logical CPUs the process may run on, for a platform that names no cores.
size_t logicalCpus() {
#if defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0) return static_cast<size_t>(CPU_COUNT(&allowed));
#endif
    return std::thread::hardware_concurrency();
}
} // namespace

size_t ThreadPool::defaultThreadCount() {
    // One fewer than the cores, as the parallelFor caller runs a chunk too.
    const size_t cores = physicalCores();
    return std::max<size_t>(cores > 0 ? cores : logicalCpus(), 2) - 1;
}

bool ThreadPool::isWorkerThread() {
    return t_isWorker;
}

ThreadPool::ThreadPool(size_t threadCount) : m_running(true) {
    LOG_INFO("Thread pool: %zu worker(s) beside the main thread", threadCount);
    for (size_t i = 0; i < threadCount; ++i) {
        m_threads.emplace_back([this]() { process(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

ThreadPool& ThreadPool::get() {
    static ThreadPool s_instance(defaultThreadCount());
    return s_instance;
}

void ThreadPool::addTask(std::function<void()> && task) {
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        if (!m_running) {
            LOG_WARNING("ThreadPool::addTask after shutdown; the task is dropped");
            return;
        }
        m_backgroundTasks.push_back(QueuedTask{std::move(task), nullptr, 0});
    }

    m_tasksCV.notify_one();
}

void ThreadPool::addBatch(Batch& batch, size_t count) {
    if (count == 0) return;
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        // Queued again while running, its counters would reset under workers still claiming from it.
        VKM_ASSERT(batch.m_pending == 0, "a batch queued again before it was waited for");
        if (batch.m_pending != 0) {
            LOG_ERROR("A batch was queued again before it was waited for; the second queuing is dropped");
            return;
        }
        batch.m_count   = count;
        batch.m_claimed = 0;
        batch.m_error   = nullptr;
        batch.m_pending += batch.m_count;
        m_batches.push_back(&batch);
    }

    m_tasksCV.notify_all();
}

void ThreadPool::waitForBatch(Batch& batch) {
    // A batch counter only falls, so a zero read is final without a lock.
    std::atomic<size_t>& pending = batch.m_pending;
    if (pending.load() != 0) {
        // Claimed as a worker claims, wherever the entry stands: another batch may be ahead.
        for (;;) {
            size_t index = 0;
            {
                std::lock_guard<std::mutex> lock(m_tasksMutex);
                const auto entry = std::find(m_batches.begin(), m_batches.end(), &batch);
                if (entry == m_batches.end()) break;

                index = batch.m_claimed++;
                if (batch.m_claimed == batch.m_count) m_batches.erase(entry);
            }
            runIndex(batch, index);
        }

        std::unique_lock<std::mutex> lock(m_tasksMutex);
        m_doneCV.wait(lock, [&pending]() { return pending.load() == 0; });
    }

    // Written before the retire that zeroed the counter, so the read that saw zero sees it.
    if (batch.m_error) std::rethrow_exception(std::exchange(batch.m_error, nullptr));
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        m_running = false;
    }
    m_tasksCV.notify_all();

    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    m_threads.clear();

    // A batch stays: its waiter runs every index no worker claimed.
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    m_backgroundTasks.clear();
}

void ThreadPool::process() {
    t_isWorker = true;
    while (m_running) {
        QueuedTask queued;
        {
            std::unique_lock<std::mutex> lock(m_tasksMutex);
            m_tasksCV.wait(lock, [this]() {
                return !m_batches.empty() || !m_backgroundTasks.empty() || !m_running;
            });

            if (!m_running) continue;

            // Batches first; the class comment says why this cannot starve. A batch stays at the
            // front until its last index is claimed.
            if (!m_batches.empty()) {
                Batch* batch = m_batches.front();
                queued.batch = batch;
                queued.index = batch->m_claimed++;
                if (batch->m_claimed == batch->m_count) m_batches.erase(m_batches.begin());
            } else if (!m_backgroundTasks.empty()) {
                queued = std::move(m_backgroundTasks.front());
                m_backgroundTasks.pop_front();
            } else {
                continue;
            }
        }

        if (queued.batch) {
            runIndex(*queued.batch, queued.index);
            continue;
        }

        // Nobody waits on a background task to rethrow to; this is the net under it.
        try {
            queued.function();
        } catch (const std::exception& e) {
            LOG_ERROR("ThreadPool task threw: %s", e.what());
        } catch (...) {
            LOG_ERROR("ThreadPool task threw unknown exception");
        }
    }
}

void ThreadPool::runIndex(Batch& batch, size_t index) {
    // Retire whatever the index does, or waitForBatch blocks forever.
    try {
        batch.m_run(batch.m_task, index);
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        if (!batch.m_error) batch.m_error = std::current_exception();
    }
    retire(batch.m_pending);
}

void ThreadPool::retire(std::atomic<size_t>& pending) {
    if (pending.fetch_sub(1) != 1) return;

    // Empty on purpose: a waiter holds this lock from reading nonzero until it sleeps, so taking
    // it puts the notify after that sleep.
    { std::lock_guard<std::mutex> lock(m_tasksMutex); }
    m_doneCV.notify_all();
}

} // namespace Vkm::Platform
