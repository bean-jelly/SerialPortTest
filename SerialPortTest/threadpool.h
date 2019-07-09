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

    // �ύһ������
    template<class F, class... Args>
    auto Commit(F&& f, Args&&... args) -> std::future<decltype(f(args...))>
    {
        using ResType = decltype(f(args...));// ����f�ķ���ֵ����

        auto task = std::make_shared<std::packaged_task<ResType()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        {
            // ������񵽶���
            std::lock_guard<std::mutex> lock(_taskMutex);

            _tasks.emplace([task]()
            {
                (*task)();
            });

            LOG_DEBUG << "_tasks size:" << _tasks.size();
        }

        _taskCV.notify_all(); //�����߳�ִ��

        std::future<ResType> future = task->get_future();
        return future;
    }

private:
    // ��ȡһ����ִ�е�task
    Task GetTask()
    {
        std::unique_lock<std::mutex> lock(_taskMutex);

        _taskCV.wait(lock, [this]() { return !_tasks.empty() || _stop.load(); }); // waitֱ����task

        if (_stop.load())
        {
            return nullptr;
        }

        Task task{ std::move(_tasks.front()) }; // ȡһ��task
        _tasks.pop();
        return task;
    }

    // �������
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

    // �ر��̳߳أ����ȴ�����
    void Shutdown()
    {
        this->_stop.store(true);
        _taskCV.notify_all();

        for (std::thread &thrd : _pool)
        {
            thrd.join();// �ȴ���������� ǰ�᣺�߳�һ����ִ����
        }
    }

private:
    // �̳߳�
    std::vector<std::thread> _pool;

    // �������
    std::queue<Task> _tasks;

    // ͬ��
    std::mutex _taskMutex;
    std::condition_variable _taskCV;

    // �Ƿ�ر��ύ
    std::atomic<bool> _stop;
};

#endif
