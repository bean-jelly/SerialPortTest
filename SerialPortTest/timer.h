#pragma once
#include <functional>
#include <algorithm>
#include <chrono>
#include <vector>
#include <set>
#include <memory>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#define XSleep(x) Sleep(x)
#else
#include <unistd.h>
#define XSleep(x) usleep(x*1000)
#endif

namespace timer
{
struct Task;
using TaskPtr = std::shared_ptr<Task>;

struct Task
{
    friend bool operator < (TaskPtr left, TaskPtr right)
    {
        return left->expiredTime < right->expiredTime;
    }

    std::chrono::steady_clock::time_point expiredTime;

    std::chrono::milliseconds interval;

    std::function<void()> callback;

    uint32_t id = 0;

    bool once = false;
};

class Relative
{
public:
    explicit Relative() : _id(1), _stop(false)
    {
        _thread = std::make_shared<std::thread>(std::bind(&Relative::Run, this));
    }

    virtual ~Relative()
    {
        _stop = true;
        if (_thread && _thread->joinable())
        {
            _thread->join();
        }
    }

    uint32_t StartTimer(int interval, bool once, std::function<void()> callback)
    {
        auto task = std::make_shared<Task>();
        task->interval = std::chrono::milliseconds(interval);
        task->expiredTime = std::chrono::steady_clock::now() + task->interval;
        task->callback = callback;
        task->once = once;
        Push(task);
        return task->id;
    }

    void KillTimer(uint32_t id)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto it = _tasks.begin(); it != _tasks.end(); ++it)
        {
            if ((*it)->id == id)
            {
                _tasks.erase(it);
                std::make_heap(_tasks.begin(), _tasks.end(), std::greater<TaskPtr>());
                return;
            }
        }
        _kills.insert(id);
    }

private:
    void Run()
    {
        while (!_stop)
        {
            Scan();
            XSleep(1);
        }
    }

    TaskPtr Pop()
    {
        TaskPtr result = nullptr;
        auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(_mutex);

        auto it = _tasks.begin();
        if (it != _tasks.end() && (*it)->expiredTime <= now)
        {
            result = *it;

            std::pop_heap(_tasks.begin(), _tasks.end(), std::greater<TaskPtr>());
            _tasks.pop_back();
        }
        return result;
    }

    void Push(TaskPtr task)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _tasks.push_back(task);

        std::push_heap(_tasks.begin(), _tasks.end(), std::greater<TaskPtr>());
    }

    void Scan()
    {
        auto task = Pop();
        if (!task)
        {
            return;
        }
        DoTask(task);
    }

    void DoTask(TaskPtr task)
    {
        task->callback();
        Recovery(task);
    }

    void Recovery(TaskPtr task)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        auto it = _kills.find(task->id);
        if (it != _kills.end())
        {
            _kills.erase(it);
            return;
        }

        if (task->once)
        {
            return;
        }

        // 重置任务时间
        task->expiredTime = std::chrono::steady_clock::now() + task->interval;

        // 放入队列
        _tasks.push_back(task);
        std::push_heap(_tasks.begin(), _tasks.end(), std::greater<TaskPtr>());
    }

    // 生成定时器ID
    uint32_t GetTaskID()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return ++_id;
    }

private:
    std::vector<TaskPtr> _tasks; // 定时器列表
    std::set<uint32_t> _kills;   // 当任务在执行中，已杀死任务列表
    std::mutex _mutex;
    uint32_t _id;

    std::shared_ptr<std::thread> _thread;
    bool _stop;
};
}
