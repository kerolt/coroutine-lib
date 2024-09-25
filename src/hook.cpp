#include "hook.h"

#include <asm-generic/ioctls.h>
#include <asm-generic/socket.h>
#include <bits/types/struct_timeval.h>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <dlfcn.h>
#include <fcntl.h>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

#include <functional>
#include <utility>

#include "coroutine.h"
#include "fd_manager.h"
#include "iomanager.h"
#include "timer.h"
#include "util.h"

static thread_local bool hook_enable = false;

#define HOOK_FUNC(XX) \
    XX(sleep)         \
    XX(usleep)        \
    XX(nanosleep)     \
    XX(socket)        \
    XX(connect)       \
    XX(accept)        \
    XX(read)          \
    XX(readv)         \
    XX(recv)          \
    XX(recvfrom)      \
    XX(recvmsg)       \
    XX(write)         \
    XX(writev)        \
    XX(send)          \
    XX(sendto)        \
    XX(sendmsg)       \
    XX(close)         \
    XX(fcntl)         \
    XX(ioctl)         \
    XX(getsockopt)    \
    XX(setsockopt)

// 类似js中的立即执行函数
auto hook_init = []() {
#define XX(name) name##_func = (name##_func_t) dlsym(RTLD_NEXT, #name);
    HOOK_FUNC(XX)
#undef XX
    return true;
}();

bool IsHookEnable() {
    return hook_enable;
}

void SetHookFlag(bool flag) {
    hook_enable = flag;
}

struct TimerInfo {
    int cancelled = 0;
};

// for IO_API
template <typename OriginFunc, typename... Args>
static ssize_t DoIO(int fd, OriginFunc origin_func, const char* hook_func_name, uint32_t event, int type, Args&&... args) {
    if (!hook_enable) {
        return origin_func(fd, std::forward<Args>(args)...);
    }

    auto ctx = FdManager::GetInstance()->Get(fd);
    if (!ctx || !ctx->IsSocket() || ctx->GetUserNonblock()) {
        // 为什么要对用户主动设置非阻塞也要做返回处理呢，可能是因为用户有自己的处理方式
        // 我们的hook操作只针对系统设置的非阻塞
        return origin_func(fd, std::forward<Args>(args)...);
    }
    if (ctx->IsClose()) {
        // 文件描述符都关闭了，还处理啥呢，设置错误码后返回-1
        errno = EBADF;
        return -1;
    }

    auto timeout = ctx->GetTimeout(type);
    auto timer_info = std::make_shared<TimerInfo>();

    ssize_t ret;
    bool retry = true;
    while (retry) {
        while (ret == -1 && errno == EINTR) {
            ret = origin_func(fd, std::forward<Args>(args)...);
        }
        // 当调用IO函数时，如果返回 -1 且 errno 为 EAGAIN，表示当前操作无法完成，这时进入异步处理逻辑
        if (ret == -1 && errno == EAGAIN) {
            auto iom = IOManager::GetIOManager();
            Timer::Ptr timer;
            std::weak_ptr<TimerInfo> wptr_for_tinfo(timer_info);

            // 超时时间有效，则为fd设置超时后取消对应的事件
            if (timeout != (uint64_t) -1) {
                timer = iom->AddConditionTimer(
                    timeout, [wptr_for_tinfo, fd, iom, event] {
                        auto sptr = wptr_for_tinfo.lock();
                        if (!sptr || sptr->cancelled) {
                            return;
                        }
                        sptr->cancelled = ETIMEDOUT;
                        iom->CancelEvent(fd, (Event) event);
                    },
                    wptr_for_tinfo);
            }

            bool ok = iom->AddEvent(fd, (Event) event);
            if (!ok) {
                LOG_ERROR << hook_func_name << " AddEvent(" << fd << ", " << event << ") failed\n";
                if (timer) {
                    timer->Cancel();
                }
                return -1;
            } else {
                Coroutine::GetNowCoroutine()->Yield();
                if (timer) {
                    timer->Cancel();
                }
                if (timer_info->cancelled) {
                    errno = timer_info->cancelled;
                    return -1;
                }
            }
        } else {
            retry = false;
        }
    };

    return ret;
}

// 对于没开启hook的情况，我们应该使用原来的API
extern "C" {
#define XX(name) name##_func_t name##_func = nullptr;
HOOK_FUNC(XX);
#undef XX

// sleep类的API把当前运行的协程暂停后让出，并为其设置一个定时器，在规定时间后再将这个协程加入调度
// 这样就实现了sleep类API不阻塞当前线程的功能
#pragma region SLEEP_API
unsigned int sleep(unsigned int seconds) {
    if (!hook_enable) {
        return sleep_func(seconds);
    }
    auto co = Coroutine::GetNowCoroutine();
    auto iom = IOManager::GetIOManager();
    iom->AddTimer(seconds * 1000, [iom, co] {
        iom->Sched(co);
    });
    Coroutine::GetNowCoroutine()->Yield();
    return 0;
}

int usleep(useconds_t usec) {
    if (!hook_enable) {
        return usleep_func(usec);
    }
    auto co = Coroutine::GetNowCoroutine();
    auto iom = IOManager::GetIOManager();
    // 把当前运行的协程暂停后让出，并为其设置一个定时器，在规定时间后再将这个协程加入调度
    // 这样就实现了sleep不阻塞的功能
    iom->AddTimer(usec / 1000, [iom, co] {
        iom->Sched(co);
    });
    Coroutine::GetNowCoroutine()->Yield();
    return 0;
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
    if (!hook_enable) {
        return nanosleep_func(req, rem);
    }
    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 / 1000;
    auto co = Coroutine::GetNowCoroutine();
    auto iom = IOManager::GetIOManager();
    iom->AddTimer(timeout_ms, [iom, co] {
        iom->Sched(co);
    });
    Coroutine::GetNowCoroutine()->Yield();
    return 0;
}
#pragma endregion

#pragma region SOCKET_API
int socket(int domain, int type, int protocol) {
    if (!hook_enable) {
        return socket_func(domain, type, protocol);
    }
    int fd = socket_func(domain, type, protocol);
    if (fd == -1) {
        LOG_ERROR << "create socket fd failed\n";
        return fd;
    }
    // 创建了socket fd后，需要将它添加到FdManager中
    // 我觉得最好用一个Put API来完成，而不是耦合到一个Get API中
    FdManager::GetInstance()->Get(fd, true);
    return fd;
}

// 为connect hook上超时时间
int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) {
    if (!hook_enable) {
        return connect_func(fd, addr, addrlen);
    }

    auto ctx = FdManager::GetInstance()->Get(fd);
    if (!ctx || ctx->IsClose()) {
        errno = EBADF;
        return -1;
    }
    // 使用原始的connect操作可以避免对不适当的文件描述符进行无效的连接尝试
    if (!ctx->IsSocket() || ctx->GetUserNonblock()) {
        return connect_func(fd, addr, addrlen);
    }

    int ret = connect_func(fd, addr, addrlen);
    if (ret == 0) {
        // 连接成功
        return 0;
    } else if (ret != -1 || errno != EINPROGRESS) {
        // 连接成功或者其他错误值
        return ret;
    }

    auto iom = IOManager::GetIOManager();
    auto sptr_for_tinfo = std::make_shared<TimerInfo>();
    std::weak_ptr<TimerInfo> wptr_for_tinfo(sptr_for_tinfo);
    Timer::Ptr timer;

    // 如果超时参数有效，则添加一个条件定时器，到时间后取消文件描述符的写事件
    if (timeout_ms != (uint64_t) -1) {
        timer = iom->AddConditionTimer(
            timeout_ms, [wptr_for_tinfo, fd, iom] {
                // 相当于拿到sptr_for_tinfo
                auto tinfo = wptr_for_tinfo.lock();
                if (!tinfo || tinfo->cancelled) {
                    return;
                }
                tinfo->cancelled = ETIMEDOUT;
                iom->CancelEvent(fd, Event::WRITE);
            },
            wptr_for_tinfo);
    }

    if (iom->AddEvent(fd, Event::WRITE)) {
        Coroutine::GetNowCoroutine()->Yield();
        if (timer) {
            timer->Cancel();
        }
        if (sptr_for_tinfo->cancelled) {
            errno = sptr_for_tinfo->cancelled;
            return -1;
        }
    } else {
        if (timer) {
            timer->Cancel();
        }
        LOG_ERROR << "connect add event(" << fd << "WRITE) error\n";
    }

    // 检查socket fd的错误状态
    int err = 0;
    socklen_t len = sizeof(int);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == -1) {
        return -1;
    }
    if (!err) {
        // 连接成功
        return 0;
    } else {
        // 连接失败
        errno = err;
        return -1;
    }
}

int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen) {
    // 好吧，这里根本没hook啥~
    return getsockopt_func(sockfd, level, optname, optval, optlen);
}

// 增强setsockopt对于超时的处理
int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
    if (!hook_enable) {
        return setsockopt_func(sockfd, level, optname, optval, optlen);
    }
    if (level == SOL_SOCKET) {
        if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            auto ctx = FdManager::GetInstance()->Get(sockfd);
            if (ctx) {
                timeval* v = (timeval*) optval;
                ctx->SetTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_func(sockfd, level, optname, optval, optlen);
}
#pragma endregion

#pragma region IO_API
int accept(int s, struct sockaddr* addr, socklen_t* addrlen) {
    int fd = DoIO(s, accept_func, "accept", Event::READ, SO_RCVTIMEO, addr, addrlen);
    if (fd >= 0) {
        FdManager::GetInstance()->Get(fd, true);
    }
    return fd;
}

ssize_t read(int fd, void* buf, size_t count) {
    return DoIO(fd, read_func, "read", Event::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec* iov, int iovcnt) {
    return DoIO(fd, readv_func, "readv", Event::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
    return DoIO(sockfd, recv_func, "recv", Event::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen) {
    return DoIO(sockfd, recvfrom_func, "recvfrom", Event::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr* msg, int flags) {
    return DoIO(sockfd, recvmsg_func, "recvmsg", Event::READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void* buf, size_t count) {
    return DoIO(fd, write_func, "write", Event::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec* iov, int iovcnt) {
    return DoIO(fd, writev_func, "writev", Event::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void* msg, size_t len, int flags) {
    return DoIO(s, send_func, "send", Event::WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void* msg, size_t len, int flags, const struct sockaddr* to, socklen_t tolen) {
    return DoIO(s, sendto_func, "sendto", Event::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr* msg, int flags) {
    return DoIO(s, sendmsg_func, "sendmsg", Event::WRITE, SO_SNDTIMEO, msg, flags);
}
#pragma endregion

#pragma region OTHER_IO
int close(int fd) {
    if (!hook_enable) {
        return close_func(fd);
    }
    // 在close fd时，也要将注册到FdManager中的fd删除
    auto ctx = FdManager::GetInstance()->Get(fd);
    if (ctx) {
        auto iom = IOManager::GetIOManager();
        if (iom) {
            iom->CancelAllEvent(fd);
        }
        FdManager::GetInstance()->Delete(fd);
    }
    return close_func(fd);
}

// 能够设置和获取用户自定义的非阻塞标志，方便实现异步 I/O 操作
int fcntl(int fd, int cmd, ... /* arg */) {
    va_list va;
    va_start(va, cmd);
    switch (cmd) {
        case F_SETFL: {
            int arg = va_arg(va, int);
            va_end(va);
            auto ctx = FdManager::GetInstance()->Get(fd);
            if (!ctx || ctx->IsClose() || !ctx->IsSocket()) {
                return fcntl_func(fd, cmd, arg);
            }
            ctx->SetUserNonblock(true);
            if (ctx->GetSysNonblock()) {
                arg |= O_NONBLOCK;
            } else {
                arg &= ~O_NONBLOCK;
            }
            return fcntl_func(fd, cmd, arg);
        } break;
        case F_GETFL: {
            va_end(va);
            int arg = fcntl_func(fd, F_GETFL);
            auto ctx = FdManager::GetInstance()->Get(fd);
            if (!ctx || ctx->IsClose() || !ctx->IsSocket()) {
                return arg;
            }
            return ctx->GetUserNonblock() ? (arg | O_NONBLOCK) : (arg & ~O_NONBLOCK);
        } break;
        case F_DUPFD:         // 复制一个现有的文件描述符
        case F_DUPFD_CLOEXEC: // 复制文件描述符，但同时会设置新文件描述符的 close-on-exec 标志
        case F_SETFD:         // 设置文件描述符的标志
        case F_SETOWN:        // 设置接收特定信号的进程 ID 或进程组 ID
        case F_SETSIG:        // 设置与文件描述符相关的信号
        case F_SETLEASE:      // 设置文件的租约，可以控制对文件的独占访问权
        case F_NOTIFY:        // 设置文件描述符的通知机制
#ifdef F_SETPIPE_SZ           // 设置管道的容量大小
        case F_SETPIPE_SZ:
#endif
        {
            int arg = va_arg(va, int);
            va_end(va);
            return fcntl_func(fd, cmd, arg);
        } break;
        case F_SETLK:   // 设置文件锁
        case F_SETLKW:  // 与F_SETLK类似，但在冲突的情况下将会阻塞等待
        case F_GETLK: { // 用于检查fd是否有锁冲突
            struct flock* arg = va_arg(va, struct flock*);
            va_end(va);
            return fcntl_func(fd, cmd, arg);
        } break;
        case F_GETOWN_EX:
        case F_SETOWN_EX: {
            struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
            va_end(va);
            return fcntl_func(fd, cmd, arg);
        } break;
        default:
            va_end(va);
            return fcntl_func(fd, cmd);
    }
}

int ioctl(int d, unsigned long int request, ...) {
    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);
    if (request == FIONBIO) {
        bool user_nonblock = !!*(int*) arg;
        auto ctx = FdManager::GetInstance()->Get(d);
        if (!ctx || ctx->IsClose() || !ctx->IsSocket()) {
            return ioctl_func(d, request, arg);
        }
        ctx->SetUserNonblock(user_nonblock);
    }
    return ioctl_func(d, request, arg);
}
#pragma endregion
}
