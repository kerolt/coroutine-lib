#ifndef FD_MANAGER_H_
#define FD_MANAGER_H_

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <vector>

// 管理fd上下文
class FdContext : public std::enable_shared_from_this<FdContext> {
public:
    using Ptr = std::shared_ptr<FdContext>;

    FdContext(int fd);
    ~FdContext();

    void SetTimeout(int type, uint64_t ms);
    uint64_t GetTimeout(int type);

    bool IsInit() {
        return is_init_;
    }

    bool IsSocket() {
        return is_socket_;
    }

    bool IsClose() {
        return is_close_;
    }

    void SetUserNonblock(bool flag) {
        is_user_nonblock_ = flag;
    }

    bool GetUserNonblock() {
        return is_user_nonblock_;
    }

    void SetSysNonblock(bool flag) {
        is_sys_nonblock_ = flag;
    }

    bool GetSysNonblock() {
        return is_sys_nonblock_;
    }

private:
    void Init();

private:
    bool is_init_;
    bool is_socket_;
    bool is_sys_nonblock_;
    bool is_user_nonblock_;
    bool is_close_;

    int fd_;
    uint64_t recv_timeout_;
    uint64_t send_timeout_;
};

// 管理FdContext，采用单例模式
class FdManager {
public:
    static FdManager* GetInstance() {
        static FdManager fd_manager;
        return &fd_manager;
    }

    FdManager(const FdManager&) = delete;
    FdManager& operator=(const FdManager&) = delete;

    FdContext::Ptr Get(int fd, bool need_auto_create = false);

    void Delete(int fd);

private:
    FdManager();

private:
    std::shared_mutex rw_mutex_;
    std::vector<FdContext::Ptr> fd_contexts_;
};

#endif /* FD_MANAGER_H_ */
