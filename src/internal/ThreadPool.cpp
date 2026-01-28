#include "ThreadPool.h"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace MapGenerator {
namespace internal {

class ThreadPool::Impl {
public:
    Impl(uint32_t threadCount) : m_stop(false) {
        threadCount = std::max(1u, std::min(threadCount, 
            std::thread::hardware_concurrency()));
        
        for (uint32_t i = 0; i < threadCount; ++i) {
            m_workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_condition.wait(lock, [this] {
                            return m_stop || !m_tasks.empty();
                        });
                        
                        if (m_stop && m_tasks.empty()) {
                            return;
                        }
                        
                        task = std::move(m_tasks.front());
                        m_tasks.pop();
                    }
                    
                    task();
                }
            });
        }
    }
    
    ~Impl() {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        
        m_condition.notify_all();
        
        for (auto& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    void enqueue(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_tasks.emplace(std::move(task));
        }
        m_condition.notify_one();
    }
    
    uint32_t getThreadCount() const {
        return static_cast<uint32_t>(m_workers.size());
    }
    
private:
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_stop;
};

ThreadPool::ThreadPool(uint32_t threadCount) 
    : m_impl(std::make_unique<Impl>(threadCount)) {
}

ThreadPool::~ThreadPool() = default;

void ThreadPool::enqueue(std::function<void()> task) {
    m_impl->enqueue(std::move(task));
}

uint32_t ThreadPool::getThreadCount() const {
    return m_impl->getThreadCount();
}

} // namespace internal
} // namespace MapGenerator