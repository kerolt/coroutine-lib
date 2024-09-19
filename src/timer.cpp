#include "timer.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <shared_mutex>

// 获取系统自启动来经过的时间
static uint64_t GetElapsedMS() {
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

Timer::Timer(uint64_t ms, std::function<void()> cb, bool recur, TimerManager* manager)
    : exec_cycle_(ms)
    , callback_(cb)
    , is_recur_(recur)
    , manager_(manager) {
    next_ = GetElapsedMS() + exec_cycle_;
}

Timer::Timer(uint64_t next)
    : next_(next) {
}

// 取消定时器，从timer manager的时间堆中移除该定时器，并将回调函数置为空
// 所以用std::set可能也是避免手写堆的麻烦
bool Timer::Cancel() {
    std::shared_lock lock(manager_->rw_mutex_);
    if (callback_) {
        callback_ = nullptr;
        auto iter = manager_->timer_heap_.find(shared_from_this());
        manager_->timer_heap_.erase(iter);
        return true;
    }
    return false;
}

// 刷新定时器，将定时器从管理器中移除后重新设置到期时间并再次插入time manager中
bool Timer::Refresh() {
    std::shared_lock lock(manager_->rw_mutex_);

    if (!callback_) {
        return false;
    }

    auto iter = manager_->timer_heap_.find(shared_from_this());
    if (iter == manager_->timer_heap_.end()) {
        return false;
    }
    manager_->timer_heap_.erase(iter);
    next_ = GetElapsedMS() + exec_cycle_;
    manager_->timer_heap_.insert(shared_from_this());

    return false;
}

// 重置定时器的时间间隔和到期时间
bool Timer::Reset(uint64_t ms, bool from_now) {
    // 时间间隔与之前一样且不从现在重新开始，则说明不需要reset操作
    if (ms == exec_cycle_ || !from_now) {
        return true;
    }

    std::shared_lock lock(manager_->rw_mutex_);

    if (!callback_) {
        return false;
    }

    auto iter = manager_->timer_heap_.find(shared_from_this());
    if (iter == manager_->timer_heap_.end()) {
        return false;
    }
    manager_->timer_heap_.erase(iter);

    uint64_t start = from_now ? GetElapsedMS() : next_ - exec_cycle_;
    exec_cycle_ = ms;
    next_ = start + exec_cycle_;
    // TODO 为什么这里要使用AddTimer来插入，且其参数的含义是什么
    manager_->AddTimer(shared_from_this(), lock);

    return true;
}

TimerManager::TimerManager() {
    pre_exec_time_ = GetElapsedMS();
}

// virtual析构，由IOManager继承时实现资源的释放处理
TimerManager::~TimerManager() {
}

Timer::Ptr TimerManager::AddTimer(uint64_t ms, std::function<void()> cb, bool is_recur) {
    std::shared_ptr<Timer> timer(new Timer(ms, cb, is_recur, this));
    // auto timer = std::make_shared<Timer>(ms, cb, is_recur, this);
    std::shared_lock lock(rw_mutex_);
    AddTimer(timer, lock);
    return timer;
}

void TimerManager::AddTimer(Timer::Ptr timer, std::shared_lock<std::shared_mutex>& lock) {
    auto [iter, _] = timer_heap_.insert(timer);
    bool is_at_front = iter == timer_heap_.begin() && !is_tickled_;
    if (is_at_front) {
        is_tickled_ = true;
    }
    lock.unlock();
    if (is_at_front) {
        OnTimerInsertAtFront();
    }
}

static void OnTimer(std::weak_ptr<void> cond, std::function<void()> cb) {
    std::shared_ptr<void> tmp = cond.lock();
    if (tmp) {
        cb();
    }
}

Timer::Ptr TimerManager::AddConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> cond, bool is_recur) {
    return AddTimer(ms, std::bind(&OnTimer, cond, cb), is_recur);
}

uint64_t TimerManager::GetNextTimerInterval() {
    std::shared_lock lock(rw_mutex_);
    is_tickled_ = false; // TODO 为什么？
    if (timer_heap_.empty()) {
        return ~0ull;
    }
    auto& next_time = *timer_heap_.begin();
    uint64_t now_time = GetElapsedMS();
    // 如果当前时间 now_ms 已经大于等于下一个定时器的触发时间 next->next_，则返回 0，表示定时器已经触发。否则，返回定时器触发时间与当前时间的差值，表示还有多少毫秒触发定时器
    return now_time >= next_time->next_ ? 0 : next_time->next_ - now_time;
}

std::vector<std::function<void()>> TimerManager::GetExpiredCbList() {
    uint64_t now_ms = GetElapsedMS();
    std::shared_lock lock(rw_mutex_);
    if (timer_heap_.empty()) {
        return {};
    }

    bool rollover = DetectClockRollover(now_ms);
    if (!rollover && ((*timer_heap_.begin())->next_ > now_ms)) {
        // 时钟没有回绕且现在还没有触发定时器中最早的定时器，说明没有过时的定时器
        return {};
    }

    // 将过时的定时器移入expired
    std::vector<Timer::Ptr> expired;
    std::shared_ptr<Timer> now_timer(new Timer(now_ms));
    // auto now_timer = std::make_shared<Timer>(now_ms);
    auto iter = rollover ? timer_heap_.end() : timer_heap_.lower_bound(now_timer);
    while (iter != timer_heap_.end() && (*iter)->next_ <= now_ms) {
        iter++;
    }
    expired.insert(expired.begin(), timer_heap_.begin(), iter);
    timer_heap_.erase(timer_heap_.begin(), iter);

    std::vector<std::function<void()>> cbs;
    for (auto& timer : expired) {
        cbs.push_back(timer->callback_);
        if (timer->is_recur_) {
            // 对于这些过期的定时器，如果是循环定时器，那么需要重新计算这个定时器的下一次触发时间
            // 也就是当前时间加上这个定时器的执行周期，并将其重新加入时间堆
            timer->next_ = now_ms + timer->exec_cycle_;
            timer_heap_.insert(timer);
        } else {
            timer->callback_ = nullptr;
        }
    }
    return cbs;
}

bool TimerManager::HasTimer() {
    std::shared_lock lock(rw_mutex_);
    return timer_heap_.empty();
}

bool TimerManager::DetectClockRollover(uint64_t now_ms) {
    bool rollover = false;
    if (now_ms < pre_exec_time_ && now_ms < (pre_exec_time_ - 60 * 60 * 1000)) {
        rollover = true;
    }
    pre_exec_time_ = now_ms;
    return rollover;
}
