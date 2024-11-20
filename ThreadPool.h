#pragma once

#include <atomic>
#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>

class ThreadPool {
    std::vector<std::thread> threads;
    std::queue<std::function<void()> > tasks;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> stopped;

    void run() {
        for (;;) {
            std::unique_lock lock(m);
            cv.wait(lock, [this] { return stopped || !tasks.empty(); });
            if (stopped && tasks.empty()) {
                return;
            }
            auto task = std::move(tasks.front());
            tasks.pop();
            lock.unlock();
            task();
        }
    }

public:
    explicit ThreadPool(const uint32_t n) : stopped(false) {
        threads.reserve(n);
        for (uint32_t i = 0; i < n; i++) {
            threads.emplace_back(&ThreadPool::run, this);
        }
    }

    ~ThreadPool() {
        stopAll();
    }

    template<typename F, typename... Args>
    auto addTask(F &&f, Args &&... args) {
        using RetType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<RetType()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        auto res = task->get_future(); {
            std::lock_guard lock(m);
            tasks.emplace([task]() { (*task)(); });
        }
        cv.notify_one();
        return res;
    }

    void waitAll() {
        while (!tasks.empty()) {
            std::this_thread::yield();
        }
    }

    void stopAll() {
        stopped = true;
        cv.notify_all();
        for (auto &t: threads) {
            t.join();
        }
    }
};
