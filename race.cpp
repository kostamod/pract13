#include "winutil.h"
#include <cstdio>
#include <iomanip>

const DWORD classes[] = {IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS,
                          ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS};
const char* names[] = {"IDLE", "BELOW_NORMAL", "NORMAL", "ABOVE_NORMAL", "HIGH"};
DWORD priority(int n) {
    if (n < 1 || n > 5) throw std::runtime_error("Класс должен быть от 1 до 5");
    return classes[n - 1];
}
std::string priorityName(DWORD p) {
    for (int i = 0; i < 5; ++i) if (p == classes[i]) return names[i];
    return "UNKNOWN";
}
struct Racer {
    Handle process, thread, ready;
    DWORD pid = 0, initial = 0, current = 0, exitCode = STILL_ACTIVE;
    bool done = false;
    unsigned long long end = 0, cpu = 0;
};
void refresh(std::vector<Racer>& racers) {
    for (auto& r : racers) if (!r.done) {
        DWORD state = WaitForSingleObject(r.process.value, 0);
        if (state == WAIT_FAILED) check(FALSE, "WaitForSingleObject");
        if (state == WAIT_OBJECT_0) {
            FILETIME create, exit, kernel, user;
            check(GetProcessTimes(r.process.value, &create, &exit, &kernel, &user), "GetProcessTimes");
            check(GetExitCodeProcess(r.process.value, &r.exitCode), "GetExitCodeProcess");
            r.end = ticks(exit); r.cpu = ticks(kernel) + ticks(user); r.done = true;
            std::cout << "Финиш PID=" << r.pid << ", код=" << r.exitCode << std::endl;
        }
    }
}
void show(std::vector<Racer>& racers) {
    refresh(racers);
    for (size_t i = 0; i < racers.size(); ++i) {
        auto& r = racers[i];
        if (!r.done) { r.current = GetPriorityClass(r.process.value); check(r.current != 0, "GetPriorityClass"); }
        std::cout << i + 1 << ": PID=" << r.pid << ", " << (r.done ? "завершён" : "работает")
                  << ", класс=" << priorityName(r.current) << '\n';
    }
}
void change(std::vector<Racer>& racers, int id, int cls) {
    if (id < 1 || id > (int)racers.size()) throw std::runtime_error("Нет такого участника");
    DWORD value = priority(cls);
    refresh(racers);
    auto& r = racers[id - 1];
    if (r.done) throw std::runtime_error("Участник уже завершён");
    check(SetPriorityClass(r.process.value, value), "SetPriorityClass");
    r.current = GetPriorityClass(r.process.value);
    check(r.current != 0, "GetPriorityClass");
    std::cout << "CHANGE PID=" << r.pid << ", " << priorityName(r.current) << std::endl;
}
int main(int argc, char** argv) {
    consoleSetup();
    std::vector<Racer> racers;
    try {
        bool automatic = argc > 1 && std::string(argv[1]) == "--auto";
        int count; long long first, last;
        std::vector<int> selected;
        int changeMs = -1, changeId = 0, changeClass = 0;
        std::cout << "Классы: 1=IDLE, 2=BELOW_NORMAL, 3=NORMAL, 4=ABOVE_NORMAL, 5=HIGH\n";
        if (automatic) {
            if (argc != 6 && argc != 9) throw std::runtime_error("--auto N начало конец классы_через_запятую [мс номер класс]");
            count = (int)number(argv[2]); first = number(argv[3]); last = number(argv[4]);
            std::stringstream ss(argv[5]); std::string part;
            while (std::getline(ss, part, ',')) selected.push_back((int)number(part));
            if (argc == 9) { changeMs = (int)number(argv[6]); changeId = (int)number(argv[7]); changeClass = (int)number(argv[8]); }
        } else {
            if (argc != 1) throw std::runtime_error("Неизвестные параметры");
            count = (int)number(line("Количество процессов (2-5): "));
            first = number(line("Начало диапазона: ")); last = number(line("Конец диапазона: "));
            if (count < 2 || count > 5) throw std::runtime_error("Нужно от 2 до 5 процессов");
            for (int i = 0; i < count; ++i) selected.push_back((int)number(line(("Класс участника " + std::to_string(i + 1) + ": ").c_str())));
        }
        if (count < 2 || count > 5 || selected.size() != (size_t)count || first > last)
            throw std::runtime_error("Проверьте количество, диапазон и список классов");
        for (int c : selected) priority(c);
        if (changeMs >= 0) {
            priority(changeClass);
            if (changeId < 1 || changeId > count) throw std::runtime_error("Нет участника для изменения класса");
        }
        DWORD_PTR allowed, system;
        check(GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system), "GetProcessAffinityMask");
        if (!allowed) throw std::runtime_error("Не удалось выбрать процессор в текущей группе");
        DWORD_PTR cpu = allowed & (~allowed + 1); // Младший разрешённый бит, ровно один логический CPU.
        std::cout << "Маска процессора: " << cpu << ", диапазон: " << first << ".." << last << '\n';
        std::string eventName = "Local\\StudentRace_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64());
        Handle start(CreateEventW(nullptr, TRUE, FALSE, widen(eventName).c_str()));
        check(start.value != nullptr, "CreateEventW(start)");
        std::vector<HANDLE> readyHandles;
        for (int i = 0; i < count; ++i) {
            std::string readyName = eventName + "_ready_" + std::to_string(i);
            Racer r;
            r.ready = Handle(CreateEventW(nullptr, TRUE, FALSE, widen(readyName).c_str()));
            check(r.ready.value != nullptr, "CreateEventW(ready)");
            PROCESS_INFORMATION pi = launch(folder() + L"prime_worker.exe",
                std::to_wstring(first) + L" " + std::to_wstring(last) + L" " + widen(eventName) + L" " + widen(readyName),
                CREATE_SUSPENDED | priority(selected[i]));
            r.process = Handle(pi.hProcess); r.thread = Handle(pi.hThread); r.pid = pi.dwProcessId;
            r.initial = r.current = priority(selected[i]);
            racers.push_back(std::move(r)); // Сначала сохраняем handles для очистки при ошибке.
            check(SetProcessAffinityMask(racers.back().process.value, cpu), "SetProcessAffinityMask");
            check(ResumeThread(racers.back().thread.value) != (DWORD)-1, "ResumeThread");
            readyHandles.push_back(racers.back().ready.value);
        }
        DWORD readyState = WaitForMultipleObjects((DWORD)readyHandles.size(), readyHandles.data(), TRUE, 30000);
        if (readyState != WAIT_OBJECT_0) throw std::runtime_error("Не все процессы готовы к старту за 30 секунд");
        show(racers);
        std::cout << "Команды: s - состояния, c N K - изменить класс участника N на K.\n";
        FILETIME startTime;
        GetSystemTimePreciseAsFileTime(&startTime);
        unsigned long long begin = ticks(startTime);
        ULONGLONG started = GetTickCount64();
        std::cout << "СТАРТ" << std::endl;
        check(SetEvent(start.value), "SetEvent(start)");
        std::string input, cmd;
        std::vector<std::string> changes;
        while (true) {
            refresh(racers);
            if (std::all_of(racers.begin(), racers.end(), [](const Racer& r) { return r.done; })) break;
            try {
                if (changeMs >= 0 && GetTickCount64() - started >= (unsigned)changeMs) {
                    int scheduled = changeMs; changeMs = -1;
                    change(racers, changeId, changeClass);
                    changes.push_back(std::to_string(scheduled) + " ms; participant=" + std::to_string(changeId) + "; class=" + priorityName(priority(changeClass)));
                }
                if (!automatic && command(input, cmd)) {
                    std::istringstream ss(cmd); char action = 0; int id = 0, cls = 0;
                    if (ss >> action && action == 's') show(racers);
                    else if (action == 'c' && ss >> id >> cls) {
                        change(racers, id, cls);
                        changes.push_back(std::to_string(GetTickCount64() - started) + " ms; participant=" + std::to_string(id) + "; class=" + priorityName(priority(cls)));
                    } else std::cout << "Команды: s или c N K\n";
                }
            } catch (const std::exception& e) { std::cerr << "Ошибка команды: " << e.what() << '\n'; }
            Sleep(20);
        }
        std::vector<int> order;
        for (int i = 0; i < count; ++i) if (racers[i].exitCode == 0) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return racers[a].end < racers[b].end; });
        FILE* csv = _wfopen((folder() + L"results.csv").c_str(), L"wb");
        if (!csv) throw std::runtime_error("Не удалось сохранить results.csv");
        fputs("\xEF\xBB\xBFМесто;Участник;PID;Начальный класс;Конечный класс;Время с;CPU с;Код выхода\n", csv);
        std::cout << "\nМесто  Участник  PID       Класс (начало -> конец)     Время, с   CPU, с\n";
        unsigned long long previousEnd = 0;
        int place = 0;
        for (size_t k = 0; k < order.size(); ++k) {
            int i = order[k]; auto& r = racers[i];
            if (r.end != previousEnd) place = (int)k + 1;
            previousEnd = r.end;
            double elapsed = (r.end >= begin ? r.end - begin : 0) / 10000000.0;
            double cpuTime = r.cpu / 10000000.0;
            std::cout << place << "      " << i + 1 << "        " << r.pid << "   " << priorityName(r.initial) << " -> "
                      << priorityName(r.current) << "   " << std::fixed << std::setprecision(3) << elapsed << "   " << cpuTime << '\n';
            fprintf(csv, "%d;%d;%lu;%s;%s;%.6f;%.6f;%lu\n", place, i + 1, (unsigned long)r.pid,
                priorityName(r.initial).c_str(), priorityName(r.current).c_str(), elapsed, cpuTime, (unsigned long)r.exitCode);
        }
        bool failed = false;
        for (int i = 0; i < count; ++i) if (racers[i].exitCode != 0) {
            failed = true;
            std::cout << "Участник " << i + 1 << " завершился с ошибкой " << racers[i].exitCode << ", место не присваивается\n";
            fprintf(csv, ";%d;%lu;%s;%s;;;%lu\n", i + 1, (unsigned long)racers[i].pid,
                priorityName(racers[i].initial).c_str(), priorityName(racers[i].current).c_str(), (unsigned long)racers[i].exitCode);
        }
        if (fclose(csv) != 0) throw std::runtime_error("Ошибка записи results.csv");
        FILE* history = _wfopen((folder() + L"changes.txt").c_str(), L"wb");
        if (!history) throw std::runtime_error("Не удалось сохранить changes.txt");
        for (const auto& s : changes) fprintf(history, "%s\n", s.c_str());
        if (fclose(history) != 0) throw std::runtime_error("Ошибка записи changes.txt");
        std::cout << "Итоги сохранены в results.csv рядом с программой.\n";
        return failed ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << '\n';
        // При неполном запуске не оставляем процессы в ожидании стартового события.
        for (auto& r : racers) if (WaitForSingleObject(r.process.value, 0) == WAIT_TIMEOUT) {
            if (TerminateProcess(r.process.value, 1)) WaitForSingleObject(r.process.value, INFINITE);
        }
        return 1;
    }
}
