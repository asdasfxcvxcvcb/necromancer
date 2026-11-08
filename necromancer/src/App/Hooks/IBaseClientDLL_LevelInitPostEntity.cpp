#include "../../SDK/SDK.h"

#include "../Features/Players/Players.h"
#include "../Features/CFG.h"
#include "../Features/NetworkFix/NetworkFix.h"
#include "../Features/MiscVisuals/MiscVisuals.h"
#include "../Features/Models/Models.h"
#include "../Features/ChatESP/ChatESP.h"
#include "../Features/NavBot/NavEngine/NavEngine.h"
#include "../Features/NavBot/NavBot.h"

MAKE_HOOK(IBaseClientDLL_LevelInitPostEntity, Memory::GetVFunc(I::BaseClientDLL, 6), void, __fastcall,
	void* ecx)
{
	CALL_ORIGINAL(ecx);

	// Clear level transition flag - entities are now valid
	G::bLevelTransition = false;

	H::Entities->UpdateModelIndexes();
	H::Entities->ForceRefreshPlayerInfo(); // Force refresh F2P and party info on new server
	
	// Reset ping reducer rates on map change
	F::NetworkFix->ResetRates();

	F::Models->OnLevelInit();

	// Reapply aspect ratio on level change
	F::MiscVisuals->ReapplyAspectRatio();
	
	// Clear chat bubbles from previous map
	F::ChatESP->OnLevelInit();

	// Reset NavEngine on map change (reloads nav mesh for new map)
	G_NavEngine.Reset(true);

	// Full NavBot state scrub on map change: curtime restarts at 0, so any
	// absolute-time throttle from the old map (defend rescan, escape cooldown,
	// objective scan...) blocks every goal for the whole new map — the "bot is
	// dead until re-toggled" bug (toggling called Stop, which is what healed it)
	g_NavBot.Stop();

	// Reset NavBot auto-join timers so they don't carry stale values from previous session
	g_NavBot.ResetAutoJoinTimers();  // Reset so auto-join fires immediately after reconnect

	if (CFG::Visuals_Chat_Player_List_Info)
	{
		for (int n{}; n < I::EngineClient->GetMaxClients() + 1; n++)
		{
			PlayerPriority pi{};

			if (F::Players->GetInfo(n, pi))
			{
				player_info_t pi_game{};

				if (!I::EngineClient->GetPlayerInfo(n, &pi_game))
				{
					continue;
				}

				// every tag announces on level init, in the same priority order
				// the ESP name/tag colors use
				if (pi.Cheater)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Cheater]", pi_game.name, CFG::Color_Cheater.toHexStr()).c_str());
				}

				if (pi.Targeted)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Targeted]", pi_game.name, CFG::Color_Targeted.toHexStr()).c_str());
				}

				if (pi.Nigger)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Nigger]", pi_game.name, CFG::Color_Nigger.toHexStr()).c_str());
				}

				if (pi.RetardLegit)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Retard Legit]", pi_game.name, CFG::Color_RetardLegit.toHexStr()).c_str());
				}

				if (pi.Streamer)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Streamer]", pi_game.name, CFG::Color_Streamer.toHexStr()).c_str());
				}

				if (pi.Ignored)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Ignored]", pi_game.name, CFG::Color_Friend.toHexStr()).c_str());
				}
			}
		}
	}
}
