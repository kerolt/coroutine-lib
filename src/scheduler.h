#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include <atomic>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include "coroutine.h"

// 协程调度器
class Scheduler {
private:
    // 调度任务，任务类型可以是协程/函数二选一，并且可指定调度线程
    using ThreadIdPtr = std::shared_ptr<std::thread::id>;

    struct SchedulerTask {
        ThreadIdPtr thread_id_;
        Coroutine::Ptr coroutine_;
        std::function<void()> callback_;

        SchedulerTask() {}

        SchedulerTask(Coroutine::Ptr co, ThreadIdPtr id)
            : coroutine_(co)
            , thread_id_(std::move(id)) {}

        SchedulerTask(std::function<void()> callback, ThreadIdPtr id)
            : callback_(callback)
            , thread_id_(std::move(id)) {}

        void Reset() {
            thread_id_.reset();
            callback_ = nullptr;
            coroutine_ = nullptr;
        }
    };

public:
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "scheduler");

    virtual ~Scheduler();

    std::string GetName() const {
        return name_;
    }

    void Start();

    void Stop();

    template <typename TaskType>
    void Sched(TaskType t, ThreadIdPtr id = nullptr) requires(std::is_function_v<std::remove_reference_t<TaskType>> || std::is_same_v<TaskType, Coroutine::Ptr>) {
        bool is_need_tick = false;

        {
            std::lock_guard lock(mutex_);
            is_need_tick = tasks_.empty();
            SchedulerTask task(t, id);
            if (task.callback_ || task.coroutine_) {
                tasks_.push_back(task);
            }
        }

        if (is_need_tick) {
            Tick();
        }
    }

public:
    static Scheduler* GetScheduler();

    static Coroutine* GetSchedCoroutine();

protected:
    void Tick();

    void Run();

    void SetThisAsScheduler();

    void Idle();

    bool IsStop();

private:
    std::string name_;
    std::mutex mutex_;
    std::vector<std::thread> thread_pool_;
    std::vector<std::thread::id> thread_ids_;
    std::list<SchedulerTask> tasks_;

    size_t threads_size_;
    std::atomic_size_t active_threads_{0};
    std::atomic_size_t idle_threads_{0};

    std::thread::id sched_id_; // use_caller为true时，调度器所在的线程id
    Coroutine::Ptr sched_co_;  // use_caller为true时调度器所在线程的调度协程
    bool is_stop_;
    bool is_use_caller_;
};

#endif /* SCHEDULER_H_ */
