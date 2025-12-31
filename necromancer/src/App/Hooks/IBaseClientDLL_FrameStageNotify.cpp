#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/WorldModulation/WorldModulation.h"
#include "../Features/LagRecords/LagRecords.h"
#include "../Features/MiscVisuals/MiscVisuals.h"
#include "../Features/Crits/Crits.h"
#include "../Features/Weather/Weather.h"
#include "../Features/amalgam_port/AmalgamCompat.h"
#include "../Features/amalgam_port/Simulation/MovementSimulation/AmalgamMoveSim.h"

MAKE_HOOK(IBaseClientDLL_FrameStageNotify, Memory::GetVFunc(I::BaseClientDLL, 35), void, __fastcall,
	void* ecx, ClientFrameStage_t curStage)
{
	CALL_ORIGINAL(ecx, curStage);

	switch (curStage)
	{
		case FRAME_NET_UPDATE_START:
		{
			H::Entities->ClearCache();
			
			// Clear amalgam tracking data when not in-game to prevent memory leaks
			if (I::EngineClient && !I::EngineClient->IsInGame())
			{
				g_AmalgamEntitiesExt.Clear();
				F::MoveSim.Clear();
			}

			break;
		}

		case FRAME_NET_UPDATE_END:
		{
			H::Entities->UpdateCache();
			F::CritHack->Store(); // Store player health for crit damage tracking
			
			// Only store movement records if we're in-game
			if (I::EngineClient && I::EngineClient->IsInGame())
			{
				F::MoveSim.Store(); // Store movement records for projectile prediction
			}

			if (const auto pLocal = H::Entities->GetLocal())
			{
				for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
				{
					if (!pEntity || pEntity == pLocal)
						continue;

					const auto pPlayer = pEntity->As<C_TFPlayer>();

					if (const auto nDifference = std::clamp(TIME_TO_TICKS(pPlayer->m_flSimulationTime() - pPlayer->m_flOldSimulationTime()), 0, 22))
					{
						//deal with animations, local player is dealt with in RunCommand
						// Do manual animation updates if Disable Interp is on
						if (CFG::Misc_Accuracy_Improvements && CFG::Visuals_Disable_Interp)
						{
							const float flOldFrameTime = I::GlobalVars->frametime;

							I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.0f : TICK_INTERVAL;

							for (int n = 0; n < nDifference; n++)
							{
								G::bUpdatingAnims = true;
								pPlayer->UpdateClientSideAnimation();
								G::bUpdatingAnims = false;
							}

							I::GlobalVars->frametime = flOldFrameTime;
						}

						//add the lag record
						if (CFG::Misc_SetupBones_Optimization)
						{
							if (!pPlayer->deadflag())
							{
								F::LagRecords->AddRecord(pPlayer);
							}
						}

						else
						{
							if (pPlayer->m_iTeamNum() != pLocal->m_iTeamNum() && !pPlayer->deadflag())
							{
								F::LagRecords->AddRecord(pPlayer);
							}
						}
					}
				}
			}

			F::LagRecords->UpdateDatagram();
			F::LagRecords->UpdateRecords();

			if (G::mapVelFixRecords.size() > 64)
			{
				G::mapVelFixRecords.clear();
			}

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
			{
				if (!pEntity)
					continue;

				const auto pPlayer = pEntity->As<C_TFPlayer>();

				if (pPlayer->deadflag())
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
