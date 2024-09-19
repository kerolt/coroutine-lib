#include "iomanager.h"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

#include <memory>
#include <mutex>
#include <algorithm>
#include <shared_mutex>

#include "coroutine.h"
#include "scheduler.h"
#include "util.h"

FdContext::EventContext& FdContext::GetEventContext(Event& e) {
    switch (e) {
        case Event::READ:
            return read_ctx;
        case Event::WRITE:
            return write_ctx;
        default:
            LOG_ERROR << "error event\n";
            assert(false);
    }
}

void FdContext::ResetEventContext(EventContext& ectx) {
    ectx.scheduler = nullptr;
    ectx.callback = nullptr;
    ectx.coroutine.reset();
}

// 根据读写事件来添加FdContext对应的任务
void FdContext::TriggerEvent(Event e) {
    assert(events & e);
    // 清除事件e
    events = (Event) (events & ~e);
    auto& ctx = GetEventContext(e);
    if (ctx.callback) {
        ctx.scheduler->Sched(ctx.callback);
    } else {
        ctx.scheduler->Sched(ctx.coroutine);
    }
    // 添加完任务后就reset
    ResetEventContext(ctx);
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string& name)
    : Scheduler(threads, use_caller, name) {
    epfd_ = epoll_create(1);
    if (pipe(tickle_fd_) < 0) {
        perror("pipe");
        exit(1);
    }

    epoll_event event = {0};
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = tickle_fd_[0];

    if (fcntl(tickle_fd_[0], F_SETFL, O_NONBLOCK) < 0) {
        perror("fntl");
        exit(1);
    }

    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, tickle_fd_[0], &event) < 0) {
        perror("epoll_ctl");
        exit(1);
    }

    ResizeContexts(32);

    Scheduler::Start(); // IOManager一创建即开始运行
}

IOManager::~IOManager() {
    Scheduler::Stop();

    close(tickle_fd_[0]);
    close(tickle_fd_[1]);
    close(epfd_);

    for (int i = 0; i < fd_contexts_.size(); i++) {
        if (fd_contexts_[i]) {
            delete fd_contexts_[i];
        }
    }
}

IOManager* IOManager::GetIOManager() {
    return static_cast<IOManager*>(Scheduler::GetScheduler());
}

// Idle协程，不断等待事件、处理事件、然后再次等待事件的循环过程
// 它在没有其他协程运行时保持系统的活跃度，并在有事件发生时进行相应的处理
void IOManager::Idle() {
    LOG << "idle coroutine start up\n";

    const int MAX_EVENTS = 256;
    const int MAX_TIMEOUT = 5000;
    epoll_event events[MAX_EVENTS]{};

    while (true) {
        // LOG << "in idle now\n";
        if (IsStop()) {
            LOG << GetName() << "idle stop now\n";
            break;
        }

        uint64_t next_timeout = GetNextTimerInterval();
        int triggered_events;
        do {
            // 如果时间堆中有超时的定时器，则比较这个超时定时器的下一次触发的时间与MAX_TIMEOUT（5s），选取最小值作为超时时间
            next_timeout = next_timeout != ~0ull ? std::min(static_cast<int>(next_timeout), MAX_TIMEOUT) : MAX_TIMEOUT;

            // 没有事件到来时会阻塞在epoll_wait上，除非到了超时时间
            triggered_events = epoll_wait(epfd_, events, MAX_EVENTS, static_cast<int>(next_timeout));
            if (triggered_events < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        } while (true); // 用while(true)的目的是确保在出现特定错误情况时能够重新尝试执行 epoll_wait

        // 将超时的定时器的回调函数加入调度器
        // 这些回调函数的作用可能是关闭连接等操作
        std::vector<std::function<void()>> cbs = GetExpiredCbList();
        for (auto& cb : cbs) {
            Sched(cb);
        }

        // 处理事件
        for (int i = 0; i < triggered_events; i++) {
            epoll_event& event = events[i];
            // 是一个用于通知协程调度的事件
            // epoll中监听了用于通知的管道读端fd，当有数据到时即会触发
            if (event.data.fd == tickle_fd_[0]) {
                char buf[256]{};
                // 将管道内的数据读完
                while (read(tickle_fd_[0], buf, sizeof(buf)) > 0)
                    ;
                continue;
            }

            // FdContext* fd_ctx = (FdContext*) event.data.ptr;
            FdContext* fd_ctx = static_cast<FdContext*>(event.data.ptr);
            std::lock_guard lock(fd_ctx->mutex);

            // 发生错误时，如果原来的文件描述符上下文（fd_ctx）中有可读或可写事件标志被设置，那么现在将重新触发这些事件
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }

            // 获取fd_ctx对应的事件
            int real_event = Event::NONE;
            if (event.events & EPOLLIN) {
                real_event |= Event::READ;
            }
            if (event.events & EPOLLOUT) {
                real_event |= Event::WRITE;
            }

            if ((fd_ctx->events & real_event) == Event::NONE) {
                continue;
            }

            // 如果还有剩余事件，则修改；否则将其从epoll中删除
            // 注意获取rest_events时不是使用的event.events & ~real_event，因为是要去除fd_ctx->fd中本次触发的事件
            int rest_events = fd_ctx->events & ~real_event;
            int op = rest_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | rest_events;
            if (epoll_ctl(epfd_, op, fd_ctx->fd, &event) < 0) {
                LOG_ERROR << strerror(errno) << "\n";
                continue;
            }

            if (real_event & Event::READ) {
                fd_ctx->TriggerEvent(Event::READ);
                --pending_evt_cnt_;
            }
            if (real_event & Event::WRITE) {
                fd_ctx->TriggerEvent(Event::WRITE);
                --pending_evt_cnt_;
            }
        }

        // 将当前线程运行的协程暂停（也就是暂停Idle协程），并将执行权交给调度协程
        Coroutine::Ptr co_ptr = Coroutine::GetNowCoroutine();
        auto co = co_ptr.get();
        co_ptr.reset();
        co->Yield();
    }
}

void IOManager::Tickle() {
    if (!HasIdleThreads()) {
        return;
    }
    write(tickle_fd_[1], "T", 1);
}

bool IOManager::IsStop() {
    // 应该保证所有调度事件都完成了，并且没有剩余的定时器待触发了才能退出
    return GetNextTimerInterval() == ~0ull && pending_evt_cnt_ == 0 && Scheduler::IsStop();
}

void IOManager::ResizeContexts(size_t size) {
    fd_contexts_.resize(size);
    for (int i = 0; i < fd_contexts_.size(); i++) {
        if (!fd_contexts_[i]) {
            fd_contexts_[i] = new FdContext();
            fd_contexts_[i]->fd = i;
        }
    }
}

bool IOManager::AddEvent(int fd, Event event, std::function<void()> cb) {
    FdContext* fd_ctx = nullptr;
    {
        std::shared_lock rw_lock(rw_mutex_);
        if (fd_contexts_.size() > fd) {
            fd_ctx = fd_contexts_[fd];
            rw_lock.unlock();
        } else {
            rw_lock.unlock();
            std::unique_lock rw_lock2(rw_mutex_);
            ResizeContexts(fd * 1.5);
            fd_ctx = fd_contexts_[fd];
        }
    }

    std::lock_guard lock(mutex_);
    if (fd_ctx->events & event) {
        LOG_ERROR << "A fd can't add same event\n";
        return false;
    }

    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event ep_evt{};
    ep_evt.events = static_cast<int>(fd_ctx->events) | EPOLLET | event;
    ep_evt.data.ptr = fd_ctx; // 在Idle()中将使用fd对应的这个ep_evt

    int ret = epoll_ctl(epfd_, op, fd, &ep_evt);
    if (ret) {
        LOG_ERROR << "epoll_ctl " << strerror(errno);
        return false;
    }

    ++pending_evt_cnt_;

    // 设置fd对应事件的EventContext
    fd_ctx->events = static_cast<Event>(fd_ctx->events | event);
    // 使用event_ctx相当于使用fd_ctx->read_ctx or fd_ctx->write_ctx（注意是auto&而不是auto）
    auto& event_ctx = fd_ctx->GetEventContext(event);
    assert(!event_ctx.scheduler && !event_ctx.callback && !event_ctx.coroutine);
    event_ctx.scheduler = Scheduler::GetScheduler();
    if (cb) {
        event_ctx.callback = cb;
    } else {
        // 设置fd相关事件触发时使用的协程为当前
        event_ctx.coroutine = Coroutine::GetNowCoroutine();
        assert(event_ctx.coroutine->GetState() == Coroutine::RUNNING);
    }
    return true;
}

// 删除指定文件描述符的特定事件监听
bool IOManager::DelEvent(int fd, Event event) {
    // 找到fd对应的FdContext
    FdContext* fd_ctx = nullptr;
    {
        std::shared_lock lock(rw_mutex_);
        if (fd_contexts_.size() <= fd) {
            return false;
        }
        fd_ctx = fd_contexts_[fd];
    }

    std::lock_guard lock2(fd_ctx->mutex);
    if (fd_ctx->events & event) {
        LOG_ERROR << "A fd can't delete existing event\n";
        return false;
    }

    // 删除指定的事件
    Event new_events = static_cast<Event>(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | static_cast<int>(new_events);
    epevent.data.ptr = fd_ctx;

    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        LOG_ERROR << "epoll_ctl " << strerror(errno);
        return false;
    }

    --pending_evt_cnt_;

    // 重置该fd对应的event事件上下文
    fd_ctx->events = new_events;
    auto& event_ctx = fd_ctx->GetEventContext(event);
    fd_ctx->ResetEventContext(event_ctx);
    return true;
}

// 取消指定文件描述符的特定事件监听
bool IOManager::CancelEvent(int fd, Event event) {
    std::shared_lock rlock(rw_mutex_);
    if (fd_contexts_.size() <= fd) {
        return false;
    }

    FdContext* fd_ctx = fd_contexts_[fd];
    rlock.unlock();

    std::lock_guard lock(fd_ctx->mutex);
    if (fd_ctx->events & event) {
        return false;
    }

    // 删除事件
    Event new_events = static_cast<Event>(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | static_cast<int>(new_events);
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(epfd_, op, fd, &epevent);
    if (rt) {
        LOG_ERROR << "epoll_ctl " << strerror(errno);
        return false;
    }

    // 只是取消特定事件，其他事件还是要保留
    fd_ctx->TriggerEvent(event);
    --pending_evt_cnt_;
    return true;
}

// 取消指定文件描述符的所有事件
bool IOManager::CancelAllEvent(int fd) {
    std::shared_lock rlock(rw_mutex_);
    if (fd_contexts_.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = fd_contexts_[fd];
    rlock.unlock();

    std::lock_guard lock(fd_ctx->mutex);
    if (!fd_ctx->events) {
        return false;
    }

    // 删除全部事件
    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(epfd_, op, fd, &epevent);
    if (rt) {
        LOG_ERROR << "epoll_ctl " << strerror(errno);
        return false;
    }

    // 触发全部已注册的事件
    if (fd_ctx->events & READ) {
        fd_ctx->TriggerEvent(READ);
        --pending_evt_cnt_;
    }
    if (fd_ctx->events & WRITE) {
        fd_ctx->TriggerEvent(WRITE);
        --pending_evt_cnt_;
    }

    assert(fd_ctx->events == 0);
    return true;
}

void IOManager::OnTimerInsertAtFront() {
    Tickle();
}
