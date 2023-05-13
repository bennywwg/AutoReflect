#include "Utilities.hpp"

#include <iostream>
#include <fstream>

void Log(std::filesystem::path const& Task, std::string const& Str) {
    static std::mutex Mutex;

    std::lock_guard<std::mutex> Lock(Mutex);
    std::cout << Task << ": " << Str << std::endl;
}

CallOnDtor::CallOnDtor(std::function<void()> Func) : Func(Func) { }
CallOnDtor::~CallOnDtor() { Func(); }