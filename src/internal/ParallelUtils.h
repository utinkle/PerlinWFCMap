// src/internal/ParallelUtils.h
#ifndef MAPGENERATOR_INTERNAL_PARALLELUTILS_H
#define MAPGENERATOR_INTERNAL_PARALLELUTILS_H

#include "MapGenerator.h"
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>

namespace MapGenerator {
namespace internal {

class ParallelProcessor {
public:
    ParallelProcessor(uint32_t threadCount);
    ~ParallelProcessor();

    // 1D并行循环
    void parallelFor1D(uint32_t count, std::function<void(uint32_t, uint32_t)> func);
    void parallelFor1DChunked(uint32_t count, uint32_t chunkSize,
                             std::function<void(uint32_t, uint32_t)> func);

    // 专门用于数组操作的并行函数
    template<typename T>
    void parallelProcessArray(T* data, uint32_t count,
                              std::function<void(uint32_t, T&)> func) {

        parallelFor1DChunked(count, 1024, [&](uint32_t startIdx, uint32_t endIdx) {
            for (uint32_t i = startIdx; i < endIdx; ++i) {
                func(i, data[i]);
            }
        });
    }
    
    // 查找数组的最小最大值
    template<typename T>
    std::pair<T, T> parallelMinMax(const T* data, uint32_t count) {

        if (count == 0) {
            return {T(), T()};
        }

        // 每个线程的局部最小最大值
        std::vector<T> localMins(m_threadCount, std::numeric_limits<T>::max());
        std::vector<T> localMaxs(m_threadCount, std::numeric_limits<T>::lowest());

        // 原子计数器用于任务分发
        std::atomic<uint32_t> nextChunk{0};
        const uint32_t chunkSize = 1024;
        uint32_t numChunks = (count + chunkSize - 1) / chunkSize;

        // 工作函数
        auto worker = [&](uint32_t threadId) {
            while (true) {
                uint32_t chunkIdx = nextChunk.fetch_add(1);
                if (chunkIdx >= numChunks) break;

                uint32_t startIdx = chunkIdx * chunkSize;
                uint32_t endIdx = std::min(startIdx + chunkSize, count);

                T& localMin = localMins[threadId];
                T& localMax = localMaxs[threadId];

                for (uint32_t i = startIdx; i < endIdx; ++i) {
                    T val = data[i];
                    if (val < localMin) localMin = val;
                    if (val > localMax) localMax = val;
                }
            }
        };

        // 启动工作线程
        std::vector<std::thread> workers;
        for (uint32_t t = 0; t < m_threadCount; ++t) {
            workers.emplace_back(worker, t);
        }

        // 主线程也参与工作
        worker(0);

        for (auto& worker : workers) {
            worker.join();
        }

        // 合并结果
        T globalMin = *std::min_element(localMins.begin(), localMins.end());
        T globalMax = *std::max_element(localMaxs.begin(), localMaxs.end());

        return {globalMin, globalMax};
    }
    
    // 归一化数组
    template<typename T>
    void parallelNormalize(T* data, uint32_t count, T minVal, T maxVal) {

        if (count == 0) return;

        T range = maxVal - minVal;

        if (range == T(0)) {
            // 所有值相同，设为中间值
            T middle = (minVal + maxVal) / T(2);
            std::fill(data, data + count, middle);
            return;
        }

        // 并行归一化
        parallelFor1DChunked(count, 1024, [&](uint32_t startIdx, uint32_t endIdx) {
            for (uint32_t i = startIdx; i < endIdx; ++i) {
                data[i] = (data[i] - minVal) / range;
                // 确保在合理范围内
                data[i] = std::clamp(data[i], T(0), T(1));
            }
        });
    }
    
    // 2D并行处理
    void parallelFor2D(uint32_t width, uint32_t height,
                      std::function<void(uint32_t, uint32_t)> func);
    
    // 分块2D并行处理
    void parallelFor2DChunked(uint32_t width, uint32_t height, uint32_t chunkSize,
                             std::function<void(uint32_t, uint32_t, uint32_t, uint32_t)> func);
    
    // 并行处理高度图
    void processHeightMapParallel(const HeightMap& heightmap, uint32_t width, uint32_t height,
                                 std::function<void(uint32_t, uint32_t, float)> func);
    
    // 并行生成（支持线程安全的结果收集）
    template<typename T>
    std::vector<T> parallelGenerate(uint32_t count, std::function<T(uint32_t)> generator);
    
    uint32_t getThreadCount() const { return m_threadCount; }
    
private:
    uint32_t m_threadCount;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{false};
    
    // 任务队列
    struct Task {
        std::function<void()> func;
        bool valid = false;
    };
    
    std::vector<Task> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    
    void workerThread(uint32_t threadId);
};

} // namespace internal
} // namespace MapGenerator

#endif // MAPGENERATOR_INTERNAL_PARALLELUTILS_H
