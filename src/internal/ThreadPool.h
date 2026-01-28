#pragma once

#include "MapGenerator.h"

#include <memory>
#include <thread>
#include <functional>
#include <future>

namespace MapGenerator {
namespace internal {

class ThreadPool {
public:
    ThreadPool(uint32_t threadCount);
    ~ThreadPool();

    template<typename F>
    auto enqueueTask(F&& f) -> std::future<decltype(f())> {
        using ReturnType = decltype(f());
        
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::forward<F>(f)
        );
        
        std::future<ReturnType> result = task->get_future();
        
        enqueue([task]() {
            (*task)();
        });
        
        return result;
    }

    void enqueue(std::function<void()> task);
    uint32_t getThreadCount() const;

private:
    class Impl;

    std::unique_ptr<Impl> m_impl;
};

}  // internal
}  // MapGenerator