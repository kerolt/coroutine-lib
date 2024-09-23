#include "hook.h"

#include <dlfcn.h>
#include <unistd.h>

#include <functional>

#include "coroutine.h"
#include "iomanager.h"

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
}
