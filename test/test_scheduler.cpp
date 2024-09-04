#include "coroutine.h"
#include "scheduler.h"
#include <iostream>
#include <thread>

unsigned int test_number;
std::mutex mutex_cout;

void task() {
    {
        // 这里为什么加锁？我想是为了避免cout的线程不安全造成输出混乱
        std::lock_guard lock(mutex_cout);
        std::cout << "task " << test_number++ << " is under processing in thread: " << std::this_thread::get_id() << std::endl;
    }
    sleep(1);
}

int main() {
    // 使用三个线程，其中一个使用调度器所在线程（也就是main线程）
    std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler>(3, true);

    // 创建内部线程池并启动
    scheduler->Start();

    sleep(2);

    std::cout << "\n=== begin post ===\n\n";
    for (int i = 0; i < 5; i++) {
        std::shared_ptr<Coroutine> fiber = std::make_shared<Coroutine>(task);
        scheduler->Sched(fiber);
    }

    sleep(6);

    std::cout << "\n=== post again ===\n\n";
    for (int i = 0; i < 15; i++) {
        std::shared_ptr<Coroutine> fiber = std::make_shared<Coroutine>(task);
        scheduler->Sched(fiber);
    }

    // 这里只等待3S，“post again”中加入的任务肯定无法被除main线程外的两个调度线程执行完，
    // 这样就可以看到main线程中的调度协程进行任务调度了，
    // 要是将时间设置的久一点，例如15，就看不到main线程的调度了
    sleep(3);

    // 如果只有一个线程时（此时main线程会创建调度协程），在这里才会开始调度
    scheduler->Stop();
}