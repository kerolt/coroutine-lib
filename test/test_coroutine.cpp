#include "coroutine.h"
#include <iostream>

void Foo(int i) {
    std::cout << "test_coroutine " << i + 1 << " start\n";
    Coroutine::GetNowCoroutine()->Yield();
    std::cout << "test_coroutine " << i + 1 << " end\n";
}

void TestCoroutine() {
    Coroutine::GetNowCoroutine();

    auto co1 = std::make_shared<Coroutine>(std::bind(Foo, 0), 0, false);
    auto co2 = std::make_shared<Coroutine>(std::bind(Foo, 1), 0, false);
    co1->Resume();
    co2->Resume();

    co1->Resume();
    co2->Resume();
}

int main() {
    TestCoroutine();
    // 输出应为：
    // test_coroutine 1 start
    // test_coroutine 2 start
    // test_coroutine 1 end
    // test_coroutine 2 end
}