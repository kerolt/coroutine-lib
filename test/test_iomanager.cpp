#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "iomanager.h"
#include "util.h"

const char data[] = "GET / HTTP/1.0\r\n\r\n";
char recv_data[4096];
int sock;

// 读取从百度服务器发回的数据
void Read() {
    recv(sock, recv_data, 4096, 0);
    LOG << "recv data:\n"
        << recv_data << "\n";
}

// 向百度的服务器发送一个GET请求，获取百度首页的html
void Write() {
    send(sock, data, sizeof(data), 0);
    LOG << "send messege to the server!\n";
}

int main() {
    IOManager iom(1);

    sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(80);
    server.sin_addr.s_addr = inet_addr("103.235.46.96");

    fcntl(sock, F_SETFL, O_NONBLOCK);

    if (connect(sock, (struct sockaddr*) &server, sizeof(server)) != 0) {
        // 在非阻塞套接字上调用connect时，会得到EINPROGRESS，而不是阻塞等待连接握手完成
        if (errno == EINPROGRESS) {
            iom.AddEvent(sock, Event::WRITE, Write);
            iom.AddEvent(sock, Event::READ, Read);
        }
    }

    LOG << "event has been posted\n";
}