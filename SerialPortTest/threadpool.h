#pragma once
#include <functional>
#include <thread>
#include <mlog/mlog.hpp>

#ifdef DISABLE_C11_THREAD

class ThreadPool
{
public:
    ThreadPool(size_t size = 4) {};

    template<class F, class... Args>
    void Commit(F&& f, Args&&... args)
    {
        auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        std::thread th(task);
        th.detach();
    }
};

#else

#include <condition_variable>
#include <future>
#include <atomic>
#include <queue>
#include <vector>

class ThreadPool
{
    using Task = std::function<void()>;

public:
    ThreadPool(size_t size = 4)
        : _stop(false)
    {
        size = size < 1 ? 1 : size;

        for (size_t i = 0; i < size; ++i)
        {
            _pool.emplace_back(&ThreadPool::Schedual, this);
        }
    }

    ~ThreadPool()
    {
        Shutdown();
    }

    // 提交一个任务
    template<class F, class... Args>
    auto Commit(F&& f, Args&&... args) -> std::future<decltype(f(args...))>
    {
        using ResType = decltype(f(args...));// 函数f的返回值类型

        auto task = std::make_shared<std::packaged_task<ResType()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        {
            // 添加任务到队列
            std::lock_guard<std::mutex> lock(_taskMutex);

            _tasks.emplace([task]()
            {
                (*task)();
            });

            LOG_DEBUG << "_tasks size:" << _tasks.size();
        }

        _taskCV.notify_all(); //唤醒线程执行

        std::future<ResType> future = task->get_future();
        return future;
    }

private:
    // 获取一个待执行的task
    Task GetTask()
    {
        std::unique_lock<std::mutex> lock(_taskMutex);

        _taskCV.wait(lock, [this]() { return !_tasks.empty() || _stop.load(); }); // wait直到有task

        if (_stop.load())
        {
            return nullptr;
        }

        Task task{ std::move(_tasks.front()) }; // 取一个task
        _tasks.pop();
        return task;
    }

    // 任务调度
    void Schedual()
    {
        while (!_stop.load())
        {
            if (Task task = GetTask())
            {
                task();
            }
        }
    }

    // 关闭线程池，并等待结束
    void Shutdown()
    {
        this->_stop.store(true);
        _taskCV.notify_all();

        for (std::thread &thrd : _pool)
        {
            thrd.join();// 等待任务结束， 前提：线程一定会执行完
        }
    }

private:
    // 线程池
    std::vector<std::thread> _pool;

    // 任务队列
    std::queue<Task> _tasks;

    // 同步
    std::mutex _taskMutex;
    std::condition_variable _taskCV;

    // 是否关闭提交
    std::atomic<bool> _stop;
};

#endif
