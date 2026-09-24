// scoot would - minimal job system (worker thread pool + parallel_for)
#pragma once

#include <functional>
#include <cstdint>

namespace sw::jobs {

void init(int workerCount = -1);  // -1 = hardware threads - 1
void shutdown();
int workerCount();

// run fn(begin, end) over [0, count) split into chunks on worker threads; blocks until done
void parallelFor(uint32_t count, uint32_t minChunk, const std::function<void(uint32_t, uint32_t)>& fn);

// fire and forget background task (asset loading etc.)
void submit(std::function<void()> fn);
void waitIdle();

}  // namespace sw::jobs
