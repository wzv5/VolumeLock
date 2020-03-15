#pragma once

#include <iostream>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>

inline void PrintTime()
{
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    tm t;
    localtime_s(&t, &now);
    std::cout << std::put_time(&t, "[%H:%M:%S] ");
}

inline void Log(const std::string& msg)
{
    PrintTime();
    std::cout << msg << std::endl;
}

inline void Log(const std::wstring& msg)
{
    PrintTime();
    std::wcout << msg << std::endl;
}

inline void Log(const char* msg)
{
    Log(std::string(msg));
}

inline void Log(const wchar_t* msg)
{
    Log(std::wstring(msg));
}

template <typename T>
inline void Log(const T& ss)
{
    Log(ss.str());
}
