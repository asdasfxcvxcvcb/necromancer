#pragma once

#include <Windows.h>

#include <cstdint>

typedef unsigned __int64 QWORD;

namespace Memory
{
	std::uintptr_t FindSignature(const char *szModule, const char *szPattern);
	PVOID FindInterface(const char *szModule, const char *szObject);

	inline void* GetVFunc(void* instance, size_t index)
	{
		const auto vtable = *static_cast<void***>(instance);
		return vtable[index];
	}

	// True when pPtr (and with bCheckVTable, the vtable it points at) lives
	// inside the named module's image. Virtual calls through an interface
	// whose pointer or vtable escaped its module AV in random game code, so
	// hot-path callers verify first (crash_20260928_170007: SetViewAngles
	// called with an engine pointer whose "vtable" pointed at heap memory)
	// inner walker: the base is resolved once by IsInModule so the vtable
	// recursion doesn't re-run GetModuleHandleA and re-parse the PE headers
	inline bool IsInModuleWithBase(const void *pPtr, const std::uintptr_t dwBase, const bool bCheckVTable)
	{
		if (!pPtr || !dwBase)
			return false;

		const auto pDos = reinterpret_cast<const IMAGE_DOS_HEADER *>(dwBase);
		if (pDos->e_magic != IMAGE_DOS_SIGNATURE)
			return false;

		const auto pNt = reinterpret_cast<const IMAGE_NT_HEADERS *>(dwBase + pDos->e_lfanew);
		if (pNt->Signature != IMAGE_NT_SIGNATURE)
			return false;

		const auto dwPtr = reinterpret_cast<std::uintptr_t>(pPtr);
		if (dwPtr < dwBase || dwPtr >= dwBase + pNt->OptionalHeader.SizeOfImage)
			return false;

		return !bCheckVTable || IsInModuleWithBase(*static_cast<const void *const *>(pPtr), dwBase, false);
	}

	inline bool IsInModule(const char *szModule, const void *pPtr, const bool bCheckVTable = false)
	{
		if (!pPtr)
			return false;

		const auto dwBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(szModule));
		return IsInModuleWithBase(pPtr, dwBase, bCheckVTable);
	}

	// True when [pAddr, pAddr + uSize) is committed readable memory. Never
	// faults itself - use before dereferencing pointers that can be stale or
	// come from drifted struct offsets.
	inline bool IsReadableMemory(const void *pAddr, const size_t uSize)
	{
		if (!pAddr || uSize > 0x10000)
			return false;

		const auto dwAddr = reinterpret_cast<std::uintptr_t>(pAddr);
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!VirtualQuery(pAddr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
			return false;
		if (mbi.Protect & PAGE_GUARD)
			return false;

		constexpr DWORD dwReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
			| PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		if (!(mbi.Protect & dwReadable))
			return false;

		const auto dwRegionEnd = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		return dwAddr + uSize <= dwRegionEnd;
	}

	inline std::uintptr_t RelToAbs(const std::uintptr_t address)
	{
		return *reinterpret_cast<std::int32_t*>(address + 0x3) + address + 0x7;
	}

	inline void**& get_vtable(void* inst, const unsigned int offset)
	{
		return *reinterpret_cast<void***>(reinterpret_cast<uintptr_t>(inst) + offset);
	}

	inline const void** get_vtable(const void* inst, const unsigned int offset)
	{
		return *reinterpret_cast<const void***>(reinterpret_cast<uintptr_t>(inst) + offset);
	}

	inline QWORD rel2abs(QWORD Address, QWORD RVAOffset)
	{
		return *reinterpret_cast<int32_t*>(Address + RVAOffset) + Address + (RVAOffset + sizeof(int32_t));
	}

	template<typename T>
	inline T get_vfunc(void* inst, const unsigned int index, const unsigned int offset = 0u) {
		return reinterpret_cast<T>(get_vtable(inst, offset)[index]);
	}
}