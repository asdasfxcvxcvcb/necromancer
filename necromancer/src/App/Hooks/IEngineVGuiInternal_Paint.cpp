#include "../../SDK/SDK.h"

#include "../Features/Menu/Menu.h"
#include "../CheaterDatabase/CheaterDatabase.h"
#include "../Features/Chat/Chat.h"
#include "../Features/Chat/AutoMathSolver.h"
#include "../Features/CFG.h"

#include "../Features/ESP/ESP.h"
#include "../Features/MoveSimDebug/MoveSimDebug.h"
#include "../Features/Radar/Radar.h"
#include "../Features/MiscVisuals/MiscVisuals.h"
#include "../Features/SpectatorList/SpectatorList.h"
#include "../Features/StatusWindow/StatusWindow.h"
#include "../Features/SpyCamera/SpyCamera.h"
#include "../Features/SpyWarning/SpyWarning.h"
#include "../Features/TeamWellBeing/TeamWellBeing.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/Aimbot/AimbotHitscan/AimbotHitscan.h"
#include "../Features/Triggerbot/AutoSapper/AutoSapper.h"
#include "../Features/ChatESP/ChatESP.h"
#include "../Features/NavBot/NavBot.h"
#include "../Features/Misc/IgnoreVAC/IgnoreVAC.h"
#include "../Features/Misc/Soundpad/Soundpad.h"
#include "../Features/NecroNet/NecroNet.h"
#include "../Features/AutoVote/AutoVote.h"
#include "../Features/AutoQueue/AutoQueue.h"
#include "../../Utils/CrashHandler/CrashHandler.h"

namespace
{
	// The ESP/menu draw span. A commit-space exhaustion (pagefile can't grow,
	// disk full) makes VirtualAlloc fail inside materialsystem's queued-render
	// allocator, which writes through the null result — ACCESS VIOLATION WRITE
	// at 0x18 during the ESP paint (crash on cp_dustbowl 2026-10-13). The fault
	// is transient: swallow it and drop the rest of this frame's draw instead
	// of taking the game down; the next frame retries.
	// Static free function with no C++ locals: required for __try/__except
	// (C2712) — same pattern as RunOutlinePassesGuarded.
	void RunPaintDrawGuarded()
	{
		CCrashHandler::s_iSuppressDepth++;
		__try
		{
			// Only draw entity-dependent stuff if in game
			if (I::EngineClient && I::EngineClient->IsInGame())
			{
				// EXTREME: Skip ESP entirely — biggest rendering cost
				if (!CFG::Perf_Extreme_Skip_ESP && !CFG::Perf_Extreme_Skip_All_Visuals)
				{
					F::ESP->Run();
					F::MoveSimDebug->Draw();
					F::AutoSapper->DrawESP();
					F::TeamWellBeing->Run();
					F::Radar->Run();
					F::SpectatorList->Run();
					F::StatusWindow->Run();
					F::MiscVisuals->AimbotFOVCircle();
					F::MiscVisuals->CritIndicator();
					F::MiscVisuals->ShiftBar();
					F::AimbotHitscan->DrawSwitchIndicator();
					F::SpyCamera->Run();
					F::SpyWarning->Run();
					F::SeedPred->Paint();
					F::ChatESP->Think();
					F::ChatESP->Draw();
				}
				g_NavBot.Draw();
			}
			F::NecroNet->Run();
			F::Menu->Run();
			F::Soundpad->Draw();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			OutputDebugStringA("[Necromancer] paint draw faulted (likely out of commit), skipping the rest of this frame's draw\n");
		}
		CCrashHandler::s_iSuppressDepth--;
	}
}

MAKE_HOOK(IEngineVGuiInternal_Paint, Memory::GetVFunc(I::EngineVGui, 14), void, __fastcall,
	void* ecx, int mode)
{
	CALL_ORIGINAL(ecx, mode);

	if (mode & PAINT_UIPANELS)
	{
		{
			H::Draw->UpdateW2SMatrix();

			ProcessPendingBanAlerts();

			{
				static float flLastSteamSessionCheck = 0.0f;
				const float flNow = I::EngineClient->Time();
				if (flNow - flLastSteamSessionCheck > 2.0f)
				{
					flLastSteamSessionCheck = flNow;
					RefreshSteamSession();
				}
			}

			RunChatSpammer();

			ProcessMathAnswerQueue();

			F::IgnoreVAC->Run();

			F::Soundpad->Run();

			F::AutoVote->Tick();

			F::AutoQueue->Run();
		}

		I::MatSystemSurface->StartDrawing();
		{
			RunPaintDrawGuarded();
		}
		I::MatSystemSurface->FinishDrawing();
	}
}
