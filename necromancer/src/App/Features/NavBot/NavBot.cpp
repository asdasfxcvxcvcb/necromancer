#include "NavBot.h"
#include "NavEngine/NavEngine.h"
#include "../../../SDK/SDK.h"
#include "../CFG.h"
#include "../amalgam_port/AmalgamCompat.h"
#include "../Players/Players.h"
#include "../MovementSimulation/MovementSimulation.h"

#undef min
#undef max
#include <algorithm>
#include <cmath>

// Max reserve ammo per WEAPON (Unibox SDK::GetWeaponMaxReserveAmmo). Keyed by
// weapon ID, not class: the old per-class table assumed class IDs run
// Scout..Engineer (they actually run Scout=1, Sniper=2, Soldier=3, Demoman=4,
// Medic=5, Heavy=6, Pyro=7, Spy=8, Engineer=9) and that m_iAmmo indexes
// "PRIMARY/SECONDARY/METAL" (they actually run SHELLS=1, BULLETS=2, ROCKETS=3,
// METAL=7). Both wrong at once: a FULL soldier's 32 shotgun shells got compared
// against the pyro row's 200 flamethrower fuel (ratio 0.16 < 0.2) and read as
// low ammo, and engineer metal read m_iAmmo[3] = rockets, which is always 0.
static int GetWeaponMaxReserveAmmo(int iWeaponID)
{
	switch (iWeaponID)
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PISTOL:
	case TF_WEAPON_FLAMETHROWER:
		return 200;
	case TF_WEAPON_SYRINGEGUN_MEDIC:
		return 150;
	case TF_WEAPON_SMG:
		return 75;
	case TF_WEAPON_FLAME_BALL:
		return 40;
	case TF_WEAPON_CROSSBOW:
		return 38;
	case TF_WEAPON_HANDGUN_SCOUT_SECONDARY:
	case TF_WEAPON_PISTOL_SCOUT:
	case TF_WEAPON_HANDGUN_SCOUT_PRIMARY:
		return 36;
	case TF_WEAPON_SCATTERGUN:
	case TF_WEAPON_PEP_BRAWLER_BLASTER:
	case TF_WEAPON_SODA_POPPER:
	case TF_WEAPON_SHOTGUN_HWG:
	case TF_WEAPON_SHOTGUN_PRIMARY:
	case TF_WEAPON_SHOTGUN_PYRO:
	case TF_WEAPON_SHOTGUN_SOLDIER:
		return 32;
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
		return 25;
	case TF_WEAPON_STICKBOMB:
	case TF_WEAPON_STICKY_BALL_LAUNCHER:
	case TF_WEAPON_REVOLVER:
		return 24;
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		return 20;
	case TF_WEAPON_CANNON:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
	case TF_WEAPON_GRENADELAUNCHER:
	case TF_WEAPON_FLAREGUN:
		return 16;
	case TF_WEAPON_COMPOUND_BOW:
		return 12;
	default:
		return 32;   // Unibox's fallback
	}
}

// Unibox Capture.cpp's AdjustCaptureCandidateToNav: snap a ring-slot candidate
// onto the navmesh so the bot paths to a position it can actually stand on
// instead of a corner poking into geometry.
static Vec3 AdjustCaptureCandidateToNav(Vec3 vCandidate)
{
	if (!G_NavEngine.IsNavMeshLoaded())
		return vCandidate;

	if (auto pArea = G_NavEngine.m_pMap->FindClosestNavArea(vCandidate))
	{
		Vec3 vCorrected = pArea->GetNearestPoint(Vec2(vCandidate.x, vCandidate.y));
		vCorrected.z = pArea->m_vCenter.z;
		return vCorrected;
	}

	return vCandidate;
}

// Objective-based NavBot with proper nav mesh pathfinding
// Uses NavEngine for A* pathfinding through TF2 nav meshes

bool CNavBot::FindObjective(C_TFPlayer* pLocal, Vec3& vOut)
{
	if (!pLocal)
		return false;

	// Unibox CanCaptureObjective: objectives play only in a live round — never
	// while waiting for players, during setup, or between rounds. Without this
	// the bot picks capture goals into the closed gates.
	if (!G_NavEngine.IsRoundLive())
		return false;

	Vec3 vLocalPos = pLocal->m_vecOrigin();
	int nLocalTeam = pLocal->m_iTeamNum();
	int nEnemyTeam = (nLocalTeam == TF_TEAM_RED) ? TF_TEAM_BLUE : TF_TEAM_RED;

	// Staging fallback (Unibox GetStagedPointGoal): the closest point we can't
	// touch RIGHT NOW — a locked next stage while attacking, or a point only
	// the enemy can cap while defending. When nothing actionable exists we hold
	// a position on our side of it instead of wandering off.
	Vec3 vStagedPos;
	float flStagedDist = FLT_MAX;
	bool bFoundStaged = false;
	C_BaseEntity* pStagedResource = nullptr;
	m_bObjectiveStaging = false;

	// === CTF Logic (ported from Amalgam's Capture.cpp / FlagController) ===
	// Find enemy flag and check its status
	C_BaseEntity* pEnemyFlag = nullptr;
	Vec3 vEnemyFlagPos;
	Vec3 vOurFlagSpawnPos;
	bool bFoundOurFlagSpawn = false;

	const int nHighestIndex = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = 1; i < nHighestIndex; i++)
	{
		IClientEntity* pClientEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pClientEntity || pClientEntity->IsDormant())
			continue;

		auto pNetworkable = pClientEntity->GetClientNetworkable();
		if (!pNetworkable)
			continue;

		auto pClientClass = pNetworkable->GetClientClass();
		if (!pClientClass)
			continue;

		const auto nClassId = static_cast<ETFClassIds>(pClientClass->m_ClassID);

		// Look for CCaptureFlag entities
		if (nClassId == ETFClassIds::CCaptureFlag)
		{
			auto pEntity = pClientEntity->As<C_BaseEntity>();
			if (!pEntity)
				continue;

			int nFlagTeam = pEntity->m_iTeamNum();

			// Find enemy flag
			if (nFlagTeam == nEnemyTeam)
			{
				pEnemyFlag = pEntity;
				vEnemyFlagPos = pEntity->m_vecOrigin();
			}

			// Remember our flag's spawn position (this IS the capture zone in CTF)
			// When flag is at home (TF_FLAGINFO_HOME = 0), its position is the spawn/cap zone
			if (nFlagTeam == nLocalTeam)
			{
				// m_nFlagStatus netvar: 0 = at home, 1 = stolen, 2 = dropped
				static int nFlagStatusOffset = NetVars::GetNetVar("CCaptureFlag", "m_nFlagStatus");
				int nFlagStatus = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(pEntity) + nFlagStatusOffset);

				if (nFlagStatus == TF_FLAGINFO_HOME)
				{
					vOurFlagSpawnPos = pEntity->m_vecOrigin();
					bFoundOurFlagSpawn = true;
				}
			}
		}
	}

	// If we found the enemy flag, check if we're carrying it
	if (pEnemyFlag)
	{
		// Check flag status via netvar
		static int nFlagStatusOffset = NetVars::GetNetVar("CCaptureFlag", "m_nFlagStatus");
		int nFlagStatus = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(pEnemyFlag) + nFlagStatusOffset);

		if (nFlagStatus & TF_FLAGINFO_STOLEN)
		{
			// Flag is stolen - check if WE are the carrier via m_hOwnerEntity
			auto pOwner = pEnemyFlag->m_hOwnerEntity().Get();
			if (pOwner && pOwner == pLocal)
			{
				// We have the flag! Navigate to our capture zone (our flag's spawn position)
				if (bFoundOurFlagSpawn)
				{
					vOut = vOurFlagSpawnPos;
					m_pTargetEntity = pEnemyFlag;
					return true;
				}
			}
			// Someone else has the flag - skip it, can't pick it up
			pEnemyFlag = nullptr;
		}
		else if (nFlagStatus & TF_FLAGINFO_DROPPED)
		{
			// Flag is dropped on the ground - go pick it up
			vOut = vEnemyFlagPos;
			m_pTargetEntity = pEnemyFlag;
			return true;
		}
		else
		{
			// Flag is at home - go steal it
			vOut = vEnemyFlagPos;
			m_pTargetEntity = pEnemyFlag;
			return true;
		}
	}

	// === Payload logic (ported from Amalgam's PLController) ===
	// Runs BEFORE the control-point scan: on payload maps the objective
	// resource also lists the cart's checkpoints as "control points", and the
	// bot would march to the next checkpoint trigger instead of the cart
	// (Unibox's PL game mode never consults them). Any cart on the field marks
	// a payload map — push OUR cart; if only the enemy's cart exists (we're
	// defense) there is nothing to capture and the defend goal takes over.
	{
		C_BaseEntity* pNearestCart = nullptr;
		float flNearestDist = FLT_MAX;
		bool bAnyCart = false;

		for (int i = 1; i < nHighestIndex; i++)
		{
			IClientEntity* pClientEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pClientEntity)
				continue;

			auto pNetworkable = pClientEntity->GetClientNetworkable();
			if (!pNetworkable)
				continue;

			auto pClientClass = pNetworkable->GetClientClass();
			if (!pClientClass)
				continue;

			if (static_cast<ETFClassIds>(pClientClass->m_ClassID) == ETFClassIds::CObjectCartDispenser)
			{
				auto pEntity = pClientEntity->As<C_BaseEntity>();
				if (!pEntity)
					continue;

				// Dormant carts keep their last networked origin — a slightly
				// stale cart position still beats a checkpoint, so only reject
				// carts that were never networked (origin still zero)
				Vec3 vCartPos = pEntity->m_vecOrigin();
				if (vCartPos.IsZero())
					continue;

				bAnyCart = true;

				// Only push our team's cart
				if (pEntity->m_iTeamNum() != nLocalTeam)
					continue;

				float flDist = vLocalPos.DistTo(vCartPos);
				if (flDist < flNearestDist)
				{
					flNearestDist = flDist;
					pNearestCart = pEntity;
				}
			}
		}

		if (pNearestCart)
		{
			vOut = pNearestCart->m_vecOrigin();
			m_pTargetEntity = pNearestCart;
			return true;
		}

		// Enemy cart only = payload defense, not a capture target
		if (bAnyCart)
			return false;
	}

	// === Control Point logic (ported from Amalgam's CPController) ===
	// Uses CBaseTeamObjectiveResource to find uncaptured control points on CP/KOTH maps.
	// This is the proper way to find CPs — CCaptureZone only exists in CTF.
	{
		C_BaseEntity* pObjectiveResource = nullptr;

		for (int i = 1; i < nHighestIndex; i++)
		{
			IClientEntity* pClientEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pClientEntity)
				continue;

			auto pNetworkable = pClientEntity->GetClientNetworkable();
			if (!pNetworkable)
				continue;

			auto pClientClass = pNetworkable->GetClientClass();
			if (!pClientClass)
				continue;

			if (static_cast<ETFClassIds>(pClientClass->m_ClassID) == ETFClassIds::CTFObjectiveResource)
			{
				pObjectiveResource = pClientEntity->As<C_BaseEntity>();
				break;
			}
		}

		if (pObjectiveResource)
		{
			static int nNumCPsOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_iNumControlPoints");
			static int nCPPositionsOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_vCPPositions[0]");
			static int nOwnerOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_iOwner");
			static int nCPLockedOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_bCPLocked");
			static int nTeamCanCapOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_bTeamCanCap");
			static int nInMiniRoundOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_bInMiniRound");
			static int nPlayingMiniRoundsOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_bPlayingMiniRounds");
			pStagedResource = pObjectiveResource;

			// GetNetVar returns 0 on failure - validate critical offsets
			if (nNumCPsOffset == 0 || nCPPositionsOffset == 0 || nOwnerOffset == 0)
				return false;

			{
				auto pBase = reinterpret_cast<std::uintptr_t>(pObjectiveResource);
				int nNumCPs = *reinterpret_cast<int*>(pBase + nNumCPsOffset);

				if (nNumCPs > 0 && nNumCPs <= 8)
				{
					bool bPlayingMiniRounds = nPlayingMiniRoundsOffset > 0 ? *reinterpret_cast<bool*>(pBase + nPlayingMiniRoundsOffset) : false;

					// Find the closest cappable control point
					C_BaseEntity* pBestCP = nullptr;
					Vec3 vBestCPPos;
					float flBestDist = FLT_MAX;

					for (int i = 0; i < nNumCPs; i++)
					{
						// Get position first
						Vec3 vCPPos = *reinterpret_cast<Vec3*>(pBase + nCPPositionsOffset + i * sizeof(Vec3));

						// Check if we already own this point
						int nOwner = *reinterpret_cast<int*>(pBase + nOwnerOffset + i * sizeof(int));

						bool bLocked = nCPLockedOffset > 0 ? *reinterpret_cast<bool*>(pBase + nCPLockedOffset + i * sizeof(bool)) : false;
						bool bTeamCanCap = nTeamCanCapOffset > 0 ? *reinterpret_cast<bool*>(pBase + nTeamCanCapOffset + (i + nLocalTeam * 8) * sizeof(bool)) : true;

						if (nOwner == nLocalTeam)
							continue;

						// Check if this point is in the current mini-round
						if (bPlayingMiniRounds && nInMiniRoundOffset > 0)
						{
							bool bInMiniRound = *reinterpret_cast<bool*>(pBase + nInMiniRoundOffset + i * sizeof(bool));
							if (!bInMiniRound)
								continue;
						}

						if (vCPPos.IsZero())
							continue;

						float flDist = vLocalPos.DistTo(vCPPos);

						// Can the ENEMY take this point? (they cap it → we defend it,
						// we don't stage behind it)
						const bool bEnemyCanCap = nTeamCanCapOffset > 0 ? *reinterpret_cast<bool*>(pBase + nTeamCanCapOffset + (i + nEnemyTeam * 8) * sizeof(bool)) : true;

						if (!bLocked && bTeamCanCap)
						{
							if (flDist < flBestDist)
							{
								flBestDist = flDist;
								vBestCPPos = vCPPos;
								pBestCP = pObjectiveResource;  // Use resource as target entity
							}
						}
						else if (!bEnemyCanCap && flDist < flStagedDist)
						{
							// Locked for BOTH sides — attack pipeline staging
							flStagedDist = flDist;
							vStagedPos = vCPPos;
							bFoundStaged = true;
						}
					}

					if (pBestCP)
					{
						vOut = vBestCPPos;
						m_pTargetEntity = pBestCP;
						return true;
					}
					else
					{
					}
				}
			}
		}
		else
		{
		}
	}

	// === CTF fallback: CCaptureZone (capture zone where you bring the flag) ===
	// This is only used if no control points or payload were found (pure CTF without flags detected)
	{
		C_BaseEntity* pNearestObjective = nullptr;
		float flNearestDist = FLT_MAX;

		for (int i = 1; i < nHighestIndex; i++)
		{
			IClientEntity* pClientEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pClientEntity || pClientEntity->IsDormant())
				continue;

			auto pNetworkable = pClientEntity->GetClientNetworkable();
			if (!pNetworkable)
				continue;

			auto pClientClass = pNetworkable->GetClientClass();
			if (!pClientClass)
				continue;

			const auto nClassId = static_cast<ETFClassIds>(pClientClass->m_ClassID);

			if (nClassId == ETFClassIds::CCaptureZone)
			{
				auto pEntity = pClientEntity->As<C_BaseEntity>();
				if (!pEntity)
					continue;

				int nObjTeam = pEntity->m_iTeamNum();

				// Target enemy/neutral capture zones
				if (nObjTeam == nLocalTeam)
					continue;

				Vec3 vObjPos = pEntity->m_vecOrigin();
				float flDist = vLocalPos.DistTo(vObjPos);

				if (flDist < flNearestDist)
				{
					flNearestDist = flDist;
					pNearestObjective = pEntity;
				}
			}
		}

		if (pNearestObjective)
		{
			vOut = pNearestObjective->m_vecOrigin();
			m_pTargetEntity = pNearestObjective;
			return true;
		}
	}

	// === Staged point fallback (Unibox GetStagedPointGoal) ===
	// No actionable objective, but a point exists locked for BOTH sides (next
	// stage in the attack pipeline). Hold a staging position 350u on OUR side
	// of it — attackers wait at the gate — instead of wandering to the other
	// end of the map and missing the fight when it opens. Points the ENEMY can
	// take are excluded here on purpose: that's a defend job, not a stage.
	if (bFoundStaged && pStagedResource && G_NavEngine.IsNavMeshLoaded())
	{
		Vec3 vStage = vStagedPos;
		{
			// Our spawn side: closest spawn-flagged area of OUR team to the point
			Vec3 vSpawnCenter(0, 0, 0);
			float flBestSpawnDist = FLT_MAX;
			for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
			{
				const bool bOurSpawn = (nLocalTeam == TF_TEAM_RED && (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED)) ||
					(nLocalTeam == TF_TEAM_BLUE && (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE));
				if (!bOurSpawn)
					continue;
				float flDist = tArea.m_vCenter.DistToSqr(vStagedPos);
				if (flDist < flBestSpawnDist)
				{
					flBestSpawnDist = flDist;
					vSpawnCenter = tArea.m_vCenter;
				}
			}

			if (flBestSpawnDist < FLT_MAX)
			{
				Vec3 vToSpawn = vSpawnCenter - vStagedPos;
				vToSpawn.z = 0.0f;
				if (vToSpawn.Length() > 1.0f)
				{
					vToSpawn.Normalize();
					vStage = vStage + vToSpawn * 350.0f;
				}
			}
		}
		vStage = AdjustCaptureCandidateToNav(vStage);

		m_bObjectiveStaging = true;
		vOut = vStage;
		m_pTargetEntity = pStagedResource;
		return true;
	}

	return false;
}

bool CNavBot::ShouldSearchHealth(C_TFPlayer* pLocal)
{
	if (!CFG::NavBot_SearchHealth)
		return false;

	if (!pLocal)
		return false;

	// Don't search if already being healed (ported from TF2 bot's GetNumHealers check)
	if (pLocal->m_nPlayerCond() & (1 << 21)) // TFCond_Healing
		return false;

	float flHealthPercent = static_cast<float>(pLocal->m_iHealth()) / pLocal->GetMaxHealth();

	// Ported from TF2 bot's CTFBotTacticalMonitor and CTFBotGetHealth
	// Dynamic thresholds based on combat state, like TF2 bots:
	// - In combat: stay in fight until nearly dead (critical ratio 0.3)
	// - Out of combat: go get health when hurt (ok ratio 0.8)
	// - On fire: always seek health immediately (critical)
	const float flCriticalRatio = 0.3f;
	const float flOkRatio = 0.8f;

	// On fire - get health now (ported from TF2 bot's burning check)
	if (pLocal->InCond(TF_COND_BURNING))
		return true;

	// Approximate "in combat" by checking if there are visible enemies nearby
	// TF2 bot uses CTFNavArea::IsInCombat() which tracks recent damage in the area
	// We approximate by checking if we can see living enemies within 800 units
	bool bInCombat = false;
	{
		Vec3 vLocalPos = pLocal->m_vecOrigin();
		const int nMaxClients = I::EngineClient->GetMaxClients();
		for (int i = 1; i <= nMaxClients; i++)
		{
			if (i == I::EngineClient->GetLocalPlayer())
				continue;
			auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pClientEntity)
				continue;
			auto pPlayer = pClientEntity->As<C_TFPlayer>();
			if (!pPlayer || !pPlayer->IsAlive() || pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
				continue;
			if (pPlayer->IsDormant())
				continue;
			if (vLocalPos.DistTo(pPlayer->m_vecOrigin()) < 800.0f)
			{
				bInCombat = true;
				break;
			}
		}
	}

	if (bInCombat)
	{
		// In combat - stay in the fight until nearly dead (ported from TF2 bot)
		return flHealthPercent < flCriticalRatio;
	}
	else
	{
		// Not in combat - go get health when hurt (ported from TF2 bot)
		return flHealthPercent < flOkRatio;
	}
}

bool CNavBot::ShouldSearchAmmo(C_TFPlayer* pLocal)
{
	if (!CFG::NavBot_SearchAmmo)
		return false;

	if (!pLocal)
		return false;

	// Ported from TF2 bot's CTFBot::IsAmmoLow
	// TF2 bot checks: primary ammo ratio < 0.2 (20% of max)
	// Also skips melee weapons and weapons with no projectiles (medigun, etc.)

	int iClass = pLocal->m_iClass();
	if (iClass < 1 || iClass > 9)
		return false;

	// Decision trace (CFG::NavBot_Debug-gated): this check runs every tick, so
	// throttle to one line per 2s — enough to see why a pack chase triggered
	auto TraceLowAmmo = [](const char* szFmt, ...)
	{
		static float flLastTraceTime = -1.0f;
		const float flNow = I::GlobalVars->curtime;
		if (flLastTraceTime >= 0.0f && flNow >= flLastTraceTime && flNow - flLastTraceTime < 2.0f)
			return;
		flLastTraceTime = flNow;

		char szBuf[256];
		va_list args;
		va_start(args, szFmt);
		vsnprintf_s(szBuf, sizeof(szBuf), _TRUNCATE, szFmt, args);
		va_end(args);
		NavDebug("%s", szBuf);
	};

	for (int nSlot = 0; nSlot <= 1; nSlot++)  // 0=Primary, 1=Secondary
	{
		auto pWeapon = pLocal->GetWeaponFromSlot(nSlot);
		if (!pWeapon)
			continue;

		auto pWeaponBase = pWeapon->As<C_TFWeaponBase>();
		if (!pWeaponBase)
			continue;

		// Wrench is special — it's melee that uses metal (ammo)
		// TF2 bot: "return ( GetAmmoCount( TF_AMMO_METAL ) <= 0 )"
		if (pWeaponBase->GetWeaponID() == TF_WEAPON_WRENCH)
		{
			int iMetal = pLocal->GetAmmoCount(7);  // TF_AMMO_METAL = 7 (3 is ROCKETS)
			if (iMetal <= 0)
			{
				TraceLowAmmo("wrench metal %d/200", iMetal);
				return true;
			}
			continue;
		}

		int iAmmoType = pWeaponBase->m_iPrimaryAmmoType();
		if (iAmmoType < 0)
			continue;  // Weapon doesn't use ammo (e.g., medigun)

		int iReserve = pLocal->GetAmmoCount(iAmmoType);

		// Get max ammo from the weapon-ID table (Unibox GetWeaponMaxReserveAmmo)
		int iMaxAmmo = GetWeaponMaxReserveAmmo(pWeaponBase->GetWeaponID());

		if (iMaxAmmo > 0)
		{
			// TF2 bot: ratio < 0.2f means low ammo
			float flRatio = static_cast<float>(iReserve) / static_cast<float>(iMaxAmmo);
			if (flRatio < 0.2f)
			{
				// Prints the exact numbers the decision used — a full-ammo bot
				// seeking packs with this line visible means the read is wrong
				TraceLowAmmo("slot %d wep %d type %d %d/%d (ratio %.2f) class %d",
					nSlot, pWeaponBase->GetWeaponID(), iAmmoType, iReserve, iMaxAmmo, flRatio, iClass);
				return true;
			}
		}
		else if (iReserve <= 5)
		{
			// Unreachable in practice — the weapon table has a fallback; kept
			// as a safety net for exotic weapons
			TraceLowAmmo("slot %d wep %d type %d %d (no max table entry, class %d)",
				nSlot, pWeaponBase->GetWeaponID(), iAmmoType, iReserve, iClass);
			return true;
		}
	}

	return false;
}

bool CNavBot::FindSupply(C_TFPlayer* pLocal, Vec3& vOut, bool bHealth)
{
	if (!pLocal)
		return false;

	// Throttle: every candidate pack costs a MicroPather solve (full mesh search
	// when unreachable), and when we need supplies but none are in range this
	// runs every tick. 0.5s granularity is plenty — packs don't spawn faster.
	{
		const int iSlot = bHealth ? 1 : 0;
		if (I::GlobalVars->curtime < m_flNextSupplySearchTime[iSlot])
			return false;
		m_flNextSupplySearchTime[iSlot] = I::GlobalVars->curtime + 0.5f;
	}

	Vec3 vLocalPos = pLocal->m_vecOrigin();
	C_BaseEntity* pBestSupply = nullptr;
	float flBestCost = FLT_MAX;

	// When following a player (tagged or teammate), limit how far we'll go for supplies
	// Uses NavBot_FollowSupplyDistance as max distance from the FOLLOWED PLAYER (not from us)
	// This prevents the bot from abandoning the player it's following
	float flMaxSupplyDistFromPlayer = FLT_MAX;
	bool bIsFollowing = m_bIsFollowingTaggedPlayer || m_bIsFollowingTeammate;

	if (bIsFollowing && CFG::NavBot_FollowSupplyDistance > 0.0f)
	{
		flMaxSupplyDistFromPlayer = CFG::NavBot_FollowSupplyDistance;
	}

	// Get the followed player's position for distance checking
	Vec3 vFollowedPlayerPos(0, 0, 0);
	bool bHasFollowedPlayerPos = false;
	if (bIsFollowing)
	{
		int iFollowIdx = m_bIsFollowingTaggedPlayer ? m_iFollowTaggedTargetIdx : m_iFollowTargetIdx;
		if (iFollowIdx > 0)
		{
			auto pFollowedEntity = I::ClientEntityList->GetClientEntity(iFollowIdx);
			if (pFollowedEntity)
			{
				auto pFollowedPlayer = pFollowedEntity->As<C_TFPlayer>();
				if (pFollowedPlayer && pFollowedPlayer->IsAlive())
				{
					vFollowedPlayerPos = pFollowedPlayer->m_vecOrigin();
					bHasFollowedPlayerPos = true;
				}
			}
		}
	}

	// Ported from TF2 bot's dynamic search range (CTFBotGetHealth/CTFBotGetAmmo)
	// The more hurt/low on ammo we are, the farther we'll travel to get supplies
	float flSearchRange = 1000.0f;  // Default near range
	if (bHealth)
	{
		float flHealthPercent = static_cast<float>(pLocal->m_iHealth()) / pLocal->GetMaxHealth();
		const float flCriticalRatio = 0.3f;
		const float flOkRatio = 0.8f;
		float t = (flHealthPercent - flCriticalRatio) / (flOkRatio - flCriticalRatio);
		t = std::clamp(t, 0.0f, 1.0f);
		// On fire - search far
		if (pLocal->InCond(TF_COND_BURNING))
			t = 0.0f;
		// Interpolate: critical = far range (2000), ok = near range (1000)
		flSearchRange = 2000.0f + t * (1000.0f - 2000.0f);
	}
	else
	{
		// Ammo search range (ported from TF2 bot's tf_bot_ammo_search_range)
		flSearchRange = 5000.0f;
	}

	// Get local player's nav area for path-cost-based selection
	CNavArea* pLocalArea = G_NavEngine.IsNavMeshLoaded() ? G_NavEngine.m_pMap->FindClosestNavArea(vLocalPos) : nullptr;
	int iTeam = pLocal->m_iTeamNum();

	// Helper: compute travel cost to a supply via nav mesh (ported from TF2 bot's CollectSurroundingAreas approach)
	// If nav mesh is available, uses actual path cost. Falls back to straight-line distance.
	auto fnComputeCost = [&](C_BaseEntity* pEntity) -> float {
		Vec3 vPackPos = pEntity->m_vecOrigin();

		// Straight-line pre-filter: skip if too far away even in a straight line
		float flStraightDist = vLocalPos.DistTo(vPackPos);
		if (flStraightDist > flSearchRange * 1.5f)
			return FLT_MAX;  // Way too far, skip

		// When following, also check distance from the followed player
		if (bHasFollowedPlayerPos)
		{
			float flDistFromPlayer = vFollowedPlayerPos.DistTo(vPackPos);
			if (flDistFromPlayer > flMaxSupplyDistFromPlayer)
				return FLT_MAX;
		}

		// Ported from TF2 bot's CHealthFilter/CAmmoFilter: skip items where an enemy is closer
		// TF2 bot: "if ( close.m_closePlayer && !m_me->InSameTeam( close.m_closePlayer ) ) return false"
		// This prevents selecting items that enemies will grab first
		{
			float flOurDist = vLocalPos.DistTo(vPackPos);
			const int nMaxClients = I::EngineClient->GetMaxClients();
			for (int i = 1; i <= nMaxClients; i++)
			{
				if (i == I::EngineClient->GetLocalPlayer())
					continue;
				auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
				if (!pClientEntity)
					continue;
				auto pPlayer = pClientEntity->As<C_TFPlayer>();
				if (!pPlayer || !pPlayer->IsAlive() || pPlayer->m_iTeamNum() == iTeam)
					continue;
				if (pPlayer->IsDormant())
					continue;

				float flEnemyDist = pPlayer->m_vecOrigin().DistTo(vPackPos);
				if (flEnemyDist < flOurDist * 0.8f)
					return FLT_MAX;  // Enemy is significantly closer — skip this item
			}
		}

		// Ported from TF2 bot's CHealthFilter/CAmmoFilter: check nav area TF attributes
		// to avoid enemy spawn rooms and inaccessible resupply cabinets
		if (pLocalArea && G_NavEngine.IsNavMeshLoaded())
		{
			CNavArea* pPackArea = G_NavEngine.m_pMap->FindClosestNavArea(vPackPos);
			if (pPackArea)
			{
				// Don't go into enemy spawn rooms (ported from TF2 bot's spawn room check)
				const bool bRedSpawn = pPackArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED;
				const bool bBlueSpawn = pPackArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE;
				if (iTeam == TF_TEAM_RED && bBlueSpawn && !bRedSpawn)
					return FLT_MAX;  // Red player, blue spawn - avoid
				if (iTeam == TF_TEAM_BLUE && bRedSpawn && !bBlueSpawn)
					return FLT_MAX;  // Blue player, red spawn - avoid

				// Ported from TF2 bot's path-cost-based supply selection:
				// Use nav mesh path cost instead of straight-line distance.
				// A supply 500 units away behind a wall is worse than one 800 units away on a direct path.
				CNavArea* pStartArea = pLocalArea;
				float flCost;
				std::vector<void*> vPath;
				int iResult = G_NavEngine.m_pMap->m_pather.Solve(
					reinterpret_cast<void*>(pStartArea),
					reinterpret_cast<void*>(pPackArea),
					&vPath, &flCost);

				if (iResult == micropather::MicroPather::START_END_SAME)
					return 1.0f;  // Same area, very close

				if (iResult == micropather::MicroPather::SOLVED && flCost > 0.0f)
					return flCost;

				// Path not solvable via nav mesh - fall through to straight-line
			}
		}

		// Fallback: straight-line distance (no nav mesh or path not found)
		return flStraightDist;
	};

	if (bHealth)
	{
		// Search health packs from entity group
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::HEALTHPACKS))
		{
			if (!pEntity || pEntity->IsDormant())
				continue;

			// Skip picked-up items (EF_NODRAW = invisible, waiting to respawn)
			// TF2 bot: "if ( candidate->IsEffectActive( EF_NODRAW ) ) return false"
			if (pEntity->m_fEffects() & EF_NODRAW)
				continue;

			float flCost = fnComputeCost(pEntity);
			if (flCost < flBestCost)
			{
				flBestCost = flCost;
				pBestSupply = pEntity;
			}
		}

		// Ported from TF2 bot's CHealthFilter: also search for friendly dispensers
		const int nHighestIndex = I::ClientEntityList->GetHighestEntityIndex();
		for (int i = 1; i <= nHighestIndex; i++)
		{
			IClientEntity* pClientEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pClientEntity || pClientEntity->IsDormant())
				continue;

			auto pNetworkable = pClientEntity->GetClientNetworkable();
			if (!pNetworkable)
				continue;

			auto pClientClass = pNetworkable->GetClientClass();
			if (!pClientClass)
				continue;

			const auto nClassId = static_cast<ETFClassIds>(pClientClass->m_ClassID);

			// Friendly dispenser (ported from TF2 bot's obj_dispenser check)
			if (nClassId == ETFClassIds::CObjectDispenser)
			{
				auto pEntity = pClientEntity->As<C_BaseEntity>();
				if (!pEntity)
					continue;

				// Only use friendly team dispensers
				if (pEntity->m_iTeamNum() != iTeam)
					continue;

				// Skip dispensers that aren't operational
				// TF2 bot: "if ( !dispenser->IsBuilding() && !dispenser->IsPlacing() && !dispenser->IsDisabled() )"
				auto pBuilding = pEntity->As<C_BaseObject>();
				if (pBuilding && (pBuilding->m_bPlacing() || pBuilding->m_bBuilding() || pBuilding->m_bHasSapper()))
					continue;

				float flCost = fnComputeCost(pEntity);
				if (flCost < flBestCost)
				{
					flBestCost = flCost;
					pBestSupply = pEntity;
				}
			}
		}
	}
	else
	{
		// Search ammo packs - CTFAmmoPack entities + model-based ammo packs
		// First check CTFAmmoPack class ID
		const int nHighestIndex = I::ClientEntityList->GetHighestEntityIndex();
		for (int i = 1; i < nHighestIndex; i++)
		{
			IClientEntity* pClientEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pClientEntity || pClientEntity->IsDormant())
				continue;

			auto pNetworkable = pClientEntity->GetClientNetworkable();
			if (!pNetworkable)
				continue;

			auto pClientClass = pNetworkable->GetClientClass();
			if (!pClientClass)
				continue;

			const auto nClassId = static_cast<ETFClassIds>(pClientClass->m_ClassID);

			if (nClassId == ETFClassIds::CTFAmmoPack)
			{
				auto pEntity = pClientEntity->As<C_BaseEntity>();
				if (!pEntity)
					continue;

				// Skip picked-up items (EF_NODRAW = invisible, waiting to respawn)
				if (pEntity->m_fEffects() & EF_NODRAW)
					continue;

				float flCost = fnComputeCost(pEntity);
				if (flCost < flBestCost)
				{
					flBestCost = flCost;
					pBestSupply = pEntity;
				}
			}

			// Friendly dispenser also provides ammo (ported from TF2 bot's CAmmoFilter)
			if (nClassId == ETFClassIds::CObjectDispenser)
			{
				auto pEntity = pClientEntity->As<C_BaseEntity>();
				if (!pEntity)
					continue;

				// Only use friendly team dispensers
				if (pEntity->m_iTeamNum() != iTeam)
					continue;

				// Skip dispensers that aren't operational
				auto pBuilding = pEntity->As<C_BaseObject>();
				if (pBuilding && (pBuilding->m_bPlacing() || pBuilding->m_bBuilding() || pBuilding->m_bHasSapper()))
					continue;

				float flCost = fnComputeCost(pEntity);
				if (flCost < flBestCost)
				{
					flBestCost = flCost;
					pBestSupply = pEntity;
				}
			}
		}

		// Also check model-based ammo packs from entity group
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::AMMOPACKS))
		{
			if (!pEntity || pEntity->IsDormant())
				continue;

			// Skip picked-up items (EF_NODRAW = invisible, waiting to respawn)
			if (pEntity->m_fEffects() & EF_NODRAW)
				continue;

			float flCost = fnComputeCost(pEntity);
			if (flCost < flBestCost)
			{
				flBestCost = flCost;
				pBestSupply = pEntity;
			}
		}
	}

	if (pBestSupply)
	{
		// Use GetCenter() (WorldSpaceCenter approximation) instead of m_vecOrigin (base position)
		// TF2 bot paths to WorldSpaceCenter — this is the actual pickup point, not the floor position
		vOut = pBestSupply->GetCenter();
		m_pTargetEntity = pBestSupply;
		m_pSupplyEntity = pBestSupply;
		m_iSupplyEntityIdx = pBestSupply->entindex();
		m_bSupplyIsDispenser = (pBestSupply->GetClassId() == ETFClassIds::CObjectDispenser);
		return true;
	}

	return false;
}

void CNavBot::PickRandomWanderTarget(C_TFPlayer* pLocal)
{
	if (!pLocal)
		return;

	float flCurTime = I::GlobalVars->curtime;

	// Clear visited areas if they get too large or after some time (like Amalgam's Roam)
	if (flCurTime >= m_flNextVisitedClearTime || m_vVisitedAreaCenters.size() > 40)
	{
		m_vVisitedAreaCenters.clear();
		m_nConsecutiveFails = 0;
		m_flNextVisitedClearTime = flCurTime + 60.0f;
	}

	// Reset wander target
	m_vWanderTarget = Vec3(0, 0, 0);

	// Check if NavEngine has a valid nav mesh loaded
	if (!G_NavEngine.IsNavMeshLoaded())
		return;

	auto& vAreas = G_NavEngine.m_pMap->m_navfile.m_vAreas;
	if (vAreas.empty())
		return;

	Vec3 vLocalPos = pLocal->m_vecOrigin();
	std::vector<CNavArea*> vValidAreas;

	// Filter valid areas (ported from Amalgam's Roam.cpp)
	for (auto& tArea : vAreas)
	{
		// Skip areas marked as AVOID
		if (tArea.m_iAttributeFlags & NAV_MESH_AVOID)
			continue;

		// Skip spawn rooms (like Amalgam: "Dont run in spawn bitch")
		if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED))
			continue;

		// Skip if we recently visited an area near this one (750 unit radius, like Amalgam)
		bool bTooCloseToVisited = false;
		for (const auto& vVisited : m_vVisitedAreaCenters)
		{
			if (tArea.m_vCenter.DistTo(vVisited) < 750.0f)
			{
				bTooCloseToVisited = true;
				break;
			}
		}
		if (bTooCloseToVisited)
			continue;

		// Skip areas that are too close to us (like Amalgam's 500 unit minimum)
		float flDist = tArea.m_vCenter.DistTo(vLocalPos);
		if (flDist < 500.0f)
			continue;

		vValidAreas.push_back(&tArea);
	}

	// No valid areas found
	if (vValidAreas.empty())
	{
		// If we failed too many times in a row, clear visited areas (like Amalgam)
		if (++m_nConsecutiveFails >= 3)
		{
			m_vVisitedAreaCenters.clear();
			m_nConsecutiveFails = 0;
		}
		return;
	}

	// Reset fail counter since we found valid areas
	m_nConsecutiveFails = 0;

	// Bias toward NEARBY wander targets. The old farthest-first sort sent the bot
	// across the whole map through the longest chain of crumbs, and any mesh
	// inaccuracy anywhere along that chain meant minutes of walking into walls.
	// Casual/community maps are small — nearest-first still tours the whole map
	// over time because visited areas carry 750u exclusion zones.
	std::sort(vValidAreas.begin(), vValidAreas.end(), [&vLocalPos](CNavArea* a, CNavArea* b) {
		if (!a || !b) return false;
		return a->m_vCenter.DistToSqr(vLocalPos) < b->m_vCenter.DistToSqr(vLocalPos);
	});

	// Shuffle only among the closest few for variety (like Amalgam's 20% swap
	// chance, but constrained to the near pool so randomness can't pick the far
	// corner of the map)
	{
		const size_t uVarietyPool = (std::min)(vValidAreas.size(), static_cast<size_t>(8));
		for (size_t i = 0; i < uVarietyPool; ++i)
		{
			if (static_cast<float>(rand()) / RAND_MAX < 0.25f)
			{
				size_t j = (i + 1 < uVarietyPool) ? i + 1 : (i > 0 ? i - 1 : i);
				if (i != j)
					std::swap(vValidAreas[i], vValidAreas[j]);
			}
		}
	}

	// Try to path to each candidate area until one succeeds (like Amalgam's Roam).
	// Capped at 10 attempts — a fully unreachable candidate set would otherwise
	// A*-search the entire mesh once per area on every call.
	size_t uAttempts = 0;
	for (auto pArea : vValidAreas)
	{
		if (!pArea)
			continue;

		if (++uAttempts > 10)
			break;

		// Validate that we can actually path there before committing
		if (G_NavEngine.NavTo(pArea->m_vCenter, NAV_PRIO_PATROL))
		{
			m_vWanderTarget = pArea->m_vCenter;
			m_vVisitedAreaCenters.push_back(pArea->m_vCenter);
			return;
		}
	}

	// All areas failed to path - count it so repeated failures eventually clear
	// the visited-areas list (otherwise a fully "visited" map stalls forever)
	if (++m_nConsecutiveFails >= 3)
	{
		m_vVisitedAreaCenters.clear();
		m_nConsecutiveFails = 0;
	}
}

// ============================================================
// === Goal machine (recode of the old per-tick job chain) ===
// Ported from the TF2 bot's NextBot action stack:
//   CTFBotScenarioMonitor  -> ONE committed main goal per life-ish
//   CTFBotTacticalMonitor  -> reflex interrupts (dodge / retreat /
//                             spawn exit / supplies) that SUSPEND the
//                             main goal and RESUME it when done
// The old chain re-evaluated every job every tick with only scattered
// 0.5s throttles, so goals kept stealing each other's paths — the
// "changes path, cancels it, changes back, cancels again" loop.
// Commitment rules:
//   - a main goal is picked ONLY when idle or preempted by an event
//     (Follow tag appears / objective appears under an idle goal)
//   - goal helpers return TRUE while their target is valid even if
//     this tick's NavTo was rate-limited — validity is not pathability
//   - every NavTo except the goal's initial one is timer-gated
// ============================================================

void CNavBot::Run(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	// Auto join team/class (ported from Amalgam's AutoJoin) - runs before alive check
	// so we can join team/class even when dead or not yet on a team
	// Note: these may send ClientCmd_Unrestricted which can change player state,
	// so we re-fetch pLocal afterwards to avoid stale pointers
	AutoJoinTeam(pLocal);
	AutoJoinClass(pLocal);

	// Re-fetch local player after auto join commands may have changed game state
	pLocal = H::Entities->GetLocal();

	if (!pLocal || !pLocal->IsAlive())
	{
		m_bActive = false;
		m_pTargetEntity = nullptr;
		G_NavEngine.CancelPath();
		m_bWasDeadLastTick = true;
		// TF2: death -> CTFBotDead -> fresh CTFBotMainAction on respawn,
		// which re-picks the scenario action from scratch
		m_eGoal = GOAL_NONE;
		m_eSuspendedGoal = GOAL_NONE;
		return;
	}

	float flCurTime = I::GlobalVars->curtime;

	// === Death pause: stop moving for a duration after respawning ===
	if (CFG::NavBot_DeathPause)
	{
		// Detect respawn (was dead last tick, now alive)
		if (m_bWasDeadLastTick)
		{
			m_flDeathPauseEndTime = flCurTime + CFG::NavBot_DeathPauseDuration;
			m_bWasDeadLastTick = false;
		}

		// While pause is active, cancel path and suppress movement
		if (flCurTime < m_flDeathPauseEndTime)
		{
			G_NavEngine.CancelPath();
			return;
		}
	}
	else
	{
		m_bWasDeadLastTick = false;
	}

	Vec3 vLocalPos = pLocal->m_vecOrigin();

	// === Per-tick teleport / round-reset detection ===
	// If player suddenly moved a huge distance since last tick (respawn, round reset),
	// the old path is completely invalid - cancel it immediately
	if (G_NavEngine.IsPathing() && m_bHasLastTickPos)
	{
		float flDistFromLast = vLocalPos.DistTo(m_vLastTickPos);
		if (flDistFromLast > CFG::NavBot_TeleportThreshold)
		{
			G_NavEngine.CancelPath();
			m_nStuckCount = 0;
			m_nStuckCheckTicks = 0;
			m_bStuckWiggle = false;
			m_flNextWanderTime = 0.0f;
			m_flNextRepathTime = 0.0f;
			m_flNextFollowRepathTime = 0.0f;
		}
	}
	m_vLastTickPos = vLocalPos;
	m_bHasLastTickPos = true;

	// Stuck anchor tracking (Valve StuckMonitor-style, works with and without a
	// nav mesh): re-arm whenever we're actually moving. ApplyStuckWiggle — called
	// at the END of movement, inside MoveAndFinish — reacts to the time since
	// the last re-arm.
	if (m_flStuckAnchorTime == 0.0f || pLocal->m_vecVelocity().Length2D() > 40.0f)
	{
		m_vStuckCheckPos = vLocalPos;
		m_flStuckAnchorTime = flCurTime;
	}

	// Movement lock (Unibox NavRuntime::IsMovementLocked — the REAL "wait for
	// setup" gate): during waiting-for-players, preround (gates closed) and
	// between-rounds Unibox's bot simply cannot move — its job system stands
	// down and its engine cancels any live path. The old SetupWander roam only
	// covered BLU standing inside a spawn-flagged nav area and silently did
	// nothing whenever the game-rules read or the area flags disagreed; the
	// lock holds for both teams everywhere. When the round goes live the lock
	// lifts and EscapeSpawn walks the bot out of spawn.
	if (CFG::NavBot_WaitForSetup && G_NavEngine.IsMovementLocked())
	{
		if (G_NavEngine.IsPathing())
			G_NavEngine.CancelPath();

		return;
	}

	// ================= Per-tick upkeep =================
	if (G_NavEngine.IsPathing())
		G_NavEngine.UpdateStuckTime(pLocal);

	UpdateCloseEnemies(pLocal);

	if (G_NavEngine.IsPathing())
		G_NavEngine.VischeckPath(pLocal);

	// Unibox UpdateSlot: refresh the active slot every tick (the weapon
	// switcher only reads it on its own 0.2s cadence) and compute which slot
	// needs reloading so SwitchWeapon can prioritize it
	{
		auto pActiveWep = pLocal->m_hActiveWeapon().Get();
		auto pActiveWeaponBase = pActiveWep ? pActiveWep->As<C_TFWeaponBase>() : nullptr;
		m_iCurrentSlot = pActiveWeaponBase ? pActiveWeaponBase->GetSlot() : -1;
		m_iLastReloadSlot = GetReloadWeaponSlot(pLocal);
	}

	SwitchWeapon(pLocal);
	UpdateDangerBlacklist(pLocal);

	// ================= Interrupt layer =================
	// TF2's CTFBotTacticalMonitor runs ABOVE the scenario action: reflexes
	// suspend the main goal, finish, and it resumes — they never re-decide
	// what the main goal should be.

	// 1) Projectile dodge — pure reflex, checked every tick (cheap scan)
	if (EscapeProjectiles(pLocal))
	{
		if (m_eGoal != GOAL_ESCAPE_PROJECTILE)
			BeginInterrupt(GOAL_ESCAPE_PROJECTILE);
		MoveAndFinish(pLocal, pCmd, flCurTime);
		return;
	}
	if (m_eGoal == GOAL_ESCAPE_PROJECTILE)
		ResumeFromInterrupt();   // dodge complete (helper canceled its own path)

	// 2) Danger retreat — TF2 maintains this at 0.3-0.5s; while escaping, the
	//    helper's upkeep is cheap (validates the escape target area and returns)
	if (m_eGoal == GOAL_ESCAPE_DANGER)
	{
		if (EscapeDanger(pLocal))
		{
			MoveAndFinish(pLocal, pCmd, flCurTime);
			return;
		}
		ResumeFromInterrupt();   // danger cleared (helper canceled + set cooldown)
	}
	else if (flCurTime >= m_flNextDangerCheckTime)
	{
		m_flNextDangerCheckTime = flCurTime + 0.4f;
		if (EscapeDanger(pLocal))
		{
			BeginInterrupt(GOAL_ESCAPE_DANGER);
			MoveAndFinish(pLocal, pCmd, flCurTime);
			return;
		}
	}

	// 3) Spawn exit — cheap area-flag check every tick (solve throttled inside)
	if (EscapeSpawn(pLocal))
	{
		if (m_eGoal != GOAL_ESCAPE_SPAWN)
			BeginInterrupt(GOAL_ESCAPE_SPAWN);
		MoveAndFinish(pLocal, pCmd, flCurTime);
		return;
	}
	if (m_eGoal == GOAL_ESCAPE_SPAWN)
		ResumeFromInterrupt();   // out of the spawn room (helper canceled)

	// 4) Supply fetch — TF2 checks health before ammo (GetHealth/GetAmmo)
	if (RunSupplyInterrupt(pLocal, pCmd))
	{
		MoveAndFinish(pLocal, pCmd, flCurTime);
		return;
	}
	// (a released run restored the suspended goal and fell through here)

	// 5) Reload retreat (Unibox RunReload): while a ranged slot is reloading
	//    and the closest enemy is known, back off to a spot that breaks their
	//    sightline. Only interrupts the idle goals — capture/defend/escort
	//    keep their path (Unibox's RunReload sits below Capture too)
	{
		const bool bIdleGoal = m_eGoal == GOAL_WANDER || m_eGoal == GOAL_STAYNEAR ||
			m_eGoal == GOAL_SNIPER_SPOT || m_eGoal == GOAL_GROUP || m_eGoal == GOAL_RUN_RELOAD;
		if (bIdleGoal && RunReloadInterrupt(pLocal))
		{
			if (m_eGoal != GOAL_RUN_RELOAD)
				BeginInterrupt(GOAL_RUN_RELOAD);

			// Unibox UpdateRunReloadInput: hold reload while retreating so the
			// clip tops up even with cl_autoreload off
			if ((m_iCurrentSlot >= WEAPON_SLOT_PRIMARY && m_iCurrentSlot <= WEAPON_SLOT_SECONDARY)
				&& (m_iCurrentSlot == m_iLastReloadSlot || G::bReloading))
				pCmd->buttons |= IN_RELOAD;

			MoveAndFinish(pLocal, pCmd, flCurTime);
			return;
		}
		if (m_eGoal == GOAL_RUN_RELOAD)
			ResumeFromInterrupt();   // reload finished / nothing to hide from
	}

	// ================= Main goal layer =================
	if (m_eGoal == GOAL_NONE)
	{
		if (flCurTime >= m_flNextGoalScanTime)
		{
			m_flNextGoalScanTime = flCurTime + 0.5f;   // failed picks retry at 0.5s
			PickMainGoal(pLocal);
		}
	}
	else if (flCurTime >= m_flNextGoalScanTime)
	{
		// TF2-style event preemption at the point re-eval cadence (1-2s):
		// a Follow tag is explicit user intent and always wins; a live
		// objective pulls the bot out of escort/idle goals — TF2 bots drop
		// what they're doing to play the objective
		m_flNextGoalScanTime = flCurTime + 1.5f;

		if (m_eGoal != GOAL_FOLLOW_TAGGED && CFG::NavBot_FollowTaggedPlayers && HasTaggedTarget(pLocal))
		{
			ClearGoal();
			m_eGoal = GOAL_FOLLOW_TAGGED;
			m_flNextFollowScanTime = 0.0f;
		}
		else if ((m_eGoal == GOAL_WANDER || m_eGoal == GOAL_STAYNEAR || m_eGoal == GOAL_SNIPER_SPOT ||
		          m_eGoal == GOAL_FOLLOW_TEAMMATE || m_eGoal == GOAL_DEFEND || m_eGoal == GOAL_GROUP) &&
			CFG::NavBot_CaptureObjectives && flCurTime >= m_flEscapeCooldownEnd)
		{
			Vec3 vObjectivePos;
			if (FindObjective(pLocal, vObjectivePos) && !vObjectivePos.IsZero())
			{
				ClearGoal();
				m_eGoal = GOAL_CAPTURE;
				m_flNextObjectiveScanTime = 0.0f;
				NavDebug("preempt: objective live, goal -> CAPTURE");
			}
		}

		// Idle-tier displacement (Unibox job scoring): roam-family jobs (defend,
		// wander) run at 140 + 140 active stickiness = 280, while StayNear
		// scores 340+ the moment any enemy is in stalk range — in Unibox a
		// defending bot is pulled off its cover spot as soon as something to
		// stalk appears and only stands its post when nothing else scores.
		// Our committed goals had no such displacement: with defend on, the
		// re-arm below reclaimed every idle goal and the bot parked at its
		// cover spot all round — the "defend freezes the bot" bug.
		if ((m_eGoal == GOAL_WANDER || m_eGoal == GOAL_DEFEND || m_eGoal == GOAL_SNIPER_SPOT ||
		     m_eGoal == GOAL_FOLLOW_TEAMMATE || m_eGoal == GOAL_GROUP) &&
			CFG::NavBot_StalkEnemies && G_NavEngine.IsNavMeshLoaded())
		{
			// StayNear's own target predicate (known origin, within 2000u, not
			// holed up in their spawn room) — only displace when stalking would
			// actually run, or we'd flap goals against an unstalkable enemy
			auto bWouldStalk = [&](C_TFPlayer* pEnemy) -> bool
			{
				Vec3 vOrigin;
				if (!GetDormantOrigin(pEnemy->entindex(), vOrigin))
					return false;
				const Vec3 vDelta = pLocal->m_vecOrigin() - vOrigin;
				if (vDelta.Length2D() > 2000.0f)
					return false;

				CNavArea* pTargetArea = G_NavEngine.m_pMap->FindClosestNavArea(vOrigin);
				if (pTargetArea)
				{
					const bool bInEnemySpawn = (pLocal->m_iTeamNum() == TF_TEAM_RED && (pTargetArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE)) ||
						(pLocal->m_iTeamNum() == TF_TEAM_BLUE && (pTargetArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED));
					if (bInEnemySpawn)
						return false;
				}
				return true;
			};

			bool bStalkTarget = false;
			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
			{
				auto pEnemy = pEntity ? pEntity->As<C_TFPlayer>() : nullptr;
				if (pEnemy && pEnemy->IsAlive() && bWouldStalk(pEnemy))
				{
					bStalkTarget = true;
					break;
				}
			}

			if (bStalkTarget)
			{
				ClearGoal();
				m_eGoal = GOAL_STAYNEAR;
				m_flNextStayNearCheck = 0.0f;
				NavDebug("preempt: stalk target in range, goal -> STAYNEAR");
			}
		}

		// Defend re-arm: the anchor can vanish temporarily (payload cart went
		// dormant behind the enemy gate) and defend then falls back to wander,
		// which never yields the goal again on its own. Re-arm the moment an
		// anchor exists again — capture keeps its precedence above. StayNear is
		// excluded: an active stalk (340 + 140 stickiness in Unibox) outranks
		// roam's 140, so a bot mid-stalk keeps stalking until its target dies
		// and StayNear() itself releases the goal back to the ladder.
		if ((m_eGoal == GOAL_WANDER || m_eGoal == GOAL_SNIPER_SPOT ||
		     m_eGoal == GOAL_FOLLOW_TEAMMATE || m_eGoal == GOAL_GROUP) &&
			CFG::NavBot_DefendObjectives)
		{
			Vec3 vAnchor;
			if (FindDefendAnchor(pLocal, vAnchor) && !vAnchor.IsZero())
			{
				ClearGoal();
				m_eGoal = GOAL_DEFEND;
				m_flNextDefendScanTime = 0.0f;
				NavDebug("preempt: defend anchor live, goal -> DEFEND");
			}
		}
	}

	switch (m_eGoal)
	{
	case GOAL_FOLLOW_TAGGED:
		if (!FollowTaggedPlayer(pLocal))
			ClearGoal();
		break;
	case GOAL_FOLLOW_TEAMMATE:
		if (!FollowTeammates(pLocal))
			ClearGoal();
		break;
	case GOAL_CAPTURE:
		if (!CaptureTick(pLocal))
			ClearGoal();
		break;
	case GOAL_DEFEND:
		if (!DefendTick(pLocal))
			ClearGoal();
		break;
	case GOAL_STAYNEAR:
		if (!StayNear(pLocal))
			ClearGoal();
		break;
	case GOAL_SNIPER_SPOT:
		if (!SniperSpot(pLocal))
			ClearGoal();
		break;
	case GOAL_GROUP:
		if (!GroupTick(pLocal))
			ClearGoal();
		break;
	case GOAL_WANDER:
		WanderTick(pLocal);
		break;
	default:
		break;
	}

	MoveAndFinish(pLocal, pCmd, flCurTime);
}

// === Main-goal picker (TF2 ScenarioMonitor::DesiredScenarioAndClassAction) ===
// Runs only when no goal is active. One goal is committed to and held until
// its helper reports it finished/invalid — never re-decided per tick.
bool CNavBot::PickMainGoal(C_TFPlayer* pLocal)
{
	if (!pLocal)
		return false;

	m_flGoalStartTime = I::GlobalVars->curtime;

	// Explicit user tag first (TF2 "mission" intent), then the map objective,
	// then teammate escort, then the idle behaviors. TF2 default bots always
	// play the objective — escort is this cheat's extra, so it only takes over
	// when there's nothing to capture this instant (point locked, all owned).
	// When a new objective appears later, the preemption scan below converts
	// an escort goal into a capture goal.
	// Each helper returns false when it has nothing to work with, and we fall
	// to the next.
	if (CFG::NavBot_FollowTaggedPlayers)
	{
		m_eGoal = GOAL_FOLLOW_TAGGED;
		m_flNextFollowScanTime = 0.0f;
		if (FollowTaggedPlayer(pLocal))
		{
			NavDebug("goal: FOLLOW_TAGGED");
			return true;
		}
	}

	if (CFG::NavBot_CaptureObjectives && I::GlobalVars->curtime >= m_flEscapeCooldownEnd)
	{
		m_eGoal = GOAL_CAPTURE;
		m_flNextObjectiveScanTime = 0.0f;
		if (CaptureTick(pLocal))
		{
			NavDebug("goal: CAPTURE");
			return true;
		}
	}

	// Nothing to capture right now — defend what the enemy is pushing
	// (Unibox runs defend inside roam, below capture)
	if (CFG::NavBot_DefendObjectives)
	{
		m_eGoal = GOAL_DEFEND;
		if (DefendTick(pLocal))
		{
			NavDebug("goal: DEFEND");
			return true;
		}
	}

	if (CFG::NavBot_FollowTeammates)
	{
		m_eGoal = GOAL_FOLLOW_TEAMMATE;
		m_flNextFollowScanTime = 0.0f;
		if (FollowTeammates(pLocal))
		{
			NavDebug("goal: FOLLOW_TEAMMATE");
			return true;
		}
	}

	if (CFG::NavBot_StalkEnemies)
	{
		m_eGoal = GOAL_STAYNEAR;
		if (StayNear(pLocal))
			return true;
	}

	if (CFG::NavBot_SniperSpots)
	{
		m_eGoal = GOAL_SNIPER_SPOT;
		if (SniperSpot(pLocal))
			return true;
	}

	// Formation-follow sits between the "real" idle behaviors and raw wander
	// (Unibox scores GroupWithOthers 180-210, above Roam's 110-140)
	if (CFG::NavBot_GroupWithOthers)
	{
		m_eGoal = GOAL_GROUP;
		if (GroupTick(pLocal))
		{
			NavDebug("goal: GROUP (slot %d)", m_iPositionInFormation);
			return true;
		}
	}

	if (CFG::NavBot_WanderWhenIdle)
	{
		m_eGoal = GOAL_WANDER;
		WanderTick(pLocal);
		return true;
	}

	m_eGoal = GOAL_NONE;
	return false;
}

// === ComputeCaptureSpot (ported from Unibox Capture.cpp GetControlPointGoal) ===
// Spread bots around the point instead of stacking on its center: teammates
// within 28u of a candidate claim it, so we take a ring slot keyed by our
// entindex (with ring fallbacks when the close slots are taken). When the
// point is contested (enemy within 800u) and we're not on it yet, approach
// via the nearest IN_COVER spot instead of walking exposed into the fight.
Vec3 CNavBot::ComputeCaptureSpot(C_TFPlayer* pLocal, const Vec3& vPoint)
{
	// The point itself changed (map progression, round flip) — drop the old slot
	if (!m_bHasCaptureSpot || m_vCaptureSpotCenter.DistToSqr(vPoint) > 1.0f)
	{
		m_vCaptureSpotCenter = vPoint;
		m_bHasCaptureSpot = false;
		m_vCaptureSpotPos = Vec3(0, 0, 0);
	}

	constexpr float flCapRadius = 100.0f;
	constexpr float flThreatRadius = 800.0f;
	constexpr float flOccupancyRadiusSq = 28.0f * 28.0f;
	const int iLocalIndex = I::EngineClient->GetLocalPlayer();

	std::vector<Vec3> vTeammates;
	vTeammates.reserve(8);
	int iTeammatesOnPoint = 0;
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_TEAMMATES))
	{
		auto pTeammate = pEntity ? pEntity->As<C_TFPlayer>() : nullptr;
		if (!pTeammate || pTeammate->entindex() == iLocalIndex || !pTeammate->IsAlive() || pTeammate->IsDormant())
			continue;

		Vec3 vPos = pTeammate->m_vecOrigin();
		vTeammates.push_back(vPos);
		if (vPos.DistTo(vPoint) <= flCapRadius)
			iTeammatesOnPoint++;
	}

	auto SpotTaken = [&](const Vec3& vSpot) -> bool
		{
			for (const auto& vPos : vTeammates)
			{
				const float flDx = vPos.x - vSpot.x;
				const float flDy = vPos.y - vSpot.y;
				if (flDx * flDx + flDy * flDy <= flOccupancyRadiusSq)
					return true;
			}
			return false;
		};

	// Contested approach: enemies near the point and we're not on it yet.
	// (Unibox also checks IsCapturing — we approximate "already capping" with
	// "we're standing on the point", so a cap in progress is never abandoned.)
	bool bEnemiesNear = false;
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		auto pEnemy = pEntity ? pEntity->As<C_TFPlayer>() : nullptr;
		if (!pEnemy || !pEnemy->IsAlive() || pEnemy->IsDormant())
			continue;
		if (pEnemy->m_vecOrigin().DistTo(vPoint) <= flThreatRadius)
		{
			bEnemiesNear = true;
			break;
		}
	}

	const bool bOnPoint = (pLocal->m_vecOrigin() - vPoint).Length2D() <= flCapRadius;
	if (bEnemiesNear && !bOnPoint)
	{
		m_bHasCaptureSpot = false;
		Vec3 vCover = G_NavEngine.m_pMap->FindCoverSpot(vPoint, flCapRadius, pLocal->m_iTeamNum());
		if (!vCover.IsZero())
			return vCover;
		return vPoint;
	}

	// Slot upkeep: a teammate walked into our slot — pick a new one
	if (m_bHasCaptureSpot && SpotTaken(m_vCaptureSpotPos))
		m_bHasCaptureSpot = false;

	if (!m_bHasCaptureSpot)
	{
		const int iSlots = std::clamp(iTeammatesOnPoint + 1, 1, 8);
		const float flBaseRadius = iSlots == 1 ? 0.0f : std::min(flCapRadius - 12.0f, 45.0f + 12.0f * static_cast<float>(iSlots - 1));
		const int iPreferredSlot = iLocalIndex % iSlots;

		std::vector<Vec3> vFallbacks;
		vFallbacks.reserve(static_cast<size_t>(iSlots) + 12);

		auto TryCandidate = [&](float flAngle, float flRadius) -> bool
			{
				Vec3 vCandidate = vPoint;
				if (flRadius > 1.0f)
				{
					vCandidate.x += std::cos(flAngle) * flRadius;
					vCandidate.y += std::sin(flAngle) * flRadius;
				}

				vCandidate = AdjustCaptureCandidateToNav(vCandidate);
				vFallbacks.push_back(vCandidate);
				if (SpotTaken(vCandidate))
					return false;

				m_vCaptureSpotPos = vCandidate;
				return true;
			};

		constexpr float flPI = 3.14159265f;
		for (int iOffset = 0; iOffset < iSlots && !m_bHasCaptureSpot; ++iOffset)
		{
			const int iSlotIndex = (iPreferredSlot + iOffset) % iSlots;
			m_bHasCaptureSpot = TryCandidate(static_cast<float>(iSlotIndex) / static_cast<float>(iSlots) * flPI * 2.0f, flBaseRadius);
		}

		for (int iRing = 1; iRing <= 2 && !m_bHasCaptureSpot; ++iRing)
		{
			const float flRingRadius = std::min(flCapRadius - 12.0f, flBaseRadius + 14.0f * static_cast<float>(iRing));
			const int iMaxSegments = std::max(6, iSlots + iRing * 2);
			for (int iSeg = 0; iSeg < iMaxSegments && !m_bHasCaptureSpot; ++iSeg)
				m_bHasCaptureSpot = TryCandidate(static_cast<float>(iSeg) / static_cast<float>(iMaxSegments) * flPI * 2.0f, flRingRadius);
		}

		// Every slot claimed: take whichever candidate is FARTHEST from any
		// teammate (Unibox's best-of-fallbacks)
		if (!m_bHasCaptureSpot)
		{
			vFallbacks.push_back(AdjustCaptureCandidateToNav(vPoint));

			Vec3 vBest = vPoint;
			float flBestScore = -1.0f;
			for (const auto& vCandidate : vFallbacks)
			{
				float flScore = FLT_MAX;
				for (const auto& vPos : vTeammates)
				{
					const float flDx = vPos.x - vCandidate.x;
					const float flDy = vPos.y - vCandidate.y;
					flScore = std::min(flScore, flDx * flDx + flDy * flDy);
				}
				if (flScore > flBestScore)
				{
					flBestScore = flScore;
					vBest = vCandidate;
				}
			}
			m_vCaptureSpotPos = vBest;
			m_bHasCaptureSpot = true;
		}
	}

	return m_bHasCaptureSpot ? m_vCaptureSpotPos : vPoint;
}

// === Capture goal (TF2 CTFBotCapturePoint / PayloadPush) ===
// Committed march to the current objective. The objective is re-scanned every
// second (TF2 re-evaluates control points every 1-2s) so point flips, flag
// pickups and cart movement update the destination WITHOUT dropping the goal.
bool CNavBot::CaptureTick(C_TFPlayer* pLocal)
{
	if (!pLocal || !CFG::NavBot_CaptureObjectives || !G_NavEngine.IsNavMeshLoaded())
		return false;

	float flCurTime = I::GlobalVars->curtime;

	// Don't walk straight back into what we just fled from
	if (flCurTime < m_flEscapeCooldownEnd)
		return false;

	Vec3 vObjectivePos;
	if (flCurTime >= m_flNextObjectiveScanTime)
	{
		m_flNextObjectiveScanTime = flCurTime + 1.0f;
		if (!FindObjective(pLocal, vObjectivePos) || vObjectivePos.IsZero())
		{
			m_flUnreachableSince = 0.0f;
			return false;   // nothing to capture right now — goal ends, re-pick later
		}
		m_vTargetPos = vObjectivePos;
		m_flUnreachableSince = 0.0f;
	}
	vObjectivePos = m_vTargetPos;

	// Reachability memo (2s) — the full A* connectivity check is far too heavy
	// per tick, and objectives (cart, dropped flag) barely move between checks
	bool bReachable = false;
	if (!m_vReachCachePos.IsZero() && vObjectivePos.DistTo(m_vReachCachePos) < 100.0f && flCurTime < m_flReachCacheUntil)
	{
		bReachable = m_bReachCacheResult;
	}
	else
	{
		bReachable = IsReachable(pLocal, vObjectivePos);
		m_bReachCacheResult = bReachable;
		m_vReachCachePos = vObjectivePos;
		m_flReachCacheUntil = flCurTime + 2.0f;
	}

	if (!bReachable)
	{
		// Stay committed for a few seconds of continuous failure before giving
		// the goal up (dropping it instantly would re-run FindObjective per tick)
		if (m_flUnreachableSince == 0.0f)
			m_flUnreachableSince = flCurTime;
		if (flCurTime - m_flUnreachableSince > 4.0f)
			return false;
		return true;
	}
	m_flUnreachableSince = 0.0f;

	// Arrived: stand on the point / push the cart by proximity.
	// (TF2 wanders small random spots ON the point every 0.5-1s; the slot ring
	// below spreads multiple bots across the point instead of stacking them)
	const bool bIsCart = m_pTargetEntity && m_pTargetEntity->GetClassId() == ETFClassIds::CObjectCartDispenser;
	const bool bIsControlPoint = m_pTargetEntity && m_pTargetEntity->GetClassId() == ETFClassIds::CTFObjectiveResource;

	// Slot the position we'll actually stand on (Unibox capture-spot ring /
	// contested-cover approach). Staging holds don't spread — one post.
	if (bIsControlPoint && !m_bObjectiveStaging)
		vObjectivePos = ComputeCaptureSpot(pLocal, vObjectivePos);

	const float flArriveRadius = m_bObjectiveStaging ? 120.0f
		: bIsCart ? 140.0f
		: bIsControlPoint ? 45.0f : 120.0f;
	if ((pLocal->m_vecOrigin() - vObjectivePos).Length2D() < flArriveRadius)
	{
		if (G_NavEngine.IsPathing() && G_NavEngine.m_eCurrentPriority == NAV_PRIO_CAPTURE)
			G_NavEngine.CancelPath();
		return true;
	}

	// Repath discipline (TF2 capture march: 2-3s; the cart gets faster updates).
	// While the current path still aims at the objective, keep walking it.
	const bool bPathAimsAtObjective = G_NavEngine.IsPathing() &&
		G_NavEngine.m_eCurrentPriority == NAV_PRIO_CAPTURE &&
		G_NavEngine.GetDestination().DistTo(vObjectivePos) < 200.0f;
	if (bPathAimsAtObjective)
		return true;

	if (flCurTime < m_flNextRepathTime)
		return true;   // goal still valid — the re-solve is just timer-gated

	if (G_NavEngine.NavTo(vObjectivePos, NAV_PRIO_CAPTURE))
		m_flNextRepathTime = flCurTime + (bIsCart ? 1.0f : 2.5f);
	else
		m_flNextRepathTime = flCurTime + 0.5f;   // solve failed / rate-limited — retry soon
	return true;
}

// === Wander goal (TF2 SeekAndDestroy roam) ===
// Committed to one area at a time; a new target is only picked when the
// current one is reached or the path died.
void CNavBot::WanderTick(C_TFPlayer* pLocal)
{
	if (!pLocal || !CFG::NavBot_WanderWhenIdle)
	{
		m_eGoal = GOAL_NONE;
		return;
	}

	// Meshless maps: MoveAndFinish's blind wander owns target picking —
	// PickRandomWanderTarget would just wipe the blind target every call
	if (!G_NavEngine.IsNavMeshLoaded())
		return;

	float flCurTime = I::GlobalVars->curtime;

	if (G_NavEngine.IsPathing() && G_NavEngine.m_eCurrentPriority == NAV_PRIO_PATROL)
		return;   // committed to the current roam target

	if (flCurTime < m_flNextWanderTime)
		return;

	m_flNextWanderTime = flCurTime + 0.5f;
	PickRandomWanderTarget(pLocal);   // validates + NavTos internally

	if (!m_vWanderTarget.IsZero())
		m_vTargetPos = m_vWanderTarget;
}

// === FindDefendAnchor (ported from Unibox Roam's GetDefendTarget) ===
// The objective the ENEMY team is pushing: their cart on payload maps, or the
// closest control point they can take. Unibox deliberately skips plain CTF
// here (camping the intel room all round looks wrong), so we do too.
bool CNavBot::FindDefendAnchor(C_TFPlayer* pLocal, Vec3& vOut)
{
	if (!pLocal)
		return false;

	const Vec3 vLocalPos = pLocal->m_vecOrigin();
	const int nLocalTeam = pLocal->m_iTeamNum();
	const int nEnemyTeam = (nLocalTeam == TF_TEAM_RED) ? TF_TEAM_BLUE : TF_TEAM_RED;

	// Payload: guard the enemy's cart (ours gets pushed, theirs gets stopped).
	// Unibox skips dormant payloads in GetClosestPayload and, when no cart is
	// visible, simply doesn't defend that tick (roam keeps the bot moving).
	// Falling through to the control-point anchor instead parked the bot on a
	// FIXED point all round while the cart sat dormant behind the enemy gate —
	// the "defend freezes on payload" bug. On payload maps the cart is the
	// only valid anchor: no visible enemy cart right now means no anchor.
	{
		const int nHighestIndex = I::ClientEntityList->GetHighestEntityIndex();
		C_BaseEntity* pNearestCart = nullptr;
		float flNearestDist = FLT_MAX;
		bool bAnyCart = false;
		for (int i = 1; i < nHighestIndex; i++)
		{
			IClientEntity* pClientEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pClientEntity)
				continue;

			auto pNetworkable = pClientEntity->GetClientNetworkable();
			if (!pNetworkable)
				continue;

			auto pClientClass = pNetworkable->GetClientClass();
			if (!pClientClass || static_cast<ETFClassIds>(pClientClass->m_ClassID) != ETFClassIds::CObjectCartDispenser)
				continue;

			auto pEntity = pClientEntity->As<C_BaseEntity>();
			if (!pEntity)
				continue;

			bAnyCart = true;
			// Dormant = out of PVS behind the enemy gate; the position would
			// be stale and, worse, we'd anchor through geometry we can't see
			if (pClientEntity->IsDormant() || pEntity->m_iTeamNum() != nEnemyTeam)
				continue;

			float flDist = vLocalPos.DistTo(pEntity->m_vecOrigin());
			if (flDist < flNearestDist)
			{
				flNearestDist = flDist;
				pNearestCart = pEntity;
			}
		}

		if (pNearestCart)
		{
			vOut = pNearestCart->m_vecOrigin();
			return true;
		}

		// Payload map (a cart exists) with no visible enemy cart: DON'T fall
		// through to the fixed-CP fallback — no anchor, wander instead
		if (bAnyCart)
			return false;
	}

	// Control points: the closest point the enemy can currently take (not
	// already theirs, not locked, in the current mini-round)
	{
		C_BaseEntity* pObjectiveResource = nullptr;
		const int nHighestIndex = I::ClientEntityList->GetHighestEntityIndex();
		for (int i = 1; i < nHighestIndex; i++)
		{
			IClientEntity* pClientEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pClientEntity)
				continue;

			auto pNetworkable = pClientEntity->GetClientNetworkable();
			if (!pNetworkable)
				continue;

			auto pClientClass = pNetworkable->GetClientClass();
			if (!pClientClass)
				continue;

			if (static_cast<ETFClassIds>(pClientClass->m_ClassID) == ETFClassIds::CTFObjectiveResource)
			{
				pObjectiveResource = pClientEntity->As<C_BaseEntity>();
				break;
			}
		}

		if (pObjectiveResource)
		{
			static int nNumCPsOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_iNumControlPoints");
			static int nCPPositionsOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_vCPPositions[0]");
			static int nOwnerOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_iOwner");
			static int nCPLockedOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_bCPLocked");
			static int nTeamCanCapOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_bTeamCanCap");
			static int nInMiniRoundOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_bInMiniRound");
			static int nPlayingMiniRoundsOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_bPlayingMiniRounds");

			if (nNumCPsOffset != 0 && nCPPositionsOffset != 0 && nOwnerOffset != 0)
			{
				auto pBase = reinterpret_cast<std::uintptr_t>(pObjectiveResource);
				int nNumCPs = *reinterpret_cast<int*>(pBase + nNumCPsOffset);

				if (nNumCPs > 0 && nNumCPs <= 8)
				{
					const bool bPlayingMiniRounds = nPlayingMiniRoundsOffset > 0 ? *reinterpret_cast<bool*>(pBase + nPlayingMiniRoundsOffset) : false;

					Vec3 vBestPos;
					float flBestDist = FLT_MAX;
					bool bFound = false;

					for (int i = 0; i < nNumCPs; i++)
					{
						Vec3 vCPPos = *reinterpret_cast<Vec3*>(pBase + nCPPositionsOffset + i * sizeof(Vec3));
						if (vCPPos.IsZero())
							continue;

						int nOwner = *reinterpret_cast<int*>(pBase + nOwnerOffset + i * sizeof(int));
						if (nOwner == nEnemyTeam)
							continue;   // they already hold it

						bool bLocked = nCPLockedOffset > 0 ? *reinterpret_cast<bool*>(pBase + nCPLockedOffset + i * sizeof(bool)) : false;
						if (bLocked)
							continue;

						// Can the ENEMY take this point?
						bool bEnemyCanCap = nTeamCanCapOffset > 0 ? *reinterpret_cast<bool*>(pBase + nTeamCanCapOffset + (i + nEnemyTeam * 8) * sizeof(bool)) : true;
						if (!bEnemyCanCap)
							continue;

						if (bPlayingMiniRounds && nInMiniRoundOffset > 0)
						{
							bool bInMiniRound = *reinterpret_cast<bool*>(pBase + nInMiniRoundOffset + i * sizeof(bool));
							if (!bInMiniRound)
								continue;
						}

						float flDist = vLocalPos.DistTo(vCPPos);
						if (flDist < flBestDist)
						{
							flBestDist = flDist;
							vBestPos = vCPPos;
							bFound = true;
						}
					}

					if (bFound)
					{
						vOut = vBestPos;
						return true;
					}
				}
			}
		}
	}

	return false;
}

// === Defend goal (ported from Unibox Roam's RunDefend) ===
// Hold the nearest IN_COVER spot near the enemy's objective that breaks line
// of sight to the closest threat within 1000u. Walks there once, then stands
// its ground; rescans at the Unibox roam cadence (0.5s) so the spot follows
// the fight. Runs at patrol priority — every interrupt and capture outrank it.
bool CNavBot::DefendTick(C_TFPlayer* pLocal)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded() || !CFG::NavBot_DefendObjectives)
	{
		m_pDefendSpotArea = nullptr;
		return false;
	}

	const float flCurTime = I::GlobalVars->curtime;
	const Vec3 vLocalPos = pLocal->m_vecOrigin();

	Vec3 vAnchor;
	if (!FindDefendAnchor(pLocal, vAnchor) || vAnchor.IsZero())
	{
		m_pDefendSpotArea = nullptr;
		return false;
	}

	// Committed to a defend spot and still on the way there (Unibox keeps the
	// path while we're over 250u out)
	if (m_pDefendSpotArea && G_NavEngine.m_eCurrentPriority == NAV_PRIO_PATROL &&
		G_NavEngine.IsPathing() && m_pDefendSpotArea->m_vCenter.DistTo(vLocalPos) > 250.0f)
	{
		m_vTargetPos = vAnchor;
		return true;
	}

	// Goal still valid between 0.5s rescans (Unibox roam timer semantics)
	if (m_pDefendSpotArea && !G_NavEngine.IsPathing() && flCurTime < m_flNextDefendScanTime)
	{
		m_vTargetPos = vAnchor;
		return true;
	}
	m_flNextDefendScanTime = flCurTime + 0.5f;

	// The threat our cover must break LOS to: closest living enemy near the
	// anchor (only threats within 1000u of the point matter — Unibox gate)
	Vec3 vThreatPos(0, 0, 0);
	bool bHasThreat = false;
	{
		C_TFPlayer* pClosest = nullptr;
		float flBest = 1000.0f;
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
		{
			auto pEnemy = pEntity ? pEntity->As<C_TFPlayer>() : nullptr;
			if (!pEnemy || !pEnemy->IsAlive() || pEnemy->IsDormant())
				continue;

			float flDist = pEnemy->m_vecOrigin().DistTo(vAnchor);
			if (flDist < flBest)
			{
				flBest = flDist;
				pClosest = pEnemy;
			}
		}

		if (pClosest)
		{
			vThreatPos = pClosest->m_vecOrigin();
			bHasThreat = true;
		}
	}

	// Cover spot near the anchor the threat can't see (no threat visible =
	// plain nearest IN_COVER — watch the point from behind something)
	Vec3 vSpot = G_NavEngine.m_pMap->FindDefendSpot(vAnchor, vThreatPos, bHasThreat, 400.0f, pLocal->m_iTeamNum());
	CNavArea* pSpotArea = nullptr;
	if (vSpot.IsZero())
	{
		// No cover near the objective. Unibox fails the defend here and lets
		// roam keep the bot moving — walking into the open to hug the point
		// just parked the bot next to the enemy push all round. Only body-
		// block the anchor when already standing on it (Unibox's
		// m_bOverwriteCapture stand).
		CNavArea* pAnchorArea = G_NavEngine.m_pMap->FindClosestNavArea(vAnchor, 1000.0f);
		if (!pAnchorArea || pAnchorArea->m_vCenter.DistTo(vLocalPos) > 250.0f)
		{
			m_vDefendAnchorPos = vAnchor;
			return false;
		}

		if (G_NavEngine.IsPathing() && G_NavEngine.m_eCurrentPriority == NAV_PRIO_PATROL)
			G_NavEngine.CancelPath();
		m_pDefendSpotArea = pAnchorArea;
		m_vDefendAnchorPos = vAnchor;
		m_vTargetPos = vAnchor;
		return true;
	}
	else
		pSpotArea = G_NavEngine.m_pMap->FindClosestNavArea(vSpot, 500.0f);

	if (!pSpotArea)
		return false;

	// Standing in is only allowed while the objective itself hasn't moved
	// away from the spot we solved for. On payload the cart creeps forward —
	// without this check the bot camped its first cover spot while the cart
	// (and the whole fight) inched away, looking frozen for minutes.
	const bool bAnchorMoved = m_vDefendAnchorPos.DistTo(vAnchor) > 150.0f;
	m_vDefendAnchorPos = vAnchor;

	if (!bAnchorMoved && pSpotArea->m_vCenter.DistTo(vLocalPos) <= 250.0f)
	{
		if (G_NavEngine.IsPathing() && G_NavEngine.m_eCurrentPriority == NAV_PRIO_PATROL)
			G_NavEngine.CancelPath();
		m_pDefendSpotArea = pSpotArea;
		m_vTargetPos = vAnchor;   // watch the point we're defending
		return true;
	}

	if (G_NavEngine.NavTo(vSpot, NAV_PRIO_PATROL))
	{
		m_pDefendSpotArea = pSpotArea;
		m_vTargetPos = vSpot;
		return true;
	}

	return false;
}

// === Supply interrupt (TF2 CTFBotGetHealth / CTFBotGetAmmo) ===
// TF2's tactical monitor suspends the main action for a kit and resumes it
// when done ("SuspendFor" / "Done"). This replaces the old twin 140-line
// cancellation blocks: one trigger path, one upkeep path, and the main goal
// is stored and restored instead of being fought via path priorities.
bool CNavBot::RunSupplyInterrupt(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal)
		return false;

	float flCurTime = I::GlobalVars->curtime;

	// === Active run: upkeep until a release condition fires ===
	if (m_eGoal == GOAL_SUPPLY)
	{
		// 1. Item gone (invalid / dormant / picked up: EF_NODRAW)
		//    TF2: "if ( m_healthKit == NULL || m_healthKit->IsEffectActive( EF_NODRAW ) ) return Done"
		if (!m_pSupplyEntity || !H::Entities->SafeIsEntityValid(m_pSupplyEntity, m_iSupplyEntityIdx) ||
			m_pSupplyEntity->IsDormant() || (m_pSupplyEntity->m_fEffects() & EF_NODRAW))
		{
			SupplyRelease();
			return false;
		}

		// 1b. Dispenser no longer operational (sapped / building / hauled)
		if (m_bSupplyIsDispenser)
		{
			auto pBuilding = m_pSupplyEntity->As<C_BaseObject>();
			if (pBuilding && (pBuilding->m_bPlacing() || pBuilding->m_bBuilding() || pBuilding->m_bHasSapper() || pBuilding->m_bCarryDeploy()))
			{
				SupplyRelease();
				return false;
			}
		}

		// 2. Got what we came for (TF2 get-health: "if health >= max return Done")
		if (m_bSupplyIsHealth && !ShouldSearchHealth(pLocal))
		{
			SupplyRelease();
			return false;
		}
		if (!m_bSupplyIsHealth && !ShouldSearchAmmo(pLocal))
		{
			SupplyRelease();
			return false;
		}

		// 3. A medic is healing us — no need for the kit
		//    TF2: "if ( i < me->m_Shared.GetNumHealers() ) return Done"
		if (m_bSupplyIsHealth && (pLocal->m_nPlayerCond() & (1 << 21)))  // TFCond_Healing
		{
			SupplyRelease();
			return false;
		}

		// 4. An enemy is significantly closer to the item — they'll take it first
		{
			Vec3 vSupplyPos = m_pSupplyEntity->m_vecOrigin();
			float flOurDist = pLocal->m_vecOrigin().DistTo(vSupplyPos);
			bool bEnemyCloser = false;
			const int nMaxClients = I::EngineClient->GetMaxClients();
			for (int i = 1; i <= nMaxClients && !bEnemyCloser; i++)
			{
				if (i == I::EngineClient->GetLocalPlayer())
					continue;
				auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
				if (!pClientEntity)
					continue;
				auto pPlayer = pClientEntity->As<C_TFPlayer>();
				if (!pPlayer || !pPlayer->IsAlive() || pPlayer->m_iTeamNum() == pLocal->m_iTeamNum() || pPlayer->IsDormant())
					continue;
				if (pPlayer->m_vecOrigin().DistTo(vSupplyPos) < flOurDist * 0.8f)
					bEnemyCloser = true;
			}
			if (bEnemyCloser)
			{
				SupplyRelease();
				return false;
			}
		}

		// 5. Hopelessly stuck on the way — TF2 gives up on OnStuck/OnMoveToFailure
		if (flCurTime - m_flStuckAnchorTime > 6.0f && !m_bWaitingAtDispenser)
		{
			SupplyRelease();
			return false;
		}

		// 6. Dispenser proximity — wait next to it instead of walking into it
		//    TF2: "if ( (me->GetAbsOrigin() - m_ammo->GetAbsOrigin()).IsLengthLessThan( nearRange ) )"
		if (m_bSupplyIsDispenser)
		{
			Vec3 vDispenserPos = m_pSupplyEntity->m_vecOrigin();
			float flDistToDispenser = pLocal->m_vecOrigin().DistTo(vDispenserPos);
			if (flDistToDispenser < 75.0f)
			{
				Vec3 vStart = pLocal->m_vecOrigin(); vStart.z += PLAYER_CROUCHED_JUMP_HEIGHT;
				Vec3 vEnd = vDispenserPos; vEnd.z += 40.0f;
				if (H::AimUtils->TracePositionWorld(vStart, vEnd))
				{
					bool bInCombat = (G::nTargetIndex != -1);
					bool bCriticallyLow = static_cast<float>(pLocal->m_iHealth()) / pLocal->GetMaxHealth() < 0.3f;

					// Fight instead of camping the dispenser when shot at and not critical
					// TF2: "No time to wait for more ammo, I must fight"
					if (bInCombat && !bCriticallyLow)
					{
						SupplyRelease();
						return false;
					}

					m_bWaitingAtDispenser = true;
					if (G_NavEngine.IsPathing())
						G_NavEngine.CancelPath();
					pCmd->forwardmove = 0.0f;
					pCmd->sidemove = 0.0f;
					m_vTargetPos = m_pSupplyEntity->GetCenter();
					return true;
				}
			}
		}
		m_bWaitingAtDispenser = false;

		// Keep walking to the item (re-path if the path died)
		m_vTargetPos = m_pSupplyEntity->GetCenter();
		if (!G_NavEngine.IsPathing() && flCurTime >= m_flNextRepathTime)
		{
			G_NavEngine.NavTo(m_pSupplyEntity->GetCenter(), m_bSupplyIsHealth ? NAV_PRIO_GETHEALTH : NAV_PRIO_GETAMMO);
			m_flNextRepathTime = flCurTime + 0.5f;
		}
		return true;
	}

	// === Trigger check (TF2 checks health before ammo) ===
	const bool bNeedHealth = ShouldSearchHealth(pLocal);
	const bool bNeedAmmo = ShouldSearchAmmo(pLocal);
	if (!bNeedHealth && !bNeedAmmo)
		return false;

	Vec3 vSupplyPos;
	if (!FindSupply(pLocal, vSupplyPos, bNeedHealth))   // 0.5s internal throttle
		return false;

	// While following someone, don't chase supplies that would abandon them
	// (the old FollowSupplyDistance config still bounds the detour)
	if ((m_eGoal == GOAL_FOLLOW_TEAMMATE || m_eGoal == GOAL_FOLLOW_TAGGED) && CFG::NavBot_FollowSupplyDistance > 0.0f)
	{
		const int iFollowIdx = (m_eGoal == GOAL_FOLLOW_TAGGED) ? m_iFollowTaggedTargetIdx : m_iFollowTargetIdx;
		auto pFollowedCE = (iFollowIdx > 0) ? I::ClientEntityList->GetClientEntity(iFollowIdx) : nullptr;
		auto pFollowedPlayer = pFollowedCE ? pFollowedCE->As<C_TFPlayer>() : nullptr;
		if (pFollowedPlayer && pFollowedPlayer->IsAlive() &&
			pFollowedPlayer->m_vecOrigin().DistTo(vSupplyPos) > CFG::NavBot_FollowSupplyDistance)
			return false;   // too far from who we're escorting — stay with them
	}

	if (!IsReachable(pLocal, vSupplyPos))
		return false;

	if (!G_NavEngine.NavTo(vSupplyPos, bNeedHealth ? NAV_PRIO_GETHEALTH : NAV_PRIO_GETAMMO))
		return false;   // solve rate-limited this tick — retry next tick

	m_bSupplyIsHealth = bNeedHealth;
	m_eSuspendedGoal = m_eGoal;
	m_eGoal = GOAL_SUPPLY;
	m_flNextRepathTime = flCurTime + 0.5f;
	m_vTargetPos = vSupplyPos;
	return true;
}

// End the supply run and resume what we were doing (TF2 Done -> OnResume)
void CNavBot::SupplyRelease()
{
	m_pSupplyEntity = nullptr;
	m_iSupplyEntityIdx = -1;
	m_bSupplyIsDispenser = false;
	m_bSupplyIsHealth = false;
	m_bWaitingAtDispenser = false;

	if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_GETHEALTH || G_NavEngine.m_eCurrentPriority == NAV_PRIO_GETAMMO)
		G_NavEngine.CancelPath();

	m_eGoal = m_eSuspendedGoal;
	m_eSuspendedGoal = GOAL_NONE;
	// The supply walk replaced our path — let the resumed goal re-solve now
	m_flNextRepathTime = 0.0f;
	m_flNextFollowRepathTime = 0.0f;
	m_flNextWanderTime = 0.0f;
}

// === Goal-machine plumbing ===

// TF2 SuspendFor: park the running goal under an interrupt
void CNavBot::BeginInterrupt(EBotGoal eGoal)
{
	if (m_eGoal == eGoal)
		return;
	if (m_eGoal != GOAL_NONE)
		m_eSuspendedGoal = m_eGoal;
	m_eGoal = eGoal;
}

// TF2 Done: the interrupt finished — bring the parked goal back
void CNavBot::ResumeFromInterrupt()
{
	m_eGoal = m_eSuspendedGoal;
	m_eSuspendedGoal = GOAL_NONE;
	// The interrupt replaced our path — let the resumed goal re-solve right away
	m_flNextRepathTime = 0.0f;
	m_flNextFollowRepathTime = 0.0f;
	m_flNextWanderTime = 0.0f;
}

void CNavBot::ClearGoal()
{
	if (m_eGoal == GOAL_CAPTURE)
		m_flNextObjectiveScanTime = 0.0f;   // a fresh capture pick must re-scan
	if (m_eGoal == GOAL_DEFEND)
		m_pDefendSpotArea = nullptr;       // don't resume a stale committed spot
	m_eGoal = GOAL_NONE;
	m_flNextGoalScanTime = 0.0f;   // re-pick immediately next tick
}

// Light scan for any alive player carrying the FollowPlayer tag
// (preemption check — the full follow logic runs in FollowTaggedPlayer)
bool CNavBot::HasTaggedTarget(C_TFPlayer* pLocal)
{
	if (!pLocal)
		return false;

	const int nHighestIndex = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = 1; i <= nHighestIndex; i++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pClientEntity)
			continue;

		auto pNetworkable = pClientEntity->GetClientNetworkable();
		if (!pNetworkable)
			continue;

		auto pClientClass = pNetworkable->GetClientClass();
		if (!pClientClass || static_cast<ETFClassIds>(pClientClass->m_ClassID) != ETFClassIds::CTFPlayer)
			continue;

		auto pPlayer = pClientEntity->As<C_TFPlayer>();
		if (!pPlayer || pPlayer == pLocal || !pPlayer->IsAlive())
			continue;

		PlayerPriority info{};
		if (F::Players->GetInfo(i, info) && info.FollowPlayer)
			return true;
	}
	return false;
}

// Shared path-follow + look epilogue (the old chain had three copies of this)
void CNavBot::FollowAndLook(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!G_NavEngine.IsNavMeshLoaded() || !G_NavEngine.IsPathing())
		return;

	G_NavEngine.FollowPath(pLocal, pCmd);

	// Pause look-along-path while the aimbot has a target with smooth aim —
	// smooth aimbot sets viewangles that LookAtPath would stomp
	bool bAimbotHasTarget = (G::nTargetIndex != -1);
	bool bUsingSmoothAim = (CFG::Aimbot_Hitscan_Aim_Type == 2 || CFG::Aimbot_Melee_Aim_Type == 2);

	if (CFG::NavBot_LookAtPath && !(bAimbotHasTarget && bUsingSmoothAim))
	{
		Vec3 vCurrentWaypoint = G_NavEngine.GetCurrentWaypoint();
		LookAtPath(pLocal, pCmd, vCurrentWaypoint);
	}
}

// Full movement epilogue: blind wander (meshless), path following, AutoScope,
// stuck wiggle. Every goal / interrupt branch funnels through here so none of
// them can forget a piece.
void CNavBot::MoveAndFinish(C_TFPlayer* pLocal, CUserCmd* pCmd, float flCurTime)
{
	// === Blind wander (maps without a nav mesh) ===
	// No goal helper can path without a mesh, so the wander goal walks between
	// hull-probed points directly (probes reject blocked paths and cliffs).
	if (m_eGoal == GOAL_WANDER && !G_NavEngine.IsNavMeshLoaded() && !G_NavEngine.IsPathing() && CFG::NavBot_BlindWander)
	{
		Vec3 vToTarget = m_vWanderTarget - pLocal->m_vecOrigin();
		vToTarget.z = 0.0f;

		bool bNeedTarget = m_vWanderTarget.IsZero() ||
		                   vToTarget.Length() < 60.0f ||
		                   flCurTime > m_flBlindWanderExpireTime;
		if (bNeedTarget)
		{
			m_vWanderTarget = PickBlindWanderPoint(pLocal);
			// Boxed in (zero target): retry once per second instead of probing every tick
			m_flBlindWanderExpireTime = flCurTime + (m_vWanderTarget.IsZero() ? 1.0f : 8.0f);
			m_vTargetPos = m_vWanderTarget;
			vToTarget = m_vWanderTarget - pLocal->m_vecOrigin();
			vToTarget.z = 0.0f;
		}

		if (!m_vWanderTarget.IsZero() && vToTarget.Length() > 20.0f)
		{
			vToTarget.Normalize();

			// Convert direction to forward/side move
			Vec3 vForward, vRight;
			Math::AngleVectors(pCmd->viewangles, &vForward, &vRight, nullptr);
			vForward.z = 0.0f;
			vRight.z = 0.0f;
			vForward.Normalize();
			vRight.Normalize();

			pCmd->forwardmove = vToTarget.Dot(vForward) * 450.0f;
			pCmd->sidemove = vToTarget.Dot(vRight) * 450.0f;

			// Stuck jump while blind wandering (no SmartJump without a mesh)
			if (CFG::NavBot_AutoJump && flCurTime >= m_flNextBlindJumpTime &&
			    (pLocal->m_fFlags() & FL_ONGROUND) &&
			    flCurTime - m_flStuckAnchorTime > 0.8f)
			{
				pCmd->buttons |= IN_JUMP;
				m_flNextBlindJumpTime = flCurTime + 0.6f;
			}
		}
	}

	FollowAndLook(pLocal, pCmd);

	// === Auto Scope (ported from Amalgam's BotUtils::AutoScope) ===
	{
		const auto pWeapon = H::Entities->GetWeapon();
		if (pLocal && pWeapon)
			AutoScope(pLocal, pWeapon, pCmd);
	}

	// Stuck wiggle AFTER movement wrote forward/side so it can override sidemove
	ApplyStuckWiggle(pLocal, pCmd, flCurTime);

	m_bActive = true;
}

Vec3 CNavBot::PickBlindWanderPoint(C_TFPlayer* pLocal)
{
	if (!pLocal)
		return Vec3(0, 0, 0);

	Vec3 vLocalPos = pLocal->m_vecOrigin();

	CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
	const Vec3 vHullMin(-20.0f, -20.0f, 18.0f);  // lifted above step height — stairs read as clear
	const Vec3 vHullMax(20.0f, 20.0f, 62.0f);

	struct BlindCand_t
	{
		Vec3 m_vPoint;
	};
	std::vector<BlindCand_t> vCandidates;

	// Probe 8 directions with a random angular offset so paths differ each time
	const float flYawJitter = (static_cast<float>(rand()) / RAND_MAX) * 45.0f;
	for (int i = 0; i < 8; i++)
	{
		float flYawDeg = flYawJitter + i * 45.0f;
		float flYawRad = flYawDeg * (3.14159265f / 180.0f);
		Vec3 vDir(cosf(flYawRad), sinf(flYawRad), 0.0f);

		float flRange = 250.0f + (static_cast<float>(rand()) / RAND_MAX) * 250.0f;

		Vec3 vStart = vLocalPos + Vec3(0.0f, 0.0f, 18.0f);
		Vec3 vEnd = vStart + vDir * flRange;

		CGameTrace trace = {};
		H::AimUtils->TraceHull(vStart, vEnd, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);
		if (trace.fraction < 1.0f && !trace.startsolid)
			continue;  // blocked

		// Check the ground at the endpoint — don't wander off cliffs or into pits
		Vec3 vGroundStart = vEnd;
		vGroundStart.z = vLocalPos.z + 72.0f;
		Vec3 vGroundEnd = vEnd;
		vGroundEnd.z = vLocalPos.z - 250.0f;

		CGameTrace groundTrace = {};
		H::AimUtils->TraceHull(vGroundStart, vGroundEnd, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &groundTrace);
		if (!groundTrace.DidHit() || groundTrace.startsolid)
			continue;  // void below
		if (groundTrace.plane.normal.z < 0.7f)
			continue;  // too steep to stand
		float flDrop = (vLocalPos.z + 72.0f) - groundTrace.endpos.z - 72.0f;
		if (flDrop > 150.0f)
			continue;  // big drop

		BlindCand_t tCand = {};
		tCand.m_vPoint = Vec3(vEnd.x, vEnd.y, groundTrace.endpos.z);
		vCandidates.push_back(tCand);
	}

	if (vCandidates.empty())
	{
		// Fully boxed in — stand still this round (wiggle/jump may free us)
		return Vec3(0, 0, 0);
	}

	const BlindCand_t& tChosen = vCandidates[rand() % vCandidates.size()];
	return tChosen.m_vPoint;
}

void CNavBot::ApplyStuckWiggle(C_TFPlayer* pLocal, CUserCmd* pCmd, float flCurTime)
{
	if (!pLocal || !pCmd)
		return;

	// Are we trying to go anywhere at all?
	bool bTryingToMove = G_NavEngine.IsPathing() ||
	                     (!G_NavEngine.IsNavMeshLoaded() && !m_vWanderTarget.IsZero());

	if (!bTryingToMove || !CFG::NavBot_StuckWiggle)
	{
		if (m_bStuckWiggle)
			m_bStuckWiggle = false;
		return;
	}

	// Time since we last made real progress (anchor-based, see Run)
	float flStuckTime = flCurTime - m_flStuckAnchorTime;
	if (flStuckTime > 0.5f)
	{
		// Flip direction on a timer, not every tick — a per-tick flip cancels itself out
		if (flCurTime >= m_flNextWiggleToggleTime)
		{
			m_bStuckWiggle = !m_bStuckWiggle;
			m_flNextWiggleToggleTime = flCurTime + 0.4f;
		}

		// Overwrite only the side component — keep forward pressure so we slide
		// along the obstacle instead of dancing in place
		pCmd->sidemove = m_bStuckWiggle ? 400.0f : -400.0f;
	}
	else if (m_bStuckWiggle)
	{
		m_bStuckWiggle = false;
	}
}

void CNavBot::Draw()
{
	if (!m_bActive)
		return;
	
	// Draw target position
	if (!m_vTargetPos.IsZero())
	{
		Vec3 vDrawPos = m_vTargetPos;
		vDrawPos.z += 64.0f;
		
		Vec3 vScreen;
		if (H::Draw->W2S(vDrawPos, vScreen))
		{
			Color_t color = Color_t(0, 255, 0, 255);
			const char* szLabel = (m_pTargetEntity != nullptr) ? "OBJECTIVE" : "WANDER";
			H::Draw->String(H::Fonts->Get(EFonts::ESP), static_cast<int>(vScreen.x), static_cast<int>(vScreen.y), color, POS_CENTERXY, szLabel);
		}
	}
	
	// Draw path waypoints if enabled
	if (CFG::NavBot_DrawWaypoints && G_NavEngine.IsPathing())
	{
		const auto& vCrumbs = G_NavEngine.GetCrumbs();
		
		// Draw lines between crumbs
		// (i + 1 < size, not i < size - 1: size_t underflows to SIZE_MAX on an
		// empty crumb list and the loop reads wild memory)
		for (size_t i = 0; i + 1 < vCrumbs.size(); i++)
		{
			Vec3 vScreen1, vScreen2;
			if (H::Draw->W2S(vCrumbs[i].m_vPos, vScreen1) && H::Draw->W2S(vCrumbs[i + 1].m_vPos, vScreen2))
			{
				H::Draw->Line(static_cast<int>(vScreen1.x), static_cast<int>(vScreen1.y),
					static_cast<int>(vScreen2.x), static_cast<int>(vScreen2.y),
					Color_t(255, 255, 0, 255));
			}
		}
		
		// Draw crumb markers
		for (const auto& tCrumb : vCrumbs)
		{
			Vec3 vScreen;
			if (H::Draw->W2S(tCrumb.m_vPos, vScreen))
			{
				H::Draw->String(H::Fonts->Get(EFonts::ESP), static_cast<int>(vScreen.x), static_cast<int>(vScreen.y),
					Color_t(255, 255, 0, 255), POS_CENTERXY, "o");
			}
		}
	}
}

void CNavBot::Stop()
{
	m_bActive = false;
	m_pTargetEntity = nullptr;
	m_vTargetPos = Vec3(0, 0, 0);
	m_vWanderTarget = Vec3(0, 0, 0);
	m_flReachCacheUntil = 0.0f;
	m_vReachCachePos = Vec3(0, 0, 0);
	m_bReachCacheResult = false;
	m_flNextSupplySearchTime[0] = 0.0f;
	m_flNextSupplySearchTime[1] = 0.0f;
	m_vStuckCheckPos = Vec3(0, 0, 0);
	m_nStuckCheckTicks = 0;
	m_flNextWanderTime = 0.0f;
	m_flNextRepathTime = 0.0f;
	m_nStuckCount = 0;
	m_nStuckJumpCooldown = 0;
	m_bStuckWiggle = false;
	m_flStuckAnchorTime = 0.0f;
	m_flNextWiggleToggleTime = 0.0f;
	m_flBlindWanderExpireTime = 0.0f;
	m_flNextBlindJumpTime = 0.0f;
	m_vLastTickPos = Vec3(0, 0, 0);
	m_bHasLastTickPos = false;
	m_pSupplyEntity = nullptr;
	m_iSupplyEntityIdx = -1;
	m_bSupplyIsDispenser = false;
	m_bSupplyIsHealth = false;
	m_bWaitingAtDispenser = false;
	m_vVisitedAreaCenters.clear();
	m_nConsecutiveFails = 0;
	m_flNextVisitedClearTime = 0.0f;
	m_iStayNearTargetIdx = -1;
	m_flNextStayNearCheck = 0.0f;
	m_vSniperSpotPos = Vec3(0, 0, 0);
	m_flSniperPatienceEnd = 0.0f;
	m_flNextSniperSearchTime = 0.0f;
	m_tDormantPositions.clear();
	m_tClosestEnemy = {};
	m_vLastAngles = Vec3(0, 0, 0);
	m_bIsFollowingTaggedPlayer = false;
	m_iFollowTaggedTargetIdx = -1;
	m_bIsFollowingTeammate = false;
	m_iFollowTargetIdx = -1;
	// Group formation (Unibox CNavBotGroup::Reset)
	m_vFormationUserIds.clear();
	m_iPositionInFormation = -1;
	m_flFormationDistance = 120.0f;
	m_iFormationFailures = 0;
	m_vLastFormationTargetPos = Vec3(0, 0, 0);
	m_flNextFormationScanTime = 0.0f;
	m_flNextFormationNavTime = 0.0f;
	// Reload retreat + weapon slot
	m_iLastReloadSlot = -1;
	m_flNextReloadSolveTime = 0.0f;
	// Stalk scoring state
	m_iStalkFlankTarget = -1;
	m_flStalkFlankSide = 1.0f;
	m_flNextStalkSolveTime = 0.0f;
	// Goal machine reset
	m_eGoal = GOAL_NONE;
	m_eSuspendedGoal = GOAL_NONE;
	m_flGoalStartTime = 0.0f;
	m_flNextGoalScanTime = 0.0f;
	m_flNextDangerCheckTime = 0.0f;
	m_flNextEscapeSpawnCheck = 0.0f;
	m_pCommittedExitArea = nullptr;
	m_flCommittedExitTime = 0.0f;
	m_vFailedExits.clear();  // A respawn moves the bot — old dead exits don't apply
	m_flNextObjectiveScanTime = 0.0f;
	m_flUnreachableSince = 0.0f;
	m_flNextFollowRepathTime = 0.0f;
	m_flNextFollowScanTime = 0.0f;
	// Unibox capture/defend/escape job state
	m_bObjectiveStaging = false;
	m_bHasCaptureSpot = false;
	m_vCaptureSpotPos = Vec3(0, 0, 0);
	m_vCaptureSpotCenter = Vec3(0, 0, 0);
	m_pDefendSpotArea = nullptr;
	m_vDefendAnchorPos = Vec3(0, 0, 0);
	m_flNextDefendScanTime = 0.0f;
	m_pProjectileEscapeTargetArea = nullptr;
	m_flNextEscapeRepathTime = 0.0f;
	// Escape/death state that used to survive a Stop: curtime restarts at 0 on
	// a map change, so a stale absolute-time value blocks the new map for its
	// entire clock, and stale CNavArea*/entity pointers reference the old map
	m_pEscapeTargetArea = nullptr;
	m_vPreEscapeDestination = Vec3(0, 0, 0);
	m_flEscapeCooldownEnd = 0.0f;
	m_flDeathPauseEndTime = 0.0f;
	// Auto-scope / weapon-slot state
	m_bAutoScopeKeep = false;
	m_flAutoScopeTimer = 0.0f;
	m_flAutoScopeLastShotTime = 0.0f;
	m_bAutoScopeWasScopedLastTick = false;
	m_mAutoScopeCache.clear();
	m_iBestSlot = -1;
	m_iCurrentSlot = -1;
	G_NavEngine.CancelPath();
}

// === LookAtPath - smooth view angle control while pathing ===
// Ported from Amalgam's BotUtils::LookAtPath + DoSlowAim
// Makes the bot look where it's going instead of staring at a wall
void CNavBot::LookAtPath(C_TFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vTarget)
{
	if (!pLocal || !pCmd)
		return;

	Vec3 vEye = pLocal->GetEyePosition();

	// Adjust target height for terrain-following look (like Amalgam)
	// When looking at the ground ahead, don't look straight down
	const float flHeightDelta = std::clamp(vTarget.z - vEye.z, -72.0f, 96.0f);
	const float flPitchFactor = flHeightDelta >= 0.0f ? 0.55f : 0.22f;
	Vec3 vFocus(vTarget.x, vTarget.y, vEye.z + flHeightDelta * flPitchFactor + 6.0f);

	Vec3 vDesired = Math::CalcAngle(vEye, vFocus);
	Math::ClampAngles(vDesired);

	// Smooth interpolation (like Amalgam's DoSlowAim)
	float flSpeed = std::max(1.0f, CFG::NavBot_LookSpeed);
	Vec3 vWish = vDesired;

	// Yaw smoothing
	if (m_vLastAngles.y != vWish.y)
	{
		Vec3 vDelta = vWish - m_vLastAngles;
		// Normalize yaw delta
		while (vDelta.y > 180.0f) vDelta.y -= 360.0f;
		while (vDelta.y < -180.0f) vDelta.y += 360.0f;
		vDelta /= flSpeed;
		vWish = m_vLastAngles + vDelta;
		Math::ClampAngles(vWish);
	}

	// Apply view angles
	pCmd->viewangles = vWish;
	I::EngineClient->SetViewAngles(vWish);
	m_vLastAngles = vWish;
}

// === GetDormantOrigin - get last known position of dormant players ===
// Ported from Amalgam's BotUtils::GetDormantOrigin
// Caches positions when players go dormant so we can still track them
bool CNavBot::GetDormantOrigin(int iEntIndex, Vec3& vOut)
{
	if (iEntIndex <= 0)
		return false;

	auto pClientEntity = I::ClientEntityList->GetClientEntity(iEntIndex);
	if (!pClientEntity)
		return false;

	auto pEntity = pClientEntity->As<C_BaseEntity>();
	if (!pEntity)
		return false;

	// If it's a player, check alive
	auto pPlayer = pEntity->As<C_TFPlayer>();
	if (pPlayer && !pPlayer->IsAlive())
	{
		// Invalidate dormant cache for dead players
		m_tDormantPositions.erase(iEntIndex);
		return false;
	}

	// If not dormant, return current origin and update cache
	if (!pEntity->IsDormant())
	{
		vOut = pEntity->m_vecOrigin();
		m_tDormantPositions[iEntIndex] = { vOut, I::GlobalVars->curtime, true };
		return true;
	}

	// If dormant, check cache
	auto it = m_tDormantPositions.find(iEntIndex);
	if (it != m_tDormantPositions.end() && it->second.m_bValid)
	{
		// Only use cached position if it's recent (within 5 seconds)
		if (I::GlobalVars->curtime - it->second.m_flTime < 5.0f)
		{
			vOut = it->second.m_vOrigin;
			return true;
		}
	}

	return false;
}

// === UpdateCloseEnemies - find and track nearest enemies ===
// Ported from Amalgam's BotUtils::UpdateCloseEnemies
void CNavBot::UpdateCloseEnemies(C_TFPlayer* pLocal)
{
	if (!pLocal)
		return;

	m_tClosestEnemy = {};

	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer || !pPlayer->IsAlive())
			continue;

		int iEntIndex = pPlayer->entindex();
		Vec3 vOrigin;
		if (!GetDormantOrigin(iEntIndex, vOrigin))
			continue;

		float flDist = pLocal->m_vecOrigin().DistTo(vOrigin);
		if (flDist < m_tClosestEnemy.m_flDist)
		{
			m_tClosestEnemy = { iEntIndex, pPlayer, flDist };
		}
	}
}

// === Sniper Spot - move to navmesh sniper sightline spots ===
// Ported from TF2 bot's CTFBotSniperLurk behavior
// Uses CHidingSpot IDEAL_SNIPER_SPOT/GOOD_SNIPER_SPOT flags generated by navmesh analysis.
// TF2 sniper bot: picks a vantage spot, stays there for a patience duration, then picks a new one.
// This is a low-priority idle behavior — does NOT override follow, objectives, supply, or escape.
bool CNavBot::SniperSpot(C_TFPlayer* pLocal)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded())
		return false;

	// Only use sniper spots when holding a sniper rifle
	auto pWeapon = pLocal->m_hActiveWeapon();
	if (!pWeapon)
		return false;

	// Only activate for sniper class or when actively scoped
	bool bIsSniperClass = (pLocal->m_iClass() == TF_CLASS_SNIPER);
	bool bIsScoped = pLocal->InCond(TF_COND_ZOOMED);
	if (!bIsSniperClass && !bIsScoped)
		return false;

	float flCurTime = I::GlobalVars->curtime;

	// If we have a valid sniper spot and haven't exceeded patience, keep it
	if (!m_vSniperSpotPos.IsZero() && flCurTime < m_flSniperPatienceEnd)
	{
		// Check if we've reached the spot — if so, just hold position
		Vec3 vLocalPos = pLocal->m_vecOrigin();
		float flDistToSpot = vLocalPos.DistTo(m_vSniperSpotPos);

		if (flDistToSpot < 50.0f)
		{
			// At the sniper spot — hold position, don't wander away
			// TF2 bot: "m_isHomePositionValid = true" — stays at home position until patience expires
			m_vTargetPos = m_vSniperSpotPos;
			return true;
		}

		// Still pathing to the spot — keep going
		if (G_NavEngine.IsPathing() && G_NavEngine.m_eCurrentPriority >= NAV_PRIO_PATROL)
			return true;

		// Lost our path — re-path (only if still reachable)
		if (!IsReachable(pLocal, m_vSniperSpotPos))
			return false;
		G_NavEngine.NavTo(m_vSniperSpotPos, NAV_PRIO_PATROL);
		return true;
	}

	// Patience expired or no spot — search for a new one (throttled)
	if (flCurTime < m_flNextSniperSearchTime)
		return false;

	m_flNextSniperSearchTime = flCurTime + 2.0f;  // Search every 2s

	Vec3 vLocalPos = pLocal->m_vecOrigin();
	int iTeam = pLocal->m_iTeamNum();

	// Find a sniper spot — prefer ideal (long sightlines), fall back to good
	Vec3 vSpot = G_NavEngine.m_pMap->FindSniperSpot(vLocalPos, 2000.0f, iTeam);
	if (vSpot.IsZero())
		return false;  // No sniper spots on this map or within range

	// Don't go to the same spot we just came from
	if (!m_vSniperSpotPos.IsZero() && vSpot.DistTo(m_vSniperSpotPos) < 100.0f)
		return false;

	m_vSniperSpotPos = vSpot;
	// TF2 bot patience: tf_bot_sniper_patience_duration = 10s
	m_flSniperPatienceEnd = flCurTime + 10.0f;

	m_vTargetPos = vSpot;
	if (!IsReachable(pLocal, vSpot))
		return false;
	G_NavEngine.NavTo(vSpot, NAV_PRIO_PATROL);
	return true;
}

// === StayNear - stalk enemy players (ported from Unibox's StayNear.cpp) ===
// Scores every nav area around the target instead of walking straight at them:
// a per-class distance band, approach direction (ahead / flank / behind for
// spy), travel cost, danger-blacklist exclusion, sightline preference and
// hiding-spot cover. The best-scoring area becomes the stalk position.
namespace
{
	// Per-class stalk tuning (Unibox StalkProfile_t + class table)
	struct StalkProfile_t
	{
		float m_flPreferredRadius = 420.0f;
		float m_flMinRadius = 120.0f;
		float m_flMaxRadius = 1200.0f;
		float m_flLeadBase = 0.18f;
		float m_flAheadDistance = 0.0f;
		float m_flSideDistance = 0.0f;
		float m_flRangeWeight = 1.4f;
		float m_flAnchorWeight = 0.9f;
		float m_flTravelWeight = 0.12f;
		float m_flHazardWeight = 0.08f;
		float m_flCoverWeight = 1.0f;
		bool m_bPreferAhead = true;
		bool m_bPreferSightline = false;
		bool m_bPreferBackstab = false;
	};

	StalkProfile_t GetStalkProfile(C_TFPlayer* pLocal)
	{
		StalkProfile_t tProfile{};
		if (!pLocal)
			return tProfile;

		auto pPrimary = pLocal->GetWeaponFromSlot(WEAPON_SLOT_PRIMARY);
		const bool bHuntsman = pPrimary && pPrimary->GetWeaponID() == TF_WEAPON_COMPOUND_BOW;
		auto pMelee = pLocal->GetWeaponFromSlot(WEAPON_SLOT_MELEE);
		const bool bGunslinger = pMelee && pMelee->m_iItemDefinitionIndex() == Engi_t_TheGunslinger;

		switch (pLocal->m_iClass())
		{
		case TF_CLASS_SCOUT:
			tProfile.m_flPreferredRadius = 210.0f;
			tProfile.m_flMinRadius = 80.0f;
			tProfile.m_flMaxRadius = 520.0f;
			tProfile.m_flLeadBase = 0.13f;
			tProfile.m_flAheadDistance = 70.0f;
			tProfile.m_flSideDistance = 90.0f;
			break;
		case TF_CLASS_SOLDIER:
			tProfile.m_flPreferredRadius = 430.0f;
			tProfile.m_flMinRadius = 220.0f;
			tProfile.m_flMaxRadius = 820.0f;
			tProfile.m_flLeadBase = 0.21f;
			tProfile.m_flAheadDistance = 120.0f;
			tProfile.m_flSideDistance = 110.0f;
			break;
		case TF_CLASS_PYRO:
			tProfile.m_flPreferredRadius = 115.0f;
			tProfile.m_flMinRadius = 45.0f;
			tProfile.m_flMaxRadius = 330.0f;
			tProfile.m_flLeadBase = 0.12f;
			tProfile.m_flAheadDistance = 45.0f;
			tProfile.m_flSideDistance = 55.0f;
			tProfile.m_flRangeWeight = 2.4f;
			break;
		case TF_CLASS_DEMOMAN:
			tProfile.m_flPreferredRadius = 470.0f;
			tProfile.m_flMinRadius = 260.0f;
			tProfile.m_flMaxRadius = 900.0f;
			tProfile.m_flLeadBase = 0.22f;
			tProfile.m_flAheadDistance = 130.0f;
			tProfile.m_flSideDistance = 130.0f;
			break;
		case TF_CLASS_HEAVYWEAPONS:
			tProfile.m_flPreferredRadius = 240.0f;
			tProfile.m_flMinRadius = 110.0f;
			tProfile.m_flMaxRadius = 620.0f;
			tProfile.m_flLeadBase = 0.17f;
			tProfile.m_flAheadDistance = 85.0f;
			tProfile.m_flSideDistance = 90.0f;
			break;
		case TF_CLASS_ENGINEER:
			tProfile.m_flPreferredRadius = bGunslinger ? 150.0f : 260.0f;
			tProfile.m_flMinRadius = 90.0f;
			tProfile.m_flMaxRadius = 650.0f;
			tProfile.m_flLeadBase = 0.16f;
			tProfile.m_flAheadDistance = 60.0f;
			tProfile.m_flSideDistance = 100.0f;
			break;
		case TF_CLASS_MEDIC:
			tProfile.m_flPreferredRadius = 360.0f;
			tProfile.m_flMinRadius = 180.0f;
			tProfile.m_flMaxRadius = 760.0f;
			tProfile.m_flLeadBase = 0.18f;
			tProfile.m_flAheadDistance = 70.0f;
			tProfile.m_flSideDistance = 120.0f;
			break;
		case TF_CLASS_SNIPER:
			tProfile.m_flPreferredRadius = bHuntsman ? 620.0f : 920.0f;
			tProfile.m_flMinRadius = 420.0f;
			tProfile.m_flMaxRadius = 1600.0f;
			tProfile.m_flLeadBase = bHuntsman ? 0.28f : 0.38f;
			tProfile.m_flAheadDistance = 220.0f;
			tProfile.m_flSideDistance = 180.0f;
			tProfile.m_flCoverWeight = 0.45f;
			tProfile.m_bPreferSightline = true;
			break;
		case TF_CLASS_SPY:
			tProfile.m_flPreferredRadius = 120.0f;
			tProfile.m_flMinRadius = 55.0f;
			tProfile.m_flMaxRadius = 420.0f;
			tProfile.m_flLeadBase = 0.14f;
			tProfile.m_flAheadDistance = -80.0f;
			tProfile.m_flSideDistance = 65.0f;
			tProfile.m_flRangeWeight = 2.1f;
			tProfile.m_bPreferAhead = false;
			tProfile.m_bPreferBackstab = true;
			break;
		default:
			break;
		}
		return tProfile;
	}

	// Lead time grows with distance; a stationary target gets less lead
	float GetStalkLeadTime(const StalkProfile_t& tProfile, float flTargetDistance, float flTargetSpeed)
	{
		float flLeadTime = tProfile.m_flLeadBase;
		flLeadTime += std::clamp(flTargetDistance / 2500.0f, 0.0f, 0.2f);
		if (flTargetSpeed < 25.0f)
			flLeadTime *= 0.6f;
		return std::clamp(flLeadTime, 0.08f, 0.55f);
	}

	Vec3 NormalizePlanarStalk(Vec3 vDirection)
	{
		vDirection.z = 0.0f;
		const float flLength = vDirection.Length();
		return flLength > 0.01f ? vDirection / flLength : Vec3(0.0f, 0.0f, 0.0f);
	}

	bool StalkTraceVisible(const Vec3& vFrom, const Vec3& vTo)
	{
		CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
		trace_t trace = {};
		H::AimUtils->Trace(vFrom, vTo, MASK_SHOT, &filter, &trace);
		return trace.fraction >= 1.0f;
	}

	// Unibox GetHidingSpotCoverScore: hiding-spot flags near the area plus a
	// trace from the target to the spot. Negative score = good cover when hiding
	// (bPreferSightline=false); inverted for snipers who want the sightline.
	float GetStalkCoverScore(CNavArea* pArea, const Vec3& vTargetOrigin, bool bPreferSightline)
	{
		float flScore = 0.0f;
		for (const auto& tSpot : pArea->m_vHidingSpots)
		{
			if (tSpot.m_vPos.DistTo(pArea->m_vCenter) > 220.0f)
				continue;

			if (tSpot.HasGoodCover())
				flScore -= bPreferSightline ? 30.0f : 120.0f;
			if (tSpot.IsGoodSniperSpot())
				flScore += bPreferSightline ? -90.0f : 20.0f;
			if (tSpot.IsIdealSniperSpot())
				flScore += bPreferSightline ? -140.0f : 30.0f;
			if (tSpot.IsExposed())
				flScore += bPreferSightline ? -20.0f : 100.0f;
			if (!StalkTraceVisible(vTargetOrigin, tSpot.m_vPos + Vec3(0.0f, 0.0f, PLAYER_CROUCHED_JUMP_HEIGHT)))
				flScore += bPreferSightline ? 140.0f : -70.0f;
		}
		return flScore;
	}

	// Unibox GetBackstabScore: spy wants to end up BEHIND the target
	float GetStalkBackstabScore(const Vec3& vAreaOrigin, const Vec3& vTargetOrigin, const Vec3& vTargetForward)
	{
		Vec3 vToArea = NormalizePlanarStalk(vAreaOrigin - vTargetOrigin);
		if (vToArea.IsZero())
			return 240.0f;

		Vec3 vBehind = NormalizePlanarStalk(vTargetForward * -1.0f);
		if (vBehind.IsZero())
			return 0.0f;
		const Vec3 vSide(-vTargetForward.y, vTargetForward.x, 0.0f);
		const float flBehindDot = std::clamp(vToArea.Dot(vBehind), -1.0f, 1.0f);
		return (1.0f - flBehindDot) * 280.0f + std::fabs(vToArea.Dot(vSide)) * 45.0f;
	}
}

// Score nav areas around the stalk target and path to the best one
bool CNavBot::StayNearTarget(C_TFPlayer* pLocal, int iEntIndex)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded())
		return false;

	auto pClientEntity = I::ClientEntityList->GetClientEntity(iEntIndex);
	if (!pClientEntity)
		return false;
	auto pPlayer = pClientEntity->As<C_TFPlayer>();

	Vec3 vOrigin;
	if (!GetDormantOrigin(iEntIndex, vOrigin))
		return false;

	CNavArea* pLocalArea = G_NavEngine.GetLocalNavArea();
	if (!pLocalArea)
		return false;

	vOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	const Vec3 vTargetOrigin = vOrigin;

	Vec3 vTargetVelocity(0.0f, 0.0f, 0.0f);
	if (pPlayer && !pPlayer->IsDormant())
		vTargetVelocity = pPlayer->m_vecVelocity();
	vTargetVelocity.z = 0.0f;

	const float flTargetDistance = vTargetOrigin.DistTo(pLocal->m_vecOrigin());
	const float flTargetSpeed = vTargetVelocity.Length2D();

	const StalkProfile_t tProfile = GetStalkProfile(pLocal);
	const float flLeadTime = GetStalkLeadTime(tProfile, flTargetDistance, flTargetSpeed);
	const Vec3 vPredictedOrigin = vTargetOrigin + vTargetVelocity * flLeadTime;

	// Approach direction: the target's movement, or our line to them; a spy
	// stalks relative to where the target is LOOKING instead
	Vec3 vForward = NormalizePlanarStalk(vTargetVelocity);
	if (vForward.IsZero())
		vForward = NormalizePlanarStalk(vPredictedOrigin - pLocal->m_vecOrigin());

	if (tProfile.m_bPreferBackstab && pPlayer && !pPlayer->IsDormant())
	{
		Vec3 vTargetForward;
		Math::AngleVectors(pPlayer->GetEyeAngles(), &vTargetForward);
		vForward = NormalizePlanarStalk(vTargetForward);
	}

	// Persistent flank side per target so the approach doesn't mirror-flip
	// between solves (Unibox's flFlankSide)
	if (m_iStalkFlankTarget != iEntIndex)
	{
		m_iStalkFlankTarget = iEntIndex;
		m_flStalkFlankSide = SDKUtils::RandomFloat(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
	}

	const Vec3 vSide(-vForward.y, vForward.x, 0.0f);
	const Vec3 vAnchor = vPredictedOrigin + vForward * tProfile.m_flAheadDistance
		+ vSide * (tProfile.m_flSideDistance * m_flStalkFlankSide);

	auto pDangerMap = G_NavEngine.GetDangerBlacklist();
	const float flCurTime = I::GlobalVars->curtime;

	struct StalkCandidate_t { CNavArea* m_pArea = nullptr; float m_flScore = 0.0f; };
	std::vector<StalkCandidate_t> vCandidates;
	vCandidates.reserve(128);

	for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
	{
		// Spawn rooms, sealed areas and actively dangerous areas are off the table
		if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE))
			continue;
		if (tArea.m_iTFAttributeFlags & TF_NAV_BLOCKED)
			continue;
		if (pDangerMap)
		{
			auto it = pDangerMap->find(&tArea);
			if (it != pDangerMap->end())
			{
				const auto& entry = it->second;
				bool bExpired = entry.m_flExpireTime > 0.0f && flCurTime > entry.m_flExpireTime;
				if (entry.m_iExpireTick > 0 && I::GlobalVars->tickcount > entry.m_iExpireTick)
					bExpired = true;
				if (entry.m_flExpireTime == 0.0f && entry.m_iExpireTick == 0)
					bExpired = false;
				if (!bExpired)
					continue;
			}
		}

		const Vec3& vAreaOrigin = tArea.m_vCenter;
		const float flDistToPredicted = vAreaOrigin.DistTo(vPredictedOrigin);
		if (flDistToPredicted < tProfile.m_flMinRadius || flDistToPredicted > tProfile.m_flMaxRadius)
			continue;

		const float flRangePenalty = std::fabs(flDistToPredicted - tProfile.m_flPreferredRadius);
		const float flAnchorPenalty = vAreaOrigin.DistTo(vAnchor) * tProfile.m_flAnchorWeight;
		const float flTravelPenalty = pLocalArea->m_vCenter.DistTo(vAreaOrigin);

		float flAheadPenalty = 0.0f;
		if (!vForward.IsZero())
		{
			const Vec3 vToArea = NormalizePlanarStalk(vAreaOrigin - vPredictedOrigin);
			const float flAheadDot = std::clamp(vToArea.Dot(vForward), -1.0f, 1.0f);
			flAheadPenalty = tProfile.m_bPreferAhead ? (1.0f - flAheadDot) * 120.0f : (1.0f + flAheadDot) * 120.0f;
		}

		float flScore = flRangePenalty * tProfile.m_flRangeWeight + flAnchorPenalty
			+ flTravelPenalty * tProfile.m_flTravelWeight + flAheadPenalty;
		if (tProfile.m_bPreferBackstab)
			flScore += GetStalkBackstabScore(vAreaOrigin, vPredictedOrigin, vForward);
		// Jitter so a failed solve doesn't re-pick the exact same area next pass
		flScore += SDKUtils::RandomFloat(-10.0f, 10.0f);

		vCandidates.push_back({ &tArea, flScore });
	}

	if (vCandidates.empty())
		return false;

	std::sort(vCandidates.begin(), vCandidates.end(),
		[](const StalkCandidate_t& a, const StalkCandidate_t& b) { return a.m_flScore < b.m_flScore; });

	// Sightline + hiding-spot cover pass over the best handful (Unibox caps
	// this at 28 — the traces are the expensive part)
	const size_t nTrace = (std::min)(vCandidates.size(), static_cast<size_t>(28));
	for (size_t i = 0; i < nTrace; i++)
	{
		StalkCandidate_t& tCandidate = vCandidates[i];
		const Vec3 vAreaEye = tCandidate.m_pArea->m_vCenter + Vec3(0.0f, 0.0f, PLAYER_CROUCHED_JUMP_HEIGHT);
		const bool bVisible = StalkTraceVisible(vTargetOrigin, vAreaEye);
		tCandidate.m_flScore += (bVisible == tProfile.m_bPreferSightline) ? -160.0f : 180.0f;
		tCandidate.m_flScore += GetStalkCoverScore(tCandidate.m_pArea, vTargetOrigin, tProfile.m_bPreferSightline) * tProfile.m_flCoverWeight;
	}

	std::sort(vCandidates.begin(), vCandidates.begin() + nTrace,
		[](const StalkCandidate_t& a, const StalkCandidate_t& b) { return a.m_flScore < b.m_flScore; });

	// Best-first attempts — the engine's solve floor means one real solve per
	// pass; the jitter above rotates the pick when one candidate fails
	const size_t nAttempts = (std::min)(nTrace, static_cast<size_t>(3));
	for (size_t i = 0; i < nAttempts; i++)
	{
		if (G_NavEngine.NavTo(vCandidates[i].m_pArea->m_vCenter, NAV_PRIO_STAYNEAR))
			return true;
	}

	return false;
}

bool CNavBot::StayNear(C_TFPlayer* pLocal)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded())
		return false;

	const float flCurTime = I::GlobalVars->curtime;
	const bool bStalkPathAlive = G_NavEngine.m_eCurrentPriority == NAV_PRIO_STAYNEAR && G_NavEngine.IsPathing();

	// Target validity: known position, not inside their own spawn room
	auto bTargetValid = [&](int iIdx) -> bool
	{
		if (iIdx <= 0 || iIdx == pLocal->entindex())
			return false;
		Vec3 vOrigin;
		if (!GetDormantOrigin(iIdx, vOrigin))
			return false;

		CNavArea* pTargetArea = G_NavEngine.m_pMap->FindClosestNavArea(vOrigin);
		if (pTargetArea)
		{
			const int iLocalTeam = pLocal->m_iTeamNum();
			const bool bInEnemySpawn = (iLocalTeam == TF_TEAM_RED && (pTargetArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE)) ||
				(iLocalTeam == TF_TEAM_BLUE && (pTargetArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED));
			if (bInEnemySpawn)
				return false;
		}
		return true;
	};

	// Keep the current target while valid — re-solve the stalk position on a
	// cadence instead of every tick (Unibox: 0.5s, 2s when preferring distance)
	if (m_iStayNearTargetIdx > 0 && bTargetValid(m_iStayNearTargetIdx))
	{
		if (flCurTime < m_flNextStalkSolveTime)
			return bStalkPathAlive;

		m_flNextStalkSolveTime = flCurTime + 0.75f;
		if (StayNearTarget(pLocal, m_iStayNearTargetIdx))
			return true;

		return bStalkPathAlive;
	}

	// Target lost — drop it and stop walking its stale path
	if (m_iStayNearTargetIdx > 0)
	{
		m_iStayNearTargetIdx = -1;
		if (bStalkPathAlive)
			G_NavEngine.CancelPath();
	}

	// Pick a new target (throttled scan, Unibox: 0.3s cooldown)
	if (flCurTime < m_flNextStayNearCheck)
		return bStalkPathAlive;
	m_flNextStayNearCheck = flCurTime + 0.3f;

	// Closest enemy wins, non-dormant preferred; 2D distance so an enemy on
	// another floor doesn't read as "near"
	C_TFPlayer* pBestTarget = nullptr;
	float flBestDist2D = FLT_MAX;
	int iBestIdx = -1;

	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer || !pPlayer->IsAlive())
			continue;

		const int iEntIndex = pPlayer->entindex();
		Vec3 vOrigin;
		if (!GetDormantOrigin(iEntIndex, vOrigin))
			continue;

		const Vec3 vDelta = pLocal->m_vecOrigin() - vOrigin;
		const float flDist2D = vDelta.Length2D();

		// Only stalk nearby enemies. The closest enemy on a casual server is
		// often across the map — stalking them sends the bot on one giant
		// cross-map path whose only observable behavior is walking at walls
		// where the mesh disagrees with the geometry. Far enemies get picked
		// once the bot's wander brings it closer.
		if (flDist2D > 2000.0f)
			continue;

		if (pPlayer->IsDormant() && flBestDist2D < FLT_MAX)
			continue;

		if (flDist2D < flBestDist2D)
		{
			flBestDist2D = flDist2D;
			pBestTarget = pPlayer;
			iBestIdx = iEntIndex;
		}
	}

	if (iBestIdx < 0 || !bTargetValid(iBestIdx))
		return false;

	m_iStayNearTargetIdx = iBestIdx;
	m_pTargetEntity = pBestTarget;
	m_vTargetPos = pBestTarget->m_vecOrigin();
	m_iStalkFlankTarget = -1;  // re-roll the flank side for the new target
	m_flNextStalkSolveTime = 0.0f;

	if (StayNearTarget(pLocal, iBestIdx))
		return true;

	m_iStayNearTargetIdx = -1;
	return false;
}

// === Group formation (ported from Unibox's CNavBotGroup) ===
// Build the column: same-team players sorted by userID (a proxy for join
// order — the earliest joiner leads). Our position = how many members sort
// before us; the leader is members.front(). Rescanned every 0.5s.
void CNavBot::UpdateFormation(C_TFPlayer* pLocal)
{
	const float flCurTime = I::GlobalVars->curtime;
	if (flCurTime < m_flNextFormationScanTime)
		return;
	m_flNextFormationScanTime = flCurTime + 0.5f;

	m_vFormationUserIds.clear();
	m_iPositionInFormation = -1;

	const int iLocalIdx = pLocal->entindex();
	const int iLocalTeam = pLocal->m_iTeamNum();

	player_info_t info{};
	int iLocalUserId = -1;
	if (I::EngineClient->GetPlayerInfo(iLocalIdx, &info))
		iLocalUserId = info.userID;

	const int nMaxClients = I::EngineClient->GetMaxClients();
	for (int i = 1; i <= nMaxClients; i++)
	{
		if (i == iLocalIdx)
			continue;

		auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pClientEntity)
			continue;

		auto pPlayer = pClientEntity->As<C_TFPlayer>();
		if (!pPlayer || pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->m_iTeamNum() != iLocalTeam)
			continue;

		if (!I::EngineClient->GetPlayerInfo(i, &info))
			continue;

		m_vFormationUserIds.push_back(info.userID);
	}

	std::sort(m_vFormationUserIds.begin(), m_vFormationUserIds.end());

	m_iPositionInFormation = static_cast<int>(std::count_if(
		m_vFormationUserIds.begin(), m_vFormationUserIds.end(),
		[iLocalUserId](int iUserId) { return iUserId < iLocalUserId; }));
}

// Walk to our slot in the formation: leader pos - leaderForward * spacing * position
bool CNavBot::GroupTick(C_TFPlayer* pLocal)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded() || !CFG::NavBot_GroupWithOthers)
		return false;

	UpdateFormation(pLocal);

	// Position 0 = we lead (or alone) — nothing to group behind
	if (m_iPositionInFormation <= 0 || m_vFormationUserIds.empty())
		return false;

	const int iLeaderIdx = I::EngineClient->GetPlayerForUserID(m_vFormationUserIds.front());
	auto pLeaderEntity = iLeaderIdx > 0 ? I::ClientEntityList->GetClientEntity(iLeaderIdx) : nullptr;
	auto pLeader = pLeaderEntity ? pLeaderEntity->As<C_TFPlayer>() : nullptr;
	if (!pLeader || !pLeader->IsAlive() || pLeader->IsDormant() || pLeader->m_iTeamNum() != pLocal->m_iTeamNum())
		return false;

	// Formation axis: where the leader is heading (velocity, or view when standing)
	Vec3 vDirection = pLeader->m_vecVelocity();
	if (vDirection.Length() < 10.0f)
		Math::AngleVectors(pLeader->GetEyeAngles(), &vDirection);

	// Grouping is an idle behavior: never fight a higher-priority path for the
	// engine (Unibox bails when m_eCurrentPriority > Patrol)
	if (G_NavEngine.m_eCurrentPriority > NAV_PRIO_PATROL)
		return false;

	Vec3 vOffset = NormalizePlanarStalk(vDirection) * (-m_flFormationDistance * static_cast<float>(m_iPositionInFormation));
	if (vOffset.IsZero())
		return false;

	const Vec3 vTargetPos = pLeader->m_vecOrigin() + vOffset;
	const float flCurTime = I::GlobalVars->curtime;
	m_pTargetEntity = pLeader;
	m_vTargetPos = vTargetPos;

	// In our slot — stand
	if (pLocal->m_vecOrigin().DistTo(vTargetPos) <= 30.0f)
	{
		if (G_NavEngine.IsPathing())
			G_NavEngine.CancelPath();

		m_iFormationFailures = 0;
		m_vLastFormationTargetPos = vTargetPos;
		return true;
	}

	// Can't reach our slot 3 checks in a row (target barely moved and we're
	// still pathless) — widen the spacing, then wrap back to 120u past 300u
	if (m_vLastFormationTargetPos.DistTo(vTargetPos) <= 50.0f)
	{
		if (!G_NavEngine.IsPathing() && ++m_iFormationFailures >= 3)
		{
			m_iFormationFailures = 0;
			m_flFormationDistance += 50.0f;
			if (m_flFormationDistance > 300.0f)
				m_flFormationDistance = 120.0f;
			return false;
		}
	}
	else
		m_iFormationFailures = 0;
	m_vLastFormationTargetPos = vTargetPos;

	// Repath cadence while the slot keeps moving with the leader (Unibox: 0.5s)
	if (flCurTime < m_flNextFormationNavTime && G_NavEngine.IsPathing())
		return true;
	m_flNextFormationNavTime = flCurTime + 0.5f;

	return G_NavEngine.NavTo(vTargetPos, NAV_PRIO_PATROL);
}

// === UpdateDangerBlacklist - pre-compute danger zones on nav areas ===
// Ported from Amalgam's UpdateEnemyBlacklist + Map::UpdateIgnores
void CNavBot::UpdateDangerBlacklist(C_TFPlayer* pLocal)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded())
		return;

	// Check if any danger type is enabled
	if (!CFG::NavBot_DangerBL_Players && !CFG::NavBot_DangerBL_Sentries &&
		!CFG::NavBot_DangerBL_Stickies)
		return;

	static float flLastUpdateTime = 0.0f;
	float flCurTime = I::GlobalVars->curtime;
	// Negative delta = curtime reset on a map change — recompute instead of
	// staying throttled for the whole new map (blacklist would never rebuild)
	{
		const float flSinceUpdate = flCurTime - flLastUpdateTime;
		if (flSinceUpdate >= 0.0f && flSinceUpdate < 1.0f)
			return;
	}
	flLastUpdateTime = flCurTime;

	auto pBlacklist = G_NavEngine.GetDangerBlacklist();
	if (!pBlacklist) return;
	G_NavEngine.ClearDangerBlacklist();

	bool bStrongClass = (pLocal->m_iClass() == TF_CLASS_HEAVYWEAPONS || pLocal->m_iClass() == TF_CLASS_SOLDIER);

	// === Enemy players ===
	if (CFG::NavBot_DangerBL_Players)
	{
		if (!CFG::NavBot_DangerBL_DormantThreats)
			G_NavEngine.ClearDangerBlacklist(DANGER_ENEMY_DORMANT);

		if (CFG::NavBot_DangerBL_NormalThreats)
		{
			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
			{
				auto pPlayer = pEntity->As<C_TFPlayer>();
				if (!pPlayer || !pPlayer->IsAlive()) continue;

				bool bDormant = pPlayer->IsDormant();
				Vec3 vOrigin;
				if (bDormant) {
					if (!CFG::NavBot_DangerBL_DormantThreats) continue;
					if (!GetDormantOrigin(pPlayer->entindex(), vOrigin)) continue;
				}
				else vOrigin = pPlayer->m_vecOrigin();
				vOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

				// Danger marks are only consumed by paths near us — tracing LOS from
				// every enemy on the server to every area in the mesh is what made
				// the frame time explode on busy servers. Only nearby threats mark.
				if (vOrigin.DistTo(pLocal->m_vecOrigin()) > 2000.0f)
					continue;

				if (pPlayer->IsInvulnerable())
				{
					for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
					{
						if (tArea.m_vCenter.DistTo(vOrigin) < 1000.0f)
						{
							Vec3 vAreaPos = tArea.m_vCenter; vAreaPos.z += PLAYER_CROUCHED_JUMP_HEIGHT;
							if (H::AimUtils->TracePositionWorld(vOrigin, vAreaPos))
								pBlacklist->insert_or_assign(&tArea, DangerBlacklistEntry_t(DANGER_ENEMY_INVULN));
						}
					}
					continue;
				}

				EDangerReason eReason = bDormant ? DANGER_ENEMY_DORMANT : DANGER_ENEMY_NORMAL;
				float flDangerDist = bDormant ? 300.0f : 500.0f;
				for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
				{
					float flDist = tArea.m_vCenter.DistTo(vOrigin);
					if (flDist < flDangerDist)
					{
						Vec3 vAreaPos = tArea.m_vCenter; vAreaPos.z += PLAYER_CROUCHED_JUMP_HEIGHT;
						if (H::AimUtils->TracePositionWorld(vOrigin, vAreaPos))
						{
							auto it = pBlacklist->find(&tArea);
							if (it == pBlacklist->end() || GetDangerPenalty(eReason) > GetDangerPenalty(it->second.m_eReason))
								pBlacklist->insert_or_assign(&tArea, DangerBlacklistEntry_t(eReason));
						}
					}
				}
			}
		}
	}

	// === Enemy sentries ===
	if (CFG::NavBot_DangerBL_Sentries)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
		{
			if (!pEntity || pEntity->IsDormant()) continue;
			auto pBuilding = pEntity->As<C_BaseObject>();
			if (!pBuilding || pBuilding->m_bPlacing()) continue;
			if (pEntity->GetClassId() != ETFClassIds::CObjectSentrygun) continue;
			auto pSentry = pBuilding->As<C_ObjectSentrygun>();
			if (!pSentry) continue;
			if (pSentry->m_iState() == 0) continue;
			if (bStrongClass && (pSentry->m_bMiniBuilding() || pSentry->m_iUpgradeLevel() == 1)) continue;
			int iBullets = pSentry->m_iAmmoShells(); int iRockets = pSentry->m_iAmmoRockets();
			if (iBullets == 0 && (pSentry->m_iUpgradeLevel() != 3 || iRockets == 0)) continue;
			if (pSentry->m_bHasSapper() || pSentry->m_bBuilding() || pSentry->m_bCarryDeploy()) continue;

			Vec3 vSentryPos = pSentry->m_vecOrigin(); vSentryPos.z += PLAYER_CROUCHED_JUMP_HEIGHT;
			// Same distance gate as players — sentry danger ranges top out at 1200u,
			// so only nearby sentries can mark anything we'd path through
			if (vSentryPos.DistTo(pLocal->m_vecOrigin()) > 1500.0f)
				continue;
			constexpr float flHighDangerRange = 900.0f, flMediumDangerRange = 1050.0f, flLowDangerRange = 1200.0f;

			for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
			{
				float flDist = tArea.m_vCenter.DistTo(vSentryPos);
				EDangerReason eReason = DANGER_NONE;
				if (flDist < flHighDangerRange) eReason = DANGER_SENTRY;
				else if (flDist < flMediumDangerRange) eReason = DANGER_SENTRY_MEDIUM;
				else if (flDist < flLowDangerRange && !bStrongClass) eReason = DANGER_SENTRY_LOW;
				if (eReason != DANGER_NONE)
				{
					Vec3 vAreaPos = tArea.m_vCenter; vAreaPos.z += PLAYER_CROUCHED_JUMP_HEIGHT;
					if (H::AimUtils->TracePositionWorld(vSentryPos, vAreaPos))
					{
						auto it = pBlacklist->find(&tArea);
						if (it == pBlacklist->end() || GetDangerPenalty(eReason) > GetDangerPenalty(it->second.m_eReason))
							pBlacklist->insert_or_assign(&tArea, DangerBlacklistEntry_t(eReason));
					}
				}
			}
		}
	}

	// === Enemy stickies ===
	if (CFG::NavBot_DangerBL_Stickies)
	{
		constexpr float flStickyRadius = 130.0f + HALF_PLAYER_WIDTH;
		constexpr int iStickyIgnoreTicks = 300;
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
		{
			if (!pEntity) continue;
			if (pEntity->GetClassId() != ETFClassIds::CTFGrenadePipebombProjectile) continue;
			auto pSticky = pEntity->As<C_TFGrenadePipebombProjectile>();
			if (!pSticky || pSticky->m_iType() != TF_GL_MODE_REMOTE_DETONATE) continue;
			if (pSticky->m_vecVelocity().Length() > 1.0f) continue;
			Vec3 vStickyPos = pSticky->m_vecOrigin(); vStickyPos.z += PLAYER_JUMP_HEIGHT / 2.0f;
			int iExpireTick = I::GlobalVars->tickcount + iStickyIgnoreTicks;
			for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
			{
				if (tArea.m_vCenter.DistTo(vStickyPos) < flStickyRadius)
				{
					auto it = pBlacklist->find(&tArea);
					if (it == pBlacklist->end() || GetDangerPenalty(DANGER_STICKY) > GetDangerPenalty(it->second.m_eReason))
						pBlacklist->insert_or_assign(&tArea, DangerBlacklistEntry_t(DANGER_STICKY, 0.0f, iExpireTick));
				}
			}
		}
	}
}

// Unibox CanUseDangerArea: what counts as acceptable terrain for an escape
// route. High danger never; medium only with a goal to stay near and health
// to spare; low/dormant always.
static bool CanUseDangerReason(EDangerReason eReason, bool bHasTarget, bool bLowHealth)
{
	switch (eReason)
	{
	case DANGER_SENTRY:
	case DANGER_ENEMY_INVULN:
	case DANGER_STICKY:
		return false;
	case DANGER_SENTRY_MEDIUM:
	case DANGER_ENEMY_NORMAL:
		return bHasTarget && !bLowHealth;
	default:
		return true;
	}
}

// Unibox IsEscapePathSafe, checked against the SOLVE instead of a cached path:
// after NavTo commits a route, walk its crumbs and reject routes whose areas
// (past the from-side crumb) traverse danger we're not allowed to use — no
// point escaping a sentry through another sentry.
static bool IsEscapePathSafe(std::unordered_map<CNavArea*, DangerBlacklistEntry_t>* pBlacklist, bool bHasTarget, bool bLowHealth)
{
	if (!pBlacklist)
		return true;

	const auto& vCrumbs = G_NavEngine.GetCrumbs();
	for (size_t i = 1; i < vCrumbs.size(); ++i)
	{
		CNavArea* pCrumbArea = vCrumbs[i].m_pNavArea;
		if (!pCrumbArea)
			continue;

		auto it = pBlacklist->find(pCrumbArea);
		if (it == pBlacklist->end())
			continue;

		const auto& tEntry = it->second;
		if (tEntry.m_iExpireTick > 0 && I::GlobalVars->tickcount > tEntry.m_iExpireTick)
			continue;   // already expired

		if (!CanUseDangerReason(tEntry.m_eReason, bHasTarget, bLowHealth))
			return false;
	}
	return true;
}

// Unibox GetHazardAhead: the worst blacklist entry among the crumbs we're
// ABOUT to walk (up to 6 within 1000u, starting at the active crumb so ground
// already covered doesn't count), ranked by penalty. Escaping when the sentry
// is two rooms ahead beats grinding into it and escaping after.
static const DangerBlacklistEntry_t* GetDangerAheadOnPath(C_TFPlayer* pLocal, std::unordered_map<CNavArea*, DangerBlacklistEntry_t>* pBlacklist)
{
	if (!pLocal || !pBlacklist || !G_NavEngine.IsPathing())
		return nullptr;

	const auto& vCrumbs = G_NavEngine.GetCrumbs();
	const Vec3 vLocalPos = pLocal->m_vecOrigin();

	const DangerBlacklistEntry_t* pWorst = nullptr;
	float flWorstPenalty = 0.0f;
	int iChecked = 0;
	for (size_t i = G_NavEngine.GetCurrentCrumbIndex(); i < vCrumbs.size(); ++i)
	{
		if (iChecked >= 6)
			break;

		const Crumb_t& tCrumb = vCrumbs[i];
		if (!tCrumb.m_pNavArea)
			continue;

		if (tCrumb.m_vPos.DistTo(vLocalPos) > 1000.0f)
			break;
		++iChecked;

		auto it = pBlacklist->find(tCrumb.m_pNavArea);
		if (it == pBlacklist->end() || it->second.m_eReason == DANGER_BAD_BUILD_SPOT)
			continue;

		const auto& tEntry = it->second;
		if (tEntry.m_iExpireTick > 0 && I::GlobalVars->tickcount > tEntry.m_iExpireTick)
			continue;

		const float flPenalty = GetDangerPenalty(tEntry.m_eReason);
		if (flPenalty > flWorstPenalty)
		{
			flWorstPenalty = flPenalty;
			pWorst = &it->second;
		}
	}
	return pWorst;
}

// === EscapeDanger - flee from enemies/sentries when taking fire ===
// Ported from Amalgam's EscapeDanger.cpp with blacklist-based detection
// Key improvement: when escaping while on an objective (Capture), try to stay
// near the objective so the bot doesn't oscillate between escape and capture.
bool CNavBot::EscapeDanger(C_TFPlayer* pLocal)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded()) return false;
	if (!CFG::NavBot_EscapeDanger) return false;
	if (G_NavEngine.m_eCurrentPriority > NAV_PRIO_ESCAPE_DANGER) return false;

	auto pLocalArea = G_NavEngine.GetLocalNavArea();
	if (pLocalArea && (pLocalArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED ||
		pLocalArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE))
		return false;

	auto pBlacklist = G_NavEngine.GetDangerBlacklist();
	bool bInHighDanger = false, bInMediumDanger = false, bInLowDanger = false;

	// Effective hazard (Unibox): the area we're standing in, else the worst
	// hazard on the path AHEAD. A bot marching into a sentry room used to only
	// react once it was standing inside the marked area.
	const bool bLocalHazard = pBlacklist && pLocalArea && pBlacklist->count(pLocalArea) &&
		(*pBlacklist)[pLocalArea].m_eReason != DANGER_BAD_BUILD_SPOT;
	const DangerBlacklistEntry_t* pAheadHazard = bLocalHazard ? nullptr : GetDangerAheadOnPath(pLocal, pBlacklist);
	const DangerBlacklistEntry_t* pHazardEntry = bLocalHazard ? &(*pBlacklist)[pLocalArea] : pAheadHazard;

	if (pHazardEntry)
	{
		const float flCurTime = I::GlobalVars->curtime;
		const bool bFromAhead = !bLocalHazard;
		auto& entry = *pHazardEntry;

		switch (entry.m_eReason)
		{
		case DANGER_SENTRY: case DANGER_STICKY: case DANGER_ENEMY_INVULN: bInHighDanger = true; break;
		case DANGER_SENTRY_MEDIUM: case DANGER_ENEMY_NORMAL: bInMediumDanger = true; break;
		case DANGER_SENTRY_LOW: case DANGER_ENEMY_DORMANT: bInLowDanger = true; break;
		default: break;
		}

		// A long-range sentry AHEAD only matters when already hurt (Unibox:
		// ahead SentryLow + low health escalates to medium)
		const bool bLowHealth = pLocal->m_iHealth() < pLocal->GetMaxHealth() * 0.5f;
		if (bFromAhead && bInLowDanger && bLowHealth)
		{
			bInMediumDanger = true;
			bInLowDanger = false;
		}

		// Unibox repath cooldown: while an escape path is alive, keep walking it
		// at least 0.35s between re-solves (EscapeProjectiles shares this cap)
		if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_ESCAPE_DANGER && G_NavEngine.IsPathing()
			&& flCurTime < m_flNextEscapeRepathTime)
			return true;

		bool bShouldEscape = bInHighDanger || (bInMediumDanger && bLowHealth);

		// Amalgam-style: don't escape from medium danger if on an important task (Capture, GetHealth, Engineer)
		bool bImportantTask = (G_NavEngine.m_eCurrentPriority == NAV_PRIO_CAPTURE ||
		                      G_NavEngine.m_eCurrentPriority == NAV_PRIO_GETHEALTH);
		if (!bInHighDanger && bImportantTask)
			bShouldEscape = false;

		if (bInLowDanger && !bInMediumDanger && !bInHighDanger && G_NavEngine.m_eCurrentPriority != NAV_PRIO_NONE)
			return false;

		if (!bShouldEscape)
		{
			if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_ESCAPE_DANGER)
			{
				G_NavEngine.CancelPath();
				// Set cooldown so Capture doesn't immediately walk back into danger
				m_flEscapeCooldownEnd = I::GlobalVars->curtime + 3.0f;
			}
			return false;
		}

		// Already escaping - validate that escape target is still safe AND we're making progress
		if (G_NavEngine.IsPathing() && G_NavEngine.m_eCurrentPriority == NAV_PRIO_ESCAPE_DANGER && m_pEscapeTargetArea)
		{
			if (!pBlacklist->count(m_pEscapeTargetArea))
			{
				float flDistToTarget = pLocal->m_vecOrigin().DistTo(m_pEscapeTargetArea->m_vCenter);
				if (flDistToTarget > 100.0f)
					return true;
			}
			// Target is now in danger or we're stuck - need to repath
		}
		// Path died (canceled externally / completed) while still in danger —
		// fall through and pick a fresh escape target instead of standing still

		// === Amalgam-style reference position ===
		// If we were pursuing an objective, try to stay near it while escaping.
		// This prevents the bot from fleeing far away, then walking right back into danger.
		Vec3 vReferencePosition = pLocal->m_vecOrigin();
		bool bHasObjective = false;

		if (G_NavEngine.m_eCurrentPriority != NAV_PRIO_NONE &&
		    G_NavEngine.m_eCurrentPriority != NAV_PRIO_ESCAPE_DANGER &&
		    G_NavEngine.IsPathing())
		{
			// Use the destination of our current path as the reference position
			vReferencePosition = G_NavEngine.GetDestination();
			bHasObjective = true;
			// Save it so we remember where we were going even after escape overrides the path
			m_vPreEscapeDestination = vReferencePosition;
		}
		else if (!m_vPreEscapeDestination.IsZero())
		{
			// We just started escaping — use the saved destination from before
			vReferencePosition = m_vPreEscapeDestination;
			bHasObjective = true;
		}

		// Find safe areas - prioritize getting AWAY from danger source while staying near objective
		Vec3 vDangerSource = pLocal->m_vecOrigin();
		if (m_tClosestEnemy.m_pPlayer && H::Entities->IsEntityValid(m_tClosestEnemy.m_pPlayer) && m_tClosestEnemy.m_flDist < FLT_MAX)
			vDangerSource = m_tClosestEnemy.m_pPlayer->m_vecOrigin();

		// === Cover spot priority (ported from TF2 bot's hiding spot usage) ===
		// TF2 bots use navmesh hiding spots (IN_COVER) to take cover from sentries
		// and hide behind walls when healing. Try to find a nearby cover spot first.
		if (G_NavEngine.m_pMap)
		{
			Vec3 vCoverSpot = G_NavEngine.m_pMap->FindCoverSpot(
				pLocal->m_vecOrigin(), 1500.0f, pLocal->m_iTeamNum());
			if (!vCoverSpot.IsZero())
			{
				// Make sure the cover spot isn't in the danger zone
				auto pCoverArea = G_NavEngine.m_pMap->FindClosestNavArea(vCoverSpot);
				if (pCoverArea && !pBlacklist->count(pCoverArea))
				{
					// Prefer cover spots that are away from the danger source
					float flDistFromDanger = vCoverSpot.DistTo(vDangerSource);
					if (flDistFromDanger > 200.0f)
					{
						if (G_NavEngine.NavTo(vCoverSpot, NAV_PRIO_ESCAPE_DANGER, true))
						{
							if (!IsEscapePathSafe(pBlacklist, bHasObjective, bLowHealth))
							{
								G_NavEngine.CancelPath();
							}
							else
							{
								m_pEscapeTargetArea = pCoverArea;
								m_flNextEscapeRepathTime = flCurTime + 0.35f;
								return true;
							}
						}
					}
				}
			}
		}

		std::vector<std::pair<CNavArea*, float>> vSafeAreas;
		for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
		{
			Vec3 vAreaCenter = tArea.m_vCenter;
			float flDistToUs = vAreaCenter.DistTo(pLocal->m_vecOrigin());
			if (flDistToUs < 200.0f || flDistToUs > 1500.0f) continue;

			// Skip ALL blacklisted areas when escaping
			if (pBlacklist->count(&tArea)) continue;

			// Amalgam-style scoring: if we have an objective, prioritize staying near it
			// Otherwise, just get away from danger
			float flScore;
			if (bHasObjective)
			{
				// Score = distance to objective (closer is better, like Amalgam)
				flScore = vAreaCenter.DistTo(vReferencePosition);
			}
			else
			{
				// No objective — just get away from danger
				float flDistFromDanger = vAreaCenter.DistTo(vDangerSource);
				flScore = flDistToUs - (flDistFromDanger * 0.5f);
			}
			vSafeAreas.push_back({ &tArea, flScore });
		}

		std::sort(vSafeAreas.begin(), vSafeAreas.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

		int iAttempts = 0;
		for (auto& tPair : vSafeAreas)
		{
			if (++iAttempts > 10) break;
			bool bIsSafe = true;
			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
			{
				auto pPlayer = pEntity->As<C_TFPlayer>();
				if (!pPlayer || !pPlayer->IsAlive() || pPlayer->IsDormant()) continue;
				if (tPair.first->m_vCenter.DistTo(pPlayer->m_vecOrigin()) < 300.0f) { bIsSafe = false; break; }
			}
			if (!bIsSafe) continue;
			if (G_NavEngine.NavTo(tPair.first->m_vCenter, NAV_PRIO_ESCAPE_DANGER, true))
			{
				if (!IsEscapePathSafe(pBlacklist, bHasObjective, bLowHealth))
				{
					G_NavEngine.CancelPath();
					continue;
				}
				m_pEscapeTargetArea = tPair.first;
				m_flNextEscapeRepathTime = flCurTime + 0.35f;
				return true;
			}
		}

		// Fallback: try any completely safe area, sorted by distance from danger source
		if (bInHighDanger)
		{
			std::vector<std::pair<CNavArea*, float>> vFallbackAreas;
			for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
			{
				if (pBlacklist->count(&tArea)) continue;
				// Bound the fallback to a sane radius — collecting and sorting the
				// entire mesh here every tick while the escape solve is failing was
				// a major part of the "navbot lags the game" report
				if (tArea.m_vCenter.DistTo(pLocal->m_vecOrigin()) > 2500.0f) continue;
				// If we have an objective, sort by distance to it (stay near objective)
				// Otherwise, sort by distance from danger (get far away)
				float flScore = bHasObjective
					? tArea.m_vCenter.DistTo(vReferencePosition)
					: -tArea.m_vCenter.DistTo(vDangerSource);
				vFallbackAreas.push_back({ &tArea, flScore });
			}
			std::sort(vFallbackAreas.begin(), vFallbackAreas.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
			int iFallbackAttempts = 0;
			for (auto& tPair : vFallbackAreas)
			{
				if (++iFallbackAttempts > 10) break;
				if (G_NavEngine.NavTo(tPair.first->m_vCenter, NAV_PRIO_ESCAPE_DANGER, true))
				{
					if (!IsEscapePathSafe(pBlacklist, bHasObjective, bLowHealth))
					{
						G_NavEngine.CancelPath();
						continue;
					}
					m_pEscapeTargetArea = tPair.first;
					m_flNextEscapeRepathTime = flCurTime + 0.35f;
					return true;
				}
			}
		}

		if (m_tClosestEnemy.m_pPlayer && H::Entities->IsEntityValid(m_tClosestEnemy.m_pPlayer) && m_tClosestEnemy.m_flDist < FLT_MAX)
		{
			Vec3 vEnemyPos = m_tClosestEnemy.m_pPlayer->m_vecOrigin();
			Vec3 vAwayDir = pLocal->m_vecOrigin() - vEnemyPos; vAwayDir.z = 0.0f;
			if (vAwayDir.Length() > 1.0f)
			{
				vAwayDir.Normalize();
				Vec3 vFleeTarget = pLocal->m_vecOrigin() + vAwayDir * 500.0f;
				if (G_NavEngine.NavTo(vFleeTarget, NAV_PRIO_ESCAPE_DANGER, true))
				{
					if (IsEscapePathSafe(pBlacklist, bHasObjective, bLowHealth))
					{
						m_pEscapeTargetArea = nullptr;
						m_flNextEscapeRepathTime = flCurTime + 0.35f;
						return true;
					}
					G_NavEngine.CancelPath();
				}
			}
		}
	}
	else if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_ESCAPE_DANGER)
	{
		// No longer in danger — cancel escape path and set cooldown
		G_NavEngine.CancelPath();
		m_vPreEscapeDestination = Vec3(0, 0, 0);
		m_flEscapeCooldownEnd = I::GlobalVars->curtime + 3.0f;
	}

	return false;
}

// Unibox IsPositionSafe: no enemy projectile's threat sphere covers this
// position. Settled stickies (200u), rockets and regular pipes (300u).
static bool IsPositionSafeFromProjectiles(const Vec3& vPos)
{
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
	{
		if (!pEntity)
			continue;

		const auto nClassId = pEntity->GetClassId();
		if (nClassId == ETFClassIds::CTFGrenadePipebombProjectile)
		{
			auto pPipe = pEntity->As<C_TFGrenadePipebombProjectile>();
			if (pPipe && pPipe->m_iType() == TF_GL_MODE_REMOTE_DETONATE)
			{
				// A sticky still in flight isn't a dodge target — only settled ones
				if (CFG::NavBot_DangerBL_Stickies && pPipe->m_vecVelocity().Length() <= 1.0f
					&& vPos.DistTo(pPipe->m_vecOrigin()) < 200.0f)
					return false;
				continue;
			}
			if (CFG::NavBot_DangerBL_Projectiles && vPos.DistTo(pEntity->m_vecOrigin()) < 300.0f)
				return false;
			continue;
		}

		if (CFG::NavBot_DangerBL_Projectiles && nClassId == ETFClassIds::CTFProjectile_Rocket
			&& vPos.DistTo(pEntity->m_vecOrigin()) < 300.0f)
			return false;
	}
	return true;
}

// === EscapeProjectiles - flee from incoming rockets and stickies ===
// Ported from Amalgam's EscapeProjectiles + Unibox's persistent dodge target
bool CNavBot::EscapeProjectiles(C_TFPlayer* pLocal)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded()) return false;
	if (!CFG::NavBot_DangerBL_Stickies && !CFG::NavBot_DangerBL_Projectiles) return false;
	if (G_NavEngine.m_eCurrentPriority > NAV_PRIO_ESCAPE_DANGER) return false;

	Vec3 vLocalPos = pLocal->m_vecOrigin();
	const float flCurTime = I::GlobalVars->curtime;

	if (IsPositionSafeFromProjectiles(vLocalPos))
	{
		// Only cancel OUR dodge path — EscapeDanger shares this priority, and
		// canceling its retreat just because no projectiles are nearby anymore
		// left the bot standing in the danger zone with no path
		m_pProjectileEscapeTargetArea = nullptr;
		if (m_eGoal == GOAL_ESCAPE_PROJECTILE && G_NavEngine.m_eCurrentPriority == NAV_PRIO_ESCAPE_DANGER)
			G_NavEngine.CancelPath();
		return false;
	}

	// Shared escape repath cooldown — cap the dodge re-solve rate
	if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_ESCAPE_DANGER && G_NavEngine.IsPathing()
		&& flCurTime < m_flNextEscapeRepathTime)
		return true;

	// Persistent dodge target (Unibox): keep walking to a target that is still
	// safe instead of re-solving the whole candidate scan every tick
	if (m_pProjectileEscapeTargetArea)
	{
		auto pDangerBlacklist = G_NavEngine.GetDangerBlacklist();
		const bool bTargetSafe = !(pDangerBlacklist && pDangerBlacklist->count(m_pProjectileEscapeTargetArea))
			&& IsPositionSafeFromProjectiles(m_pProjectileEscapeTargetArea->m_vCenter);
		if (bTargetSafe)
		{
			if (G_NavEngine.IsPathing())
				return true;

			if (G_NavEngine.NavTo(m_pProjectileEscapeTargetArea->m_vCenter, NAV_PRIO_ESCAPE_DANGER, true))
			{
				m_flNextEscapeRepathTime = flCurTime + 0.35f;
				return true;
			}
		}
		else
			m_pProjectileEscapeTargetArea = nullptr;
	}

	std::vector<std::pair<CNavArea*, float>> vSafeAreas;
	auto pDangerBlacklist = G_NavEngine.GetDangerBlacklist();
	for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
	{
		float flDist = tArea.m_vCenter.DistTo(vLocalPos);
		if (flDist > 1000.0f || flDist < 50.0f) continue;
		if (pDangerBlacklist && pDangerBlacklist->count(&tArea)) continue;
		if (!IsPositionSafeFromProjectiles(tArea.m_vCenter)) continue;
		vSafeAreas.push_back({ &tArea, flDist });
	}

	std::sort(vSafeAreas.begin(), vSafeAreas.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
	int iEscapeAttempts = 0;
	for (auto& tPair : vSafeAreas)
	{
		// Nearest-first with a small attempt budget — a dodge just needs one
		// reachable safe area, not the whole list solved
		if (++iEscapeAttempts > 15) break;
		if (G_NavEngine.NavTo(tPair.first->m_vCenter, NAV_PRIO_ESCAPE_DANGER, true))
		{
			m_pProjectileEscapeTargetArea = tPair.first;
			m_flNextEscapeRepathTime = flCurTime + 0.35f;
			return true;
		}
	}
	return false;
}

// === EscapeSpawn - navigate out of spawn rooms ===
// Ported from Amalgam's EscapeSpawn
bool CNavBot::EscapeSpawn(C_TFPlayer* pLocal)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded()) return false;

	// Populate the exit cache on first need. The only other call site is
	// FollowPath, which returns before reaching it while the bot has no path —
	// so a bot that spawns frozen (no path ever solved) would otherwise see an
	// empty exit list forever ("no exit area found").
	G_NavEngine.EnsureRespawnRoomsUpdated();

	auto pLocalArea = G_NavEngine.GetLocalNavArea();
	if (!pLocalArea || !(pLocalArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE)))
	{
		if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_ESCAPE_SPAWN) G_NavEngine.CancelPath();
		m_pCommittedExitArea = nullptr;   // out of spawn — a later visit picks fresh
		return false;
	}

	float flCurTime = I::GlobalVars->curtime;
	if (m_flNextEscapeSpawnCheck > 0.0f && flCurTime < m_flNextEscapeSpawnCheck)
		return G_NavEngine.m_eCurrentPriority == NAV_PRIO_ESCAPE_SPAWN;
	// 0.6s cadence: one exit attempt per call, rotating through the candidates
	// via m_vFailedExits. The old 2s cadence with a single "nearest exit" pick
	// spent 2s per dead exit (the exit list includes areas only vertically
	// adjacent to the spawn room, which have no walk route from inside) before
	// ever reaching one that actually paths — that was the frozen-in-spawn loop.
	m_flNextEscapeSpawnCheck = flCurTime + 0.6f;

	// Committed exit still being followed: keep walking it out. Re-picking
	// here would re-sort the exits by distance from our CURRENT position —
	// which flips between two nearly-equidistant candidates as we move along
	// the route, replacing the live path every 0.6s (the bot then runs back
	// and forth inside spawn forever).
	if (m_pCommittedExitArea)
	{
		if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_ESCAPE_SPAWN && G_NavEngine.IsPathing()
			&& flCurTime - m_flCommittedExitTime < 10.0f)
			return true;

		// The committed exit stopped getting us out: its path finished or was
		// abandoned while we're still flagged in-spawn (exit center sits on the
		// spawn boundary), or 10s passed without leaving (sealed setup gate).
		// Blacklist it briefly so the pick below rotates to another exit.
		m_vFailedExits.push_back({ m_pCommittedExitArea, flCurTime + 10.0f });
		NavDebug("EscapeSpawn: committed exit %u stalled, rotating", m_pCommittedExitArea->m_uId);
		m_pCommittedExitArea = nullptr;
		if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_ESCAPE_SPAWN)
			G_NavEngine.CancelPath();
	}

	Vec3 vLocalPos = pLocal->m_vecOrigin();

	// Drop expired failures
	m_vFailedExits.erase(
		std::remove_if(m_vFailedExits.begin(), m_vFailedExits.end(),
			[&](const FailedExit_t& tFail) { return flCurTime >= tFail.m_flUntil; }),
		m_vFailedExits.end());

	auto IsFailedExit = [&](CNavArea* pArea) -> bool
	{
		for (auto& tFail : m_vFailedExits)
		{
			if (tFail.m_pArea == pArea)
				return true;
		}
		return false;
	};

	// Collect exit candidates: cached exit areas from UpdateRespawnRooms, or the
	// spawn-exit flag fallback scan.
	std::vector<CNavArea*> vExits;
	auto pExitAreas = G_NavEngine.GetRespawnRoomExitAreas();
	if (pExitAreas && !pExitAreas->empty())
		vExits = *pExitAreas;
	else
	{
		for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
		{
			if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE)) continue;
			if (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_EXIT)
				vExits.push_back(&tArea);
		}
	}

	// Nearest-first, minus exits that failed to path recently
	vExits.erase(
		std::remove_if(vExits.begin(), vExits.end(),
			[&](CNavArea* pArea) { return pArea && IsFailedExit(pArea); }),
		vExits.end());
	std::sort(vExits.begin(), vExits.end(),
		[&](CNavArea* pA, CNavArea* pB)
		{ return pA->m_vCenter.DistToSqr(vLocalPos) < pB->m_vCenter.DistToSqr(vLocalPos); });

	for (CNavArea* pExit : vExits)
	{
		if (G_NavEngine.NavTo(pExit->m_vCenter, NAV_PRIO_ESCAPE_SPAWN, true))
		{
			m_pCommittedExitArea = pExit;
			m_flCommittedExitTime = flCurTime;
			return true;
		}

		// NavTo enforces a 0.2s floor between solves, so further attempts in this
		// call would be refused WITHOUT solving — recording those as failures
		// would poison this list with exits that were never actually tried.
		// Mark this one failed and try the next-nearest on a later call.
		m_vFailedExits.push_back({ pExit, flCurTime + 3.0f });
		NavDebug("EscapeSpawn: exit area %u failed to path (%zu dead), will try next-nearest",
			pExit->m_uId, m_vFailedExits.size());
		return false;
	}

	// Every exit failed recently (or none exist) — fall back to the nearest
	// non-spawn area so the bot still tries to leave.
	{
		CNavArea* pClosestArea = nullptr;
		float flBestDist = FLT_MAX;
		const int iLocalTeam = pLocal->m_iTeamNum();
		for (auto& tArea : G_NavEngine.m_pMap->m_navfile.m_vAreas)
		{
			if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE)) continue;
			// Never route the escape through the enemy's spawn (blocked for us anyway)
			if (iLocalTeam == TF_TEAM_RED && (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE)) continue;
			if (iLocalTeam == TF_TEAM_BLUE && (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED)) continue;
			float flDist = tArea.m_vCenter.DistTo(vLocalPos);
			if (flDist < flBestDist) { flBestDist = flDist; pClosestArea = &tArea; }
		}

		if (pClosestArea && G_NavEngine.NavTo(pClosestArea->m_vCenter, NAV_PRIO_ESCAPE_SPAWN, true))
		{
			m_pCommittedExitArea = pClosestArea;   // same commitment as a real exit
			m_flCommittedExitTime = flCurTime;
			return true;
		}

		NavDebug("EscapeSpawn: in spawn (area %u) but no exit area found",
			pLocalArea ? pLocalArea->m_uId : 0u);
	}

	return false;
}

// === Weapon switching (ported from Unibox Amalgam's BotUtils::UpdateBestSlot / SelectBestSlot) ===
// Faithful port of Unibox's slot brain on top of Necromancer's config surface.
// The old version only compared clip counts with rough 300-750u distance gates;
// Unibox additionally weighs reserve ammo, whether a slot can fire RIGHT NOW,
// enemy health (finish kills with the right tool), enemy loadout (soldier
// swaps to shotgun vs an airblasting pyro), the melee Z limit (never knife at
// someone a floor above/below), spy backstab windows, and ranged fallbacks
// when melee is chosen but not carried.

namespace
{
	// Melee is only "reachable" when the enemy is within this Z band (Unibox kMeleeZLimit)
	constexpr float kMeleeZLimit = 80.0f;

	// Per-slot ammo snapshot (Unibox's G::AmmoInSlot cache, computed on demand)
	struct WeaponSlotInfo_t
	{
		C_TFWeaponBase* m_pWeapon = nullptr;
		bool m_bUsesAmmo = false;  // ammo type >= 0 (melee, medigun, drinks -> false)
		int m_iClip = 0;           // m_iClip1()
		int m_iReserve = 0;        // GetAmmoCount(ammo type)
		int m_iMaxReserve = 0;     // per-class default cap

		bool HasWeapon() const { return m_pWeapon != nullptr; }
		bool UsesAmmo() const { return m_bUsesAmmo; }
		bool HasClip() const { return m_iClip > 0; }
		bool HasReserve() const { return m_bUsesAmmo && m_iReserve > 0; }
		bool HasAnyAmmo() const { return !m_bUsesAmmo || m_iClip > 0 || m_iReserve > 0; }
		bool CanUseNow() const { return !m_bUsesAmmo || m_iClip > 0; }  // can fire this tick
	};

	WeaponSlotInfo_t GetWeaponSlotInfo(C_TFPlayer* pLocal, int iSlot)
	{
		WeaponSlotInfo_t tInfo{};
		auto pWeapon = pLocal->GetWeaponFromSlot(iSlot);
		if (!pWeapon)
			return tInfo;

		tInfo.m_pWeapon = pWeapon;
		tInfo.m_bUsesAmmo = pWeapon->m_iPrimaryAmmoType() >= 0;
		tInfo.m_iClip = pWeapon->m_iClip1();
		if (tInfo.m_bUsesAmmo)
		{
			tInfo.m_iReserve = pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType());
			tInfo.m_iMaxReserve = GetWeaponMaxReserveAmmo(pWeapon->GetWeaponID());
		}
		return tInfo;
	}

	// Unibox GetRangedFallbackSlot — best ranged slot when melee is off the table
	int GetRangedFallbackSlot(C_TFPlayer* pLocal)
	{
		auto HasUsableWeapon = [&](int iSlot)
		{
			const WeaponSlotInfo_t tInfo = GetWeaponSlotInfo(pLocal, iSlot);
			if (!tInfo.HasWeapon()) return false;
			if (!tInfo.UsesAmmo()) return true;
			return tInfo.HasClip() || tInfo.HasReserve();
		};

		if (HasUsableWeapon(WEAPON_SLOT_PRIMARY)) return WEAPON_SLOT_PRIMARY;
		if (HasUsableWeapon(WEAPON_SLOT_SECONDARY)) return WEAPON_SLOT_SECONDARY;
		if (GetWeaponSlotInfo(pLocal, WEAPON_SLOT_PRIMARY).HasWeapon()) return WEAPON_SLOT_PRIMARY;
		if (GetWeaponSlotInfo(pLocal, WEAPON_SLOT_SECONDARY).HasWeapon()) return WEAPON_SLOT_SECONDARY;
		return WEAPON_SLOT_MELEE;
	}

	// True when the enemy is facing away from us (Unibox IsTargetBehindLocal)
	bool IsTargetBehindLocal(C_TFPlayer* pLocal, C_TFPlayer* pTarget)
	{
		if (!pLocal || !pTarget)
			return false;

		Vec3 vForward{};
		Math::AngleVectors(pTarget->GetEyeAngles(), &vForward);
		Vec3 vToLocal = pLocal->m_vecOrigin() - pTarget->m_vecOrigin();
		vForward.z = 0.0f;
		vToLocal.z = 0.0f;
		if (vToLocal.Normalize() <= 0.01f || vForward.Normalize() <= 0.01f)
			return false;
		return vForward.Dot(vToLocal) < -0.5f;
	}

	// Unibox GetSniperTargetHealthTier: 2 = nearly dead (<35%), 1 = hurt (<75%), 0 = healthy
	int GetSniperTargetHealthTier(C_TFPlayer* pTarget)
	{
		if (!pTarget)
			return -1;
		const int iHealth = pTarget->m_iHealth();
		const int iMaxHealth = pTarget->GetMaxHealth();
		if (iHealth < iMaxHealth * 0.35f)
			return 2;
		return iHealth < iMaxHealth * 0.75f ? 1 : 0;
	}

	// Unibox CBotUtils::HasMedigunTargets — a teammate in medigun range worth
	// keeping the beam on. Gated on auto-heal, like Unibox gates on its AutoHeal var.
	bool HasMedigunTargets(C_TFPlayer* pLocal)
	{
		if (!CFG::AutoUber_AutoHeal_Active)
			return false;

		const Vec3 vShootPos = pLocal->GetShootPos();
		const int iLocalIdx = pLocal->entindex();
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
		{
			auto pPlayer = pEntity->As<C_TFPlayer>();
			if (!pPlayer || pPlayer->entindex() == iLocalIdx || !pPlayer->IsAlive())
				continue;
			if (pPlayer->m_iTeamNum() != pLocal->m_iTeamNum() || pEntity->IsDormant())
				continue;
			if (pPlayer->InCond(TF_COND_STEALTHED))
				continue;
			if (vShootPos.DistTo(pPlayer->m_vecOrigin()) > 450.0f)  // medigun heal range
				continue;
			return true;
		}
		return false;
	}
}

void CNavBot::UpdateBestSlot(C_TFPlayer* pLocal)
{
	if (!pLocal)
		return;

	m_iBestSlot = -1;

	// Weapon preference: 0=Off (don't switch), 1=Best (auto), 2=Primary, 3=Secondary, 4=Melee
	const int iPreference = CFG::NavBot_WeaponPreference;
	if (iPreference == 0)
		return;
	if (iPreference != 1)
	{
		m_iBestSlot = iPreference - 2;
		return;
	}

	const WeaponSlotInfo_t tPrimary = GetWeaponSlotInfo(pLocal, WEAPON_SLOT_PRIMARY);
	const WeaponSlotInfo_t tSecondary = GetWeaponSlotInfo(pLocal, WEAPON_SLOT_SECONDARY);
	const bool bHasMelee = pLocal->GetWeaponFromSlot(WEAPON_SLOT_MELEE) != nullptr;

	C_TFPlayer* pEnemy = m_tClosestEnemy.m_pPlayer;
	const bool bHasEnemy = pEnemy != nullptr;
	const float flEnemyDist = m_tClosestEnemy.m_flDist;
	const float flEnemyDistZ = bHasEnemy ? pEnemy->m_vecOrigin().z - pLocal->m_vecOrigin().z : 0.0f;
	const bool bMeleeReachable = bHasEnemy && std::abs(flEnemyDistZ) <= kMeleeZLimit;

	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT:
	{
		// Bat only when both guns are effectively dry; pistol past scattergun range
		const bool bPrimaryEmpty = !tPrimary.HasClip();
		const bool bSecondaryLow = !tSecondary.UsesAmmo() || !tSecondary.HasClip()
			|| tSecondary.m_iReserve <= tSecondary.m_iMaxReserve / 4;
		if (bMeleeReachable && bPrimaryEmpty && bSecondaryLow && flEnemyDist <= 200.0f)
			m_iBestSlot = WEAPON_SLOT_MELEE;
		else if (tSecondary.UsesAmmo() && tSecondary.HasClip() && (bPrimaryEmpty || flEnemyDist > 900.0f))
			m_iBestSlot = WEAPON_SLOT_SECONDARY;
		else if (tPrimary.HasClip())
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_HEAVYWEAPONS:
	{
		// Holiday Punch crits through uber — punch invulnerable targets
		auto pMelee = pLocal->GetWeaponFromSlot(WEAPON_SLOT_MELEE);
		const bool bHolidayPunch = pMelee && pMelee->m_iItemDefinitionIndex() == Heavy_t_TheHolidayPunch;
		const bool bPunchTarget = bHolidayPunch && bHasEnemy && !pEnemy->InCond(TF_COND_TAUNTING)
			&& pEnemy->IsInvulnerable() && flEnemyDist < 400.0f;
		const bool bOutOfAmmo = !tPrimary.HasClip() && !tSecondary.HasClip() && !tSecondary.HasReserve();
		if (bMeleeReachable && (bOutOfAmmo || bPunchTarget))
			m_iBestSlot = WEAPON_SLOT_MELEE;
		else if (tPrimary.HasClip())
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_MEDIC:
	{
		auto pSecondaryWeapon = pLocal->GetWeaponFromSlot(WEAPON_SLOT_SECONDARY);
		if (!pSecondaryWeapon)
			break;

		// Medigun out while actually healing or with a heal target nearby;
		// otherwise saw/needles — the old version parked on the medigun forever
		const bool bHealing = pSecondaryWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN
			&& pSecondaryWeapon->As<C_WeaponMedigun>()->m_hHealingTarget().Get();
		if (bHealing || HasMedigunTargets(pLocal))
			m_iBestSlot = WEAPON_SLOT_SECONDARY;
		else if (bMeleeReachable && (!tPrimary.HasClip() || flEnemyDist <= 400.0f))
			m_iBestSlot = WEAPON_SLOT_MELEE;
		else
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_SPY:
	{
		// Knife for backstabs and cloak picks; revolver otherwise
		const bool bIsBehind = IsTargetBehindLocal(pLocal, pEnemy);
		auto pKnife = pLocal->GetWeaponFromSlot(WEAPON_SLOT_MELEE);
		const bool bReadyToBackstab = pKnife && pKnife->GetWeaponID() == TF_WEAPON_KNIFE
			&& pKnife->As<C_TFKnife>()->m_bReadyToBackstab();
		const bool bKnifeLethal = pKnife && bHasEnemy && pEnemy->m_iHealth() <= pKnife->GetDamage();
		if (bMeleeReachable && flEnemyDist <= 250.0f && (bReadyToBackstab || bKnifeLethal))
			m_iBestSlot = WEAPON_SLOT_MELEE;
		else if (bMeleeReachable && (pLocal->InCond(TF_COND_STEALTHED) || bIsBehind) && flEnemyDist <= 1000.0f)
			m_iBestSlot = WEAPON_SLOT_MELEE;
		else if (tPrimary.HasClip() || tPrimary.HasReserve())
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_SNIPER:
	{
		// Finish wounded enemies with the SMG instead of re-charging the rifle
		const int iTargetLowHp = GetSniperTargetHealthTier(pEnemy);
		const bool bOutOfAmmo = !tPrimary.HasReserve() && !tSecondary.HasClip();
		if (bMeleeReachable && (bOutOfAmmo || flEnemyDist <= 200.0f))
			m_iBestSlot = WEAPON_SLOT_MELEE;
		else if (tSecondary.UsesAmmo() && tSecondary.CanUseNow() && bHasEnemy && flEnemyDist <= 240.0f
			&& (!tPrimary.CanUseNow() || iTargetLowHp > 1))
			m_iBestSlot = WEAPON_SLOT_SECONDARY;
		else if (m_iCurrentSlot >= WEAPON_SLOT_PRIMARY && m_iCurrentSlot < WEAPON_SLOT_MELEE)
		{
			// Whatever hitscan we're already holding finishes a nearly-dead target
			const WeaponSlotInfo_t tCurrent = GetWeaponSlotInfo(pLocal, m_iCurrentSlot);
			if (tCurrent.UsesAmmo() && tCurrent.HasClip() && bHasEnemy && flEnemyDist <= 800.0f && iTargetLowHp > 1)
				m_iBestSlot = m_iCurrentSlot;
			else if (tPrimary.HasClip())
				m_iBestSlot = WEAPON_SLOT_PRIMARY;
		}
		else if (tPrimary.HasClip())
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_PYRO:
	{
		// Flamethrower inside flame range; flares/shotgun past it or when dry
		const bool bSecondaryLow = !tSecondary.HasClip() && tSecondary.UsesAmmo()
			&& tSecondary.m_iReserve <= tSecondary.m_iMaxReserve / 4;
		if (bMeleeReachable && !tPrimary.HasClip() && bSecondaryLow && flEnemyDist <= 300.0f)
			m_iBestSlot = WEAPON_SLOT_MELEE;
		else if (tPrimary.CanUseNow() && (!bHasEnemy || flEnemyDist <= 520.0f))
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		else if (tSecondary.HasClip() && (!tPrimary.CanUseNow() || (bHasEnemy && flEnemyDist > 650.0f)))
			m_iBestSlot = WEAPON_SLOT_SECONDARY;
		else if (tPrimary.CanUseNow())
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_SOLDIER:
	{
		// Shotgun up close, vs weak targets, and vs pyros that can airblast
		auto pEnemyWeaponEnt = bHasEnemy ? pEnemy->m_hActiveWeapon().Get() : nullptr;
		auto pEnemyWeapon = pEnemyWeaponEnt ? pEnemyWeaponEnt->As<C_TFWeaponBase>() : nullptr;
		const bool bEnemyCanAirblast = pEnemyWeapon && pEnemyWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER
			&& pEnemyWeapon->m_iItemDefinitionIndex() != Pyro_m_ThePhlogistinator;
		const bool bEnemyClose = bHasEnemy && flEnemyDist <= 250.0f;
		const bool bPrimaryEmpty = tPrimary.UsesAmmo() && !tPrimary.HasClip() && !tPrimary.HasReserve();
		if (bMeleeReachable && (m_iCurrentSlot != WEAPON_SLOT_PRIMARY || bPrimaryEmpty) && bEnemyClose
			&& (pEnemy->m_iHealth() < 80 ? !tSecondary.HasClip() : pEnemy->m_iHealth() >= 150 || tSecondary.m_iClip < 2))
			m_iBestSlot = WEAPON_SLOT_MELEE;
		else if (tSecondary.HasClip() && (bPrimaryEmpty || bEnemyCanAirblast
			|| (bHasEnemy && flEnemyDist <= 250.0f && pEnemy->m_iHealth() <= 90)))
			m_iBestSlot = WEAPON_SLOT_SECONDARY;
		else if (tPrimary.CanUseNow())
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_DEMOMAN:
	{
		// Pipes first; bottle when they can't come out
		if (tPrimary.CanUseNow())
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		else if (bMeleeReachable && flEnemyDist <= 260.0f)
			m_iBestSlot = WEAPON_SLOT_MELEE;
		else if (tPrimary.HasWeapon())
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_ENGINEER:
	{
		// Shotgun inside its falloff range; pistol beyond it or when dry
		if (bMeleeReachable && tPrimary.UsesAmmo() && !tPrimary.HasClip() && !tSecondary.HasClip() && flEnemyDist <= 200.0f)
			m_iBestSlot = WEAPON_SLOT_MELEE;
		else if (tPrimary.CanUseNow() && (!bHasEnemy || flEnemyDist <= 1000.0f))
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		else if ((!tPrimary.HasAnyAmmo() || (bHasEnemy && flEnemyDist > 1100.0f))
			&& (tSecondary.HasClip() || tSecondary.HasReserve()))
			m_iBestSlot = WEAPON_SLOT_SECONDARY;
		else if (tPrimary.HasAnyAmmo())
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		break;
	}
	default:
		break;
	}

	// Unibox fallbacks: never pick melee we don't carry, and never idle on melee
	// while a usable ranged slot exists
	if (m_iBestSlot == WEAPON_SLOT_MELEE && !bHasMelee)
		m_iBestSlot = GetRangedFallbackSlot(pLocal);
	if (m_iBestSlot == -1 && m_iCurrentSlot == WEAPON_SLOT_MELEE)
		m_iBestSlot = GetRangedFallbackSlot(pLocal);

	// Avoid Melee (Necromancer-specific config, kept from the old version)
	if (CFG::NavBot_AvoidMelee && m_iBestSlot == WEAPON_SLOT_MELEE)
	{
		if (tPrimary.HasClip())
			m_iBestSlot = WEAPON_SLOT_PRIMARY;
		else if (tSecondary.HasClip())
			m_iBestSlot = WEAPON_SLOT_SECONDARY;
		else
			m_iBestSlot = -1;
	}
}

void CNavBot::SwitchWeapon(C_TFPlayer* pLocal)
{
	if (!pLocal || !CFG::NavBot_SwitchWeapons)
		return;

	// Throttle weapon switching (like Amalgam's Timer - 0.2s)
	static float flLastSwitchTime = 0.0f;
	float flCurTime = I::GlobalVars->curtime;
	// Negative delta = curtime reset on a map change — don't stay throttled
	{
		const float flSinceSwitch = flCurTime - flLastSwitchTime;
		if (flSinceSwitch >= 0.0f && flSinceSwitch < 0.2f)
			return;
	}

	// Update current slot
	auto pActiveWep = pLocal->m_hActiveWeapon().Get();
	if (pActiveWep)
	{
		auto pWeaponBase = pActiveWep->As<C_TFWeaponBase>();
		m_iCurrentSlot = pWeaponBase ? pWeaponBase->GetSlot() : -1;
	}
	else
		m_iCurrentSlot = -1;

	// Determine best slot
	UpdateBestSlot(pLocal);

	// A slot picked for reloading wins over the combat-best pick (Unibox
	// UpdateSlot: iDesiredSlot = iReloadSlot != -1 ? iReloadSlot : bestSlot)
	const int iDesiredSlot = m_iLastReloadSlot >= 0 ? m_iLastReloadSlot : m_iBestSlot;

	// Switch if needed (like Amalgam's SetSlot)
	if (iDesiredSlot >= 0 && m_iCurrentSlot != iDesiredSlot)
	{
		auto sCommand = "slot" + std::to_string(iDesiredSlot + 1);
		I::EngineClient->ClientCmd_Unrestricted(sCommand.c_str());
		m_iCurrentSlot = iDesiredSlot;
		flLastSwitchTime = flCurTime;
	}
}

// === GetReloadWeaponSlot (ported from Unibox's CNavBotReload) ===
// Which ranged slot, if any, we should be holding so it refills. An active
// reload finishes first; above-capture priorities never weapon-swap; and no
// swapping mid-fight (enemy inside ~250u, or stalking within 500u). Far from
// anything dangerous, a slot swaps when its clip drops below max/divider
// (divider 1 = far from combat, 3 = otherwise).
int CNavBot::GetReloadWeaponSlot(C_TFPlayer* pLocal)
{
	if (!pLocal || !CFG::NavBot_ReloadWeapons)
		return -1;

	// Finish an in-progress reload on the active ranged slot
	if (G::bReloading && m_iCurrentSlot >= WEAPON_SLOT_PRIMARY && m_iCurrentSlot <= WEAPON_SLOT_SECONDARY)
		return m_iCurrentSlot;

	// Escapes / supply runs own the weapon (Unibox: priority > Capture)
	if (G_NavEngine.m_eCurrentPriority > NAV_PRIO_CAPTURE)
		return -1;

	const auto& tEnemy = m_tClosestEnemy;
	if ((G_NavEngine.m_eCurrentPriority == NAV_PRIO_STAYNEAR && tEnemy.m_flDist <= 500.0f) || tEnemy.m_flDist <= 250.0f)
		return -1;

	const float flDivider = G_NavEngine.m_eCurrentPriority < NAV_PRIO_STAYNEAR && tEnemy.m_flDist > 500.0f ? 1.0f : 3.0f;

	auto SlotNeedsReload = [&](int iSlot, bool bIgnoreReserve) -> bool
	{
		auto pWeapon = pLocal->GetWeaponFromSlot(iSlot);
		if (!pWeapon)
			return false;

		// Melee / medigun / drinks never reload
		if (pWeapon->m_iPrimaryAmmoType() < 0)
			return false;

		auto pWeaponInfo = pWeapon->GetWeaponInfo();
		if (!pWeaponInfo)
			return false;

		// No clip, or nothing in reserve to feed it — swapping can't help
		if (!bIgnoreReserve && (pWeaponInfo->iMaxClip1 < 0 || !pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType())))
			return false;

		return pWeapon->m_iClip1() < pWeaponInfo->iMaxClip1 / flDivider;
	};

	// Particle cannon / Pomson draw from a recharging pool, not reserve ammo
	auto SlotIs = [pLocal](int iSlot, int iWeaponId) -> bool
	{
		auto pWeapon = pLocal->GetWeaponFromSlot(iSlot);
		return pWeapon && pWeapon->GetWeaponID() == iWeaponId;
	};

	if (SlotNeedsReload(WEAPON_SLOT_PRIMARY, SlotIs(WEAPON_SLOT_PRIMARY, TF_WEAPON_PARTICLE_CANNON)
		|| SlotIs(WEAPON_SLOT_PRIMARY, TF_WEAPON_DRG_POMSON)))
		return WEAPON_SLOT_PRIMARY;
	if (SlotNeedsReload(WEAPON_SLOT_SECONDARY, SlotIs(WEAPON_SLOT_SECONDARY, TF_WEAPON_RAYGUN)))
		return WEAPON_SLOT_SECONDARY;

	return -1;
}

// === RunReloadInterrupt (ported from Unibox's CNavBotReload::Run) ===
// While a ranged slot is mid-reload and the closest enemy is known, back off
// to the nearest hiding spot that breaks the enemy's sightline (Unibox finds
// a spot up to 5 connections away that can't see the threat). Returns true
// while the retreat is in progress.
bool CNavBot::RunReloadInterrupt(C_TFPlayer* pLocal)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded())
		return false;

	// Task sources: the ReloadWeapons slot picker, or a gun that ran dry and is
	// auto-reloading (Unibox also retreats on that with StalkEnemies on)
	const bool bSlotTask = CFG::NavBot_ReloadWeapons &&
		m_iLastReloadSlot >= WEAPON_SLOT_PRIMARY && m_iLastReloadSlot <= WEAPON_SLOT_SECONDARY;
	const bool bAutoReloadTask = G::bReloading && (CFG::NavBot_ReloadWeapons || CFG::NavBot_StalkEnemies);
	if (!bSlotTask && !bAutoReloadTask)
	{
		// Task over — hand the engine back instead of walking a dead cover path
		if (m_eGoal == GOAL_RUN_RELOAD && G_NavEngine.IsPathing())
			G_NavEngine.CancelPath();
		return false;
	}

	// Retreat already under way — keep walking it
	if (m_eGoal == GOAL_RUN_RELOAD && G_NavEngine.IsPathing())
		return true;

	// Nothing to hide from — reload in place
	const auto& tEnemy = m_tClosestEnemy;
	if (!tEnemy.m_pPlayer)
		return false;

	const float flCurTime = I::GlobalVars->curtime;
	if (flCurTime < m_flNextReloadSolveTime)
		return false;
	m_flNextReloadSolveTime = flCurTime + 1.0f;

	Vec3 vThreat = tEnemy.m_pPlayer->m_vecOrigin();
	vThreat.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	// Nearest IN_COVER spot to us that CANNOT see the threat (our equivalent
	// of Unibox's FindClosestHidingSpot BFS)
	Vec3 vSpot = G_NavEngine.m_pMap->FindDefendSpot(pLocal->m_vecOrigin(), vThreat, true, 1000.0f, pLocal->m_iTeamNum());
	if (vSpot.IsZero())
		return false;

	return G_NavEngine.NavTo(vSpot, NAV_PRIO_STAYNEAR);
}

// === Follow Teammates (ported from Amalgam's FollowBot) ===
// Finds closest teammate and paths to them, following their movement
// Uses 2D distance + height awareness so the bot doesn't think it's "close"
// to a teammate on a different floor directly above/below.
// === Follow Teammates (TF2 escort-style trailing) ===
// Target re-selection runs at ~1s cadence (TF2 medic patient re-eval); the
// current target is tracked every tick. Repaths are timer-gated (0.75s) — the
// old code set a repath timer it NEVER CHECKED, so a walking teammate drifted
// >50u from the path destination every few ticks and caused a full re-solve
// each time (a big part of the path cancel / re-path loop).
C_TFPlayer* CNavBot::FindFollowTarget(C_TFPlayer* pLocal)
{
	Vec3 vLocalPos = pLocal->m_vecOrigin();

	// Positions of control points our team already owns (skip teammates camping them)
	std::vector<Vec3> vCapturedCPs;
	{
		C_BaseEntity* pObjectiveResource = nullptr;
		for (int j = 1; j <= I::ClientEntityList->GetMaxEntities(); j++)
		{
			auto pCE = I::ClientEntityList->GetClientEntity(j);
			if (!pCE) continue;
			auto pCN = pCE->GetClientNetworkable();
			if (!pCN) continue;
			auto pCC = pCN->GetClientClass();
			if (!pCC) continue;
			if (static_cast<ETFClassIds>(pCC->m_ClassID) == ETFClassIds::CTFObjectiveResource)
			{
				pObjectiveResource = pCE->As<C_BaseEntity>();
				break;
			}
		}
		if (pObjectiveResource)
		{
			static int nNumCPsOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_iNumControlPoints");
			static int nCPPositionsOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_vCPPositions[0]");
			static int nOwnerOffset = NetVars::GetNetVar("CTFObjectiveResource", "m_iOwner");
			if (nNumCPsOffset && nCPPositionsOffset && nOwnerOffset)
			{
				auto pBase = reinterpret_cast<std::uintptr_t>(pObjectiveResource);
				int nNumCPs = *reinterpret_cast<int*>(pBase + nNumCPsOffset);
				for (int j = 0; j < nNumCPs && j < 8; j++)
				{
					int nOwner = *reinterpret_cast<int*>(pBase + nOwnerOffset + j * sizeof(int));
					if (nOwner == pLocal->m_iTeamNum())  // Already captured by our team
						vCapturedCPs.push_back(*reinterpret_cast<Vec3*>(pBase + nCPPositionsOffset + j * sizeof(Vec3)));
				}
			}
		}
	}

	// Find closest valid teammate (2D distance so we don't pick someone on
	// another floor directly above/below just because they're "close" in 3D)
	C_TFPlayer* pBestTarget = nullptr;
	float flBestDist2D = FLT_MAX;
	int iBestIdx = -1;

	for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pEntity)
			continue;

		auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer || !pPlayer->IsAlive() || pPlayer->IsDormant() || pPlayer == pLocal)
			continue;

		// Only follow teammates
		if (pPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
			continue;

		// Skip ghosts/taunting players
		if (IsAGhost(pPlayer))
			continue;

		// Skip teammates in spawn rooms (they're AFK or just spawned — following them gets us stuck)
		if (G_NavEngine.IsNavMeshLoaded())
		{
			CNavArea* pPlayerArea = G_NavEngine.m_pMap->FindClosestNavArea(pPlayer->m_vecOrigin());
			if (pPlayerArea)
			{
				int iLocalTeam = pLocal->m_iTeamNum();
				bool bInOwnSpawn = (iLocalTeam == TF_TEAM_RED && (pPlayerArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED)) ||
									(iLocalTeam == TF_TEAM_BLUE && (pPlayerArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE));
				if (bInOwnSpawn)
					continue;
			}
		}

		// Skip teammates near engineer buildings (they cluster around them and get us stuck)
		bool bNearBuilding = false;
		Vec3 vPlayerPos = pPlayer->m_vecOrigin();
		for (auto pBuildingEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_TEAMMATES))
		{
			if (!pBuildingEntity)
				continue;
			auto pBuilding = pBuildingEntity->As<C_BaseObject>();
			if (!pBuilding || pBuilding->m_bPlacing())
				continue;
			if (vPlayerPos.DistTo(pBuilding->m_vecOrigin()) < 300.0f)
			{
				bNearBuilding = true;
				break;
			}
		}
		if (bNearBuilding)
			continue;

		// Skip teammates standing on already-captured control points
		bool bOnCapturedCP = false;
		for (const Vec3& vCPPos : vCapturedCPs)
		{
			if (vPlayerPos.DistTo(vCPPos) < 300.0f)
			{
				bOnCapturedCP = true;
				break;
			}
		}
		if (bOnCapturedCP)
			continue;

		Vec3 vDelta = vLocalPos - pPlayer->m_vecOrigin();
		float flDist2D = vDelta.Length2D();
		if (flDist2D < flBestDist2D)
		{
			flBestDist2D = flDist2D;
			pBestTarget = pPlayer;
			iBestIdx = i;
		}
	}

	if (!pBestTarget || iBestIdx < 0)
	{
		m_iFollowTargetIdx = -1;
		return nullptr;
	}
	m_iFollowTargetIdx = iBestIdx;
	return pBestTarget;
}

bool CNavBot::FollowTeammates(C_TFPlayer* pLocal)
{
	if (!pLocal || !CFG::NavBot_FollowTeammates || !G_NavEngine.IsNavMeshLoaded())
	{
		m_bIsFollowingTeammate = false;
		m_iFollowTargetIdx = -1;
		return false;
	}

	float flCurTime = I::GlobalVars->curtime;
	Vec3 vLocalPos = pLocal->m_vecOrigin();

	// Track the current target every tick (cheap)
	C_TFPlayer* pBestTarget = nullptr;
	if (m_iFollowTargetIdx > 0)
	{
		auto pCE = I::ClientEntityList->GetClientEntity(m_iFollowTargetIdx);
		pBestTarget = pCE ? pCE->As<C_TFPlayer>() : nullptr;
		if (pBestTarget && (!pBestTarget->IsAlive() || pBestTarget->IsDormant() || pBestTarget == pLocal ||
			pBestTarget->m_iTeamNum() != pLocal->m_iTeamNum() || IsAGhost(pBestTarget)))
			pBestTarget = nullptr;
	}

	// Re-select at TF2 cadence when we don't have one
	if (!pBestTarget && flCurTime >= m_flNextFollowScanTime)
	{
		m_flNextFollowScanTime = flCurTime + 1.0f;
		pBestTarget = FindFollowTarget(pLocal);
	}

	if (!pBestTarget || m_iFollowTargetIdx < 0)
	{
		m_bIsFollowingTeammate = false;
		m_iFollowTargetIdx = -1;
		if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_FOLLOW_TEAMMATE)
			G_NavEngine.CancelPath();
		return false;
	}

	m_bIsFollowingTeammate = true;

	Vec3 vTargetPos = pBestTarget->m_vecOrigin();
	Vec3 vDelta = vLocalPos - vTargetPos;
	float flDist2D = vDelta.Length2D();
	float flHeightDiff = std::abs(vLocalPos.z - vTargetPos.z);
	bool bOnSameFloor = flHeightDiff < 72.0f;

	// Gave-up distance — a teammate across the map isn't a follow, it's a march
	if (flDist2D > 2000.0f)
	{
		m_bIsFollowingTeammate = false;
		m_iFollowTargetIdx = -1;
		if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_FOLLOW_TEAMMATE)
			G_NavEngine.CancelPath();
		return false;
	}

	// Hysteresis (TF2 medic: stop-follow 75 / start-follow 250). Inside the
	// stop range we stand near them; between stop and start we keep doing
	// whatever we're doing (no micro step-follow dance); past start we chase.
	const float flStopFollowRange = CFG::NavBot_FollowDistance;
	const float flStartFollowRange = CFG::NavBot_FollowDistance * 2.0f;

	if (flDist2D <= flStopFollowRange && bOnSameFloor)
	{
		// Close enough — stand here until they move away
		if (G_NavEngine.IsPathing() && G_NavEngine.m_eCurrentPriority == NAV_PRIO_FOLLOW_TEAMMATE)
			G_NavEngine.CancelPath();
		m_vTargetPos = vTargetPos;
		return true;
	}
	if (flDist2D < flStartFollowRange && bOnSameFloor && !G_NavEngine.IsPathing())
	{
		// In the dead band with no active path — keep standing
		m_vTargetPos = vTargetPos;
		return true;
	}

	// === Position prediction (TF2 ChasePath::PredictSubjectPosition) ===
	// Lead moving targets so the bot walks to where they'll be
	Vec3 vPathTarget = vTargetPos;
	Vec3 vTargetVelocity = pBestTarget->m_vecVelocity();
	float flSpeed2D = vTargetVelocity.Length2D();

	if (flSpeed2D > 50.0f)
	{
		float flRunSpeed = 300.0f;
		float flLeadTime = 0.5f + (flDist2D / (flRunSpeed + 0.001f));

		Vec3 vLead = vTargetVelocity * flLeadTime;
		vLead.z = 0.0f;

		// If target is moving towards us, only use the perpendicular component
		Vec3 vToTarget = vTargetPos - vLocalPos;
		vToTarget.z = 0.0f;
		float flToTargetLen = vToTarget.Length();
		if (flToTargetLen > 1.0f)
		{
			Vec3 vToTargetNorm = vToTarget / flToTargetLen;
			float flDot = vToTargetNorm.Dot(vLead);
			if (flDot < 0.0f)
			{
				Vec3 vPerp = vLead - vToTargetNorm * flDot;
				vLead = vPerp;
			}
		}

		vPathTarget = vTargetPos + vLead;
	}

	// Snap to a nav area center so the destination respects floors
	{
		CNavArea* pTargetArea = G_NavEngine.m_pMap->FindClosestNavArea(vPathTarget);
		if (pTargetArea)
		{
			Vec3 vAreaCenter = pTargetArea->m_vCenter;
			vAreaCenter.z = vPathTarget.z;
			vPathTarget = vAreaCenter;
		}
	}

	// === Timer-gated repath (the fix for the cancel loop) ===
	bool bNeedRepath = !G_NavEngine.IsPathing() || G_NavEngine.m_eCurrentPriority != NAV_PRIO_FOLLOW_TEAMMATE;
	if (!bNeedRepath)
		bNeedRepath = G_NavEngine.GetDestination().DistTo(vPathTarget) > 150.0f;

	if (bNeedRepath && flCurTime >= m_flNextFollowRepathTime)
	{
		if (G_NavEngine.NavTo(vPathTarget, NAV_PRIO_FOLLOW_TEAMMATE))
			m_flNextFollowRepathTime = flCurTime + 0.75f;
		else
			m_flNextFollowRepathTime = flCurTime + 0.25f;   // solve throttled — retry soon
	}

	m_vTargetPos = vTargetPos;
	return true;
}

// === Auto Join Team (ported from Amalgam's AutoJoin) ===
// Automatically joins the selected team when enabled
void CNavBot::AutoJoinTeam(C_TFPlayer* pLocal)
{
	if (!pLocal)
		return;

	int iDesiredTeam = CFG::NavBot_AutoJoinTeam;
	if (!iDesiredTeam)
		return;

	float flCurTime = I::GlobalVars->curtime;
	if (flCurTime < m_flNextAutoJoinTeamTime)
		return;

	// Already on the desired team - no need to rejoin
	int iCurrentTeam = 0;
	if (pLocal->IsInValidTeam(&iCurrentTeam))
	{
		// Already on a valid team (Red or Blue) - check if it matches desired team
		if (iDesiredTeam == 4)  // Random — any valid team is fine
		{
			m_iLastRandomTeamAttempt = 0;  // Reset random tracking when on valid team
			return;  // Already on a team, no need to rejoin
		}
		else if (iDesiredTeam != 3)  // Not spectator
		{
			// Map CFG values to TF team values: 1=Blue(TF_TEAM_BLUE=3), 2=Red(TF_TEAM_RED=2)
			int iDesiredTFTeam = (iDesiredTeam == 1) ? 3 : 2;  // Blue=3, Red=2
			if (iCurrentTeam == iDesiredTFTeam)
			{
				m_iLastRandomTeamAttempt = 0;  // Reset random tracking when on valid team
				return;  // Already on the correct team
			}
		}
		else if (iCurrentTeam == 1)  // TF_TEAM_SPECTATOR = 1
			return;  // Already spectator
	}

	m_flNextAutoJoinTeamTime = flCurTime + 1.0f;  // Throttle to once per second

	switch (iDesiredTeam)
	{
	case 1:  // Blue
		I::EngineClient->ClientCmd_Unrestricted("jointeam blue");
		break;
	case 2:  // Red
		I::EngineClient->ClientCmd_Unrestricted("jointeam red");
		break;
	case 3:  // Spectator
		I::EngineClient->ClientCmd_Unrestricted("jointeam spectator");
		break;
	case 4:  // Random (Red or Blue only, never spectator) — alternate if first attempt fails
	{
		// If we tried a team last time and still aren't on a team, try the other one
		int iTeam = 0;
		if (m_iLastRandomTeamAttempt == 1)
			iTeam = 2;  // Tried Blue last, try Red
		else if (m_iLastRandomTeamAttempt == 2)
			iTeam = 1;  // Tried Red last, try Blue
		else
			iTeam = (std::rand() % 2 == 0) ? 1 : 2;  // First attempt: pick randomly

		m_iLastRandomTeamAttempt = iTeam;
		I::EngineClient->ClientCmd_Unrestricted(iTeam == 1 ? "jointeam blue" : "jointeam red");
		break;
	}
	}
	I::EngineClient->ClientCmd_Unrestricted("menuclosed");
}

bool CNavBot::IsReachable(C_TFPlayer* pLocal, const Vec3& vDestination)
{
	if (!pLocal || !G_NavEngine.IsNavMeshLoaded())
		return false;

	auto pMap = G_NavEngine.m_pMap.get();
	if (!pMap || pMap->m_eState != NavState::Active)
		return false;

	// === Step 1: Quick check - is there a nav area at/under the destination? ===
	// If the destination isn't on any nav area, there's no way to walk there.
	// This is a cheap O(1) check that eliminates most invalid destinations.
	CNavArea* pDestArea = pMap->FindClosestNavArea(vDestination);
	if (!pDestArea)
		return false;

	// FindClosestNavArea can return a fallback area that's just the closest center,
	// but the destination itself might not actually be on any nav area.
	// Verify the destination is overlapping its closest nav area.
	bool bDestOverlapping = pDestArea->IsOverlapping(vDestination) &&
	                       vDestination.z >= pDestArea->m_flMinZ && vDestination.z <= pDestArea->m_flMaxZ + 100.0f;
	if (!bDestOverlapping)
		return false;

	// === Step 2: Full check - is there a connected path from local to destination? ===
	// This runs A* pathfinding to verify all nav areas along the route are valid.
	Vec3 vLocalPos = pLocal->m_vecOrigin();
	CNavArea* pLocalArea = pMap->FindClosestNavArea(vLocalPos);
	if (!pLocalArea)
		return false;

	// Same-area case: local and destination are on the same nav area
	if (pLocalArea == pDestArea)
		return true;

	// Run A* pathfinding to verify connectivity
	int iResult = 0;
	std::vector<void*> vPath = pMap->FindPath(pLocalArea, pDestArea, &iResult);

	// Empty path with START_END_SAME means same area (already handled above)
	// Empty path with no result means no path exists
	if (vPath.empty() && iResult != micropather::MicroPather::START_END_SAME)
		return false;

	return true;
}

void CNavBot::ResetAutoJoinTimers()
{
	m_flNextAutoJoinTeamTime = 0.0f;
	m_flNextAutoJoinClassTime = 0.0f;
	m_iLastRandomTeamAttempt = 0;
}

// === Auto Join Class (ported from Amalgam's AutoJoin) ===
// Automatically joins the selected class when enabled
void CNavBot::AutoJoinClass(C_TFPlayer* pLocal)
{
	if (!pLocal)
		return;

	int iDesiredClass = CFG::NavBot_AutoJoinClass;
	if (!iDesiredClass)
		return;

	// Must be on a team first before joining a class
	if (!pLocal->IsInValidTeam())
		return;

	// Already the desired class - no need to rejoin
	if (pLocal->m_iClass() == iDesiredClass)
		return;

	float flCurTime = I::GlobalVars->curtime;
	if (flCurTime < m_flNextAutoJoinClassTime)
		return;

	m_flNextAutoJoinClassTime = flCurTime + 1.0f;  // Throttle to once per second (like Amalgam)

	static const char* szClassNames[] = {
		"scout", "sniper", "soldier", "demoman",
		"medic", "heavyweapons", "pyro", "spy", "engineer"
	};

	int iClassIndex = iDesiredClass - 1;  // 0-based index into class names

	I::EngineClient->ClientCmd_Unrestricted(std::format("joinclass {}", szClassNames[iClassIndex]).c_str());
	I::EngineClient->ClientCmd_Unrestricted("menuclosed");
}

// === Follow Tagged Players ===
// Follows players that have the "Follow Player" tag set in the player list
// Uses 2D distance + height awareness so the bot doesn't think it's "close"
// to a tagged player on a different floor directly above/below.
bool CNavBot::FollowTaggedPlayer(C_TFPlayer* pLocal)
{
	if (!pLocal || !CFG::NavBot_FollowTaggedPlayers || !G_NavEngine.IsNavMeshLoaded())
	{
		m_bIsFollowingTaggedPlayer = false;
		m_iFollowTaggedTargetIdx = -1;
		return false;
	}

	float flCurTime = I::GlobalVars->curtime;
	Vec3 vLocalPos = pLocal->m_vecOrigin();

	// Find the closest tagged player that is alive
	C_TFPlayer* pBestTarget = nullptr;
	float flBestDist2D = FLT_MAX;

	const int nMaxEntities = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = 1; i <= nMaxEntities; i++)
	{
		IClientEntity* pClientEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pClientEntity)
			continue;

		auto pNetworkable = pClientEntity->GetClientNetworkable();
		if (!pNetworkable)
			continue;

		auto pClientClass = pNetworkable->GetClientClass();
		if (!pClientClass || static_cast<ETFClassIds>(pClientClass->m_ClassID) != ETFClassIds::CTFPlayer)
			continue;

		auto pPlayer = pClientEntity->As<C_TFPlayer>();
		if (!pPlayer || pPlayer == pLocal || !pPlayer->IsAlive())
			continue;

		// Check if this player has the FollowPlayer tag
		PlayerPriority info{};
		if (!F::Players->GetInfo(i, info) || !info.FollowPlayer)
			continue;

		Vec3 vPlayerPos = pPlayer->m_vecOrigin();
		Vec3 vDelta = vLocalPos - vPlayerPos;
		float flDist2D = vDelta.Length2D();

		if (flDist2D < flBestDist2D)
		{
			flBestDist2D = flDist2D;
			pBestTarget = pPlayer;
		}
	}

	// No tagged player found
	if (!pBestTarget)
	{
		m_bIsFollowingTaggedPlayer = false;
		m_iFollowTaggedTargetIdx = -1;
		return false;
	}

	m_bIsFollowingTaggedPlayer = true;
	m_iFollowTaggedTargetIdx = pBestTarget->entindex();

	Vec3 vTargetPos = pBestTarget->m_vecOrigin();

	// === Height-aware proximity check ===
	float flHeightDiff = std::abs(vLocalPos.z - vTargetPos.z);
	bool bOnSameFloor = flHeightDiff < 72.0f;

	// === Ported from TF2 medic bot's ChasePath approach ===
	// The TF2 medic NEVER stops following — it uses ChasePath which continuously
	// updates the path to the moving target every tick. When close, it stops moving
	// but keeps the follow state active so it immediately follows when the target moves.

	// Stop-follow range: how close before we stop actively pathing (stay near)
	// Start-follow range: how far before we start actively chasing
	const float flStopFollowRange = CFG::NavBot_FollowDistance;
	const float flStartFollowRange = CFG::NavBot_FollowDistance * 3.0f;

	// If we're close enough and on the same floor, we don't need to move
	// BUT we keep the follow state active — don't cancel the path entirely
	if (flBestDist2D <= flStopFollowRange && bOnSameFloor)
	{
		// We're close enough — cancel active pathing but stay in follow state
		// This is like the TF2 medic staying near the patient without actively chasing
		if (G_NavEngine.m_eCurrentPriority == NAV_PRIO_FOLLOW_TAGGED && G_NavEngine.IsPathing())
			G_NavEngine.CancelPath();
		return true;
	}

	// === Position prediction (ported from TF2 bot's ChasePath::PredictSubjectPosition) ===
	// Estimate where the target will be when we arrive, based on their velocity.
	// This makes the bot "lead" moving targets instead of always chasing where they were.
	Vec3 vPathTarget = vTargetPos;
	Vec3 vTargetVelocity = pBestTarget->m_vecVelocity();
	float flSpeed2D = vTargetVelocity.Length2D();

	if (flSpeed2D > 50.0f && flBestDist2D <= flStartFollowRange * 3.0f)
	{
		// Estimate time to reach target based on our run speed (~300 units/s)
		float flRunSpeed = 300.0f;
		float flLeadTime = 0.5f + (flBestDist2D / (flRunSpeed + 0.001f));

		// Predict where the target will be
		Vec3 vLead = vTargetVelocity * flLeadTime;
		vLead.z = 0.0f;  // Only lead horizontally

		// If target is moving towards us, only use perpendicular velocity for leading
		Vec3 vToTarget = vTargetPos - vLocalPos;
		vToTarget.z = 0.0f;
		float flToTargetLen = vToTarget.Length();
		if (flToTargetLen > 1.0f)
		{
			Vec3 vToTargetNorm = vToTarget / flToTargetLen;
			float flDot = vToTargetNorm.Dot(vLead);
			if (flDot < 0.0f)
			{
				// Target moving towards us — use perpendicular component only
				Vec3 vPerp = vLead - vToTargetNorm * flDot;
				vLead = vPerp;
			}
		}

		vPathTarget = vTargetPos + vLead;
	}

	// === Use nav area center as pathing destination ===
	if (G_NavEngine.IsNavMeshLoaded())
	{
		CNavArea* pTargetArea = G_NavEngine.m_pMap->FindClosestNavArea(vPathTarget);
		if (pTargetArea)
		{
			Vec3 vAreaCenter = pTargetArea->m_vCenter;
			vAreaCenter.z = vPathTarget.z;
			vPathTarget = vAreaCenter;
		}
	}

	// === Repath logic (TF2 ChasePath::RefreshPath, timer-gated) ===
	// A walking target drifts >50u from the path destination every fraction of
	// a second; without a timer that meant a full re-solve every few ticks (the
	// old code SET m_flNextRepathTime here but never CHECKED it).
	bool bNeedRepath = false;

	if (!G_NavEngine.IsPathing() || G_NavEngine.m_eCurrentPriority != NAV_PRIO_FOLLOW_TAGGED)
	{
		// Not currently following — start pathing
		bNeedRepath = true;
	}
	else
	{
		float flDistFromDest = G_NavEngine.GetDestination().DistTo(vPathTarget);
		if (flDistFromDest > 150.0f)
		{
			// Target moved significantly — repath
			bNeedRepath = true;
		}
	}

	if (bNeedRepath && flCurTime >= m_flNextFollowRepathTime)
	{
		if (G_NavEngine.NavTo(vPathTarget, NAV_PRIO_FOLLOW_TAGGED))
			m_flNextFollowRepathTime = flCurTime + 0.75f;
		else
			m_flNextFollowRepathTime = flCurTime + 0.25f;   // solve throttled — retry soon
	}

	// Target still exists — the goal holds even while the solve retries
	m_vTargetPos = vTargetPos;
	return true;
}

// === AutoScope - automatically scope in when enemies are visible ===
// Ported from Amalgam's BotUtils::AutoScope
// Two modes: Simple (velocity prediction) and MoveSim (full movement simulation)
// Scopes in when a visible enemy is found, unscopes after cancel time with no enemy
void CNavBot::AutoScope(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal || !pWeapon || !pCmd)
		return;

	bool bIsClassic = pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC;

	// Only run for sniper rifles
	if (!CFG::NavBot_AutoScope
		|| (pWeapon->GetWeaponID() != TF_WEAPON_SNIPERRIFLE
			&& !bIsClassic
			&& pWeapon->GetWeaponID() != TF_WEAPON_SNIPERRIFLE_DECAP))
	{
		m_bAutoScopeKeep = false;
		m_mAutoScopeCache.clear();
		return;
	}

	// Clear cache every other tick (like Amalgam's bShouldClearCache logic)
	static bool bShouldClearCache = false;
	bShouldClearCache = !bShouldClearCache;
	if (bShouldClearCache)
		m_mAutoScopeCache.clear();

	float flCurTime = I::GlobalVars->curtime;

	// === Wait After Shot (sniper only) ===
	// After firing a scoped shot, block IN_ATTACK until the wait time expires
	// and we've re-scoped. This prevents the aimbot from firing unscoped shots
	// at the next enemy before AutoScope can re-scope.
	bool bIsSniperRifle = (pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE
		|| bIsClassic
		|| pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_DECAP);
	bool bCurrentlyScoped = pLocal->InCond(TF_COND_ZOOMED);

	if (CFG::NavBot_AutoScopeWaitAfterShot > 0.0f && bIsSniperRifle)
	{
		// Detect the scope-to-unscope transition — a scoped shot was just fired
		if (m_bAutoScopeWasScopedLastTick && !bCurrentlyScoped && m_flAutoScopeLastShotTime == 0.0f)
			m_flAutoScopeLastShotTime = flCurTime;

		// During the wait period, set flag so aimbot won't fire
		if (m_flAutoScopeLastShotTime > 0.0f)
		{
			float flElapsed = flCurTime - m_flAutoScopeLastShotTime;
			if (flElapsed < CFG::NavBot_AutoScopeWaitAfterShot || !bCurrentlyScoped)
			{
				// Still waiting or not yet re-scoped — tell aimbot not to fire
				G::bAutoScopeWaitActive = true;
			}
			else
			{
				// Wait over and we're scoped again — allow firing
				m_flAutoScopeLastShotTime = 0.0f;
			}
		}
	}

	m_bAutoScopeWasScopedLastTick = bCurrentlyScoped;

	// === Handle keep state for Classic rifle ===
	if (bIsClassic)
	{
		if (m_bAutoScopeKeep)
		{
			if (!(pCmd->buttons & IN_ATTACK))
				pCmd->buttons |= IN_ATTACK;
			if (flCurTime - m_flAutoScopeTimer >= CFG::NavBot_AutoScopeCancelTime)
				pCmd->buttons |= IN_JUMP;  // Cancel classic charge
		}
		if (!IsOnGround(pLocal) && !(pCmd->buttons & IN_ATTACK))
			m_bAutoScopeKeep = false;
	}
	else
	{
		// === Handle keep state for regular sniper rifles ===
		if (m_bAutoScopeKeep)
		{
			if (pLocal->InCond(TF_COND_ZOOMED))
			{
				if (flCurTime - m_flAutoScopeTimer >= CFG::NavBot_AutoScopeCancelTime)
				{
					m_bAutoScopeKeep = false;
					pCmd->buttons |= IN_ATTACK2;  // Unscope
					return;
				}
			}
		}
	}

	// === Find enemies sorted by distance ===
	Vec3 vLocalOrigin = pLocal->m_vecOrigin();

	// Get the nav area we're heading toward (for tracing from destination position)
	CNavArea* pCurrentDestinationArea = nullptr;
	const auto& vCrumbs = G_NavEngine.GetCrumbs();
	if (vCrumbs.size() > 4)
		pCurrentDestinationArea = vCrumbs[4].m_pNavArea;

	auto pLocalNav = pCurrentDestinationArea ? pCurrentDestinationArea : G_NavEngine.GetLocalNavArea();
	if (!pLocalNav && G_NavEngine.m_pMap)
		pLocalNav = G_NavEngine.m_pMap->FindClosestNavArea(vLocalOrigin);
	if (!pLocalNav)
		return;

	Vec3 vFrom = pLocalNav->m_vCenter;
	vFrom.z += PLAYER_JUMP_HEIGHT;

	std::vector<std::pair<C_BaseEntity*, float>> vEnemiesSorted;
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		if (!pEntity || !H::Entities->IsEntityValid(pEntity))
			continue;
		auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer || !pPlayer->IsAlive() || pPlayer->IsDormant())
			continue;
		vEnemiesSorted.emplace_back(pEntity, pPlayer->m_vecOrigin().DistToSqr(vLocalOrigin));
	}

	// Also check enemy buildings
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
	{
		if (!pEntity || !H::Entities->IsEntityValid(pEntity))
			continue;
		if (pEntity->IsDormant())
			continue;
		vEnemiesSorted.emplace_back(pEntity, pEntity->GetAbsOrigin().DistToSqr(vLocalOrigin));
	}

	if (vEnemiesSorted.empty())
		return;

	std::sort(vEnemiesSorted.begin(), vEnemiesSorted.end(),
		[](const auto& a, const auto& b) { return a.second < b.second; });

	// === Visibility check lambda ===
	auto CheckVisibility = [&](const Vec3& vTo, int iEntIndex) -> bool
	{
		CGameTrace trace = {};
		CTraceFilterWorldAndPropsOnlyAmalgam filter = {};

		// Trace from local pos first
		Vec3 vLocalEye = Vec3(vLocalOrigin.x, vLocalOrigin.y, vLocalOrigin.z + PLAYER_JUMP_HEIGHT);
		H::AimUtils->Trace(vLocalEye, vTo, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
		bool bHit = trace.fraction == 1.0f;
		if (!bHit)
		{
			// Try from our destination pos
			H::AimUtils->Trace(vFrom, vTo, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
			bHit = trace.fraction == 1.0f;
		}

		if (iEntIndex != -1)
			m_mAutoScopeCache[iEntIndex] = bHit;

		if (bHit)
		{
			if (bIsClassic)
				pCmd->buttons |= IN_ATTACK;
			else if (!pLocal->InCond(TF_COND_ZOOMED) && !(pCmd->buttons & IN_ATTACK2))
				pCmd->buttons |= IN_ATTACK2;

			m_flAutoScopeTimer = flCurTime;
			m_bAutoScopeKeep = true;
			return true;
		}
		return false;
	};

	bool bSimple = (CFG::NavBot_AutoScope == 1);  // 1=Simple, 2=MoveSim
	int iMaxTicks = TIME_TO_TICKS(0.5f);

	for (auto& [pEnemy, _] : vEnemiesSorted)
	{
		int iEntIndex = pEnemy->entindex();

		// Check cache first
		if (m_mAutoScopeCache.contains(iEntIndex))
		{
			if (m_mAutoScopeCache[iEntIndex])
			{
				if (bIsClassic)
					pCmd->buttons |= IN_ATTACK;
				else if (!pLocal->InCond(TF_COND_ZOOMED) && !(pCmd->buttons & IN_ATTACK2))
					pCmd->buttons |= IN_ATTACK2;

				m_flAutoScopeTimer = flCurTime;
				m_bAutoScopeKeep = true;
				break;
			}
			continue;
		}

		// Check non-predicted position first
		Vec3 vNonPredictedPos = pEnemy->GetAbsOrigin();
		vNonPredictedPos.z += PLAYER_JUMP_HEIGHT;
		if (CheckVisibility(vNonPredictedPos, iEntIndex))
			return;

		// === MoveSim mode: predict where the enemy will be ===
		if (!bSimple)
		{
			// Only simulate players — buildings can't be cast to C_TFPlayer
			if (!IsPlayer(pEnemy))
				continue;

			auto pEnemyPlayer = pEnemy->As<C_TFPlayer>();
			if (!pEnemyPlayer || !pEnemyPlayer->IsAlive() || !H::Entities->IsEntityValid(pEnemyPlayer))
				continue;

			F::MovementSimulation->Initialize(pEnemyPlayer);
			if (F::MovementSimulation->HasFailed())
			{
				F::MovementSimulation->Restore();
				continue;
			}

			if (!F::MovementSimulation->IsStationary())
			{
				for (int i = 0; i < iMaxTicks; i++)
					F::MovementSimulation->RunTick();
			}
		}

		bool bResult = false;
		Vec3 vPredictedPos = bSimple
			? pEnemy->GetAbsOrigin() + pEnemy->GetAbsVelocity() * TICKS_TO_TIME(iMaxTicks)
			: F::MovementSimulation->GetOrigin();

		CNavArea* pTargetNav = nullptr;
		if (G_NavEngine.m_pMap)
			pTargetNav = G_NavEngine.m_pMap->FindClosestNavArea(vPredictedPos);
		if (pTargetNav)
		{
			Vec3 vTo = pTargetNav->m_vCenter;

			// If player is in the air, don't snap to nav area below them
			if (IsPlayer(pEnemy) && !IsOnGround(pEnemy->As<C_TFPlayer>()) && vTo.DistToSqr(vPredictedPos) >= 400.0f * 400.0f)
				vTo = vPredictedPos;

			vTo.z += PLAYER_JUMP_HEIGHT;
			bResult = CheckVisibility(vTo, iEntIndex);
		}
		if (!bSimple)
			F::MovementSimulation->Restore();

		if (bResult)
			break;
	}
}
