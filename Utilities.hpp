#pragma once

#include <functional>
#include <vector>
#include <thread>
#include <filesystem>
#include <optional>
#include <set>
#include <nlohmann/json.hpp>

#ifndef _WIN32 
#define POPEN popen
#define PCLOSE pclose
#else
#define POPEN _popen
#define PCLOSE _pclose
#endif

enum class GenMode : int {
    ForwardDeclMode,
    RegularMode,
    InlineMode
};

void Log(std::filesystem::path const& Task, std::string const& Str);

struct CallOnDtor {
    std::function<void()> Func;
    CallOnDtor(std::function<void()> Func);
    ~CallOnDtor();
    CallOnDtor(CallOnDtor const&) = delete;
    CallOnDtor(CallOnDtor&&) = delete;
    CallOnDtor& operator=(CallOnDtor const&) = delete;
    CallOnDtor& operator=(CallOnDtor&&) = delete;
};

template<typename F, typename T>
inline void ParallelFor(F Func, std::vector<T> const& Inputs) {
    std::vector<std::thread> Threads;
    Threads.resize(std::thread::hardware_concurrency());
    std::vector<std::atomic<int>*> ThreadStates; // 0 = fresh, 1 = done, 2 = running
    for (size_t i = 0; i < Threads.size(); ++i) ThreadStates.push_back(new std::atomic<int>(0));

    for (size_t i = 0; i < Inputs.size();) {
        bool Found = false;
        for (size_t ThreadIndex = 0; ThreadIndex < Threads.size(); ++ThreadIndex) {
            if (ThreadStates[ThreadIndex]->load() <= 1) {
                if (ThreadStates[ThreadIndex]->load() == 1) Threads[ThreadIndex].join();
                ThreadStates[ThreadIndex]->store(2);
                Threads[ThreadIndex] = std::thread([Func, &ThreadStates, ThreadIndex, &Inputs, i](){
                    CallOnDtor OnDtor([&ThreadStates, ThreadIndex](){
                        ThreadStates[ThreadIndex]->store(1);
                    });

                    Func(Inputs[i]);
                });
                Found = true;
                ++i;
                break;
            }
        }
        if (!Found) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Join any remaining threads
    for (size_t ThreadIndex = 0; ThreadIndex < Threads.size(); ++ThreadIndex) {
        if (ThreadStates[ThreadIndex]->load() != 0) {
            Threads[ThreadIndex].join();
        }
        delete ThreadStates[ThreadIndex];
    }
}