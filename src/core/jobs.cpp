#include "core/jobs.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>

namespace sw::jobs {

namespace {
std::vector<std::thread> g_workers;
std::deque<std::function<void()>> g_queue;
std::mutex g_mutex;
std::condition_variable g_cv, g_idleCv;
std::atomic<bool> g_quit{false};
std::atomic<int> g_active{0};

void workerMain() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.wait(lock, [] { return g_quit || !g_queue.empty(); });
            if (g_quit && g_queue.empty()) return;
            job = std::move(g_queue.front());
            g_queue.pop_front();
            ++g_active;
        }
        job();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            --g_active;
        }
        g_idleCv.notify_all();
    }
}
}  // namespace

void init(int workerCount) {
    if (!g_workers.empty()) return;
    int n = workerCount;
    if (n < 0) n = std::max(1, int(std::thread::hardware_concurrency()) - 1);
    n = std::min(n, 16);
    g_quit = false;
    for (int i = 0; i < n; ++i) g_workers.emplace_back(workerMain);
}

void shutdown() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_quit = true;
    }
    g_cv.notify_all();
    for (auto& t : g_workers) t.join();
    g_workers.clear();
}

int workerCount() { return int(g_workers.size()); }

void submit(std::function<void()> fn) {
    if (g_workers.empty()) {
        fn();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_queue.push_back(std::move(fn));
    }
    g_cv.notify_one();
}

void waitIdle() {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_idleCv.wait(lock, [] { return g_queue.empty() && g_active == 0; });
}

void parallelFor(uint32_t count, uint32_t minChunk, const std::function<void(uint32_t, uint32_t)>& fn) {
    if (count == 0) return;
    uint32_t workers = uint32_t(g_workers.size()) + 1;
    uint32_t chunk = std::max(minChunk, (count + workers - 1) / workers);
    if (g_workers.empty() || chunk >= count) {
        fn(0, count);
        return;
    }
    std::atomic<uint32_t> remaining{0};
    std::mutex doneMutex;
    std::condition_variable doneCv;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    for (uint32_t b = chunk; b < count; b += chunk) ranges.push_back({b, std::min(count, b + chunk)});
    remaining = uint32_t(ranges.size());
    for (auto r : ranges) {
        submit([&, r] {
            fn(r.first, r.second);
            if (--remaining == 0) {
                std::lock_guard<std::mutex> lock(doneMutex);
                doneCv.notify_all();
            }
        });
    }
    fn(0, std::min(chunk, count));  // calling thread takes the first chunk
    std::unique_lock<std::mutex> lock(doneMutex);
    doneCv.wait(lock, [&] { return remaining.load() == 0; });
}

}  // namespace sw::jobs
