#include "fd_manager.h"

#include <asm-generic/socket.h>
#include <fcntl.h>
#include <memory>
#include <shared_mutex>
#include <sys/stat.h>

#include "hook.h"

FdContext::FdContext(int fd)
    : is_init_(false)
    , is_socket_(false)
    , is_close_(false)
    , fd_(fd)
    , recv_timeout_(-1)
    , send_timeout_(-1) {
    Init();
}

FdContext::~FdContext() {
}

void FdContext::Init() {
    if (is_init_) {
        return;
    }

    struct stat fd_stat;
    if (fstat(fd_, &fd_stat) == -1) {
        is_init_ = false;
        is_socket_ = false;
    } else {
        is_init_ = true;
        is_socket_ = S_ISSOCK(fd_stat.st_mode);
    }

    // 只针对socket fd进行hook操作
    if (is_socket_) {
        int flag = fcntl_func(fd_, F_GETFL, 0);
        if (!(flag & O_NONBLOCK)) {
            fcntl_func(fd_, F_SETFL, flag | O_NONBLOCK);
        }
        is_sys_nonblock_ = true;
    } else {
        is_sys_nonblock_ = false;
    }

    is_user_nonblock_ = false;
    is_close_ = false;
}

void FdContext::SetTimeout(int type, uint64_t ms) {
    (type == SO_RCVTIMEO ? recv_timeout_ : send_timeout_) = ms;
}

uint64_t FdContext::GetTimeout(int type) {
    return type == SO_RCVTIMEO ? recv_timeout_ : send_timeout_;
}

FdManager::FdManager() {
    fd_contexts_.resize(64);
}

FdContext::Ptr FdManager::Get(int fd, bool need_auto_create) {
    if (fd < 0) {
        return nullptr;
    }

    {
        std::shared_lock lock(rw_mutex_);
        if (fd >= fd_contexts_.size()) {
            if (!need_auto_create) {
                // 不存在且不需要自动创建FdContext，则返回nullptr
                return nullptr;
            }
        } else {
            if (fd_contexts_[fd] || !need_auto_create) {
                // fd满足要求，在fd_contexts中存在，或者不需要自动创建，则返回fd_contexts_[fd]
                return fd_contexts_[fd];
            }
        }
    }

    // 需要在fd_contexts_中创建一个FdContext
    std::shared_lock lock(rw_mutex_);
    auto ctx = std::make_shared<FdContext>(fd);
    if (fd >= fd_contexts_.size()) {
        // 自动扩容
        fd_contexts_.resize(fd * 1.5);
    }
    fd_contexts_[fd] = ctx;
    return ctx;
}

void FdManager::Delete(int fd) {
    std::shared_lock lock(rw_mutex_);
    if (fd >= fd_contexts_.size()) {
        // 需要删除的不在fd_contexts_中
        return;
    }
    // 与其说是删除，我觉得叫做释放可能更好
    fd_contexts_[fd].reset();
}