#include "coroutine.h"
#include <cassert>
#include <memory>
#include <ucontext.h>
#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <iostream>
#include "scheduler.h"

static thread_local Coroutine* cur_coroutine = nullptr;
static thread_local Coroutine::Ptr main_coroutine = nullptr;
static std::atomic<uint64_t> co_count = 0;   // 协程数量
static std::atomic<uint64_t> co_id = 0;      // 用于生成协程id
static const int CO_STACK_SIZE = 128 * 1024; // 协程的栈大小

// 用于创建主协程的构造函数
Coroutine::Coroutine() {
    SetNowCoroutine(this);
    state_ = RUNNING;
    if (getcontext(&ctx_) != 0) {
        std::cout << "err: Coroutine::getcontext\n";
        exit(1);
    }
    ++co_count;
    id_ = co_id++;
}

Coroutine::Coroutine(std::function<void()> callback, size_t stack_size, bool run_in_scheduler)
    : callback_(callback)
    , is_run_in_sched_(run_in_scheduler) {
    co_count++;
    stack_size_ = stack_size > 0 ? stack_size : CO_STACK_SIZE;
    pstack_ = malloc(stack_size_);

    // getcontext()用于保存当前上下文，以便将来可以从这个点恢复执行
    if (getcontext(&ctx_) != 0) {
        std::cout << "err: Coroutine::getcontext\n";
        exit(1);
    }
    // 初始化协程上下文
    ctx_.uc_link = nullptr;
    ctx_.uc_stack.ss_sp = pstack_;
    ctx_.uc_stack.ss_size = stack_size_;
    // 为已经初始化的上下文设置一个将在该上下文被激活时执行的函数，并为该函数传递参数。
    makecontext(&ctx_, &Coroutine::Task, 0);
}

Coroutine::~Coroutine() {
    co_count--;
    if (pstack_) {
        // 协程
        assert(state_ == FINISH);
        free(pstack_);
    } else {
        // 主线程（or 主协程）
        assert(!callback_);
        assert(state_ == RUNNING);
        if (cur_coroutine == this) {
            SetNowCoroutine(nullptr);
        }
    }
}

// 让出该协程的执行权，转交到主协程
void Coroutine::Yield() {
    assert(state_ == FINISH || state_ == RUNNING);
    SetNowCoroutine(main_coroutine.get());
    if (state_ != FINISH) {
        state_ = READY;
    }
    if (is_run_in_sched_) {
        if (swapcontext(&ctx_, &(Scheduler::GetSchedCoroutine()->ctx_)) != 0) {
            std::cout << "err: Yield::swapcontext\n";
            assert(false);
        }
    } else {
        if (swapcontext(&ctx_, &(main_coroutine->ctx_)) < 0) {
            std::cout << "err: Yield::swapcontext\n";
            exit(1);
        }
    }
}

// 从当前运行的协程恢复到该协程
void Coroutine::Resume() {
    assert(state_ != FINISH && state_ != RUNNING);
    // 每次恢复时需要将当前运行的协程设置为自身
    SetNowCoroutine(this);
    state_ = RUNNING;
    if (is_run_in_sched_) {
        if (swapcontext(&(Scheduler::GetSchedCoroutine()->ctx_), &ctx_) != 0) {
            std::cout << "err: Resume::swapcontext\n";
            assert(false);
        }
    } else {
        if (swapcontext(&(main_coroutine->ctx_), &ctx_) != 0) {
            std::cout << "err: Resume::swapcontext\n";
            exit(1);
        }
    }
}

// 重置协程
void Coroutine::Reset(std::function<void()> callback) {
    assert(pstack_);
    // assert(state_ == FINISH);
    if (getcontext(&ctx_) != 0) {
        std::cout << "err: Reset::getcontext\n";
        exit(1);
    }

    callback_ = callback;
    ctx_.uc_link = nullptr;
    ctx_.uc_stack.ss_sp = pstack_;
    ctx_.uc_stack.ss_size = stack_size_;

    makecontext(&ctx_, &Coroutine::Task, 0);
    state_ = READY;
}

// 设置当前运行的协程
void Coroutine::SetNowCoroutine(Coroutine* co) {
    cur_coroutine = co;
}

Coroutine::Ptr Coroutine::GetNowCoroutine() {
    if (cur_coroutine) {
        return cur_coroutine->shared_from_this();
    }
    // auto main_co = std::make_shared<Coroutine>();
    std::shared_ptr<Coroutine> main_co(new Coroutine());
    main_coroutine = main_co;
    return cur_coroutine->shared_from_this();
}

uint64_t Coroutine::TotalCoNums() {
    return co_count;
}

// 每个协程会运行它所绑定的callback，并且在执行完成后将重置该协程的状态，并让出执行权
void Coroutine::Task() {
    auto cur = GetNowCoroutine();
    assert(cur);

    cur->callback_();
    cur->callback_ = nullptr;
    cur->state_ = FINISH;

    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->Yield();
}

uint64_t Coroutine::GetCurrentId() {
    return cur_coroutine ? cur_coroutine->GetId() : -1;
}
