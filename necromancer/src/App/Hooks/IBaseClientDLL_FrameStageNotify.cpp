#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/WorldModulation/WorldModulation.h"
#include "../Features/LagRecords/LagRecords.h"
#include "../Features/MovementSimulation/MovementSimulation.h"
#include "../Features/MiscVisuals/MiscVisuals.h"
#include "../Features/Crits/Crits.h"
#include "../Features/Weather/Weather.h"
#include "../Features/amalgam_port/AmalgamCompat.h"
#include "../Features/amalgam_port/MoveSim/MoveSimAmalgam.h"
#include "../Features/NavBot/NavBot.h"
#include "../Features/Misc/SkinChanger/SkinChanger.h"
#include "../../Utils/CrashHandler/CrashHandler.h"

// SEH-safe entity validation — on high-player-count servers (40+), entity slots can be
// recycled mid-frame. GetClientEntity(n) returns a non-null pointer, but the entity's
// vtable/internal state is corrupted (RCX=0x0 crash on any virtual call). This helper
// catches ACCESS_VIOLATION during validation and returns null.
// Must be a plain function (no C++ objects with destructors) for __try/__except.
static C_TFPlayer* SafeGetPlayerEntity(int nIndex)
{
	__try {
		auto pClientEntity = I::ClientEntityList->GetClientEntity(nIndex);
		if (!pClientEntity || pClientEntity->IsDormant())
			return nullptr;

		auto pNetworkable = pClientEntity->GetClientNetworkable();
		if (!pNetworkable || !pNetworkable->GetClientClass())
			return nullptr;

		if (pNetworkable->GetClientClass()->m_ClassID != static_cast<int>(ETFClassIds::CTFPlayer))
			return nullptr;

		auto pPlayer = pClientEntity->As<C_TFPlayer>();
		if (!pPlayer)
			return nullptr;

		// Quick liveness check — if the entity is dead, skip it early.
		// This also validates that basic netvar access works on the entity.
		if (pPlayer->m_lifeState() != LIFE_ALIVE && pPlayer->deadflag())
			return nullptr;

		return pPlayer;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

// SEH-safe animation update — UpdateClientSideAnimation is a virtual call that
// can crash on corrupted entities. Must be plain function for __try/__except.
static void SafeUpdateClientSideAnimation(C_TFPlayer* pPlayer, int nIterations)
{
	__try {
		for (int j = 0; j < nIterations; j++)
			pPlayer->UpdateClientSideAnimation();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		// Silently skip corrupted entity animation update
	}
}

// Convar backup system: file-scope so RestoreConvarBackups() can access it.
namespace ConvarBackup
{
	enum class ConvarId
	{
		DrawWorld,
		DrawSkybox,
		Shadows,
		ShadowDraw,
		DrawParticles,
		DrawDecals,
		DecalCullSize,
		DrawBrushModels,
		DrawDisp,
		DrawRopes,
		FPSMax,
		FillMode,
		DrawEntities,
		Volume,
		SndPitchQuality,
		Picmip,
		Count
	};

	struct CachedConvar
	{
		const char* name = "";
		ConVar* var = nullptr;
		bool lookedUp = false;
		bool restoreFloat = false;
	};

	struct Entry
	{
		ConvarId id = ConvarId::Count;
		int origInt = 0;
		float origFloat = 0.0f;
		bool saved = false;
	};

	static CachedConvar s_Convars[static_cast<int>(ConvarId::Count)] =
	{
		{ "r_drawworld" },
		{ "r_drawskybox" },
		{ "r_shadows" },
		{ "r_shadowdraw" },
		{ "r_drawparticles" },
		{ "r_drawdecals" },
		{ "r_decal_cullsize", nullptr, false, true },
		{ "r_drawbrushmodels" },
		{ "r_drawdisp" },
		{ "r_drawropes" },
		{ "fps_max", nullptr, false, true },
		{ "mat_fillmode", nullptr, false, true },
		{ "r_drawentities" },
		{ "volume", nullptr, false, true },
		{ "snd_pitchquality" },
		{ "mat_picmip", nullptr, false, true }
	};

	static std::vector<Entry> s_vecBackups;
	static bool s_bCurrentlyOverriding = false;

	static ConVar* GetConvar(const ConvarId id)
	{
		if (id == ConvarId::Count)
			return nullptr;

		auto& cached = s_Convars[static_cast<int>(id)];
		if (!cached.lookedUp)
		{
			if (!I::CVar)
				return nullptr;

			cached.var = I::CVar->FindVar(cached.name);
			cached.lookedUp = true;
		}

		return cached.var;
	}

	static Entry SaveConvar(const ConvarId id)
	{
		Entry backup = { id, 1, 0.0f, false };
		if (const auto pVar = GetConvar(id))
		{
			backup.origInt = pVar->GetInt();
			backup.origFloat = pVar->GetFloat();
			backup.saved = true;
		}
		return backup;
	}

	static void SetConvarInt(const ConvarId id, const int value)
	{
		if (const auto pVar = GetConvar(id); pVar && pVar->GetInt() != value)
			pVar->SetValue(value);
	}

	static void SetConvarFloat(const ConvarId id, const float value)
	{
		if (const auto pVar = GetConvar(id); pVar && pVar->GetFloat() != value)
			pVar->SetValue(value);
	}

	static void RestoreConvar(const Entry& backup)
	{
		if (!backup.saved)
			return;

		if (const auto pVar = GetConvar(backup.id))
		{
			if (s_Convars[static_cast<int>(backup.id)].restoreFloat)
				pVar->SetValue(backup.origFloat);
			else
				pVar->SetValue(backup.origInt);
		}
	}
}

// Called from App::Shutdown to restore user's original convar values before unloading
void RestoreConvarBackups()
{
	for (auto& bk : ConvarBackup::s_vecBackups)
		ConvarBackup::RestoreConvar(bk);

	// Restore fps_max min clamp
	if (const auto pFPSMax = ConvarBackup::GetConvar(ConvarBackup::ConvarId::FPSMax); pFPSMax && pFPSMax->m_pParent)
		pFPSMax->m_pParent->m_bHasMin = true;

	ConvarBackup::s_vecBackups.clear();
	ConvarBackup::s_bCurrentlyOverriding = false;
}

MAKE_HOOK(IBaseClientDLL_FrameStageNotify, Memory::GetVFunc(I::BaseClientDLL, 35), void, __fastcall,
	void* ecx, ClientFrameStage_t curStage)
{
	CALL_ORIGINAL(ecx, curStage);

	// Skip ALL processing during level transitions
	if (G::bLevelTransition)
		return;

	// Rejoin on kick - execute retry immediately when flag is set
	if (G::bRejoinOnKickPending)
	{
		G::bRejoinOnKickPending = false;
		g_NavBot.ResetAutoJoinTimers();  // Reset so auto-join fires immediately after reconnect
		I::EngineClient->ClientCmd_Unrestricted("retry");
	}

	// Crash context: track which hook is running
	F::CrashHandler->s_Context.m_pszLastHook = "FrameStageNotify";
	// Reset feature tags per hook entry (see CreateMove - stale tags misattributed
	// a CreateMove crash to LagRecords::UpdateRecords in a prior crash report)
	F::CrashHandler->s_Context.m_pszLastFeature = "none";
	F::CrashHandler->s_Context.m_pszLastSubFeature = "none";
	F::CrashHandler->s_Context.m_nEntityIndex = -1;
	F::CrashHandler->s_Context.m_bLevelTransition = G::bLevelTransition;

	// Check if we're in game for entity-related operations
	const bool bInGame = I::EngineClient && I::EngineClient->IsInGame();
	F::CrashHandler->s_Context.m_bInGame = bInGame;

	// Track map name for crash context - this runs for every frame stage
	// (~7x per frame), so compare in place instead of building a heap
	// std::string per stage just to detect map changes
	if (bInGame && I::EngineClient)
	{
		static char s_szLastMap[MAX_PATH] = {};
		const char* pszMap = I::EngineClient->GetLevelName();
		if (pszMap && *pszMap)
		{
			if (strcmp(pszMap, s_szLastMap) != 0)
			{
				strncpy_s(s_szLastMap, pszMap, _TRUNCATE);
				strncpy_s(F::CrashHandler->s_Context.m_szMapName, pszMap, _TRUNCATE);
			}
		}
	}

	switch (curStage)
	{
		case FRAME_NET_UPDATE_START:
		{
			if (bInGame)
			{
				H::Entities->ClearCache();

				// Purge stale vel fix records (entity index may now point to a different entity after class change)
				for (int i = 0; i < G::MAX_VELFIX_SLOTS; i++)
				{
					if (!G::mapVelFixRecords[i].m_bActive)
						continue;
					const auto pEntity = I::ClientEntityList->GetClientEntity(i);
					if (!pEntity || pEntity->entindex() != i)
						G::mapVelFixRecords[i].m_bActive = false;
				}
			}
			
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
			F::SkinChanger->FrameStageNotify();

			const auto pLocal = H::Entities->GetLocal();
			if (!pLocal)
				break;

			// Skip lag record processing on the first frame after map load when fake latency is active.
			// With fake latency, the server backtracks to older ticks; on the first frame, entity
			// model/render state may not be fully initialized, causing SetupBones() to crash
			// (ACCESS_VIOLATION reading 0xffffffffffffffff). Without fake latency this doesn't happen.
			if (F::LagRecords->GetFakeLatency() > 0.0f && I::GlobalVars->tickcount <= 0)
				break;

			// Cache local team and config values for faster comparisons
			const int nLocalTeam = pLocal->m_iTeamNum();
			const bool bSetupBonesOpt = CFG::Misc_SetupBones_Optimization;
			const bool bDisableInterp = CFG::Visuals_Disable_Interp;
			const bool bAccuracyImprovements = CFG::Misc_Accuracy_Improvements;
			const bool bDoAnimUpdates = bAccuracyImprovements && bDisableInterp;
			const bool bStoreVelFix = !CFG::Perf_Extreme_Skip_VelFix;
			
			// Pre-calculate frametime once if needed
			const float flAnimFrameTime = I::Prediction->m_bEnginePaused ? 0.0f : TICK_INTERVAL;

			// Iterate entity list directly instead of using cached group pointers.
			// During class switch, cached entity pointers can become stale (entity destroyed,
			// memory freed, vtable overwritten with garbage like 0x42a66666).
			// Calling ANY virtual function (including entindex()) on a stale pointer crashes.
			// GetClientEntity(n) always returns the current valid pointer for that index.
			const int nMaxClients = I::EngineClient->GetMaxClients();
			
			for (int n = 1; n <= nMaxClients; n++)
			{
				F::CrashHandler->s_Context.m_pszLastFeature = "FrameStageNotify";
				F::CrashHandler->s_Context.m_nEntityIndex = n;
				F::CrashHandler->s_Context.m_pszLastSubFeature = "SafeGetPlayerEntity";

				// SEH-safe entity validation — high-player-count servers can expose player
				// indices up to EngineClient->GetMaxClients(). Calling ANY virtual method on a
				// bad transient entity state crashes with RCX=0x0, so validate in one protected path.
				const auto pPlayer = SafeGetPlayerEntity(n);
				if (!pPlayer)
					continue;

				// Cache deadflag check - used multiple times
				const bool bIsDead = pPlayer->deadflag();

				if (pPlayer == pLocal)
				{
					if (bStoreVelFix && !bIsDead && n > 0 && n < G::MAX_VELFIX_SLOTS)
						G::mapVelFixRecords[n] = { pPlayer->m_vecOrigin(), pPlayer->m_fFlags(), pPlayer->m_flSimulationTime(), true };

					continue;
				}
				
				const int nDifference = std::clamp(TIME_TO_TICKS(pPlayer->m_flSimulationTime() - pPlayer->m_flOldSimulationTime()), 0, 22);
				if (nDifference > 0)
				{
					// Add lag record BEFORE animation updates; bones must be captured at the
					// correct simtime state. If we capture after UpdateClientSideAnimation,
					// the animation state has already been advanced and bones won't match simtime,
					// causing lag record jitter for high-ping players.
					if (!bIsDead)
					{
						const bool bIsEnemy = pPlayer->m_iTeamNum() != nLocalTeam;
						if (bSetupBonesOpt || bIsEnemy)
						{
							if (CFG::Perf_Extreme_Skip_LagRecords_Teammates && !bIsEnemy)
								; // skip teammate records in EXTREME mode
							else
							{
								F::CrashHandler->s_Context.m_pszLastFeature = "LagRecords::AddRecord";
								F::LagRecords->AddRecord(pPlayer, n);
							}
						}

						// Push origin record for accurate velocity computation (Amalgam-style)
						// Uses GetSize().z offset like Amalgam's VelFixRecord
						g_AmalgamEntitiesExt.PushOrigin(n,
							pPlayer->m_vecOrigin() + Vec3(0, 0, pPlayer->m_vecMaxs().z),
							pPlayer->m_flSimulationTime());
					}
					else
						g_AmalgamEntitiesExt.ClearOrigins(n);

					// Do manual animation updates if Disable Interp is on
					// EXTREME: Skip anim updates entirely; saves CPU but breaks visual accuracy.
					// Skip dead players; death causes large simtime deltas that trigger
					// many animation iterations on a dying model (expensive + causes lag spikes on kill)
					if (bDoAnimUpdates && !bIsDead && !CFG::Perf_Extreme_Skip_Anim_Updates)
					{
						F::CrashHandler->s_Context.m_pszLastSubFeature = "UpdateClientSideAnimation";
						const float flOldFrameTime = I::GlobalVars->frametime;
						I::GlobalVars->frametime = flAnimFrameTime;

						G::bUpdatingAnims = true;
						
						// Smart cap: only limit iterations when there's packet loss / high ping
						// Normal case (nDifference = 1): zero overhead, no cap needed
						// Problem case (nDifference > 1): cap to prevent frame spikes
						if (nDifference == 1)
						{
							// Normal case: single tick advance (cheap, always do it)
							SafeUpdateClientSideAnimation(pPlayer, 1);
						}
						else if (nDifference > 1)
						{
							// Packet loss / high ping: cap to prevent frame spikes
							// High-ping players will catch up over multiple frames
							constexpr int MAX_ANIM_ITERATIONS_PER_FRAME = 5;
							const int nIterations = std::min(nDifference, MAX_ANIM_ITERATIONS_PER_FRAME);
							SafeUpdateClientSideAnimation(pPlayer, nIterations);
						}
						
						G::bUpdatingAnims = false;

						I::GlobalVars->frametime = flOldFrameTime;
					}
				}

				if (bStoreVelFix && !bIsDead && n > 0 && n < G::MAX_VELFIX_SLOTS)
					G::mapVelFixRecords[n] = { pPlayer->m_vecOrigin(), pPlayer->m_fFlags(), pPlayer->m_flSimulationTime(), true };
			}

	{
		F::CrashHandler->s_Context.m_pszLastFeature = "LagRecords";
		F::CrashHandler->s_Context.m_pszLastSubFeature = "UpdateDatagram";
		F::LagRecords->UpdateDatagram();

		F::CrashHandler->s_Context.m_pszLastSubFeature = "UpdateRecords";
		F::LagRecords->UpdateRecords();
		if (!CFG::Perf_Extreme_Skip_MovementSimulation)
		{
			F::MovementSimulation->Store();
			F::MoveSim->Store();
		}
	}

			break;
		}

		case FRAME_RENDER_START:
		{
			H::Input->Update();

			// SkinChanger - Apply weapon skins (client-side only)
			F::SkinChanger->FrameStageNotify();

			// EXTREME: Skip all visual updates: world modulation, sway, detail props, weather.
			if (!CFG::Perf_Extreme_Skip_All_Visuals)
			{
				F::WorldModulation->UpdateWorldModulation();
				F::MiscVisuals->ViewModelSway();
				F::MiscVisuals->DetailProps();
				F::Weather->Rain();
			}

			// EXTREME: Convar-based rendering skips
			// Rule: Don't touch ANY convar unless user explicitly enables an option.
			// On first enable: capture the user's original values.
			// On disable: restore exactly what the user had before we touched anything.
			{
				const bool bAnyConvarOverride =
					CFG::Perf_Extreme_Minimal_Render ||
					CFG::Perf_Extreme_Skip_World_Render ||
					CFG::Perf_Extreme_Skip_Shadows ||
					CFG::Perf_Extreme_Skip_Particles ||
					CFG::Perf_Extreme_Skip_Decals ||
					CFG::Perf_Extreme_Skip_World_Textures ||
					CFG::Perf_Extreme_Skip_Unused_Entities ||
					CFG::Perf_Extreme_Skip_Sound ||
					CFG::Perf_Extreme_Low_Textures ||
					CFG::Perf_Extreme_FPS_Limit > 0;

				// Cache config state to avoid redundant convar operations when nothing changed
				static bool s_bLastMinimalRender = false;
				static bool s_bLastSkipWorld = false;
				static bool s_bLastSkipShadows = false;
				static bool s_bLastSkipParticles = false;
				static bool s_bLastSkipDecals = false;
				static bool s_bLastSkipTextures = false;
				static bool s_bLastSkipEntities = false;
				static bool s_bLastSkipSound = false;
				static bool s_bLastLowTextures = false;
				static int s_nLastFPSLimit = 0;

				const bool bMinimalRender = CFG::Perf_Extreme_Minimal_Render;
				const bool bSkipWorld = CFG::Perf_Extreme_Skip_World_Render || bMinimalRender;
				const bool bSkipShadows = CFG::Perf_Extreme_Skip_Shadows || bMinimalRender;
				const bool bSkipParticles = CFG::Perf_Extreme_Skip_Particles || bMinimalRender;
				const bool bSkipDecals = CFG::Perf_Extreme_Skip_Decals || bMinimalRender;
				const bool bSkipTextures = CFG::Perf_Extreme_Skip_World_Textures || bMinimalRender;
				const bool bSkipEntities = CFG::Perf_Extreme_Skip_Unused_Entities || bMinimalRender;
				const bool bSkipSound = CFG::Perf_Extreme_Skip_Sound || bMinimalRender;
				const bool bLowTextures = CFG::Perf_Extreme_Low_Textures || bMinimalRender;
				const int nFPSLimit = CFG::Perf_Extreme_FPS_Limit;

				// Check if any config actually changed
				const bool bConfigChanged =
					bMinimalRender != s_bLastMinimalRender ||
					bSkipWorld != s_bLastSkipWorld ||
					bSkipShadows != s_bLastSkipShadows ||
					bSkipParticles != s_bLastSkipParticles ||
					bSkipDecals != s_bLastSkipDecals ||
					bSkipTextures != s_bLastSkipTextures ||
					bSkipEntities != s_bLastSkipEntities ||
					bSkipSound != s_bLastSkipSound ||
					bLowTextures != s_bLastLowTextures ||
					nFPSLimit != s_nLastFPSLimit;

				if (bAnyConvarOverride && !ConvarBackup::s_bCurrentlyOverriding)
				{
					// First time enabling: capture user's original values before we change anything.
					ConvarBackup::s_vecBackups.clear();
					ConvarBackup::s_vecBackups.reserve(static_cast<size_t>(ConvarBackup::ConvarId::Count));

					for (int i = 0; i < static_cast<int>(ConvarBackup::ConvarId::Count); i++)
						ConvarBackup::s_vecBackups.push_back(ConvarBackup::SaveConvar(static_cast<ConvarBackup::ConvarId>(i)));

					ConvarBackup::s_bCurrentlyOverriding = true;
				}

				// Only apply convar changes if config actually changed
				if (bAnyConvarOverride && bConfigChanged)
				{
					if (bSkipWorld)
					{
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::DrawWorld, 0);
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::DrawSkybox, 0);
					}
					if (bSkipShadows)
					{
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::Shadows, 0);
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::ShadowDraw, 0);
					}
					if (bSkipParticles)
					{
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::DrawParticles, 0);
					}
					if (bSkipDecals)
					{
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::DrawDecals, 0);
						ConvarBackup::SetConvarFloat(ConvarBackup::ConvarId::DecalCullSize, 9999.0f);
					}

					// Minimal render: skip brush models, displacements, ropes
					if (bMinimalRender)
					{
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::DrawBrushModels, 0);
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::DrawDisp, 0);
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::DrawRopes, 0);
					}

					// World textures: wireframe mode (mat_fillmode 1 = wireframe, 2 = solid)
					if (bSkipTextures)
					{
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::FillMode, 1);
					}

					// Skip all entity rendering
					if (bSkipEntities)
					{
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::DrawEntities, 0);
					}

					// Skip sound processing: only mute when explicitly enabled, don't force 1.0 when off.
					if (bSkipSound)
					{
						ConvarBackup::SetConvarFloat(ConvarBackup::ConvarId::Volume, 0.0f);
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::SndPitchQuality, 0);
					}

					// FPS limit: bypass engine min clamp (30) by temporarily removing the restriction.
					if (const auto pFPSMax = ConvarBackup::GetConvar(ConvarBackup::ConvarId::FPSMax))
					{
						if (nFPSLimit > 0)
						{
							// Remove min clamp so values below 30 work
							if (pFPSMax->m_pParent)
								pFPSMax->m_pParent->m_bHasMin = false;
							ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::FPSMax, nFPSLimit);
						}
					}

					// Low textures: mat_picmip 2 = lowest quality, huge VRAM savings.
					if (bLowTextures)
					{
						ConvarBackup::SetConvarInt(ConvarBackup::ConvarId::Picmip, 2);
					}

					// Update cache
					s_bLastMinimalRender = bMinimalRender;
					s_bLastSkipWorld = bSkipWorld;
					s_bLastSkipShadows = bSkipShadows;
					s_bLastSkipParticles = bSkipParticles;
					s_bLastSkipDecals = bSkipDecals;
					s_bLastSkipTextures = bSkipTextures;
					s_bLastSkipEntities = bSkipEntities;
					s_bLastSkipSound = bSkipSound;
					s_bLastLowTextures = bLowTextures;
					s_nLastFPSLimit = nFPSLimit;
				}
				else if (ConvarBackup::s_bCurrentlyOverriding && !bAnyConvarOverride)
				{
					// All options disabled: restore user's original values exactly.
					RestoreConvarBackups();
					
					// Reset cache
					s_bLastMinimalRender = false;
					s_bLastSkipWorld = false;
					s_bLastSkipShadows = false;
					s_bLastSkipParticles = false;
					s_bLastSkipDecals = false;
					s_bLastSkipTextures = false;
					s_bLastSkipEntities = false;
					s_bLastSkipSound = false;
					s_bLastLowTextures = false;
					s_nLastFPSLimit = 0;
				}
			}

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
