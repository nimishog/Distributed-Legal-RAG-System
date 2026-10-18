#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    size_t max_queue_size_;

    // Synchronization primitives
    mutable std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop;

public:
    // Constructor spins up the requested number of threads
    explicit ThreadPool(size_t numThreads, size_t maxQueueSize = 1000) 
        : max_queue_size_(maxQueueSize), stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        
                        // Wait until there is a task or the pool is stopped
                        this->condition.wait(lock, [this] { 
                            return this->stop || !this->tasks.empty(); 
                        });
                        
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }
                        
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task(); // Execute the connection handler
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
            condition.notify_all(); // Wake up all threads to exit
        }
        
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // Adds a new client task to the queue, returns false if queue is full
    bool enqueue(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (tasks.size() >= max_queue_size_) {
                return false;  // Queue full, reject request
            }
            tasks.push(std::move(task));
        }
        condition.notify_one(); // Wake up one thread to handle it
        return true;
    }

    size_t queue_size() const {
        std::lock_guard<std::mutex> lock(queueMutex);
        return tasks.size();
    }
};

#endif