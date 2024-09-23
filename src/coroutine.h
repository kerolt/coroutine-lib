#ifndef COROUTINE_H_
#define COROUTINE_H_

#include <sys/ucontext.h>
#include <ucontext.h>
#include <cstdint>
#include <functional>
#include <memory>

class Coroutine : public std::enable_shared_from_this<Coroutine> {
public:
    using Ptr = std::shared_ptr<Coroutine>;

    enum State {
        READY,
        RUNNING,
        FINISH
    };

    Coroutine(std::function<void()> callback, size_t stack_size = 0, bool run_in_scheduler = true);
    ~Coroutine();

    void Yield();

    void Resume();

    void Reset(std::function<void()> callback);

    uint64_t GetId() const {
        return id_;
    }

    State GetState() const {
        return state_;
    }

public:
    static void SetNowCoroutine(Coroutine* co);

    static Coroutine::Ptr GetNowCoroutine();

    static uint64_t TotalCoNums();

    static void Task();

    static uint64_t GetCurrentId();

private:
    Coroutine();

private:
    uint64_t id_;
    uint32_t stack_size_;
    State state_ = READY; // 默认初始化时就是READY
    bool is_run_in_sched_;

    ucontext_t ctx_;         // 协程上下文
    void* pstack_ = nullptr; // 协程的栈地址

    std::function<void()> callback_;
};

#endif /* COROUTINE_H_ */
