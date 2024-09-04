#include "scheduler.h"

#include <cassert>
#include <functional>
#include <mutex>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <memory>
#include <iostream>
#include "coroutine.h"

namespace {
thread_local Scheduler* scheduler = nullptr;       // 当前线程的调度器
thread_local Coroutine* sched_coroutine = nullptr; // 当前线程中被调度协程
} // namespace

Scheduler* Scheduler::GetScheduler() {
    return scheduler;
}

Coroutine* Scheduler::GetSchedCoroutine() {
    return sched_coroutine;
}

// 如果use_caller为true，那么创建调度器的这个线程内会创建一个专门用来调度的线程（协程）
Scheduler::Scheduler(size_t threads, bool use_caller, const std::string& name)
    : threads_size_(threads)
    , is_use_caller_(use_caller)
    , name_(name) {
    assert(threads > 0);
    if (use_caller) {
        assert(GetScheduler() == nullptr);
        threads_size_--; // 可用调度线程-1
        Coroutine::GetNowCoroutine();
        SetThisAsScheduler();
        sched_co_ = std::make_shared<Coroutine>(std::bind(&Scheduler::Run, this), 0, false); // 初始化调度协程
        sched_coroutine = sched_co_.get();
        sched_id_ = std::this_thread::get_id();
        thread_ids_.push_back(sched_id_);
    }
}

Scheduler::~Scheduler() {
    assert(IsStop() == true);
    if (GetScheduler() == this) {
        scheduler = nullptr;
    }
}

// 调度器开始，线程池创建子线程
// 一但子线程创建就开始从任务队列取任务执行
void Scheduler::Start() {
    std::cout << "Scheduler Start\n";
    std::lock_guard lock(mutex_);
    if (is_stop_) {
        std::cout << "Scheduler has stop...\n";
        return;
    }
    thread_pool_.resize(threads_size_);
    thread_ids_.resize(threads_size_);
    for (int i = 0; i < threads_size_; i++) {
        thread_pool_[i] = std::thread([this] { this->Run(); });
        thread_ids_[i] = thread_pool_[i].get_id();
    }
}

void Scheduler::Stop() {
    if (IsStop()) {
        return;
    }
    is_stop_ = true;

    if (is_use_caller_) {
        assert(GetScheduler() == this);
    } else {
        assert(GetScheduler() != this);
    }

    // 如果use_caller为true时，其调度协程将会在Stop()中才会开始调度任务，而不像其他的调度线程在创建时就开始进行任务的调度
    if (sched_co_) {
        sched_co_->Resume();
    }

    std::vector<std::thread> threads;
    {
        std::lock_guard lock(mutex_);
        threads.swap(thread_pool_);
    }
    for (auto& t : threads) {
        t.join();
    }
}

void Scheduler::SetThisAsScheduler() {
    scheduler = this;
}

void Scheduler::Tick() {
}

void Scheduler::Idle() {
    while (!IsStop()) {
        Coroutine::GetNowCoroutine()->Yield();
    }
}

bool Scheduler::IsStop() {
    std::lock_guard lock(mutex_);
    return is_stop_ && tasks_.empty() && active_threads_ == 0;
}

void Scheduler::Run() {
    std::cout << "Scheduler running...\n";
    SetThisAsScheduler();

    // 如果当前线程不是调度器所在线程，设置调度的协程为当前线程运行的协程
    if (std::this_thread::get_id() != sched_id_) {
        sched_coroutine = Coroutine::GetNowCoroutine().get();
    }

    Coroutine::Ptr idle_co = std::make_shared<Coroutine>([this] { this->Idle(); });
    Coroutine::Ptr callback_co;

    SchedulerTask task;

    while (true) {
        task.Reset();
        // bool tick_other = false;
        {
            std::lock_guard lock(mutex_);
            auto iter = tasks_.begin();
            while (iter != tasks_.end()) {
                // 当前遍历的task已经分配了线程去执行且这个线程不是当前线程，则不用管
                if (iter->thread_id_ && *iter->thread_id_ != std::this_thread::get_id()) {
                    ++iter;
                    continue;
                }
                if (iter->coroutine_ && iter->coroutine_->GetState() != Coroutine::READY) {
                    std::cout << "Coroutine task's state should be READY!\n";
                    assert(false);
                }
                task = *iter;
                tasks_.erase(iter++);
                active_threads_++;
                break;
            }
            // tick_other |= iter != tasks_.end();
        }
        // if (tick_other) {
        //     Tick();
        // }

        if (task.coroutine_) {
            // 任务类型为协程
            task.coroutine_->Resume();
            active_threads_--;
        } else if (task.callback_) {
            // 任务类型为回调函数
            if (callback_co) {
                callback_co->Reset(task.callback_);
            } else {
                callback_co = std::make_shared<Coroutine>(task.callback_);
            }
            callback_co->Resume();
            active_threads_--;
        } else {
            // 无任务，任务队列为空
            if (idle_co->GetState() == Coroutine::FINISH) {
                std::cout << "Idle coroutine finish\n";
                break;
            }
            idle_threads_++;
            idle_co->Resume();
            idle_threads_--;
        }
    }
    std::cout << "Scheduler Run() exit\n";
}
