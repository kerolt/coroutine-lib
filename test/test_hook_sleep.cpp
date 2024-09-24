#include "iomanager.h"
#include "util.h"

void TestSleep() {
    LOG << "=== test_sleep begin ===\n";
    IOManager iom;

    iom.Sched([] {
        sleep(2);
        LOG << "sleep 2\n";
    });

    iom.Sched([] {
        sleep(3);
        LOG << "sleep 3\n";
    });

    LOG << "=== test_sleep end ===\n";
}

int main() {
    TestSleep();
}
