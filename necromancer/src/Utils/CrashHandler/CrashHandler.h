#pragma once

#include "../Singleton/Singleton.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <string>
#include <vector>
#include <sstream>

#pragma comment(lib, "DbgHelp.lib")

struct StackFrame
{
    DWORD64 Address;
    std::string ModuleName;
    std::string FunctionName;
    DWORD64 Offset;
};

class CCrashHandler
{
private:
    static LPTOP_LEVEL_EXCEPTION_FILTER s_PreviousFilter;
    static bool s_Initialized;

    static LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* pExceptionInfo);
    static std::string GetExceptionCodeString(DWORD code);
    static std::vector<StackFrame> CaptureStackTrace(CONTEXT* context);
    static std::string GetModuleInfo();
    static std::string GetSysInfo();
    static std::string FormatCrashReport(EXCEPTION_POINTERS* pExceptionInfo);
    static void ShowCrashDialog(const std::string& report);
    static void WriteCrashLog(const std::string& report);

public:
    static void Initialize();
    static void Shutdown();
};

MAKE_SINGLETON(CCrashHandler, CrashHandler);
