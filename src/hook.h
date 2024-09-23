#ifndef HOOK_H_
#define HOOK_H_

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

bool IsHookEnable();
void SetHookFlag(bool flag);

extern "C" {
using sleep_func_t = unsigned int (*)(unsigned int seconds);
extern sleep_func_t sleep_func;

using usleep_func_t = int (*)(useconds_t usec);
extern usleep_func_t usleep_func;

using nanosleep_func_t = int (*)(const struct timespec* req, struct timespec* rem);
extern nanosleep_func_t nanosleep_func;

using socket_func_t = int (*)(int domain, int type, int protocol);
extern socket_func_t socket_func;

using connect_func_t = int (*)(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
extern connect_func_t connect_func;

using accept_func_t = int (*)(int s, struct sockaddr* addr, socklen_t* addrlen);
extern accept_func_t accept_func;

using read_func_t = ssize_t (*)(int fd, void* buf, size_t count);
extern read_func_t read_func;

using readv_func_t = ssize_t (*)(int fd, const struct iovec* iov, int iovcnt);
extern readv_func_t readv_func;

using recv_func_t = ssize_t (*)(int sockfd, void* buf, size_t len, int flags);
extern recv_func_t recv_func;

using recvfrom_func_t = ssize_t (*)(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen);
extern recvfrom_func_t recvfrom_func;

using recvmsg_func_t = ssize_t (*)(int sockfd, struct msghdr* msg, int flags);
extern recvmsg_func_t recvmsg_func;

using write_func_t = ssize_t (*)(int fd, const void* buf, size_t count);
extern write_func_t write_func;

using writev_func_t = ssize_t (*)(int fd, const struct iovec* iov, int iovcnt);
extern writev_func_t writev_func;

using send_func_t = ssize_t (*)(int s, const void* msg, size_t len, int flags);
extern send_func_t send_func;

using sendto_func_t = ssize_t (*)(int s, const void* msg, size_t len, int flags, const struct sockaddr* to, socklen_t tolen);
extern sendto_func_t sendto_func;

using sendmsg_func_t = ssize_t (*)(int s, const struct msghdr* msg, int flags);
extern sendmsg_func_t sendmsg_func;

using close_func_t = int (*)(int fd);
extern close_func_t close_func;

using fcntl_func_t = int (*)(int fd, int cmd, ... /* arg */);
extern fcntl_func_t fcntl_func;

using ioctl_func_t = int (*)(int d, unsigned long int request, ...);
extern ioctl_func_t ioctl_func;

using getsockopt_func_t = int (*)(int sockfd, int level, int optname, void* optval, socklen_t* optlen);
extern getsockopt_func_t getsockopt_func;

using setsockopt_func_t = int (*)(int sockfd, int level, int optname, const void* optval, socklen_t optlen);
extern setsockopt_func_t setsockopt_func;

extern int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms);
}

#endif /* HOOK_H_ */
