#ifndef IOMANAGER_H_
#define IOMANAGER_H_

#include <cstddef>
#include <sys/epoll.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "coroutine.h"
#include "scheduler.h"

// 事件：无、读、写
enum Event {
    NONE = 0x0,
    READ = EPOLLIN,
    WRITE = EPOLLOUT,
};

// socket fd 上下文
struct FdContext {
    struct EventContext {
        Scheduler* scheduler = nullptr;
        Coroutine::Ptr coroutine;
        std::function<void()> callback;
    };

    // 根据类型获取对应的上下文
    EventContext& GetEventContext(Event& e);

    void ResetEventContext(EventContext& ectx);

    void TriggerEvent(Event e);

    EventContext read_ctx, write_ctx;
    int fd;
    Event events = Event::NONE;
    std::mutex mutex;
};

// IO协程调度
class IOManager : public Scheduler {
public:
    IOManager(size_t threads = 1, bool use_caller = true, const std::string& name = "IOManager");

    ~IOManager();

    bool AddEvent(int fd, Event event, std::function<void()> cb = nullptr);

    bool DelEvent(int fd, Event event);

    bool CancelEvent(int fd, Event event);

    bool CancelAllEvent(int fd);

    static IOManager* GetIOManager();

protected:
    void Idle() override;

    void Tickle() override;

    bool IsStop() override;

    void ResizeContexts(size_t size);

private:
    int epfd_;
    int tickle_fd_[2];
    std::atomic_size_t pending_evt_cnt_;
    std::mutex mutex_;
    std::shared_mutex rw_mutex_;
    // 利用fd作为下标来获取对应的FdContext*，也可以使用哈希表代替
    std::vector<FdContext*> fd_contexts_;
};

#endif /* IOMANAGER_H_ */
