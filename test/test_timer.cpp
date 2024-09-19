#include "iomanager.h"
#include "timer.h"
#include "util.h"

static int timeout = 1000;
static Timer::Ptr timer;

void TimerCallback() {
    LOG << "timer callback, timeout = " << timeout << "\n";
    timeout += 1000;
    if (timeout < 5000) {
        timer->Reset(timeout, true);
    } else {
        timer->Cancel();
    }
}

void TestTimer() {
    IOManager iom;

    // 循环定时器
    timer = iom.AddTimer(1000, TimerCallback, true);

    // 单次定时器
    iom.AddTimer(500, [] {
        LOG << "500ms timeout"
            << "\n";
    });
    iom.AddTimer(5000, [] {
        LOG << "5000ms timeout"
            << "\n";
    });
}

int main() {
    TestTimer();
    LOG << "test timer end\n";
}