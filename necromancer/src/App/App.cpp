#include "App.h"

#include "Hooks/WINAPI_WndProc.h"

#include "Features/Materials/Materials.h"
#include "Features/Outlines/Outlines.h"
#include "Features/TF2Glow/TF2Glow.h"
#include "Features/WorldModulation/WorldModulation.h"
#include "Features/Paint/Paint.h"
#include "Features/Menu/Menu.h"
#include "Features/Players/Players.h"
#include "Features/Misc/IgnoreVAC/IgnoreVAC.h"
#include "Features/Misc/Soundpad/Soundpad.h"
#include "Features/Weather/Weather.h"
#include "Features/Models/Models.h"
#include "Features/NecroNet/NecroNet.h"
#include "CheaterDatabase/CheaterDatabase.h"
#include "../Utils/CrashHandler/CrashHandler.h"

#include "Features/CFG.h"

// Captures the user's original game settings before we override them
// This way new users get their own settings as defaults instead of forced values
static void CaptureOriginalGameSettings()
{
	// Capture FOV from convar (fov_desired is the user's preferred FOV)
	if (const auto fov_desired = I::CVar->FindVar("fov_desired"))
	{
		float originalFov = fov_desired->GetFloat();
		if (originalFov >= 70.0f && originalFov <= 140.0f)
		{
			CFG::Visuals_FOV_Override = originalFov;
		}
	}

	// Capture minimal viewmodel setting
	if (const auto tf_use_min_viewmodels = I::CVar->FindVar("tf_use_min_viewmodels"))
	{
		CFG::Visuals_ViewModel_Minimal = (tf_use_min_viewmodels->GetInt() != 0);
	}

	// Capture flip viewmodel setting
	if (const auto cl_flipviewmodels = I::CVar->FindVar("cl_flipviewmodels"))
	{
		CFG::Visuals_Viewmodel_Flip = (cl_flipviewmodels->GetInt() != 0);
	}

	// Capture world model viewmodel setting
	if (const auto cl_first_person_uses_world_model = I::CVar->FindVar("cl_first_person_uses_world_model"))
	{
		CFG::Visuals_ViewModel_WorldModel = (cl_first_person_uses_world_model->GetInt() != 0);
	}
}

void CApp::Start()
{
	// Initialize crash handler first to catch any crashes during initialization
	F::CrashHandler->Initialize();

	while (!Memory::FindSignature("client.dll", "48 8B 0D ? ? ? ? 48 8B 10 48 8B 19 48 8B C8 FF 92"))
	{
		bUnload = GetAsyncKeyState(VK_F11) & 0x8000;
		if (bUnload)
			return;

		Sleep(500);
	}

	U::Storage->Init("Necromancer");
	U::SignatureManager->InitializeAllSignatures();
	U::InterfaceManager->InitializeAllInterfaces();

	// Validate critical interfaces - abort gracefully instead of crashing on null dereference
	{
		const char* missingInterfaces = nullptr;
		if (!I::EngineClient)     missingInterfaces = "IVEngineClient (engine.dll)";
		else if (!I::BaseClientDLL) missingInterfaces = "IBaseClientDLL (client.dll)";
		else if (!I::CVar)        missingInterfaces = "ICvar (vstdlib.dll)";
		else if (!I::ClientEntityList) missingInterfaces = "IClientEntityList (client.dll)";
		else if (!I::MatSystemSurface) missingInterfaces = "IMatSystemSurface (vguimatsurface.dll)";
		else if (!I::MaterialSystem)  missingInterfaces = "IMaterialSystem (materialsystem.dll)";

		if (missingInterfaces)
		{
			MessageBoxA(nullptr,
				std::format("Failed to initialize critical interface: {}\n\n"
					"Make sure you are injected into the TF2 process.\n"
					"Press OK to unload.", missingInterfaces).c_str(),
				"Necromancer - Critical Error", MB_OK | MB_ICONERROR);
			bUnload = true;
			return;
		}
	}
	
	// Initialize TFPartyClient (for auto-queue feature)
	// The signature points to a function that loads a global pointer via RIP-relative addressing
	// We need to resolve the relative address to get the actual pointer location
	if (Signatures::Get_TFPartyClient.Get())
	{
		// The signature is: mov rax, [rip+offset]; ret
		// We need to resolve the RIP-relative address to get the pointer to the global
		auto dwTFPartyClient = Memory::RelToAbs(Signatures::Get_TFPartyClient.Get());
		if (dwTFPartyClient)
		{
			I::TFPartyClient = *reinterpret_cast<CTFPartyClient**>(dwTFPartyClient);
		}
	}

	RefreshSteamSession();

	// Initialize SteamNetworkingUtils for region selector
	if (Signatures::Get_SteamNetworkingUtils.Get())
	{
		// The signature points to a function that takes a pointer to store the interface
		// Same pattern as other software: S::Get_SteamNetworkingUtils.Call<ISteamNetworkingUtils*>(&I::SteamNetworkingUtils);
		Signatures::Get_SteamNetworkingUtils.Call<ISteamNetworkingUtils*>(&I::SteamNetworkingUtils);
	}

	H::Draw->UpdateScreenSize();
	
	H::Fonts->Reload();

	if (I::EngineClient->IsInGame() && I::EngineClient->IsConnected())
	{
		H::Entities->UpdateModelIndexes();
	}
	
	U::HookManager->InitializeAllHooks();

	Hooks::WINAPI_WndProc::Init();

	F::Players->Parse();
	F::Players->ImportLegacyPlayers(); // Auto-import players.json from old seonwdde folder
	F::Players->ParseStats(); // Pre-load stats at injection so the first kill doesn't parse megabytes mid-game

	// Initialize cheater database worker thread
	InitCheaterDatabase();

	F::NecroNet->Init();

	F::Models->Init();

	// Capture user's original game settings
	// This sets the CFG values to match the user's current game settings
	CaptureOriginalGameSettings();

	// Enable first-person tracers by default
	if (const auto r_drawtracers_firstperson = I::CVar->FindVar("r_drawtracers_firstperson"))
	{
		r_drawtracers_firstperson->SetValue(1);
	}

	// Set mat_queue_mode to 2 for better performance
	if (const auto mat_queue_mode = I::CVar->FindVar("mat_queue_mode"))
	{
		mat_queue_mode->SetValue(2);
	}

	const auto month = []
	{
		const std::time_t t = std::time(nullptr);
		tm Time = {};
		localtime_s(&Time, &t);

		return Time.tm_mon + 1;
	}();

	Color_t msgColor = { 197, 108, 240, 255 };
	if (month == 10)
	{
		I::MatSystemSurface->PlaySound("vo\\halloween_boss\\knight_alert.mp3");
		msgColor = { 247, 136, 18, 255 };
	}
	else if (month == 12 || month == 1 || month == 2)
	{
		if (month == 12)
		{
			I::MatSystemSurface->PlaySound("misc\\jingle_bells\\jingle_bells_nm_04.wav");
		}

		msgColor = { 28, 179, 210, 255 };
	}

	I::CVar->ConsoleColorPrintf(msgColor, "[Necromancer] loaded and bloated! haha just kidding, anyways this cheat was made by blizzman and G-man, enjoy!\n");
	// Build stamp: the console line to compare against the source you compiled.
	// If this timestamp doesn't match your latest build, the injected DLL is a
	// stale copy (e.g. an old file in the injection folder), not the new build.
	I::CVar->ConsoleColorPrintf(msgColor, "[Necromancer] build: %s %s\n", __DATE__, __TIME__);
}

void CApp::Loop()
{
	while (true)
	{
		bool bShouldUnload = GetAsyncKeyState(VK_F11) & 0x8000 && SDKUtils::IsGameWindowInFocus() || bUnload;
		if (bShouldUnload)
			break;

		Sleep(50);
	}
}

void CApp::Shutdown()
{
	if (!bUnload)
	{
		U::HookManager->FreeAllHooks();

		Hooks::WINAPI_WndProc::Release();

		Sleep(150);

		// Shutdown cheater database worker thread
		ShutdownCheaterDatabase();

		F::Players->ShutdownStatsWriter();

		F::NecroNet->Shutdown();

		F::Soundpad->Shutdown();

		// Disable weather before unloading
		CFG::Visuals_Weather = 0;
		F::Weather->Rain();

		// Disable all EXTREME performance options so FrameStageNotify
		// restores user's original convar values before we free hooks
		CFG::Perf_Extreme_Skip_All_Visuals = false;
		CFG::Perf_Extreme_Skip_LagRecords_Teammates = false;
		CFG::Perf_Extreme_Skip_Anim_Updates = false;
		CFG::Perf_Extreme_Skip_MovementSimulation = false;
		CFG::Perf_Extreme_Skip_VelFix = false;
		CFG::Perf_Extreme_Skip_Outlines = false;
		CFG::Perf_Extreme_Skip_ESP = false;
		CFG::Perf_Extreme_Limit_Entity_Cache = false;
		CFG::Perf_Extreme_Skip_World_Render = false;
		CFG::Perf_Extreme_Skip_Shadows = false;
		CFG::Perf_Extreme_Skip_Particles = false;
		CFG::Perf_Extreme_Skip_Decals = false;
		CFG::Perf_Extreme_Skip_World_Textures = false;
		CFG::Perf_Extreme_Skip_Unused_Entities = false;
		CFG::Perf_Extreme_Skip_Sound = false;
		CFG::Perf_Extreme_Low_Textures = false;
		CFG::Perf_Extreme_Minimal_Render = false;
		CFG::Perf_Extreme_FPS_Limit = 0;

		U::HookManager->DestroyAllHooks();

		Sleep(250);

		F::Materials->CleanUp();
		F::Outlines->CleanUp();
		F::TF2Glow->CleanUp();
		F::Paint->CleanUp();

		F::WorldModulation->RestoreWorldModulation();

		// Restore convars to user's original values (not engine defaults!)
		// This uses the backup system from FrameStageNotify which captured
		// the user's actual settings before we modified them.
		extern void RestoreConvarBackups();
		if (I::CVar)
			RestoreConvarBackups();

		if (const auto cl_wpn_sway_interp{ I::CVar->FindVar("cl_wpn_sway_interp") })
		{
			cl_wpn_sway_interp->SetValue(0.0f);
		}

		if (F::Menu->IsOpen())
		{
			I::MatSystemSurface->SetCursorAlwaysVisible(false);
		}
	}
	
	I::CVar->ConsoleColorPrintf({ 255, 70, 70, 255 }, "[Necromancer] Unloaded, enjoy being a retarded legit!\n");

	// Shutdown crash handler last
	F::CrashHandler->Shutdown();
}
