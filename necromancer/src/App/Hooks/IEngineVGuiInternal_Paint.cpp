#include "../../SDK/SDK.h"

#include "../Features/Menu/Menu.h"
#include "../CheaterDatabase/CheaterDatabase.h"
#include "../Features/Chat/Chat.h"

#include "../Features/ESP/ESP.h"
#include "../Features/Radar/Radar.h"
#include "../Features/MiscVisuals/MiscVisuals.h"
#include "../Features/SpectatorList/SpectatorList.h"
#include "../Features/SpyCamera/SpyCamera.h"
#include "../Features/SpyWarning/SpyWarning.h"
#include "../Features/TeamWellBeing/TeamWellBeing.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/Aimbot/AimbotHitscan/AimbotHitscan.h"
#include "../Features/Triggerbot/AutoSapper/AutoSapper.h"

MAKE_HOOK(IEngineVGuiInternal_Paint, Memory::GetVFunc(I::EngineVGui, 14), void, __fastcall,
	void* ecx, int mode)
{
	CALL_ORIGINAL(ecx, mode);

	// Skip during level transitions
	if (G::bLevelTransition)
		return;

	if (mode & PAINT_UIPANELS)
	{
		H::Draw->UpdateW2SMatrix();

		// Process pending ban alerts on main thread
		ProcessPendingBanAlerts();
		
		// Run chat spammer
		RunChatSpammer();

		I::MatSystemSurface->StartDrawing();
		{
			// Only draw entity-dependent stuff if in game
			if (I::EngineClient && I::EngineClient->IsInGame())
			{
				F::ESP->Run();
				F::AutoSapper->DrawESP();
				F::TeamWellBeing->Run();
				F::MiscVisuals->ShiftBar();
				F::Radar->Run();
				F::SpectatorList->Run();
				F::MiscVisuals->AimbotFOVCircle();
				F::MiscVisuals->CritIndicator();
				F::AimbotHitscan->DrawSwitchIndicator();
				F::SpyCamera->Run();
				F::SpyWarning->Run();
				F::SeedPred->Paint();
			}
			F::Menu->Run();
		}
		I::MatSystemSurface->FinishDrawing();
	}
}
