#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/WorldModulation/WorldModulation.h"
#include "../Features/LagRecords/LagRecords.h"
#include "../Features/MovementSimulation/MovementSimulation.h"
#include "../Features/MiscVisuals/MiscVisuals.h"
#include "../Features/Crits/Crits.h"
#include "../Features/Weather/Weather.h"
#include "../Features/amalgam_port/AmalgamCompat.h"

MAKE_HOOK(IBaseClientDLL_FrameStageNotify, Memory::GetVFunc(I::BaseClientDLL, 35), void, __fastcall,
	void* ecx, ClientFrameStage_t curStage)
{
	CALL_ORIGINAL(ecx, curStage);

	// Skip ALL processing during level transitions
	if (G::bLevelTransition)
		return;

	// Check if we're in game for entity-related operations
	const bool bInGame = I::EngineClient && I::EngineClient->IsInGame();

	switch (curStage)
	{
		case FRAME_NET_UPDATE_START:
		{
			if (bInGame)
				H::Entities->ClearCache();
			
			// Clear amalgam tracking data when not in-game to prevent memory leaks
			if (!bInGame)
			{
				g_AmalgamEntitiesExt.Clear();
			}

			break;
		}

		case FRAME_NET_UPDATE_END:
		{
			// Skip entity processing if not in game
			if (!bInGame)
				break;
				
			H::Entities->UpdateCache();
			F::CritHack->Store(); // Store player health for crit damage tracking

			const auto pLocal = H::Entities->GetLocal();
			if (!pLocal)
				break;

			// Cache local team and config values for faster comparisons
			const int nLocalTeam = pLocal->m_iTeamNum();
			const bool bSetupBonesOpt = CFG::Misc_SetupBones_Optimization;
			const bool bDisableInterp = CFG::Visuals_Disable_Interp;
			const bool bAccuracyImprovements = CFG::Misc_Accuracy_Improvements;
			const bool bDoAnimUpdates = bAccuracyImprovements && bDisableInterp;
			
			// Pre-calculate frametime once if needed
			const float flAnimFrameTime = I::Prediction->m_bEnginePaused ? 0.0f : TICK_INTERVAL;

			// Get the group once and iterate - use reference to avoid copy
			const auto& vPlayers = H::Entities->GetGroup(EEntGroup::PLAYERS_ALL);
			const size_t nPlayerCount = vPlayers.size();
			
			for (size_t i = 0; i < nPlayerCount; i++)
			{
				const auto pEntity = vPlayers[i];
				if (!pEntity || pEntity == pLocal)
					continue;

				const auto pPlayer = pEntity->As<C_TFPlayer>();
				if (!pPlayer)
					continue;

				// Cache deadflag check - used multiple times
				const bool bIsDead = pPlayer->deadflag();
				
				const int nDifference = std::clamp(TIME_TO_TICKS(pPlayer->m_flSimulationTime() - pPlayer->m_flOldSimulationTime()), 0, 22);
				if (nDifference > 0)
				{
					// Do manual animation updates if Disable Interp is on
					if (bDoAnimUpdates)
					{
						const float flOldFrameTime = I::GlobalVars->frametime;
						I::GlobalVars->frametime = flAnimFrameTime;

						G::bUpdatingAnims = true;
						for (int n = 0; n < nDifference; n++)
							pPlayer->UpdateClientSideAnimation();
						G::bUpdatingAnims = false;

						I::GlobalVars->frametime = flOldFrameTime;
					}

					// Add lag record - only for enemies unless SetupBones optimization is on
					if (!bIsDead)
					{
						if (bSetupBonesOpt || pPlayer->m_iTeamNum() != nLocalTeam)
							F::LagRecords->AddRecord(pPlayer);
					}
				}
			}

			F::LagRecords->UpdateDatagram();
			F::LagRecords->UpdateRecords();
			F::MovementSimulation->Store(); // Store movement records for strafe prediction

			// Clear velocity fix records if too large (prevent memory growth)
			if (G::mapVelFixRecords.size() > 64)
				G::mapVelFixRecords.clear();

			// Reuse the same player group we already fetched
			for (size_t i = 0; i < nPlayerCount; i++)
			{
				const auto pEntity = vPlayers[i];
				if (!pEntity)
					continue;

				const auto pPlayer = pEntity->As<C_TFPlayer>();
				if (!pPlayer || pPlayer->deadflag())
					continue;

				G::mapVelFixRecords[pPlayer] = { pPlayer->m_vecOrigin(), pPlayer->m_fFlags(), pPlayer->m_flSimulationTime() };
			}

			break;
		}

		case FRAME_RENDER_START:
		{
			H::Input->Update();

			F::WorldModulation->UpdateWorldModulation();
			F::MiscVisuals->ViewModelSway();
			F::MiscVisuals->DetailProps();
			F::Weather->Rain();

			// Skip entity-dependent stuff if not in game
			if (!bInGame)
				break;

			//fake taunt stuff
			{
				static bool bWasEnabled = false;

				if (CFG::Misc_Fake_Taunt)
				{
					bWasEnabled = true;

					if (G::bStartedFakeTaunt)
					{
						if (const auto pLocal = H::Entities->GetLocal())
						{
							if (const auto pAnimState = pLocal->GetAnimState())
							{
								const auto& gs = pAnimState->m_aGestureSlots[GESTURE_SLOT_VCD];

								if (gs.m_pAnimLayer && (gs.m_pAnimLayer->m_flCycle >= 1.0f || gs.m_pAnimLayer->m_nSequence <= 0))
								{
									G::bStartedFakeTaunt = false;
									pLocal->m_nForceTauntCam() = 0;
								}
							}
						}
					}
				}
				else
				{
					G::bStartedFakeTaunt = false;

					if (bWasEnabled)
					{
						bWasEnabled = false;

						if (const auto pLocal = H::Entities->GetLocal())
						{
							pLocal->m_nForceTauntCam() = 0;
						}
					}
				}
			}

			break;
		}

		default: break;
	}
}
