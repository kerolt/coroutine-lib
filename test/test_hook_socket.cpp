#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>

#include "iomanager.h"
#include "util.h"

void TestSocketAPI() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "36.152.44.96", &addr.sin_addr.s_addr);

    LOG << "=== begin connect ===\n";
    int rt = connect(sock, (const sockaddr*) &addr, sizeof(addr));
    LOG << "connect rt=" << rt << " errno=" << errno << "\n";

    if (rt) {
        return;
    }

    const char data[] = "GET / HTTP/1.0\r\n\r\n";
    rt = send(sock, data, sizeof(data), 0);
    LOG << "send rt=" << rt << " errno=" << errno << "\n";

    if (rt <= 0) {
        return;
    }

    std::string buff;
    buff.resize(4096);

    rt = recv(sock, &buff[0], buff.size(), 0);
    LOG << "recv rt=" << rt << " errno=" << errno << "\n";

    if (rt <= 0) {
        return;
    }

    buff.resize(rt);
    LOG << buff << "\n";
}

int main() {
    IOManager iom(2);
    for (int i = 0; i < 4; i++) {
        iom.Sched(TestSocketAPI);
    }
    LOG << "main end\n";
}