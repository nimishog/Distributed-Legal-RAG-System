#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

using namespace std;

class ThreadPool {
private:
    vector<thread> workers;
    queue<function<void()>> tasks;

    // Synchronization primitives
    mutex queueMutex;
    condition_variable condition;
    bool stop;

public:
    // Constructor spins up the requested number of threads
    inline ThreadPool(size_t numThreads) : stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(this->queueMutex);
                        
                        // Wait until there is a task or the pool is stopped
                        this->condition.wait(lock, [this] { 
                            return this->stop || !this->tasks.empty(); 
                        });
                        
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }
                        
                        task = move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task(); // Execute the connection handler
                }
            });
        }
    }

    inline ~ThreadPool() {
        {
            unique_lock<mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all(); // Wake up all threads to exit
        
        for (thread &worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // Adds a new client task to the queue
    inline void enqueue(function<void()> task) {
        {
            unique_lock<mutex> lock(queueMutex);
            tasks.push(move(task));
        }
        condition.notify_one(); // Wake up one thread to handle it
    }
};

#endif