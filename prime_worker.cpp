#include "winutil.h"

bool isPrime(long long n) {
    if (n < 2) return false;
    for (long long d = 2; d <= n / d; ++d)
        if (n % d == 0) return false;
    return true;
}
int main(int argc, char** argv) {
    consoleSetup();
    try {
        if (argc != 3 && argc != 5) throw std::runtime_error("prime_worker.exe начало конец [событие_старта событие_готовности]");
        long long first = number(argv[1]), last = number(argv[2]);
        if (first > last) throw std::runtime_error("Начало больше конца");
        Handle start, ready;
        if (argc == 5) {
            start = Handle(OpenEventW(SYNCHRONIZE, FALSE, widen(argv[3]).c_str()));
            check(start.value != nullptr, "OpenEventW(start)");
            ready = Handle(OpenEventW(EVENT_MODIFY_STATE, FALSE, widen(argv[4]).c_str()));
            check(ready.value != nullptr, "OpenEventW(ready)");
            check(SetEvent(ready.value), "SetEvent(ready)");
            check(WaitForSingleObject(start.value, INFINITE) == WAIT_OBJECT_0, "WaitForSingleObject(start)");
        }
        unsigned long long count = 0;
        for (long long n = first;; ++n) {
            if (isPrime(n)) ++count;
            if (n == last) break;
        }
        std::cout << "PID=" << GetCurrentProcessId() << ", простых чисел: " << count << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "Ошибка: " << e.what() << '\n'; return 1; }
}
