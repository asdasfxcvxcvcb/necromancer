#include "CrashHandler.h"

#include "../InterfaceManager/InterfaceManager.h"

#include <DbgHelp.h>
#include <Psapi.h>
#include <deque>
#include <sstream>
#include <format>
#include <ctime>
#include <unordered_map>
#include <filesystem>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

#define STATUS_RUNTIME_ERROR ((DWORD)0xE06D7363)

struct Frame_t
{
	std::string m_sModule = "";
	uintptr_t m_uBase = 0;
	uintptr_t m_uAddress = 0;
	std::string m_sFile = "";
	unsigned int m_uLine = 0;
	std::string m_sName = "";
};

PVOID CCrashHandler::s_pVectoredHandle = nullptr;
bool CCrashHandler::s_Initialized = false;
CrashContext_t CCrashHandler::s_Context = {};

static std::unordered_map<void*, bool> s_mAddresses = {};
static int s_iExceptions = 0;

static inline std::deque<Frame_t> StackTrace(PCONTEXT pContext)
{
	std::deque<Frame_t> vTrace = {};

	HANDLE hProcess = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();

	if (!SymInitialize(hProcess, nullptr, TRUE))
		return vTrace;

	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);

	STACKFRAME64 tStackFrame = {};
	tStackFrame.AddrPC.Offset = pContext->Rip;
	tStackFrame.AddrFrame.Offset = pContext->Rbp;
	tStackFrame.AddrStack.Offset = pContext->Rsp;
	tStackFrame.AddrPC.Mode = AddrModeFlat;
	tStackFrame.AddrFrame.Mode = AddrModeFlat;
	tStackFrame.AddrStack.Mode = AddrModeFlat;

	CONTEXT tContext = *pContext;
	int nFrames = 0;

	while (StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread, &tStackFrame, &tContext, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
	{
		if (tStackFrame.AddrPC.Offset == 0)
			break;
		if (nFrames++ > 64)
			break;

		vTrace.push_back({ .m_uAddress = tStackFrame.AddrPC.Offset });
		Frame_t& tFrame = vTrace.back();

		if (auto hBase = HINSTANCE(SymGetModuleBase64(hProcess, tStackFrame.AddrPC.Offset)))
		{
			tFrame.m_uBase = uintptr_t(hBase);

			char buffer[MAX_PATH];
			if (GetModuleBaseName(hProcess, hBase, buffer, sizeof(buffer) / sizeof(char)))
				tFrame.m_sModule = buffer;
			else
				tFrame.m_sModule = std::format("{:#x}", tFrame.m_uBase);
		}

		{
			DWORD dwOffset = 0;
			IMAGEHLP_LINE64 line = {};
			line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
			if (SymGetLineFromAddr64(hProcess, tStackFrame.AddrPC.Offset, &dwOffset, &line))
			{
				tFrame.m_sFile = line.FileName;
				tFrame.m_uLine = line.LineNumber;
				auto iFind = tFrame.m_sFile.rfind("\\");
				if (iFind != std::string::npos)
					tFrame.m_sFile.replace(0, iFind + 1, "");
			}
		}

		{
			DWORD64 dwOffset = 0;
			char buf[sizeof(IMAGEHLP_SYMBOL64) + 255];
			auto symbol = PIMAGEHLP_SYMBOL64(buf);
			symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64) + 255;
			symbol->MaxNameLength = 254;
			if (SymGetSymFromAddr64(hProcess, tStackFrame.AddrPC.Offset, &dwOffset, symbol))
				tFrame.m_sName = symbol->Name;
		}
	}

	SymCleanup(hProcess);
	return vTrace;
}

static std::string GetModuleOffset(void* address)
{
	HMODULE hModule = nullptr;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(address), &hModule))
	{
		char moduleName[MAX_PATH] = {};
		GetModuleFileNameA(hModule, moduleName, MAX_PATH);
		std::string fullPath(moduleName);
		size_t pos = fullPath.find_last_of("\\/");
		std::string name = (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;
		return std::format("{}+{:#x}", name, reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(hModule));
	}
	return std::format("{:#x}", reinterpret_cast<uintptr_t>(address));
}

// Validate a memory range with VirtualQuery — this never faults, and unlike
// __try/__except it also works while the vectored handler is dispatching
// another exception (SEH scope tables are bypassed during nested dispatch,
// which is exactly when our reporter used to fault itself)
static bool IsReadableMemory(const void* pAddr, size_t uSize)
{
	if (!pAddr || uSize > 0x10000)
		return false;

	const auto dwAddr = reinterpret_cast<uintptr_t>(pAddr);
	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(pAddr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
		return false;
	if (mbi.Protect & PAGE_GUARD)
		return false;

	constexpr DWORD dwReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
		| PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
	if (!(mbi.Protect & dwReadable))
		return false;

	// the whole range must sit inside this committed region
	const auto dwRegionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	return dwAddr + uSize <= dwRegionEnd;
}

// Read *pPtr without ever faulting ourselves - the pointer can be garbage
// while we're reporting the crash that garbage caused
static const void* SafeDeref(const void* pPtr)
{
	if (!pPtr || !IsReadableMemory(pPtr, sizeof(void*)))
		return nullptr;
	__try
	{
		return *static_cast<const void* const*>(pPtr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
}

// Get the directory where our DLL lives — avoids __ImageBase which isn't always available
static std::string GetModuleDirectory()
{
	char szPath[MAX_PATH] = {};
	HMODULE hMod = nullptr;
	// Find our DLL by searching for our own module handle via a known address
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(&GetModuleDirectory), &hMod))
	{
		GetModuleFileNameA(hMod, szPath, MAX_PATH);
	}
	else
	{
		// Fallback: current directory
		GetCurrentDirectoryA(MAX_PATH, szPath);
	}
	return std::string(szPath);
}

static bool WriteMinidump(PEXCEPTION_POINTERS ExceptionInfo)
{
	std::filesystem::path dumpDir = std::filesystem::path(GetModuleDirectory()).parent_path() / "dumps";
	std::filesystem::create_directories(dumpDir);

	auto now = std::time(nullptr);
	tm timeInfo = {};
	localtime_s(&timeInfo, &now);
	char timeStr[32];
	strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S", &timeInfo);
	std::string dumpPath = (dumpDir / std::format("crash_{}.dmp", timeStr)).string();

	HANDLE hFile = CreateFileA(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	MINIDUMP_EXCEPTION_INFORMATION mei = {};
	mei.ThreadId = GetCurrentThreadId();
	mei.ExceptionPointers = ExceptionInfo;
	mei.ClientPointers = FALSE;

	DWORD dwDumpType = MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo;
	bool bOk = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, static_cast<MINIDUMP_TYPE>(dwDumpType), &mei, nullptr, nullptr);

	CloseHandle(hFile);
	return bOk;
}

// Dump loaded modules — critical for resolving addresses in post-mortem
static void DumpModules(std::stringstream& ss)
{
	HMODULE hModules[256] = {};
	DWORD cbNeeded = 0;
	HANDLE hProcess = GetCurrentProcess();

	if (!EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded))
		return;

	DWORD nModules = cbNeeded / sizeof(HMODULE);
	if (nModules > 256) nModules = 256;

	MODULEINFO modInfo = {};
	for (DWORD i = 0; i < nModules; i++)
	{
		if (!GetModuleInformation(hProcess, hModules[i], &modInfo, sizeof(modInfo)))
			continue;

		char szName[MAX_PATH] = {};
		GetModuleBaseNameA(hProcess, hModules[i], szName, MAX_PATH);

		ss << std::format("  {:<24s} base={:#x} size={:#x}\n", szName,
			reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll), modInfo.SizeOfImage);
	}
}

// SEH-safe helpers — must be plain C functions (no C++ objects) to use __try/__except

// Read a pointer from memory safely — returns 0 if unreadable
static uintptr_t SafeReadPtr(uintptr_t addr)
{
	if (!IsReadableMemory(reinterpret_cast<const void*>(addr), sizeof(uintptr_t)))
		return 0;
	__try { return *reinterpret_cast<uintptr_t*>(addr); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Read 4 pointers safely into output array
static void SafeReadPtrs4(uintptr_t addr, uintptr_t out[4])
{
	__try {
		auto* p = reinterpret_cast<uintptr_t*>(addr);
		out[0] = IsReadableMemory(p, sizeof(uintptr_t)) ? p[0] : 0;
		out[1] = IsReadableMemory(p + 1, sizeof(uintptr_t)) ? p[1] : 0;
		out[2] = IsReadableMemory(p + 2, sizeof(uintptr_t)) ? p[2] : 0;
		out[3] = IsReadableMemory(p + 3, sizeof(uintptr_t)) ? p[3] : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		out[0] = out[1] = out[2] = out[3] = 0;
	}
}

// Dump raw stack memory near RSP
static void DumpStackMemory(std::stringstream& ss, PCONTEXT pContext)
{
	uintptr_t rsp = pContext->Rsp;
	constexpr int NUM_PTRS = 32;
	ss << "  RSP+0x00 .. RSP+0x100:\n";
	for (int i = 0; i < NUM_PTRS; i += 4)
	{
		uintptr_t vals[4] = {};
		SafeReadPtrs4(rsp + i * sizeof(uintptr_t), vals);
		if (vals[0] == 0 && vals[1] == 0 && vals[2] == 0 && vals[3] == 0 && i > 0)
			break;  // All zeros past the first row = unreadable or end of useful stack
		ss << std::format("  +{:#04x}: {:16x} {:16x} {:16x} {:16x}\n",
			i * sizeof(uintptr_t), vals[0], vals[1], vals[2], vals[3]);
	}
}

// Annotate stack pointers — check if any value on the stack points into a known module
static void AnnotateStackPointers(std::stringstream& ss, PCONTEXT pContext)
{
	uintptr_t rsp = pContext->Rsp;
	constexpr int NUM_PTRS = 32;
	int nAnnotated = 0;

	for (int i = 0; i < NUM_PTRS && nAnnotated < 8; i++)
	{
		uintptr_t val = SafeReadPtr(rsp + i * sizeof(uintptr_t));
		if (val == 0 || val < 0x10000)
			continue;

		HMODULE hModule = nullptr;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(val), &hModule))
		{
			char szName[MAX_PATH] = {};
			GetModuleBaseNameA(GetCurrentProcess(), hModule, szName, MAX_PATH);
			uintptr_t offset = val - reinterpret_cast<uintptr_t>(hModule);
			ss << std::format("  RSP+{:#04x} -> {}+{:#x}\n", i * sizeof(uintptr_t), szName, offset);
			nAnnotated++;
		}
	}
}

static LONG APIENTRY ExceptionFilter(PEXCEPTION_POINTERS ExceptionInfo)
{
	const char* sError = "UNKNOWN";
	switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
	case STATUS_ACCESS_VIOLATION: sError = "ACCESS VIOLATION"; break;
	case STATUS_STACK_OVERFLOW:   sError = "STACK OVERFLOW"; break;
	case STATUS_HEAP_CORRUPTION:  sError = "HEAP CORRUPTION"; break;
	case DBG_PRINTEXCEPTION_C:     return EXCEPTION_EXECUTE_HANDLER;
	// Benign first-chance exceptions the game/cheat handle themselves (MSVC
	// C++ throw, int3, debug prints, thread names). Running the full report
	// path (SymInitialize + StackWalk64 + MiniDumpWriteDump) for these stalled
	// the throwing thread and wrote bogus crash dumps; passing them through
	// dispatches identically to before. Same skip list as Amalgam's handler.
	case STATUS_RUNTIME_ERROR:
	case EXCEPTION_BREAKPOINT:
	case 0x4001000A: // DBG_PRINTEXCEPTION_WIDE_C
	case 0x406D1388: // DBG_THREAD_NAMING
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// Fault inside a guarded region (see s_iSuppressDepth): the local
	// __except at the call site handles it. Reporting here would pop a crash
	// dialog for a fault the cheat is deliberately swallowing every frame.
	if (CCrashHandler::s_iSuppressDepth > 0)
		return EXCEPTION_EXECUTE_HANDLER;

	if (s_mAddresses.contains(ExceptionInfo->ExceptionRecord->ExceptionAddress)
		|| s_iExceptions && GetAsyncKeyState(VK_SHIFT) & 0x8000 && GetAsyncKeyState(VK_RETURN) & 0x8000)
		return EXCEPTION_EXECUTE_HANDLER;
	s_mAddresses[ExceptionInfo->ExceptionRecord->ExceptionAddress];

	std::stringstream ss;
	ss << std::format("=== Necromancer Crash Report ===\n\n");
	ss << std::format("Error: {} (0x{:X}) (occurrence #{})\n", sError, ExceptionInfo->ExceptionRecord->ExceptionCode, ++s_iExceptions);
	ss << std::format("Built @ " __DATE__ ", " __TIME__ "\n");

	{
		auto now = std::time(nullptr);
		tm timeInfo = {};
		localtime_s(&timeInfo, &now);
		char timeStr[64];
		strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeInfo);
		ss << std::format("Time @ {}\n", timeStr);
	}

	// ACCESS_VIOLATION specifics
	if (ExceptionInfo->ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION)
	{
		const ULONG_PTR* pInfo = ExceptionInfo->ExceptionRecord->ExceptionInformation;
		const char* szOp = (pInfo[0] == 0) ? "READ" : (pInfo[0] == 1) ? "WRITE" : (pInfo[0] == 8) ? "DEP" : "UNKNOWN";
		ss << std::format("Operation: {} at {:#x}\n", szOp, pInfo[1]);

		// Annotate the faulting address — is it null? dangling? in freed memory?
		if (pInfo[1] == 0)
			ss << "  -> NULL pointer dereference\n";
		else if (pInfo[1] < 0x10000)
			ss << "  -> Near-null pointer (likely offset from null base)\n";
		else if (pInfo[1] > 0x7FFE0000 && pInfo[1] < 0x7FFFF000)
			ss << "  -> Address in kernel/shared region (possible use-after-free)\n";
		else
		{
			// Check if the address falls in a known module
			HMODULE hMod = nullptr;
			if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(pInfo[1]), &hMod))
			{
				char szName[MAX_PATH] = {};
				GetModuleBaseNameA(GetCurrentProcess(), hMod, szName, MAX_PATH);
				ss << std::format("  -> Address inside module: {}\n", szName);
			}
		}
	}

	// Exception flags
	ss << std::format("Exception flags: {:#x}  numParams: {}\n",
		ExceptionInfo->ExceptionRecord->ExceptionFlags,
		ExceptionInfo->ExceptionRecord->NumberParameters);

	// Crash location with module info
	const void* pCrashAddr = ExceptionInfo->ExceptionRecord->ExceptionAddress;
	ss << std::format("Crash at: {}\n", GetModuleOffset(const_cast<void*>(pCrashAddr)));

	// Thread info
	ss << std::format("Thread ID: {} (0x{:X})\n", GetCurrentThreadId(), GetCurrentThreadId());

	// Crash context
	{
		const auto& ctx = CCrashHandler::s_Context;
		ss << "\n--- Crash Context ---\n";
		ss << std::format("Hook: {}\n", ctx.m_pszLastHook);
		ss << std::format("Feature: {}::{}\n", ctx.m_pszLastFeature, ctx.m_pszLastSubFeature);
		ss << std::format("InGame: {} | LevelTransition: {}\n", ctx.m_bInGame, ctx.m_bLevelTransition);
		ss << std::format("Map: {}\n", ctx.m_szMapName[0] ? ctx.m_szMapName : "(unknown)");
		ss << std::format("CmdNum: {} | TargetIdx: {} | EntityIdx: {}\n", ctx.m_nCommandNumber, ctx.m_nTargetIndex, ctx.m_nEntityIndex);
	}

	// Interface health - a pointer or vtable reported outside its owning
	// module means our interface globals got corrupted/use-after-freed,
	// which turns virtual calls into jumps into random game code
	ss << "\n--- Interfaces ---\n";
	for (const InterfaceInit_t* pInterface : U::InterfaceManager->GetInterfaces())
	{
		if (!pInterface || !pInterface->m_pPtr)
			continue;

		const void* pPtr = *pInterface->m_pPtr;
		const void* pVTable = SafeDeref(pPtr);
		// a "vtable" outside any module is usually a plain struct with no
		// vtable at all (GlobalVars/ClientState read their first floats) -
		// don't flag those as corruption
		HMODULE hVTableMod = nullptr;
		const bool bVTableValid = pVTable && GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(pVTable), &hVTableMod);
		ss << std::format("{} ({}): ptr {} | vtable {}\n",
			pInterface->m_pszVersion, pInterface->m_pszDLLName,
			GetModuleOffset(const_cast<void*>(pPtr)),
			pVTable ? (bVTableValid ? GetModuleOffset(const_cast<void*>(pVTable))
				: std::format("0x{:x} (not in any module - plain struct?)", reinterpret_cast<uintptr_t>(pVTable)))
				: std::string("unreadable"));
	}

	// Registers
	ss << "\n--- Registers ---\n";
	ss << std::format("RIP: {:#x}  ({})\n", ExceptionInfo->ContextRecord->Rip, GetModuleOffset(reinterpret_cast<void*>(ExceptionInfo->ContextRecord->Rip)));
	ss << std::format("RAX: {:#x}\n", ExceptionInfo->ContextRecord->Rax);
	ss << std::format("RCX: {:#x}\n", ExceptionInfo->ContextRecord->Rcx);
	ss << std::format("RDX: {:#x}\n", ExceptionInfo->ContextRecord->Rdx);
	ss << std::format("RBX: {:#x}\n", ExceptionInfo->ContextRecord->Rbx);
	ss << std::format("RSP: {:#x}\n", ExceptionInfo->ContextRecord->Rsp);
	ss << std::format("RBP: {:#x}\n", ExceptionInfo->ContextRecord->Rbp);
	ss << std::format("RSI: {:#x}\n", ExceptionInfo->ContextRecord->Rsi);
	ss << std::format("RDI: {:#x}\n", ExceptionInfo->ContextRecord->Rdi);
	// Annotate register values that point into modules
	{
		struct { const char* name; DWORD64 val; } regs[] = {
			{"RAX", ExceptionInfo->ContextRecord->Rax},
			{"RCX", ExceptionInfo->ContextRecord->Rcx},
			{"RDX", ExceptionInfo->ContextRecord->Rdx},
			{"RBX", ExceptionInfo->ContextRecord->Rbx},
			{"RSI", ExceptionInfo->ContextRecord->Rsi},
			{"RDI", ExceptionInfo->ContextRecord->Rdi},
			{"RBP", ExceptionInfo->ContextRecord->Rbp},
		};
		for (auto& r : regs)
		{
			if (r.val == 0 || r.val < 0x10000)
				continue;
			HMODULE hMod = nullptr;
			if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(r.val), &hMod))
			{
				char szName[MAX_PATH] = {};
				GetModuleBaseNameA(GetCurrentProcess(), hMod, szName, MAX_PATH);
				uintptr_t offset = r.val - reinterpret_cast<uintptr_t>(hMod);
				ss << std::format("  {} points into {}+{:#x}\n", r.name, szName, offset);
			}
		}
	}

	// Stack trace
	ss << "\n--- Stack Trace ---\n";
	if (auto vTrace = StackTrace(ExceptionInfo->ContextRecord); !vTrace.empty())
	{
		for (int i = 0; i < static_cast<int>(vTrace.size()); i++)
		{
			Frame_t& tFrame = vTrace[i];

			ss << std::format("{}: ", i + 1);
			if (tFrame.m_uBase)
				ss << std::format("{}+{:#x}", tFrame.m_sModule, tFrame.m_uAddress - tFrame.m_uBase);
			else
				ss << std::format("{:#x}", tFrame.m_uAddress);
			if (!tFrame.m_sName.empty())
				ss << std::format(" ({})", tFrame.m_sName);
			if (!tFrame.m_sFile.empty())
				ss << std::format(" [{}:{}]", tFrame.m_sFile, tFrame.m_uLine);
			ss << "\n";
		}
	}
	else
	{
		// Fallback: RBP chain walk
		uintptr_t pFrameAddr = ExceptionInfo->ContextRecord->Rbp;
		ss << "(Symbol walk failed, trying RBP chain)\n";
		for (int i = 0; i < 16 && pFrameAddr; i++)
		{
			uintptr_t nextFrame = SafeReadPtr(pFrameAddr);
			uintptr_t retAddr = SafeReadPtr(pFrameAddr + sizeof(uintptr_t));
			if (retAddr == 0)
				break;
			ss << std::format("{}: {}\n", i + 1, GetModuleOffset(reinterpret_cast<void*>(retAddr)));
			pFrameAddr = nextFrame;
		}
	}

	// Stack memory dump with annotated pointers
	ss << "\n--- Stack Memory ---\n";
	DumpStackMemory(ss, ExceptionInfo->ContextRecord);
	AnnotateStackPointers(ss, ExceptionInfo->ContextRecord);

	// Loaded modules — needed to resolve addresses from the raw register/stack dump
	ss << "\n--- Loaded Modules ---\n";
	DumpModules(ss);

	// Write minidump
	std::string szDumpPath;
	if (WriteMinidump(ExceptionInfo))
	{
		szDumpPath = (std::filesystem::path(GetModuleDirectory()).parent_path() / "dumps").string();
	}

	ss << "\n";
	if (!szDumpPath.empty())
		ss << std::format("Minidump written to: {}\\crash_*.dmp\n", szDumpPath);

	// Write full crash log to file — MessageBox can truncate long reports
	std::string szLogPath;
	{
		std::filesystem::path logDir = std::filesystem::path(GetModuleDirectory()).parent_path() / "dumps";

		auto now = std::time(nullptr);
		tm timeInfo = {};
		localtime_s(&timeInfo, &now);
		char timeStr[32];
		strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S", &timeInfo);
		szLogPath = (logDir / std::format("crash_{}.log", timeStr)).string();

		std::filesystem::create_directories(logDir);
		FILE* fp = nullptr;
		if (fopen_s(&fp, szLogPath.c_str(), "w") == 0 && fp)
		{
			fputs(ss.str().c_str(), fp);
			fclose(fp);
		}
	}

	if (!szLogPath.empty())
		ss << std::format("Crash log written to: {}\n", szLogPath);
	ss << "Ctrl + C to copy this message.\n";

	// Show MessageBox for real crashes
	switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
	case STATUS_ACCESS_VIOLATION:
	case STATUS_STACK_OVERFLOW:
	case STATUS_HEAP_CORRUPTION:
		MessageBoxA(nullptr, ss.str().c_str(), "Necromancer - Unhandled Exception", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		break;
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

void CCrashHandler::Initialize()
{
	if (s_Initialized)
		return;

	s_pVectoredHandle = AddVectoredExceptionHandler(1, ExceptionFilter);
	s_Initialized = true;
}

void CCrashHandler::Shutdown()
{
	if (!s_Initialized)
		return;

	if (s_pVectoredHandle)
	{
		RemoveVectoredExceptionHandler(s_pVectoredHandle);
		s_pVectoredHandle = nullptr;
	}
	s_Initialized = false;
}
