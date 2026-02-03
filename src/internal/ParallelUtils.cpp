// src/internal/ParallelUtils.cpp
#include "ParallelUtils.h"
#include <algorithm>

namespace MapGenerator {
namespace internal {

ParallelProcessor::ParallelProcessor(uint32_t threadCount) 
    : m_threadCount(std::max(1u, threadCount)) {
    
    m_tasks.resize(m_threadCount);
    
    // 创建工作线程
    for (uint32_t i = 0; i < m_threadCount; ++i) {
        m_workers.emplace_back(&ParallelProcessor::workerThread, this, i);
    }
}

ParallelProcessor::~ParallelProcessor() {
    m_stop = true;
    m_condition.notify_all();
    
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ParallelProcessor::workerThread(uint32_t threadId) {
    while (!m_stop) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [&]() {
            return m_stop || m_tasks[threadId].valid;
        });
        
        if (m_stop) return;
        
        if (m_tasks[threadId].valid) {
            Task task = std::move(m_tasks[threadId]);
            m_tasks[threadId].valid = false;
            lock.unlock();
            
            task.func();
        }
    }
}

void ParallelProcessor::parallelFor1D(uint32_t count, 
                                     std::function<void(uint32_t, uint32_t)> func) {
    
    if (count == 0) return;
    
    // 小数组串行处理
    if (count < 1000 || m_threadCount == 1) {
        for (uint32_t i = 0; i < count; ++i) {
            func(i, i + 1);
        }
        return;
    }
    
    // 计算每个线程处理的范围
    uint32_t itemsPerThread = (count + m_threadCount - 1) / m_threadCount;
    
    std::vector<std::thread> threads;
    threads.reserve(m_threadCount);
    
    for (uint32_t t = 0; t < m_threadCount; ++t) {
        uint32_t startIdx = t * itemsPerThread;
        uint32_t endIdx = std::min(startIdx + itemsPerThread, count);
        
        if (startIdx < count) {
            threads.emplace_back([=, &func]() {
                for (uint32_t i = startIdx; i < endIdx; ++i) {
                    func(i, i + 1);
                }
            });
        }
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
}

void ParallelProcessor::parallelFor1DChunked(uint32_t count, uint32_t chunkSize,
                                           std::function<void(uint32_t, uint32_t)> func) {
    
    if (count == 0) return;
    
    // 调整块大小，确保块数适当
    if (chunkSize == 0) {
        chunkSize = std::max<uint32_t>(1, count / (m_threadCount * 4));
    }
    
    uint32_t numChunks = (count + chunkSize - 1) / chunkSize;
    
    if (numChunks <= 1) {
        func(0, count);
        return;
    }
    
    // 使用原子计数器实现无锁任务分发
    std::atomic<uint32_t> nextChunk{0};
    
    auto worker = [&]() {
        while (true) {
            uint32_t chunkIdx = nextChunk.fetch_add(1);
            if (chunkIdx >= numChunks) break;
            
            uint32_t startIdx = chunkIdx * chunkSize;
            uint32_t endIdx = std::min(startIdx + chunkSize, count);
            
            func(startIdx, endIdx);
        }
    };
    
    // 确定工作线程数
    uint32_t numWorkers = std::min(numChunks, m_threadCount);
    
    std::vector<std::thread> workers;
    workers.reserve(numWorkers - 1);
    
    // 启动工作线程
    for (uint32_t t = 0; t < numWorkers - 1; ++t) {
        workers.emplace_back(worker);
    }
    
    // 主线程也参与工作
    worker();
    
    // 等待工作线程完成
    for (auto& worker : workers) {
        worker.join();
    }
}

void ParallelProcessor::parallelFor2D(uint32_t width, uint32_t height,
                                     std::function<void(uint32_t, uint32_t)> func) {
    
    // 1. 计算总任务数
    size_t totalPixels = static_cast<size_t>(width) * height;
    
    // 2. 根据任务大小决定是否并行
    if (totalPixels < 1000) {  // 小任务串行处理
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                func(x, y);
            }
        }
        return;
    }
    
    // 3. 计算最优的工作线程数
    uint32_t effectiveThreads = std::min(m_threadCount, 
                                        static_cast<uint32_t>(totalPixels / 1000));
    effectiveThreads = std::max(effectiveThreads, 1u);
    
    // 4. 使用更细粒度的任务分片（按块而不是按行）
    const uint32_t optimalChunkSize = 16; // 16x16的块，利于缓存
    
    // 5. 任务队列实现（无锁或细粒度锁）
    struct TaskRange {
        uint32_t startX, endX, startY, endY;
    };
    
    std::vector<TaskRange> tasks;
    
    // 生成任务块
    for (uint32_t y = 0; y < height; y += optimalChunkSize) {
        for (uint32_t x = 0; x < width; x += optimalChunkSize) {
            TaskRange task;
            task.startX = x;
            task.startY = y;
            task.endX = std::min(x + optimalChunkSize, width);
            task.endY = std::min(y + optimalChunkSize, height);
            tasks.push_back(task);
        }
    }
    
    // 6. 使用原子计数器实现无锁任务分发
    std::atomic<size_t> nextTask{0};
    
    // 7. 创建工作线程
    std::vector<std::thread> workers;
    workers.reserve(effectiveThreads);
    
    auto workerFunc = [&]() {
        while (true) {
            size_t taskIndex = nextTask.fetch_add(1, std::memory_order_relaxed);
            if (taskIndex >= tasks.size()) break;
            
            const auto& task = tasks[taskIndex];
            
            // 处理当前任务块
            for (uint32_t y = task.startY; y < task.endY; ++y) {
                for (uint32_t x = task.startX; x < task.endX; ++x) {
                    func(x, y);
                }
            }
        }
    };
    
    // 8. 启动工作线程
    for (uint32_t t = 0; t < effectiveThreads; ++t) {
        workers.emplace_back(workerFunc);
    }
    
    // 9. 主线程也参与工作
    workerFunc();
    
    // 10. 等待所有工作线程完成
    for (auto& worker : workers) {
        worker.join();
    }
}

void ParallelProcessor::parallelFor2DChunked(uint32_t width, uint32_t height, 
                                            uint32_t chunkSize,
                                            std::function<void(uint32_t, uint32_t, uint32_t, uint32_t)> func) {
    
    uint32_t numChunksX = (width + chunkSize - 1) / chunkSize;
    uint32_t numChunksY = (height + chunkSize - 1) / chunkSize;
    uint32_t totalChunks = numChunksX * numChunksY;
    
    if (totalChunks <= 1) {
        func(0, 0, width, height);
        return;
    }
    
    std::atomic<uint32_t> nextChunk{0};
    std::vector<std::thread> threads;
    
    uint32_t numWorkerThreads = std::min(totalChunks, m_threadCount);
    threads.reserve(numWorkerThreads);
    
    for (uint32_t t = 0; t < numWorkerThreads; ++t) {
        threads.emplace_back([=, &nextChunk, &func]() {
            while (true) {
                uint32_t chunkIdx = nextChunk++;
                if (chunkIdx >= totalChunks) break;
                
                uint32_t chunkY = chunkIdx / numChunksX;
                uint32_t chunkX = chunkIdx % numChunksX;
                
                uint32_t startX = chunkX * chunkSize;
                uint32_t startY = chunkY * chunkSize;
                uint32_t endX = std::min(startX + chunkSize, width);
                uint32_t endY = std::min(startY + chunkSize, height);
                
                func(startX, startY, endX, endY);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
}

void ParallelProcessor::processHeightMapParallel(const HeightMap& heightmap, 
                                                uint32_t width, uint32_t height,
                                                std::function<void(uint32_t, uint32_t, float)> func) {
    
    parallelFor2D(width, height, [&](uint32_t x, uint32_t y) {
        uint32_t idx = y * width + x;
        func(x, y, heightmap[idx]);
    });
}

template<typename T>
std::vector<T> ParallelProcessor::parallelGenerate(uint32_t count, 
                                                   std::function<T(uint32_t)> generator) {
    
    std::vector<T> results(count);
    std::vector<std::thread> threads;
    uint32_t itemsPerThread = (count + m_threadCount - 1) / m_threadCount;
    
    for (uint32_t t = 0; t < m_threadCount; ++t) {
        uint32_t startIdx = t * itemsPerThread;
        uint32_t endIdx = std::min(startIdx + itemsPerThread, count);
        
        if (startIdx < count) {
            threads.emplace_back([=, &results, &generator]() {
                for (uint32_t i = startIdx; i < endIdx; ++i) {
                    results[i] = generator(i);
                }
            });
        }
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    return results;
}

} // namespace internal
} // namespace MapGenerator
