#include "CrashHandler.h"
#include <Psapi.h>
#include <TlHelp32.h>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <ShlObj.h>
#include <DbgHelp.h>

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "DbgHelp.lib")

LPTOP_LEVEL_EXCEPTION_FILTER CCrashHandler::s_PreviousFilter = nullptr;
bool CCrashHandler::s_Initialized = false;

// Quick log function that writes immediately to disk
static void QuickLog(const char* message)
{
    CreateDirectoryA("C:\\necromancer_tf2", nullptr);
    CreateDirectoryA("C:\\necromancer_tf2\\crashlog", nullptr);
    
    FILE* f = nullptr;
    fopen_s(&f, "C:\\necromancer_tf2\\crashlog\\quicklog.txt", "a");
    if (f)
    {
        auto now = std::time(nullptr);
        tm timeInfo = {};
        localtime_s(&timeInfo, &now);
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeInfo);
        fprintf(f, "[%s] %s\n", timeStr, message);
        fflush(f);
        fclose(f);
    }
}

// Vectored exception handler - catches exceptions before SEH
static LONG WINAPI VectoredExceptionHandler(EXCEPTION_POINTERS* pExceptionInfo)
{
    DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
    
    // Skip non-crash exceptions
    if (code == EXCEPTION_BREAKPOINT || 
        code == EXCEPTION_SINGLE_STEP ||
        code == DBG_PRINTEXCEPTION_C ||
        code == 0x406D1388 || // Thread naming exception
        code == 0x4001000A)   // Thread exit
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    
    // Get module name for the crash address
    char moduleName[MAX_PATH] = "Unknown";
    HMODULE hModule = nullptr;
    void* crashAddr = pExceptionInfo->ExceptionRecord->ExceptionAddress;
    
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)crashAddr, &hModule))
    {
        GetModuleFileNameA(hModule, moduleName, MAX_PATH);
        // Get just the filename
        char* lastSlash = strrchr(moduleName, '\\');
        if (lastSlash) memmove(moduleName, lastSlash + 1, strlen(lastSlash));
    }
    
    // Calculate offset within module
    DWORD64 offset = hModule ? ((DWORD64)crashAddr - (DWORD64)hModule) : 0;
    
    // Get access violation details
    char accessInfo[128] = "";
    if (code == EXCEPTION_ACCESS_VIOLATION && pExceptionInfo->ExceptionRecord->NumberParameters >= 2)
    {
        ULONG_PTR accessType = pExceptionInfo->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR targetAddr = pExceptionInfo->ExceptionRecord->ExceptionInformation[1];
        sprintf_s(accessInfo, " [%s 0x%p]", 
            accessType == 0 ? "READ" : (accessType == 1 ? "WRITE" : "EXEC"),
            (void*)targetAddr);
    }
    
    // Log immediately with full details
    char buf[1024];
    sprintf_s(buf, "CRASH: Code=0x%08X Module=%s+0x%llX Addr=0x%p%s", 
        code, moduleName, offset, crashAddr, accessInfo);
    QuickLog(buf);
    
    // Also log register state
    CONTEXT* ctx = pExceptionInfo->ContextRecord;
    sprintf_s(buf, "  RIP=0x%llX RSP=0x%llX RBP=0x%llX", ctx->Rip, ctx->Rsp, ctx->Rbp);
    QuickLog(buf);
    sprintf_s(buf, "  RAX=0x%llX RBX=0x%llX RCX=0x%llX RDX=0x%llX", ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx);
    QuickLog(buf);
    
    return EXCEPTION_CONTINUE_SEARCH; // Let the normal handler process it
}

void CCrashHandler::Initialize()
{
    if (s_Initialized)
        return;

    QuickLog("CrashHandler initialized");
    
    // Add vectored exception handler (first chance)
    AddVectoredExceptionHandler(1, VectoredExceptionHandler);
    
    s_PreviousFilter = SetUnhandledExceptionFilter(ExceptionFilter);
    s_Initialized = true;
}

void CCrashHandler::Shutdown()
{
    if (!s_Initialized)
        return;

    SetUnhandledExceptionFilter(s_PreviousFilter);
    s_Initialized = false;
}

std::string CCrashHandler::GetExceptionCodeString(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:       return "EXCEPTION_FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
    default:                                 return "UNKNOWN_EXCEPTION";
    }
}

std::vector<StackFrame> CCrashHandler::CaptureStackTrace(CONTEXT* context)
{
    std::vector<StackFrame> frames;
    
    HANDLE hProcess = GetCurrentProcess();
    HANDLE hThread = GetCurrentThread();
    
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(hProcess, nullptr, TRUE);

    STACKFRAME64 stackFrame = {};
    DWORD machineType;

#ifdef _M_X64
    machineType = IMAGE_FILE_MACHINE_AMD64;
    stackFrame.AddrPC.Offset = context->Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context->Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context->Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#else
    machineType = IMAGE_FILE_MACHINE_I386;
    stackFrame.AddrPC.Offset = context->Eip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context->Ebp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context->Esp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#endif

    const int maxFrames = 64;
    for (int i = 0; i < maxFrames; i++)
    {
        if (!StackWalk64(machineType, hProcess, hThread, &stackFrame, context,
            nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;

        if (stackFrame.AddrPC.Offset == 0)
            break;

        StackFrame frame = {};
        frame.Address = stackFrame.AddrPC.Offset;

        HMODULE hModule = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(frame.Address), &hModule);
        
        if (hModule)
        {
            char modulePath[MAX_PATH] = {};
            GetModuleFileNameA(hModule, modulePath, MAX_PATH);
            
            std::string fullPath(modulePath);
            size_t pos = fullPath.find_last_of("\\/");
            frame.ModuleName = (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;
            frame.Offset = frame.Address - reinterpret_cast<DWORD64>(hModule);
        }
        else
        {
            frame.ModuleName = "Unknown";
            frame.Offset = 0;
        }

        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)] = {};
        PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        if (SymFromAddr(hProcess, frame.Address, &displacement, symbol))
        {
            frame.FunctionName = symbol->Name;
        }
        else
        {
            frame.FunctionName = "Unknown";
        }

        frames.push_back(frame);
    }

    SymCleanup(hProcess);
    return frames;
}

std::string CCrashHandler::GetModuleInfo()
{
    std::stringstream ss;
    ss << "=== Loaded Modules ===\n";

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return ss.str();

    MODULEENTRY32W me32 = {};
    me32.dwSize = sizeof(MODULEENTRY32W);

    if (Module32FirstW(hSnapshot, &me32))
    {
        do
        {
            char moduleName[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, me32.szModule, -1, moduleName, MAX_PATH, nullptr, nullptr);
            
            ss << std::hex << std::uppercase << std::setfill('0');
            ss << "  " << std::setw(sizeof(void*) * 2) << reinterpret_cast<DWORD_PTR>(me32.modBaseAddr);
            ss << " - " << std::setw(sizeof(void*) * 2) << reinterpret_cast<DWORD_PTR>(me32.modBaseAddr + me32.modBaseSize);
            ss << std::dec << " | " << moduleName << "\n";
        } while (Module32NextW(hSnapshot, &me32));
    }

    CloseHandle(hSnapshot);
    return ss.str();
}

std::string CCrashHandler::GetSysInfo()
{
    std::stringstream ss;
    ss << "=== System Info ===\n";

    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    
    typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll)
    {
        auto RtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(hNtdll, "RtlGetVersion"));
        if (RtlGetVersion)
        {
            RtlGetVersion(reinterpret_cast<PRTL_OSVERSIONINFOW>(&osvi));
            ss << "  Windows Version: " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion 
               << " (Build " << osvi.dwBuildNumber << ")\n";
        }
    }

    SYSTEM_INFO sysInfo = {};
    ::GetSystemInfo(&sysInfo);
    ss << "  Processor Architecture: ";
    switch (sysInfo.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64: ss << "x64"; break;
    case PROCESSOR_ARCHITECTURE_INTEL: ss << "x86"; break;
    default: ss << "Unknown"; break;
    }
    ss << "\n";
    ss << "  Number of Processors: " << sysInfo.dwNumberOfProcessors << "\n";

    MEMORYSTATUSEX memStatus = {};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus))
    {
        ss << "  Total Physical Memory: " << (memStatus.ullTotalPhys / (1024 * 1024)) << " MB\n";
        ss << "  Available Physical Memory: " << (memStatus.ullAvailPhys / (1024 * 1024)) << " MB\n";
    }

    return ss.str();
}

std::string CCrashHandler::FormatCrashReport(EXCEPTION_POINTERS* pExceptionInfo)
{
    std::stringstream ss;
    
    auto now = std::time(nullptr);
    tm timeInfo = {};
    localtime_s(&timeInfo, &now);
    ss << "=== CRASH REPORT ===\n";
    ss << "Timestamp: " << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S") << "\n\n";

    ss << "=== Exception Info ===\n";
    DWORD exceptionCode = pExceptionInfo->ExceptionRecord->ExceptionCode;
    ss << "  Exception Code: 0x" << std::hex << std::uppercase << exceptionCode << std::dec;
    ss << " (" << GetExceptionCodeString(exceptionCode) << ")\n";
    ss << "  Exception Address: 0x" << std::hex << std::uppercase 
       << reinterpret_cast<DWORD_PTR>(pExceptionInfo->ExceptionRecord->ExceptionAddress) << std::dec << "\n";

    if (exceptionCode == EXCEPTION_ACCESS_VIOLATION && pExceptionInfo->ExceptionRecord->NumberParameters >= 2)
    {
        ULONG_PTR accessType = pExceptionInfo->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR targetAddr = pExceptionInfo->ExceptionRecord->ExceptionInformation[1];
        ss << "  Access Type: " << (accessType == 0 ? "Read" : (accessType == 1 ? "Write" : "Execute")) << "\n";
        ss << "  Target Address: 0x" << std::hex << std::uppercase << targetAddr << std::dec << "\n";
    }
    ss << "\n";

    ss << "=== Register State ===\n";
    CONTEXT* ctx = pExceptionInfo->ContextRecord;
#ifdef _M_X64
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "  RAX: " << std::setw(16) << ctx->Rax << "  RBX: " << std::setw(16) << ctx->Rbx << "\n";
    ss << "  RCX: " << std::setw(16) << ctx->Rcx << "  RDX: " << std::setw(16) << ctx->Rdx << "\n";
    ss << "  RSI: " << std::setw(16) << ctx->Rsi << "  RDI: " << std::setw(16) << ctx->Rdi << "\n";
    ss << "  RBP: " << std::setw(16) << ctx->Rbp << "  RSP: " << std::setw(16) << ctx->Rsp << "\n";
    ss << "  R8:  " << std::setw(16) << ctx->R8  << "  R9:  " << std::setw(16) << ctx->R9  << "\n";
    ss << "  R10: " << std::setw(16) << ctx->R10 << "  R11: " << std::setw(16) << ctx->R11 << "\n";
    ss << "  R12: " << std::setw(16) << ctx->R12 << "  R13: " << std::setw(16) << ctx->R13 << "\n";
    ss << "  R14: " << std::setw(16) << ctx->R14 << "  R15: " << std::setw(16) << ctx->R15 << "\n";
    ss << "  RIP: " << std::setw(16) << ctx->Rip << "  EFLAGS: " << std::setw(8) << ctx->EFlags << "\n";
#else
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "  EAX: " << std::setw(8) << ctx->Eax << "  EBX: " << std::setw(8) << ctx->Ebx << "\n";
    ss << "  ECX: " << std::setw(8) << ctx->Ecx << "  EDX: " << std::setw(8) << ctx->Edx << "\n";
    ss << "  ESI: " << std::setw(8) << ctx->Esi << "  EDI: " << std::setw(8) << ctx->Edi << "\n";
    ss << "  EBP: " << std::setw(8) << ctx->Ebp << "  ESP: " << std::setw(8) << ctx->Esp << "\n";
    ss << "  EIP: " << std::setw(8) << ctx->Eip << "  EFLAGS: " << std::setw(8) << ctx->EFlags << "\n";
#endif
    ss << std::dec << "\n";

    ss << "=== Stack Trace ===\n";
    CONTEXT ctxCopy = *ctx;
    auto frames = CaptureStackTrace(&ctxCopy);
    for (size_t i = 0; i < frames.size(); i++)
    {
        const auto& frame = frames[i];
        ss << "  [" << std::setw(2) << i << "] ";
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << std::setw(sizeof(void*) * 2) << frame.Address << std::dec;
        ss << " | " << frame.ModuleName << "!" << frame.FunctionName;
        ss << " + 0x" << std::hex << frame.Offset << std::dec << "\n";
    }
    ss << "\n";

    ss << GetModuleInfo() << "\n";
    ss << GetSysInfo();

    return ss.str();
}

void CCrashHandler::WriteCrashLog(const std::string& report)
{
    CreateDirectoryA("C:\\necromancer_tf2", nullptr);
    CreateDirectoryA("C:\\necromancer_tf2\\crashlog", nullptr);

    std::ofstream file("C:\\necromancer_tf2\\crashlog\\crashlog.txt", std::ios::out | std::ios::trunc);
    if (file.is_open())
    {
        file << report;
        file.close();
    }
}

void CCrashHandler::ShowCrashDialog(const std::string& report)
{
    std::string title = "Necromancer - Crash Report";
    std::string message = "The application has crashed. Please send this report to the developers.\n\n"
                          "A crash log has been saved to C:\\necromancer_tf2\\crashlog\\crashlog.txt\n\n"
                          "Click OK to copy the report to clipboard and exit.";

    int result = MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_OKCANCEL | MB_ICONERROR | MB_SYSTEMMODAL);
    
    if (result == IDOK)
    {
        if (OpenClipboard(nullptr))
        {
            EmptyClipboard();
            
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, report.size() + 1);
            if (hMem)
            {
                char* pMem = static_cast<char*>(GlobalLock(hMem));
                if (pMem)
                {
                    memcpy(pMem, report.c_str(), report.size() + 1);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_TEXT, hMem);
                }
            }
            
            CloseClipboard();
        }
    }

    std::string truncatedReport = report;
    if (truncatedReport.length() > 4000)
    {
        truncatedReport = truncatedReport.substr(0, 4000) + "\n\n[... truncated, see full log at C:\\necromancer_tf2\\crashlog\\crashlog.txt ...]";
    }
    
    MessageBoxA(nullptr, truncatedReport.c_str(), "Crash Details", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
}

LONG WINAPI CCrashHandler::ExceptionFilter(EXCEPTION_POINTERS* pExceptionInfo)
{
    QuickLog("ExceptionFilter called");
    
    if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT)
    {
        QuickLog("Breakpoint exception, continuing search");
        if (s_PreviousFilter)
            return s_PreviousFilter(pExceptionInfo);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    QuickLog("Generating crash report...");
    std::string report = FormatCrashReport(pExceptionInfo);
    
    QuickLog("Writing crash log...");
    WriteCrashLog(report);
    
    QuickLog("Showing crash dialog...");
    ShowCrashDialog(report);

    if (s_PreviousFilter)
        return s_PreviousFilter(pExceptionInfo);

    return EXCEPTION_EXECUTE_HANDLER;
}
