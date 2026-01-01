#include "App/App.h"

static HMODULE g_hModule = nullptr;
static bool g_bInitialized = false;
static bool g_bManualMapped = false;

DWORD WINAPI MainThread(LPVOID lpParam)
{
	App->Start();

	App->Loop();

	App->Shutdown();

	Sleep(500);

	// Only call FreeLibraryAndExitThread if loaded via LoadLibrary
	// Manual mapped DLLs don't have a valid module handle
	if (!g_bManualMapped && g_hModule)
	{
		FreeLibraryAndExitThread(g_hModule, EXIT_SUCCESS);
	}

	return 0;
}

void Initialize(HMODULE hModule)
{
	if (g_bInitialized)
		return;

	g_bInitialized = true;
	g_hModule = hModule;

	// Check if we're manually mapped by testing if the module handle is valid
	// GetModuleHandleEx will fail for manually mapped DLLs
	HMODULE hTest = nullptr;
	if (!hModule || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(hModule), &hTest))
	{
		g_bManualMapped = true;
	}

	if (const auto hMainThread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr))
	{
		CloseHandle(hMainThread);
	}
}

// Export for manual map injectors
extern "C" __declspec(dllexport) void __cdecl Init()
{
	Initialize(nullptr);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hinstDLL);
		Initialize(hinstDLL);
	}

	return TRUE;
}