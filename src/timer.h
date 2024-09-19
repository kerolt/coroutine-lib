#ifndef TIMER_H_
#define TIMER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <shared_mutex>
#include <vector>

class TimerManager;

class Timer : public std::enable_shared_from_this<Timer> {
    friend class TimerManager;

public:
    using Ptr = std::shared_ptr<Timer>;

    bool Cancel();
    bool Refresh();
    bool Reset(uint64_t ms, bool from_now);

private:
    Timer(uint64_t ms, std::function<void()> cb, bool recur, TimerManager* manager);
    Timer(uint64_t next);

private:
    bool is_recur_;       // 是否循环定时器
    uint64_t exec_cycle_; // 执行周期
    uint64_t next_;       // 下一次的到期时间
    std::function<void()> callback_;
    TimerManager* manager_;

    struct Comp {
        bool operator()(const Timer::Ptr& lt, const Timer::Ptr& rt) const {
            if (!lt || !rt) {
                return !lt && rt;
            }
            return lt->next_ < rt->next_;
        }
    };
};

class TimerManager {
    friend class Timer;

public:
    TimerManager();
    virtual ~TimerManager();

    // public 添加定时器
    Timer::Ptr AddTimer(uint64_t ms, std::function<void()> cb, bool is_recur = false);

    // 添加条件定时器，如果条件成立则定时器才有效
    Timer::Ptr AddConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> cond, bool is_recur = false);

    // 获取下一个定时器到现在的执行间隔时间
    // 如果没有定时器了，就返回uint64_t的最大值
    uint64_t GetNextTimerInterval();

    // 获取需要执行的定时器的回调函数列表
    std::vector<std::function<void()>> GetExpiredCbList();

    // 是否还有定时器
    bool HasTimer();

protected:
    virtual void OnTimerInsertAtFront() = 0;

    void AddTimer(Timer::Ptr timer, std::shared_lock<std::shared_mutex>& lock);

private:
    // 系统时钟是否出现了回绕（rollover）现象，即当前时间比之前记录的时间要小很多
    // 用于检测服务器时间是否被调后了
    bool DetectClockRollover(uint64_t now_ms);

private:
    std::shared_mutex rw_mutex_;
    std::set<Timer::Ptr, Timer::Comp> timer_heap_;
    bool is_tickled_;
    uint64_t pre_exec_time_;
};

#endif /* TIMER_H_ */
