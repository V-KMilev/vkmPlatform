// The thread pool: parallelFor, batches and background tasks.

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
    #include <unistd.h>
#endif

#if defined(__linux__)
    #include <sched.h>

    #include <fstream>
    #include <set>
    #include <utility>
#endif

#include "vkm_platform/threading/thread_pool.h"

#include "harness/check.h"
#include "harness/scratch.h"

namespace {

using Vkm::Platform::ThreadPool;
using Vkm::Platform::parallelFor;

/// Wait, a millisecond at a time and @p seconds at most, until @p done says so.
template<typename Done>
bool within(Done done, int seconds = 10) {
    for (int i = 0; i < seconds * 1000 && !done(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

ThreadPool& poolWithWorkers() {
    ThreadPool& pool = ThreadPool::get();
    if (pool.threadCount() == 0) Vkm::Test::skip("the pool has no workers on this machine");
    return pool;
}

} // namespace

TEST("parallelFor runs every index exactly once, from none to many, across the grain's edges") {
    int wrong = 0;
    const size_t counts[] = {0, 1, 2, 7, 8, 9, 15, 16, 17, 63, 64, 65, 1000, 2047, 2048, 5000};
    for (const size_t count : counts) {
        for (const size_t grain : {0u, 1u, 8u, 16u, 5000u}) {
            std::vector<std::atomic<int>> runs(count);
            parallelFor(count, grain, [&](size_t i) { runs[i].fetch_add(1); });
            for (const std::atomic<int>& run : runs) wrong += run.load() != 1;
        }
        std::vector<std::atomic<int>> runs(count);
        parallelFor(count, [&](size_t i) { runs[i].fetch_add(1); });
        for (const std::atomic<int>& run : runs) wrong += run.load() != 1;
    }
    CHECK(wrong == 0);
}

TEST("from a worker, parallelFor runs the whole range on that worker") {
    ThreadPool& pool = poolWithWorkers();
    std::atomic<bool>            done{false};
    std::mutex                   mutex;
    std::vector<std::thread::id> threads;
    std::thread::id              worker;
    pool.addTask([&] {
        worker = std::this_thread::get_id();
        parallelFor(500, 1, [&](size_t) {
            std::lock_guard<std::mutex> lock(mutex);
            threads.push_back(std::this_thread::get_id());
        });
        done = true;
    });
    REQUIRE(within([&] { return done.load(); }));
    CHECK(threads.size() == 500u);
    int elsewhere = 0;
    for (const std::thread::id id : threads) elsewhere += id != worker;
    CHECK(elsewhere == 0);
    CHECK(worker != std::this_thread::get_id());
}

TEST("a throw in parallelFor reaches its caller once every other chunk has run") {
    std::atomic<int> ran{0};
    std::string      caught;
    try {
        // An index a chunk: a throw ends its own chunk alone.
        parallelFor(4000, 1, [&](size_t i) {
            ran.fetch_add(1);
            if (i == 2357) throw std::runtime_error("index 2357");
        });
    } catch (const std::runtime_error& e) {
        caught = e.what();
    }
    CHECK(caught == "index 2357");
    CHECK(ran.load() == 4000);

    // The caller's own chunk throwing waits for the queued ones too, and the pool is whole after.
    std::atomic<int> queued{0};
    try {
        parallelFor(4000, 1, [&](size_t i) {
            if (i == 0) throw std::runtime_error("the caller's");
            queued.fetch_add(1);
        });
    } catch (const std::runtime_error& e) {
        caught = e.what();
    }
    CHECK(caught == "the caller's");
    CHECK(queued.load() == 3999);
    std::atomic<int> after{0};
    parallelFor(4000, 10, [&](size_t) { after.fetch_add(1); });
    CHECK(after.load() == 4000);
}

TEST("a batch is waited for alone: with every worker held by background tasks, its caller runs it whole") {
    ThreadPool& pool = poolWithWorkers();
    std::atomic<size_t> held{0};
    std::atomic<size_t> finished{0};
    std::atomic<bool>   release{false};
    for (size_t i = 0; i < pool.threadCount(); ++i) {
        pool.addTask([&] {
            held.fetch_add(1);
            within([&] { return release.load(); }, 60);
            finished.fetch_add(1);
        });
    }
    REQUIRE(within([&] { return held.load() == pool.threadCount(); }));

    std::atomic<int>  ran{0};
    std::atomic<bool> done{false};
    std::thread       caller([&] {
        parallelFor(1000, 1, [&](size_t) { ran.fetch_add(1); });
        done = true;
    });
    CHECK(within([&] { return done.load(); }));
    CHECK(ran.load() == 1000);
    CHECK(finished.load() == 0u);
    release = true;
    caller.join();
    CHECK(within([&] { return finished.load() == pool.threadCount(); }));
}

TEST("a batch is taken before a background task queued ahead of it") {
    ThreadPool& pool = poolWithWorkers();

    // Every worker held, then one let go: what it takes next is the order.
    std::atomic<size_t> held{0};
    std::atomic<size_t> gate{0};
    std::atomic<size_t> finished{0};
    for (size_t i = 0; i < pool.threadCount(); ++i) {
        pool.addTask([&, i] {
            held.fetch_add(1);
            within([&] { return gate.load() > i; });
            finished.fetch_add(1);
        });
    }
    REQUIRE(within([&] { return held.load() == pool.threadCount(); }));

    std::mutex               mutex;
    std::vector<std::string> order;

    const auto note = [&](const char* what) {
        std::lock_guard<std::mutex> lock(mutex);
        order.push_back(what);
    };
    pool.addTask([&] { note("task"); });
    const auto index = [&](size_t) { note("batch"); };
    ThreadPool::Batch batch(index);
    pool.addBatch(batch, 1);

    gate = 1;
    REQUIRE(within([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return order.size() == 2u;
    }));
    CHECK(order == std::vector<std::string>{"batch", "task"});
    gate = pool.threadCount();
    pool.waitForBatch(batch);
    // The held tasks read this test's locals: all let go before it returns.
    REQUIRE(within([&] { return finished.load() == pool.threadCount(); }));
}

TEST("a background task's throw is caught and logged by its worker, which goes on") {
    ThreadPool& pool = poolWithWorkers();
    pool.addTask([] { throw std::runtime_error("the task's own words"); });
    std::atomic<bool> later{false};
    pool.addTask([&] { later = true; });
    CHECK(within([&] { return later.load(); }));
    CHECK(within([] {
        return Vkm::Test::logText().find("ThreadPool task threw: the task's own words") != std::string::npos;
    }));
}

TEST("parallelFor on a thread of its own runs every index, though the pool shuts down as it runs") {
    ThreadPool& pool = poolWithWorkers();

    // Every worker held until shutdown() has begun, so none claims an index on its way out.
    std::atomic<size_t> held{0};
    std::atomic<bool>   release{false};
    for (size_t i = 0; i < pool.threadCount(); ++i) {
        pool.addTask([&] {
            held.fetch_add(1);
            within([&] { return release.load(); });
        });
    }
    REQUIRE(within([&] { return held.load() == pool.threadCount(); }));

    std::atomic<bool> inChunk{false};
    std::atomic<bool> stopped{false};
    std::atomic<int>  ran{0};
    std::thread       caller([&] {
        parallelFor(1000, 1, [&](size_t i) {
            // Index 0 is the caller's own chunk, which spans the shutdown.
            if (i == 0) {
                inChunk = true;
                within([&] { return stopped.load(); });
            }
            ran.fetch_add(1);
        });
    });
    CHECK(within([&] { return inChunk.load(); }));

    std::thread stopper([&] { pool.shutdown(); });
    // shutdown() has begun once a task is refused.
    CHECK(within([&] {
        pool.addTask([] {});
        return Vkm::Test::logText().find("addTask after shutdown") != std::string::npos;
    }));
    release = true;
    stopper.join();
    stopped = true;
    caller.join();
    CHECK(ran.load() == 1000);
}

TEST("the worker count may be asked on one thread while another shuts the pool down, and falls to zero") {
    ThreadPool&  pool    = poolWithWorkers();
    const size_t workers = pool.threadCount();

    std::atomic<bool> asking{false};
    std::atomic<bool> stopped{false};
    std::atomic<int>  wrong{0};
    std::thread       asker([&] {
        while (!stopped) {
            const size_t count = pool.threadCount();
            wrong += count != workers && count != 0;
            asking = true;
        }
    });
    CHECK(within([&] { return asking.load(); }));
    pool.shutdown();
    stopped = true;
    asker.join();
    CHECK(wrong.load() == 0);
    CHECK(pool.threadCount() == 0u);
}

TEST("after shutdown, parallelFor and a batch run on their caller and a background task is refused") {
    ThreadPool& pool = ThreadPool::get();
    pool.shutdown();
    pool.shutdown();
    CHECK(pool.threadCount() == 0u);

    std::vector<std::thread::id> threads;
    const auto note = [&](size_t) { threads.push_back(std::this_thread::get_id()); };
    parallelFor(300, 1, note);
    CHECK(threads == std::vector<std::thread::id>(300, std::this_thread::get_id()));

    threads.clear();
    ThreadPool::Batch batch(note);
    pool.addBatch(batch, 300);
    pool.waitForBatch(batch);
    CHECK(threads == std::vector<std::thread::id>(300, std::this_thread::get_id()));

    std::atomic<bool> ran{false};
    pool.addTask([&] { ran = true; });
    CHECK(Vkm::Test::logText().find("addTask after shutdown") != std::string::npos);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!ran.load());
}

TEST("a batch queued and waited for allocates nothing") {
    ThreadPool& pool = ThreadPool::get();
    std::atomic<int> sum{0};
    const auto task = [&](size_t i) { sum.fetch_add(static_cast<int>(i)); };
    ThreadPool::Batch batch(task);
    pool.addBatch(batch, 64);
    pool.waitForBatch(batch);
    parallelFor(4096, 16, task);

    const uint64_t before = Vkm::Test::allocations();
    for (int round = 0; round < 50; ++round) {
        pool.addBatch(batch, 64);
        pool.waitForBatch(batch);
        parallelFor(4096, 16, task);
    }
    CHECK(Vkm::Test::allocations() == before);
    CHECK(sum.load() == 51 * (2016 + 8386560));
}

TEST("the default worker count is one fewer than the physical cores the process may run on, and one at least") {
#if !defined(__linux__)
    Vkm::Test::skip("which CPUs a process may run on is set here through Linux's affinity");
#else
    // The CPUs this process may use, each with its core as the system lists it: the siblings sharing it.
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    REQUIRE(sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
    std::vector<std::pair<int, std::string>> cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        const std::string at = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
        std::ifstream     file(at + "/topology/thread_siblings_list");
        std::string   siblings;
        if (CPU_ISSET(cpu, &allowed) && std::getline(file, siblings)) cpus.emplace_back(cpu, siblings);
    }
    REQUIRE(!cpus.empty());

    // In a child of its own each, held to the CPUs given: what it would start the pool with.
    const auto countOn = [&](const std::vector<int>& given) {
        const std::filesystem::path answer = Vkm::Test::scratch() / "count.txt";
        std::filesystem::remove(answer);
        const Vkm::Test::Ended ended = Vkm::Test::inChild([&] {
            cpu_set_t set;
            CPU_ZERO(&set);
            for (const int cpu : given) CPU_SET(cpu, &set);
            if (sched_setaffinity(0, sizeof(set), &set) != 0) return;
            Vkm::Test::writeText(answer, std::to_string(ThreadPool::defaultThreadCount()));
        });
        const std::string text = Vkm::Test::readText(answer);
        return ended == Vkm::Test::Ended::Returned && !text.empty() ? std::stoi(text) : -1;
    };

    std::vector<int>      oneEachCore;
    std::vector<int>      firstCore;
    std::set<std::string> seen;
    for (const auto& [cpu, core] : cpus) {
        if (seen.insert(core).second) oneEachCore.push_back(cpu);
        if (core == cpus.front().second) firstCore.push_back(cpu);
    }
    CHECK(countOn({cpus.front().first}) == 1);
    if (oneEachCore.size() >= 3) CHECK(countOn({oneEachCore[0], oneEachCore[1], oneEachCore[2]}) == 2);
    // Two CPUs of one core are one core.
    if (firstCore.size() >= 2 && oneEachCore.size() >= 2) {
        CHECK(countOn({firstCore[0], firstCore[1], oneEachCore[1]}) == 1);
    }
    CHECK(countOn(oneEachCore) == static_cast<int>(std::max<size_t>(oneEachCore.size(), 2) - 1));
#endif
}

TEST("a batch queued again before it was waited for stops the program, or without asserts is refused and said") {
#if defined(_WIN32)
    Vkm::Test::skip("a child process here needs fork");
#else
    const auto ended = Vkm::Test::inChild([] {
        alarm(10);  // A pool the second queuing broke may never finish the batch.
        std::atomic<bool> go{false};
        const auto spin = [&](size_t) {
            while (!go) std::this_thread::yield();
        };
        ThreadPool::Batch batch(spin);
        ThreadPool::get().addBatch(batch, 4);
        ThreadPool::get().addBatch(batch, 4);
        go = true;
        ThreadPool::get().waitForBatch(batch);
    });
    if (VKM_TEST_LIBRARY_ASSERTS) {
        CHECK(ended == Vkm::Test::Ended::Stopped);
    } else {
        CHECK(ended == Vkm::Test::Ended::Returned);
        CHECK(Vkm::Test::logText().find("queued again") != std::string::npos);
    }
#endif
}
