#include "MovementSimulation.h"
#include "../LagRecords/LagRecords.h"
#include "../CFG.h"
#include "../amalgam_port/AmalgamCompat.h"
#include <numeric>

static CUserCmd s_tDummyCmd = {};

// Cached ConVars for performance - initialized once
static ConVar* s_pSvGravity = nullptr;
static ConVar* s_pSvAirAccelerate = nullptr;
static float s_flCachedGravity = 800.0f;
static float s_flCachedAirAccel = 10.0f;
static int s_nLastConVarFrame = -1;

// Update cached convars once per frame max
static inline void UpdateCachedConVars()
{
	int nFrame = I::GlobalVars ? I::GlobalVars->framecount : 0;
	if (nFrame == s_nLastConVarFrame)
		return;
	s_nLastConVarFrame = nFrame;
	
	if (!s_pSvGravity)
		s_pSvGravity = I::CVar->FindVar("sv_gravity");
	if (!s_pSvAirAccelerate)
		s_pSvAirAccelerate = I::CVar->FindVar("sv_airaccelerate");
	
	s_flCachedGravity = s_pSvGravity ? s_pSvGravity->GetFloat() : 800.0f;
	s_flCachedAirAccel = s_pSvAirAccelerate ? std::max(s_pSvAirAccelerate->GetFloat(), 1.f) : 10.f;
}

// Helper to get gravity (cached)
static inline float GetGravity()
{
	return s_flCachedGravity;
}

// Helper for air friction scaling
static inline float GetFrictionScale(float flVelocityXY, float flTurn, float flVelocityZ, float flMin = 50.f, float flMax = 150.f)
{
	if (0.f >= flVelocityZ || flVelocityZ > 250.f)
		return 1.f;

	flMin *= s_flCachedAirAccel;
	flMax *= s_flCachedAirAccel;

	return Math::RemapValClamped(fabsf(flVelocityXY * flTurn), flMin, flMax, 1.f, 0.25f);
}

// ============================================================================
// PlayerBehavior::GetPlaystyle - Moved from header to avoid TF_CLASS dependency
// ============================================================================
float PlayerBehavior::GetPlaystyle(const char** pOutCategory) const
{
	// Not enough data - can't make a judgment
	if (m_nSampleCount < 20)
	{
		if (pOutCategory) *pOutCategory = "LEARNING";
		return 0.0f;
	}
	
	float flScore = 0.0f;
	float flWeight = 0.0f;
	
	// =====================================================================
	// Q1: Does this player attack us or teammates? (most important signal)
	// =====================================================================
	if (m_Combat.m_nAttackingSamples > 5)
	{
		const float flAttackRate = static_cast<float>(m_Combat.m_nAttackingSamples) / static_cast<float>(m_nSampleCount);
		if (flAttackRate > 0.3f)
		{
			flScore += 0.4f;
			flWeight += 1.0f;
		}
		else if (flAttackRate < 0.1f)
		{
			flScore -= 0.2f;
			flWeight += 0.5f;
		}
	}
	
	// =====================================================================
	// Q2: When they have melee out and see us, do they charge?
	// =====================================================================
	if (m_Combat.m_nMeleeChargeSamples + m_Combat.m_nMeleePassiveSamples > 2)
	{
		if (m_Combat.m_flMeleeChargeRate > 0.5f)
		{
			flScore += 0.6f;
			flWeight += 1.5f;
		}
	}
	
	// =====================================================================
	// Q3: When healed, do they push forward or stay back?
	// =====================================================================
	if (m_Positioning.m_nHealedPushSamples + m_Positioning.m_nHealedPassiveSamples > 3)
	{
		if (m_Positioning.m_flHealedAggroBoost > 0.6f)
		{
			flScore += 0.3f;
			flWeight += 0.8f;
		}
		else if (m_Positioning.m_flHealedAggroBoost < 0.3f)
		{
			flScore -= 0.2f;
			flWeight += 0.6f;
		}
	}
	
	// =====================================================================
	// Q4: When low HP, do they run or fight?
	// =====================================================================
	if (m_Positioning.m_nLowHPRetreatSamples + m_Positioning.m_nLowHPFightSamples > 3)
	{
		if (m_Positioning.m_flLowHealthRetreatRate > 0.7f)
		{
			flScore -= 0.4f;
			flWeight += 1.2f;
		}
		else if (m_Positioning.m_flLowHealthRetreatRate < 0.3f)
		{
			flScore += 0.4f;
			flWeight += 1.2f;
		}
	}
	
	// =====================================================================
	// Q5: Do they dodge when shot at?
	// =====================================================================
	if (m_Combat.m_nReactionSamples > 3)
	{
		if (m_Combat.m_flReactionToThreat > 0.7f)
		{
			flScore -= 0.15f;
			flWeight += 0.5f;
		}
		else if (m_Combat.m_flReactionToThreat < 0.2f)
		{
			flScore += 0.1f;
			flWeight += 0.3f;
		}
	}
	
	// =====================================================================
	// Q6: Do they peek corners and retreat?
	// =====================================================================
	if (m_Strafe.m_nCornerPeekSamples > 2)
	{
		if (m_Strafe.m_flCornerPeekRate > 0.3f)
		{
			flScore -= 0.25f;
			flWeight += 0.7f;
		}
	}
	
	// =====================================================================
	// Q7: Do they play alone (flanker) or stick with team?
	// =====================================================================
	const int nTeamTotal = m_Positioning.m_nNearTeamSamples + m_Positioning.m_nAloneSamples;
	if (nTeamTotal > 10)
	{
		if (m_Positioning.m_flSoloPlayRate > 0.6f)
		{
			flScore += 0.2f;
			flWeight += 0.5f;
		}
		else if (m_Positioning.m_flTeamProximityRate > 0.7f)
		{
			flScore -= 0.1f;
			flWeight += 0.3f;
		}
	}
	
	// =====================================================================
	// Q8: When outnumbered, do they retreat or fight?
	// =====================================================================
	if (m_Positioning.m_flRetreatWhenOutnumbered > 0.5f)
	{
		flScore -= 0.2f;
		flWeight += 0.5f;
	}
	if (m_Positioning.m_flPushWhenAdvantage > 0.5f)
	{
		flScore += 0.15f;
		flWeight += 0.4f;
	}
	
	// =====================================================================
	// Q9: Raw movement direction - approaching or retreating?
	// =====================================================================
	const int nMoveTotal = m_Positioning.m_nAggressiveSamples + m_Positioning.m_nDefensiveSamples;
	if (nMoveTotal > 10)
	{
		const float flApproachRate = static_cast<float>(m_Positioning.m_nAggressiveSamples) / static_cast<float>(nMoveTotal);
		flScore += (flApproachRate - 0.5f) * 0.4f;
		flWeight += 0.6f;
	}
	
	// =====================================================================
	// Q10: Strafe intensity - high strafing = trying hard not to get hit
	// =====================================================================
	if (m_Strafe.m_flStrafeIntensity > 10.0f)
	{
		flScore -= 0.1f;
		flWeight += 0.3f;
	}
	
	// =====================================================================
	// CLASS-SPECIFIC BASELINE
	// =====================================================================
	float flClassBaseline = 0.0f;
	float flClassWeight = 0.0f;
	
	switch (m_nPlayerClass)
	{
	case TF_CLASS_SCOUT:
		flClassBaseline = 0.3f;
		flClassWeight = 0.4f;
		if (m_Positioning.m_flSoloPlayRate < 0.3f)
			flScore -= 0.15f;
		break;
		
	case TF_CLASS_SOLDIER:
		flClassBaseline = 0.2f;
		flClassWeight = 0.3f;
		break;
		
	case TF_CLASS_PYRO:
		flClassBaseline = 0.25f;
		flClassWeight = 0.3f;
		if (m_Combat.m_nAttackingSamples > 10)
		{
			const float flAttackRate = static_cast<float>(m_Combat.m_nAttackingSamples) / static_cast<float>(m_nSampleCount);
			if (flAttackRate > 0.4f)
				flScore += 0.2f;
		}
		break;
		
	case TF_CLASS_DEMOMAN:
		flClassBaseline = 0.0f;
		flClassWeight = 0.2f;
		break;
		
	case TF_CLASS_HEAVY:
		flClassBaseline = -0.1f;
		flClassWeight = 0.3f;
		if (m_Positioning.m_flSoloPlayRate > 0.5f)
			flScore += 0.2f;
		break;
		
	case TF_CLASS_ENGINEER:
		flClassBaseline = -0.4f;
		flClassWeight = 0.4f;
		if (m_Positioning.m_flSoloPlayRate > 0.5f)
			flScore += 0.3f;
		break;
		
	case TF_CLASS_MEDIC:
		flClassBaseline = -0.3f;
		flClassWeight = 0.4f;
		if (m_Combat.m_nAttackingSamples > 5)
		{
			const float flAttackRate = static_cast<float>(m_Combat.m_nAttackingSamples) / static_cast<float>(m_nSampleCount);
			if (flAttackRate > 0.2f)
				flScore += 0.3f;
		}
		break;
		
	case TF_CLASS_SNIPER:
		flClassBaseline = -0.5f;
		flClassWeight = 0.5f;
		if (m_Positioning.m_flAggressionScore > 0.5f)
			flScore += 0.3f;
		break;
		
	case TF_CLASS_SPY:
		flClassBaseline = 0.1f;
		flClassWeight = 0.2f;
		break;
	}
	
	// Blend class baseline with observed behavior
	const float flDataConfidence = std::min(static_cast<float>(m_nSampleCount) / 100.0f, 1.0f);
	flScore = flScore * flDataConfidence + flClassBaseline * (1.0f - flDataConfidence) * flClassWeight;
	flWeight += flClassWeight * (1.0f - flDataConfidence);
	
	// Normalize by weight
	float flFinalScore = (flWeight > 0.0f) ? (flScore / flWeight) : 0.0f;
	flFinalScore = std::clamp(flFinalScore, -1.0f, 1.0f);
	
	// Determine category
	if (pOutCategory)
	{
		if (flFinalScore > 0.5f)
			*pOutCategory = "AGGRESSIVE";
		else if (flFinalScore > 0.25f)
			*pOutCategory = "BRAVE";
		else if (flFinalScore > 0.1f)
			*pOutCategory = "CONFIDENT";
		else if (flFinalScore > -0.1f)
			*pOutCategory = "BALANCED";
		else if (flFinalScore > -0.25f)
			*pOutCategory = "CAUTIOUS";
		else if (flFinalScore > -0.5f)
			*pOutCategory = "DEFENSIVE";
		else
			*pOutCategory = "PASSIVE";
	}
	
	return flFinalScore;
}

void CMovementSimulation::Store(MoveStorage& tStorage)
{
	auto pMap = GetPredDescMap(tStorage.m_pPlayer);
	if (!pMap)
		return;

	size_t iSize = GetIntermediateDataSize(tStorage.m_pPlayer);
	if (iSize == 0)
		return;
	
	tStorage.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Alloc(iSize));
	if (!tStorage.m_pData)
		return;

	CPredictionCopy copy = { PC_NETWORKED_ONLY, tStorage.m_pData, PC_DATA_PACKED, tStorage.m_pPlayer, PC_DATA_NORMAL };
	copy.TransferData("MovementSimulationStore", tStorage.m_pPlayer->entindex(), pMap);
}

void CMovementSimulation::Reset(MoveStorage& tStorage)
{
	if (tStorage.m_pData)
	{
		auto pMap = GetPredDescMap(tStorage.m_pPlayer);
		if (pMap)
		{
			CPredictionCopy copy = { PC_NETWORKED_ONLY, tStorage.m_pPlayer, PC_DATA_NORMAL, tStorage.m_pData, PC_DATA_PACKED };
			copy.TransferData("MovementSimulationReset", tStorage.m_pPlayer->entindex(), pMap);
		}
		
		// Always free the memory, even if pMap is null
		I::MemAlloc->Free(tStorage.m_pData);
		tStorage.m_pData = nullptr;
	}
}

void CMovementSimulation::Store()
{
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	// Safety check - make sure we're in game
	if (!I::EngineClient || !I::EngineClient->IsInGame())
		return;

	// Only track when local player is holding a projectile weapon
	auto hWeapon = pLocal->m_hActiveWeapon();
	if (!hWeapon)
		return;
	
	auto pWeapon = I::ClientEntityList->GetClientEntityFromHandle(hWeapon);
	if (!pWeapon)
		return;
	
	// Check if it's a projectile weapon by weapon ID
	auto pTFWeapon = pWeapon->As<C_TFWeaponBase>();
	if (!pTFWeapon)
		return;
	
	const int nWeaponID = pTFWeapon->GetWeaponID();
	bool bIsProjectile = false;
	
	switch (nWeaponID)
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_GRENADELAUNCHER:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_FLAREGUN:
	case TF_WEAPON_FLAREGUN_REVENGE:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_CROSSBOW:
	case TF_WEAPON_SYRINGEGUN_MEDIC:
	case TF_WEAPON_CANNON:
	case TF_WEAPON_RAYGUN:
	case TF_WEAPON_DRG_POMSON:
	case TF_WEAPON_PARTICLE_CANNON:
	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_CLEAVER:
	case TF_WEAPON_GRAPPLINGHOOK:
		bIsProjectile = true;
		break;
	default:
		break;
	}
	
	if (!bIsProjectile)
		return;

	// Update cached convars once per frame
	UpdateCachedConVars();

	// Get enemy players only - no need to track teammates
	const auto& vPlayers = H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES);
	const int nDeltaCount = CFG::Aimbot_Projectile_Delta_Count;
	
	for (const auto pEntity : vPlayers)
	{
		if (!pEntity)
			continue;

		auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer)
			continue;
			
		const int nEntIndex = pPlayer->entindex();
		if (nEntIndex < 1 || nEntIndex >= 64)
			continue;
		
		// Cache commonly accessed values
		const bool bIsDead = pPlayer->deadflag();
		
		auto& vRecords = m_mRecords[nEntIndex];
		auto& vSimTimes = m_mSimTimes[nEntIndex];

		// Clear records for dead/ghost players
		if (bIsDead || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
		{
			if (!vRecords.empty()) vRecords.clear();
			if (!vSimTimes.empty()) vSimTimes.clear();
			continue;
		}
		
		const Vec3 vVelocity = pPlayer->m_vecVelocity();
		
		// Handle stationary players - clear records but don't apply any prediction bias
		if (vVelocity.IsZero())
		{
			if (!vRecords.empty()) vRecords.clear();
			continue;
		}

		const float flSimTime = pPlayer->m_flSimulationTime();
		const float flOldSimTime = pPlayer->m_flOldSimulationTime();
		const float flDeltaTime = flSimTime - flOldSimTime;

		// Skip if simulation time hasn't changed
		if (flDeltaTime <= 0.0f)
			continue;

		const Vec3 vOrigin = pPlayer->m_vecOrigin();
		const int nFlags = pPlayer->m_fFlags();
		const int nWaterLevel = pPlayer->m_nWaterLevel();
		const bool bSwimming = nWaterLevel >= 2;
		const int iMode = bSwimming ? 2 : (nFlags & FL_ONGROUND) ? 0 : 1;

		// =====================================================================
		// KNOCKBACK DETECTION - Don't record velocity changes from damage
		// If velocity changed drastically without matching expected movement,
		// it's likely knockback from a rocket/explosion
		// =====================================================================
		bool bLikelyKnockback = false;
		if (!vRecords.empty())
		{
			const auto& tLastRecord = vRecords.front();
			const Vec3 vExpectedPos = tLastRecord.m_vOrigin + tLastRecord.m_vVelocity * flDeltaTime;
			const Vec3 vActualDelta = vOrigin - tLastRecord.m_vOrigin;
			const Vec3 vExpectedDelta = tLastRecord.m_vVelocity * flDeltaTime;
			
			// Check if velocity direction changed drastically (knockback indicator)
			Vec3 vOldDir = tLastRecord.m_vVelocity;
			vOldDir.z = 0;
			Vec3 vNewDir = vVelocity;
			vNewDir.z = 0;
			
			const float flOldSpeed = vOldDir.Length();
			const float flNewSpeed = vNewDir.Length();
			
			if (flOldSpeed > 50.0f && flNewSpeed > 50.0f)
			{
				vOldDir = vOldDir * (1.0f / flOldSpeed);
				vNewDir = vNewDir * (1.0f / flNewSpeed);
				const float flDot = vOldDir.Dot(vNewDir);
				
				// Sudden direction change (>60 degrees) with speed increase = knockback
				if (flDot < 0.5f && flNewSpeed > flOldSpeed * 1.2f)
				{
					bLikelyKnockback = true;
				}
				// Or sudden large speed increase without direction change = also knockback
				else if (flNewSpeed > flOldSpeed * 1.5f && flNewSpeed > 300.0f)
				{
					bLikelyKnockback = true;
				}
			}
			// Sudden velocity from near-stationary = knockback
			else if (flOldSpeed < 30.0f && flNewSpeed > 200.0f)
			{
				bLikelyKnockback = true;
			}
		}
		
		// If knockback detected, clear records and skip this sample
		if (bLikelyKnockback)
		{
			vRecords.clear();
			continue;
		}

		// Wall collision detection - only if we have records and velocity is significant
		if (!vRecords.empty() && vVelocity.Length2DSqr() > 100.0f)
		{
			const auto& tLastRecord = vRecords.front();
			
			CGameTrace trace;
			CTraceFilterWorldAndPropsOnly filter;
			const Vec3 vStart = tLastRecord.m_vOrigin;
			const Vec3 vEnd = vStart + tLastRecord.m_vVelocity * TICK_INTERVAL;
			
			// Use player hull bounds with small inset
			const Vec3 vMins = pPlayer->m_vecMins() + 0.125f;
			const Vec3 vMaxs = pPlayer->m_vecMaxs() - 0.125f;
			
			SDK::TraceHull(vStart, vEnd, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &trace);
			
			if (trace.DidHit() && trace.plane.normal.z < 0.707f)
				vRecords.clear();
		}

		// Add new record
		Vec3 vDirection = vVelocity;
		vDirection.z = 0;
		
		vRecords.emplace_front(MoveData{ vDirection, flSimTime, iMode, vVelocity, vOrigin });

		if (vRecords.size() > 66)
			vRecords.pop_back();

		// Handle direction normalization
		auto& tCurRecord = vRecords.front();
		const float flMaxSpeed = pPlayer->TeamFortress_CalculateMaxSpeed();
		
		if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			Vec3 vNorm = vVelocity;
			vNorm.z = 0;
			vNorm.NormalizeInPlace();
			tCurRecord.m_vDirection = vNorm * flMaxSpeed;
		}
		else if (!vDirection.IsZero())
		{
			vDirection.NormalizeInPlace();
			tCurRecord.m_vDirection = vDirection * flMaxSpeed;
			if (iMode == 2)
				tCurRecord.m_vDirection = tCurRecord.m_vDirection * 2.0f;
		}

		// Store simulation time deltas and update behavior
		vSimTimes.push_front(flDeltaTime);
		if (vSimTimes.size() > static_cast<size_t>(nDeltaCount))
			vSimTimes.pop_back();
		
		UpdatePlayerBehavior(pPlayer);
	}
	
	// =========================================================================
	// PASSIVE LEARNING: Learn enemy behavior from how they fight teammates
	// This gives us pre-loaded data before we even engage them
	// Sample at ~3/sec (every ~22 ticks at 66 tick) to save CPU
	// =========================================================================
	static int s_nPassiveLearningFrame = 0;
	s_nPassiveLearningFrame++;
	
	if ((s_nPassiveLearningFrame % 22) != 0)
		return;
	
	const int nLocalTeam = pLocal->m_iTeamNum();
	const auto& vAllPlayers = H::Entities->GetGroup(EEntGroup::PLAYERS_ALL);
	
	// Find teammates who are in combat (have enemies nearby)
	for (const auto pTeammateEnt : vAllPlayers)
	{
		if (!pTeammateEnt)
			continue;
		
		auto pTeammate = pTeammateEnt->As<C_TFPlayer>();
		if (!pTeammate || pTeammate == pLocal || pTeammate->deadflag())
			continue;
		
		if (pTeammate->m_iTeamNum() != nLocalTeam)
			continue;
		
		const Vec3 vTeammatePos = pTeammate->m_vecOrigin();
		
		// Find enemies near this teammate and learn from their behavior
		for (const auto pEnemyEnt : vPlayers)
		{
			if (!pEnemyEnt)
				continue;
			
			auto pEnemy = pEnemyEnt->As<C_TFPlayer>();
			if (!pEnemy || pEnemy->deadflag())
				continue;
			
			const Vec3 vEnemyPos = pEnemy->m_vecOrigin();
			const float flDistSqr = (vEnemyPos - vTeammatePos).LengthSqr();
			
			// Only learn if enemy is within 1500 units of teammate (in combat range)
			if (flDistSqr > 2250000.0f)  // 1500^2
				continue;
			
			const int nEnemyIdx = pEnemy->entindex();
			if (nEnemyIdx < 1 || nEnemyIdx >= 64)
				continue;
			
			// Update behavior learning for this enemy (passive mode)
			// This learns aggression, class behavior, weapon awareness from teammate fights
			auto& behavior = GetOrCreateBehavior(nEnemyIdx);
			
			// Only do lightweight learning - skip expensive operations
			// These don't need local player reference, they work with any observer
			LearnClassBehavior(pEnemy, behavior);
			LearnWeaponAwareness(pEnemy, behavior);
			LearnBunnyHop(pEnemy, behavior);
			
			// Learn aggression relative to the teammate they're fighting
			Vec3 vToTeammate = vTeammatePos - vEnemyPos;
			vToTeammate.z = 0;
			const float flDist = vToTeammate.Length();
			if (flDist > 100.0f)
			{
				vToTeammate = vToTeammate * (1.0f / flDist);
				Vec3 vVel = pEnemy->m_vecVelocity();
				vVel.z = 0;
				const float flSpeed = vVel.Length();
				if (flSpeed > 50.0f)
				{
					vVel = vVel * (1.0f / flSpeed);
					const float flDot = vVel.Dot(vToTeammate);
					
					if (flDot > 0.3f)
						behavior.m_Positioning.m_nAggressiveSamples++;
					else if (flDot < -0.3f)
						behavior.m_Positioning.m_nDefensiveSamples++;
					
					const int nTotal = behavior.m_Positioning.m_nAggressiveSamples + behavior.m_Positioning.m_nDefensiveSamples;
					if (nTotal > 10)
					{
						const float flNewScore = static_cast<float>(behavior.m_Positioning.m_nAggressiveSamples) / static_cast<float>(nTotal);
						behavior.m_Positioning.m_flAggressionScore = behavior.m_Positioning.m_flAggressionScore * 0.95f + flNewScore * 0.05f;
					}
				}
			}
			
			// Mark that we've seen this enemy (increment sample count at reduced rate)
			behavior.m_nSampleCount++;
		}
	}
}

bool CMovementSimulation::Initialize(C_TFPlayer* pPlayer, MoveStorage& tStorage, bool bStrafe)
{
	if (!pPlayer || pPlayer->deadflag())
	{
		tStorage.m_bInitFailed = tStorage.m_bFailed = true;
		return false;
	}

	tStorage.m_pPlayer = pPlayer;

	// Store vars
	m_bOldInPrediction = I::Prediction->m_bInPrediction;
	m_bOldFirstTimePredicted = I::Prediction->m_bFirstTimePredicted;
	m_flOldFrametime = I::GlobalVars->frametime;

	// Store restore data
	Store(tStorage);

	// The hacks that make it work
	I::MoveHelper->SetHost(pPlayer);
	pPlayer->SetCurrentCommand(&s_tDummyCmd);

	// Duck handling
	if (pPlayer->m_bDucked() = (pPlayer->m_fFlags() & FL_DUCKING) != 0)
	{
		pPlayer->m_fFlags() &= ~FL_DUCKING;
		pPlayer->m_flDucktime() = 0.f;
		pPlayer->m_flDuckJumpTime() = 0.f;
		pPlayer->m_bDucking() = false;
		pPlayer->m_bInDuckJump() = false;
	}

	const auto pLocal = H::Entities->GetLocal();
	if (pPlayer != pLocal)
	{
		pPlayer->m_vecBaseVelocity() = Vec3();
		if (pPlayer->m_fFlags() & FL_ONGROUND)
			pPlayer->m_vecVelocity().z = std::min(pPlayer->m_vecVelocity().z, 0.f);
		else
			pPlayer->m_hGroundEntity() = nullptr;
	}

	// Setup move data
	if (!SetupMoveData(tStorage))
	{
		tStorage.m_bFailed = true;
		return false;
	}

	// Cache behavior pointer and player state for RunTick (avoid repeated lookups)
	const int nEntIndex = pPlayer->entindex();
	tStorage.m_pCachedBehavior = (nEntIndex > 0 && nEntIndex < 64) ? GetPlayerBehavior(nEntIndex) : nullptr;
	
	// Cache health/condition state once
	const int nMaxHealth = pPlayer->GetMaxHealth();
	const float flHealthPct = (nMaxHealth > 0) ? 
		static_cast<float>(pPlayer->m_iHealth()) / static_cast<float>(nMaxHealth) : 1.0f;
	tStorage.m_bCachedLowHealth = flHealthPct < 0.35f;
	tStorage.m_bCachedBeingHealed = pPlayer->InCond(TF_COND_HEALTH_BUFF) || pPlayer->InCond(TF_COND_RADIUSHEAL);
	tStorage.m_bCachedAiming = pPlayer->InCond(TF_COND_AIMING);

	const int iStrafeSamples = tStorage.m_bDirectMove
		? CFG::Aimbot_Projectile_Ground_Samples
		: CFG::Aimbot_Projectile_Air_Samples;

	// Calculate strafe if desired
	if (bStrafe)
		StrafePrediction(tStorage, iStrafeSamples);
	
	// =========================================================================
	// CIRCLE STRAFE SETUP - Set proper forward+side movement for circle strafing
	// 
	// The tightness of the circle depends on the ratio of forward to side movement:
	// - More forward, less side = big wide circle
	// - Less forward, more side = tight small circle
	// - Pure side (no forward) = spinning in place
	// 
	// avgYaw tells us how fast they're turning:
	// - avgYaw ~3-4 = slow turn = wide circle = more forward
	// - avgYaw ~6-8 = fast turn = tight circle = less forward, more side
	// - avgYaw ~10+ = very fast = very tight = mostly side movement
	// =========================================================================
	if (tStorage.m_flAverageYaw != 0.0f)
	{
		const float flMaxSpeed = tStorage.m_MoveData.m_flMaxSpeed;
		const float flAbsYaw = fabsf(tStorage.m_flAverageYaw);
		
		// Calculate forward/side ratio based on turn rate
		// Higher yaw = tighter circle = less forward, more side
		// Clamp yaw influence between 3 and 12 degrees per tick
		const float flYawFactor = std::clamp((flAbsYaw - 3.0f) / 9.0f, 0.0f, 1.0f);
		
		// Forward: 100% at slow turn (yaw=3), 20% at fast turn (yaw=12)
		const float flForwardRatio = 1.0f - flYawFactor * 0.8f;
		
		// Side: 60% at slow turn, 100% at fast turn
		const float flSideRatio = 0.6f + flYawFactor * 0.4f;
		
		tStorage.m_MoveData.m_flForwardMove = flMaxSpeed * flForwardRatio;
		
		// Side direction based on turn direction
		if (tStorage.m_flAverageYaw > 0)
			tStorage.m_MoveData.m_flSideMove = flMaxSpeed * flSideRatio;   // Strafe left
		else
			tStorage.m_MoveData.m_flSideMove = -flMaxSpeed * flSideRatio;  // Strafe right
	}

	// Run initial ticks for choked commands
	int nChoke = TIME_TO_TICKS(pPlayer->m_flSimulationTime() - pPlayer->m_flOldSimulationTime());
	nChoke = std::clamp(nChoke - 1, 0, 22);
	for (int i = 0; i < nChoke; i++)
		RunTick(tStorage, false);

	return true;
}

bool CMovementSimulation::SetupMoveData(MoveStorage& tStorage)
{
	if (!tStorage.m_pPlayer)
		return false;

	tStorage.m_MoveData.m_bFirstRunOfFunctions = false;
	tStorage.m_MoveData.m_bGameCodeMovedPlayer = false;
	tStorage.m_MoveData.m_nPlayerHandle = tStorage.m_pPlayer->GetRefEHandle();

	tStorage.m_MoveData.m_vecAbsOrigin = tStorage.m_pPlayer->m_vecOrigin();
	tStorage.m_MoveData.m_vecVelocity = tStorage.m_pPlayer->m_vecVelocity();
	tStorage.m_MoveData.m_flMaxSpeed = tStorage.m_pPlayer->TeamFortress_CalculateMaxSpeed();
	tStorage.m_MoveData.m_flClientMaxSpeed = tStorage.m_MoveData.m_flMaxSpeed;

	if (!tStorage.m_MoveData.m_vecVelocity.IsZero())
	{
		int iIndex = tStorage.m_pPlayer->entindex();
		
		// Set view angles based on velocity direction (or eye angles for shield charge)
		if (!tStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
			tStorage.m_MoveData.m_vecViewAngles = { 0.f, Math::VelocityToAngles(tStorage.m_MoveData.m_vecVelocity).y, 0.f };
		else
			tStorage.m_MoveData.m_vecViewAngles = tStorage.m_pPlayer->GetEyeAngles();

		const auto& vRecords = m_mRecords[iIndex];
		if (!vRecords.empty())
		{
			auto& tRecord = vRecords.front();
			if (!tRecord.m_vDirection.IsZero())
			{
				// Amalgam-style: direction is stored as (forwardmove, -sidemove) in world space
				// We need to convert it to the view angle space using FixMovement
				float flForwardMove = tRecord.m_vDirection.x;
				float flSideMove = -tRecord.m_vDirection.y;
				
				// Convert from world-aligned to view-aligned movement
				Math::FixMovement(flForwardMove, flSideMove, Vec3(0, 0, 0), tStorage.m_MoveData.m_vecViewAngles);
				
				tStorage.m_MoveData.m_flForwardMove = flForwardMove;
				tStorage.m_MoveData.m_flSideMove = flSideMove;
			}
		}
	}

	tStorage.m_MoveData.m_vecAngles = tStorage.m_MoveData.m_vecOldAngles = tStorage.m_MoveData.m_vecViewAngles;
	
	if (tStorage.m_pPlayer->m_hConstraintEntity())
		tStorage.m_MoveData.m_vecConstraintCenter = tStorage.m_pPlayer->m_hConstraintEntity()->GetAbsOrigin();
	else
		tStorage.m_MoveData.m_vecConstraintCenter = tStorage.m_pPlayer->m_vecConstraintCenter();
	
	tStorage.m_MoveData.m_flConstraintRadius = tStorage.m_pPlayer->m_flConstraintRadius();
	tStorage.m_MoveData.m_flConstraintWidth = tStorage.m_pPlayer->m_flConstraintWidth();
	tStorage.m_MoveData.m_flConstraintSpeedFactor = tStorage.m_pPlayer->m_flConstraintSpeedFactor();

	tStorage.m_flPredictedDelta = GetPredictedDelta(tStorage.m_pPlayer);
	tStorage.m_flSimTime = tStorage.m_pPlayer->m_flSimulationTime();
	tStorage.m_flPredictedSimTime = tStorage.m_flSimTime + tStorage.m_flPredictedDelta;
	tStorage.m_vPredictedOrigin = tStorage.m_MoveData.m_vecAbsOrigin;
	bool bSwimming = tStorage.m_pPlayer->m_nWaterLevel() >= 2;
	tStorage.m_bDirectMove = (tStorage.m_pPlayer->m_fFlags() & FL_ONGROUND) || bSwimming;

	return true;
}

static inline bool GetYawDifference(MoveData& tRecord1, MoveData& tRecord2, bool bStart, float* pYaw, 
	float flStraightFuzzyValue, int iMaxChanges = 0, int iMaxChangeTime = 0, float flMaxSpeed = 0.f)
{
	const float flYaw1 = Math::VelocityToAngles(tRecord1.m_vDirection).y;
	const float flYaw2 = Math::VelocityToAngles(tRecord2.m_vDirection).y;
	const float flTime1 = tRecord1.m_flSimTime;
	const float flTime2 = tRecord2.m_flSimTime;
	const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);

	*pYaw = Math::NormalizeAngle(flYaw1 - flYaw2);
	if (flMaxSpeed && tRecord1.m_iMode != 1)
		*pYaw *= std::clamp(tRecord1.m_vVelocity.Length2D() / flMaxSpeed, 0.f, 1.f);
	if (tRecord1.m_iMode == 1)
		*pYaw /= GetFrictionScale(tRecord1.m_vVelocity.Length2D(), *pYaw, tRecord1.m_vVelocity.z + GetGravity() * TICK_INTERVAL, 0.f, 56.f);
	if (fabsf(*pYaw) > 45.f)
		return false;

	static int iChanges, iStart;

	static int iStaticSign = 0;
	const int iLastSign = iStaticSign;
	const int iCurrSign = iStaticSign = *pYaw ? (*pYaw > 0 ? 1 : -1) : iStaticSign;

	static bool bStaticZero = false;
	const bool iLastZero = bStaticZero;
	const bool iCurrZero = bStaticZero = !*pYaw;

	const bool bChanged = iCurrSign != iLastSign || iCurrZero && iLastZero;
	const bool bStraight = fabsf(*pYaw) * tRecord1.m_vVelocity.Length2D() * iTicks < flStraightFuzzyValue;

	if (bStart)
	{
		iChanges = 0, iStart = TIME_TO_TICKS(flTime1);
		if (bStraight && ++iChanges > iMaxChanges)
			return false;
		return true;
	}
	else
	{
		if ((bChanged || bStraight) && ++iChanges > iMaxChanges)
			return false;
		return iChanges && iStart - TIME_TO_TICKS(flTime2) > iMaxChangeTime ? false : true;
	}
}

void CMovementSimulation::GetAverageYaw(MoveStorage& tStorage, int iSamples)
{
	auto pPlayer = tStorage.m_pPlayer;
	const int nEntIndex = pPlayer->entindex();
	if (nEntIndex < 1 || nEntIndex >= 64)
		return;
	
	auto& vRecords = m_mRecords[nEntIndex];
	if (vRecords.size() < 3)
		return;

	const float flMaxSpeed = pPlayer->TeamFortress_CalculateMaxSpeed();
	iSamples = std::min(iSamples, static_cast<int>(vRecords.size()));
	
	float flTotalYaw = 0.0f;
	int nTotalTicks = 0;
	int nValidSamples = 0;
	
	for (size_t i = 1; i < static_cast<size_t>(iSamples) && i < vRecords.size(); i++)
	{
		auto& rec1 = vRecords[i - 1];
		auto& rec2 = vRecords[i];
		
		// Skip if movement mode changed
		if (rec1.m_iMode != rec2.m_iMode)
			continue;
		
		// Skip if velocity is too low
		if (rec1.m_vVelocity.Length2D() < 30.0f)
			continue;
		
		// Calculate yaw change
		const float flYaw1 = Math::VelocityToAngles(rec1.m_vDirection).y;
		const float flYaw2 = Math::VelocityToAngles(rec2.m_vDirection).y;
		float flYawDelta = Math::NormalizeAngle(flYaw1 - flYaw2);
		
		// Scale by speed ratio for ground movement
		if (flMaxSpeed > 0.0f && rec1.m_iMode != 1)
		{
			const float flSpeedRatio = std::clamp(rec1.m_vVelocity.Length2D() / flMaxSpeed, 0.1f, 1.0f);
			flYawDelta *= flSpeedRatio;
		}
		
		// Air strafe friction adjustment
		if (rec1.m_iMode == 1)
		{
			const float flFriction = GetFrictionScale(rec1.m_vVelocity.Length2D(), flYawDelta, 
				rec1.m_vVelocity.z + GetGravity() * TICK_INTERVAL, 0.f, 56.f);
			if (flFriction > 0.01f)
				flYawDelta /= flFriction;
		}
		
		// Skip extreme yaw changes (teleport/lag)
		if (fabsf(flYawDelta) > 45.0f)
			continue;
		
		const int nTicks = std::max(TIME_TO_TICKS(rec1.m_flSimTime - rec2.m_flSimTime), 1);
		flTotalYaw += flYawDelta;
		nTotalTicks += nTicks;
		nValidSamples++;
	}
	
	// Need minimum samples
	if (nTotalTicks < 3 || nValidSamples < 2)
		return;
	
	// Calculate average yaw per tick
	const float flAverageYaw = flTotalYaw / static_cast<float>(nTotalTicks);
	
	// =========================================================================
	// CIRCLE STRAFE vs COUNTER-STRAFE DETECTION
	// 
	// Circle strafe: consistent turning in ONE direction
	//   - avgYaw should be 4+ degrees per tick (tight circle = 6-10)
	//   - All yaw changes should be same sign (all positive or all negative)
	// 
	// Counter-strafe: alternating left-right
	//   - avgYaw is low (0-3) because left and right cancel out
	//   - Yaw changes alternate between positive and negative
	// 
	// Threshold: 4.0 degrees per tick minimum for circle strafe
	// This prevents counter-strafing from being detected as circle strafe
	// =========================================================================
	
	if (fabsf(flAverageYaw) >= 4.0f)
	{
		// Strong consistent turning - definitely circle strafing
		tStorage.m_flAverageYaw = flAverageYaw;
	}
	else if (fabsf(flAverageYaw) >= 2.0f)
	{
		// Medium turning - could be slow circle strafe or biased counter-strafe
		// Only use if we have enough samples to be confident
		if (nValidSamples >= 8)
		{
			tStorage.m_flAverageYaw = flAverageYaw;
		}
	}
	// Low average yaw - likely counter-strafing or straight movement, don't set avgYaw
}

bool CMovementSimulation::StrafePrediction(MoveStorage& tStorage, int iSamples)
{
	if (tStorage.m_bDirectMove)
	{
		if (!CFG::Aimbot_Projectile_Ground_Strafe_Prediction)
			return false;
	}
	else
	{
		if (!CFG::Aimbot_Projectile_Air_Strafe_Prediction)
			return false;
	}

	// Try circle strafe detection first
	GetAverageYaw(tStorage, iSamples);
	
	// If we got a valid average yaw, it's circle strafing
	if (tStorage.m_flAverageYaw != 0.0f)
		return true;
	
	// =========================================================================
	// COUNTER-STRAFE DETECTION (A-D spam)
	// =========================================================================
	if (tStorage.m_bDirectMove)
	{
		auto pPlayer = tStorage.m_pPlayer;
		const int nEntIndex = pPlayer->entindex();
		if (nEntIndex < 1 || nEntIndex >= 64)
			return true;
		
		auto& vRecords = m_mRecords[nEntIndex];
		if (vRecords.size() < 4)
			return true;
		
		// Get player's forward direction (from view angles or velocity)
		Vec3 vForward;
		Math::AngleVectors(tStorage.m_MoveData.m_vecViewAngles, &vForward, nullptr, nullptr);
		vForward.z = 0;
		if (vForward.Length2D() < 0.1f)
		{
			// Fallback: use velocity direction as forward
			vForward = tStorage.m_MoveData.m_vecVelocity;
			vForward.z = 0;
		}
		vForward.NormalizeInPlace();
		
		// Right vector (perpendicular to forward)
		Vec3 vRight = Vec3(-vForward.y, vForward.x, 0);
		
		// Track lateral velocity sign changes
		// Lateral = dot(velocity, right) - positive = going right, negative = going left
		int nSignChanges = 0;
		int nLastSign = 0;
		float flTotalTime = 0.0f;
		float flLastChangeTime = 0.0f;
		float flAvgTimeBetweenChanges = 0.0f;
		int nChangeCount = 0;
		
		const float flCurTime = vRecords[0].m_flSimTime;
		
		for (size_t i = 0; i < std::min(vRecords.size(), size_t(20)); i++)
		{
			auto& rec = vRecords[i];
			
			// Only check ground movement
			if (rec.m_iMode != 0)
				continue;
			
			// Skip if too old (only look at last 1 second)
			if (flCurTime - rec.m_flSimTime > 1.0f)
				break;
			
			// Get lateral velocity component
			Vec3 vVel = rec.m_vVelocity;
			vVel.z = 0;
			
			// Need some speed to detect strafing
			if (vVel.Length2D() < 50.0f)
				continue;
			
			const float flLateral = vVel.Dot(vRight);
			
			// Need significant lateral movement (not just walking forward)
			if (fabsf(flLateral) < 30.0f)
				continue;
			
			const int nSign = (flLateral > 0) ? 1 : -1;
			
			// Count sign changes (direction reversals)
			if (nLastSign != 0 && nSign != nLastSign)
			{
				nSignChanges++;
				
				// Track time between changes
				if (flLastChangeTime > 0.0f)
				{
					const float flTimeDelta = flLastChangeTime - rec.m_flSimTime;
					if (flTimeDelta > 0.02f && flTimeDelta < 0.6f)
					{
						flAvgTimeBetweenChanges += flTimeDelta;
						nChangeCount++;
					}
				}
				flLastChangeTime = rec.m_flSimTime;
			}
			
			nLastSign = nSign;
			flTotalTime = flCurTime - rec.m_flSimTime;
		}
		
		// Counter-strafe detected if:
		// - At least 3 direction reversals (L-R-L-R pattern) - more strict
		// - Within reasonable time window
		// - Changes happen at reasonable intervals (not too fast, not too slow)
		const bool bHasPattern = nSignChanges >= 3;
		const bool bReasonableTime = flTotalTime > 0.15f && flTotalTime < 0.8f;
		const bool bReasonableRate = nChangeCount >= 2 && (flAvgTimeBetweenChanges / nChangeCount) < 0.4f;
		
		if (bHasPattern && bReasonableTime && bReasonableRate)
		{
			tStorage.m_bCounterStrafe = true;
			
			// Get current strafe direction from most recent lateral velocity
			Vec3 vVel = pPlayer->m_vecVelocity();
			vVel.z = 0;
			const float flLateral = vVel.Dot(vRight);
			tStorage.m_nCSDirection = (flLateral > 0) ? 1 : -1;
			
			// Calculate switch time from observed pattern
			if (nChangeCount > 0)
				tStorage.m_flCSSwitchTime = flAvgTimeBetweenChanges / nChangeCount;
			else
				tStorage.m_flCSSwitchTime = 0.15f;
			
			// Clamp to reasonable values
			tStorage.m_flCSSwitchTime = std::clamp(tStorage.m_flCSSwitchTime, 0.06f, 0.45f);
			
			// Estimate how long they've been in current direction
			// Use time since last sign change
			if (flLastChangeTime > 0.0f)
				tStorage.m_flCSTimeInDir = flCurTime - flLastChangeTime;
			else
				tStorage.m_flCSTimeInDir = 0.0f;
			
			// Use learned behavior if available for better timing
			auto* pBehavior = GetPlayerBehavior(nEntIndex);
			if (pBehavior && pBehavior->m_Strafe.m_nLeftToRightSamples > 5)
			{
				// Use their learned directional timing
				if (tStorage.m_nCSDirection == -1)  // Going left, will switch to right
					tStorage.m_flCSSwitchTime = pBehavior->m_Strafe.m_flAvgTimeLeftToRight;
				else  // Going right, will switch to left
					tStorage.m_flCSSwitchTime = pBehavior->m_Strafe.m_flAvgTimeRightToLeft;
				
				tStorage.m_flCSSwitchTime = std::clamp(tStorage.m_flCSSwitchTime, 0.06f, 0.45f);
			}
			
			return true;
		}
		
		// =====================================================================
		// PREDICTIVE COUNTER-STRAFE
		// Even if not currently detected, check if this player has a history
		// of counter-strafing and just changed direction
		// =====================================================================
		if (ShouldPredictCounterStrafe(nEntIndex))
		{
			tStorage.m_bCounterStrafe = true;
			
			auto* pBehavior = GetPlayerBehavior(nEntIndex);
			if (pBehavior)
			{
				// Use their current direction
				tStorage.m_nCSDirection = pBehavior->m_Strafe.m_nLastStrafeSign;
				if (tStorage.m_nCSDirection == 0)
				{
					// Fallback: use current lateral velocity
					Vec3 vVel = pPlayer->m_vecVelocity();
					vVel.z = 0;
					const float flLateral = vVel.Dot(vRight);
					tStorage.m_nCSDirection = (flLateral > 0) ? 1 : -1;
				}
				
				// Use learned timing
				tStorage.m_flCSSwitchTime = pBehavior->m_Strafe.GetPredictedTimeToReversal(tStorage.m_nCSDirection);
				tStorage.m_flCSSwitchTime = std::clamp(tStorage.m_flCSSwitchTime, 0.06f, 0.45f);
				
				// Estimate time in current direction
				const float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
				tStorage.m_flCSTimeInDir = flCurTime - pBehavior->m_Strafe.m_flLastDirectionChangeTime;
				tStorage.m_flCSTimeInDir = std::max(tStorage.m_flCSTimeInDir, 0.0f);
			}
			else
			{
				// No behavior data, use defaults
				Vec3 vVel = pPlayer->m_vecVelocity();
				vVel.z = 0;
				const float flLateral = vVel.Dot(vRight);
				tStorage.m_nCSDirection = (flLateral > 0) ? 1 : -1;
				tStorage.m_flCSSwitchTime = 0.15f;
				tStorage.m_flCSTimeInDir = 0.0f;
			}
			
			return true;
		}
	}
	
	return true;
}

void CMovementSimulation::RunTick(MoveStorage& tStorage, bool bPath)
{
	if (tStorage.m_bFailed || !tStorage.m_pPlayer)
		return;

	if (bPath)
		tStorage.m_vPath.push_back(tStorage.m_MoveData.m_vecAbsOrigin);

	// Make sure frametime and prediction vars are right
	I::Prediction->m_bInPrediction = true;
	I::Prediction->m_bFirstTimePredicted = false;
	I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;

	// Adjust bounds for non-local players to fix origin compression issues
	const bool bIsLocalPlayer = (tStorage.m_pPlayer == H::Entities->GetLocal());
	if (!bIsLocalPlayer)
		SetBounds(tStorage.m_pPlayer);

	float flCorrection = 0.f;
	
	// =========================================================================
	// COUNTER-STRAFE PREDICTION - Oscillate left/right
	// This simulates A-D spam by alternating side movement
	// =========================================================================
	if (tStorage.m_bCounterStrafe && tStorage.m_bDirectMove)
	{
		// Track time in current direction
		tStorage.m_flCSTimeInDir += TICK_INTERVAL;
		
		// Switch direction when time is up
		if (tStorage.m_flCSTimeInDir >= tStorage.m_flCSSwitchTime)
		{
			tStorage.m_nCSDirection = -tStorage.m_nCSDirection;
			tStorage.m_flCSTimeInDir = 0.0f;
			
			// Update switch time for next direction (may be asymmetric)
			if (tStorage.m_pCachedBehavior)
			{
				tStorage.m_flCSSwitchTime = tStorage.m_pCachedBehavior->m_Strafe.GetPredictedTimeToReversal(tStorage.m_nCSDirection);
				tStorage.m_flCSSwitchTime = std::clamp(tStorage.m_flCSSwitchTime, 0.06f, 0.45f);
			}
		}
		
		// Apply side movement in current direction
		// The key insight: counter-strafing is PURE lateral movement
		// They're not really going forward, they're just going left-right
		const float flStrafeSpeed = tStorage.m_MoveData.m_flMaxSpeed * 0.85f;
		tStorage.m_MoveData.m_flSideMove = flStrafeSpeed * static_cast<float>(tStorage.m_nCSDirection);
		
		// Reduce forward movement during counter-strafe (they're focused on dodging)
		// Most players slow down their forward movement when A-D spamming
		tStorage.m_MoveData.m_flForwardMove *= 0.3f;
	}
	// =========================================================================
	// CIRCLE STRAFE PREDICTION - Just apply the yaw change
	// The movement simulation will handle the rest correctly
	// =========================================================================
	else if (tStorage.m_flAverageYaw)
	{
		float flMult = 1.f;
		
		// Air strafe correction
		if (!tStorage.m_bDirectMove && !tStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			flCorrection = 90.f * (tStorage.m_flAverageYaw > 0 ? 1.f : -1.f);
			flMult = GetFrictionScale(tStorage.m_MoveData.m_vecVelocity.Length2D(), tStorage.m_flAverageYaw, 
				tStorage.m_MoveData.m_vecVelocity.z + GetGravity() * TICK_INTERVAL);
		}
		
		// Just apply the yaw change - that's all circle strafing is
		tStorage.m_MoveData.m_vecViewAngles.y += tStorage.m_flAverageYaw * flMult + flCorrection;
	}
	else if (!tStorage.m_bDirectMove)
		tStorage.m_MoveData.m_flForwardMove = tStorage.m_MoveData.m_flSideMove = 0.f;

	float flOldSpeed = tStorage.m_MoveData.m_flClientMaxSpeed;
	bool bSwimmingTick = tStorage.m_pPlayer->m_nWaterLevel() >= 2;
	if (tStorage.m_pPlayer->m_bDucked() && (tStorage.m_pPlayer->m_fFlags() & FL_ONGROUND) && !bSwimmingTick)
		tStorage.m_MoveData.m_flClientMaxSpeed /= 3.f;

	I::GameMovement->ProcessMovement(tStorage.m_pPlayer, &tStorage.m_MoveData);

	tStorage.m_MoveData.m_flClientMaxSpeed = flOldSpeed;

	tStorage.m_flSimTime += TICK_INTERVAL;
	tStorage.m_bPredictNetworked = tStorage.m_flSimTime >= tStorage.m_flPredictedSimTime;
	if (tStorage.m_bPredictNetworked)
	{
		tStorage.m_vPredictedOrigin = tStorage.m_MoveData.m_vecAbsOrigin;
		tStorage.m_flPredictedSimTime += tStorage.m_flPredictedDelta;
	}
	
	bool bLastDirectMove = tStorage.m_bDirectMove;
	bool bSwimmingAfter = tStorage.m_pPlayer->m_nWaterLevel() >= 2;
	tStorage.m_bDirectMove = (tStorage.m_pPlayer->m_fFlags() & FL_ONGROUND) || bSwimmingAfter;

	if (tStorage.m_flAverageYaw && !tStorage.m_bCounterStrafe)
		tStorage.m_MoveData.m_vecViewAngles.y -= flCorrection;
	else if (tStorage.m_bDirectMove && !bLastDirectMove
		&& !tStorage.m_MoveData.m_flForwardMove && !tStorage.m_MoveData.m_flSideMove
		&& tStorage.m_MoveData.m_vecVelocity.Length2D() > tStorage.m_MoveData.m_flMaxSpeed * 0.015f)
	{
		Vec3 vDirection = tStorage.m_MoveData.m_vecVelocity;
		vDirection.z = 0;
		vDirection.NormalizeInPlace();
		vDirection = vDirection * 450.f;
		
		float flForwardMove = vDirection.x;
		float flSideMove = -vDirection.y;
		Math::FixMovement(flForwardMove, flSideMove, Vec3(0, 0, 0), tStorage.m_MoveData.m_vecViewAngles);
		
		tStorage.m_MoveData.m_flForwardMove = flForwardMove;
		tStorage.m_MoveData.m_flSideMove = flSideMove;
	}

	// Restore bounds after movement
	if (!bIsLocalPlayer)
		RestoreBounds(tStorage.m_pPlayer);
}

void CMovementSimulation::Restore(MoveStorage& tStorage)
{
	if (tStorage.m_bInitFailed || !tStorage.m_pPlayer)
		return;

	I::MoveHelper->SetHost(nullptr);
	tStorage.m_pPlayer->SetCurrentCommand(nullptr);

	Reset(tStorage);

	I::Prediction->m_bInPrediction = m_bOldInPrediction;
	I::Prediction->m_bFirstTimePredicted = m_bOldFirstTimePredicted;
	I::GlobalVars->frametime = m_flOldFrametime;
}

float CMovementSimulation::GetPredictedDelta(C_TFPlayer* pPlayer)
{
	const int nIdx = pPlayer->entindex();
	if (nIdx < 1 || nIdx >= 64)
		return TICK_INTERVAL;
	
	auto it = m_mSimTimes.find(nIdx);
	if (it == m_mSimTimes.end() || it->second.empty())
		return TICK_INTERVAL;
	
	const auto& vSimTimes = it->second;
	float flSum = 0.0f;
	for (const float flTime : vSimTimes)
		flSum += flTime;
	
	return flSum / static_cast<float>(vSimTimes.size());
}

// ============================================================================
// PLAYER BEHAVIOR LEARNING
// ============================================================================

PlayerBehavior* CMovementSimulation::GetPlayerBehavior(int nEntIndex)
{
	if (nEntIndex < 0 || nEntIndex >= 64)
		return nullptr;
	
	auto it = m_mPlayerBehaviors.find(nEntIndex);
	if (it == m_mPlayerBehaviors.end())
		return nullptr;
	
	return &it->second;
}

// Get or create behavior for a player (use when you need to write)
PlayerBehavior& CMovementSimulation::GetOrCreateBehavior(int nEntIndex)
{
	return m_mPlayerBehaviors[nEntIndex];
}

// ============================================================================
// ON SHOT FIRED - Call when local player fires at a target
// Updates the target's "last shot at" time for contextual counter-strafe tracking
// ============================================================================
void CMovementSimulation::OnShotFired(int nTargetEntIndex)
{
	if (nTargetEntIndex < 1 || nTargetEntIndex >= 64)
		return;
	
	auto it = m_mPlayerBehaviors.find(nTargetEntIndex);
	if (it == m_mPlayerBehaviors.end())
		return;
	
	const float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	it->second.m_Strafe.m_flLastShotAtTime = flCurTime;
	it->second.m_Combat.m_flLastShotAtTime = flCurTime;
	
	// Also store position when shot at for reaction tracking
	auto pTarget = I::ClientEntityList->GetClientEntity(nTargetEntIndex);
	if (pTarget)
	{
		auto pPlayer = pTarget->As<C_TFPlayer>();
		if (pPlayer && !pPlayer->deadflag())
			it->second.m_Combat.m_vPosWhenShotAt = pPlayer->m_vecOrigin();
	}
}

// ============================================================================
// COUNTER-STRAFE PATTERN DETECTION (A-D Spam)
// 
// Simple approach:
//   1. Track when player changes strafe direction (yaw change sign flips)
//   2. Detect pattern: LEFT → RIGHT → LEFT (or R→L→R) within 0.45s per change
//   3. Count how often this player counter-strafes
//   4. Remember this for future encounters
//
// The key insight: A-D spam causes the velocity YAW to oscillate back and forth
// We just need to detect when the yaw change direction reverses quickly
// 
// NOTE: Some players strafe with ~0.35s timing, so we use 0.45s max to catch them
// ============================================================================
void CMovementSimulation::LearnCounterStrafe(int nEntIndex, PlayerBehavior& behavior)
{
	auto it = m_mRecords.find(nEntIndex);
	if (it == m_mRecords.end() || it->second.size() < 3)
		return;
	
	const auto& vRecords = it->second;
	const float flCurTime = vRecords[0].m_flSimTime;
	
	// Only track ground movement (A-D spam is a ground thing)
	if (vRecords[0].m_iMode != 0)
	{
		behavior.m_Strafe.m_bIsCurrentlyCounterStrafing = false;
		behavior.m_Strafe.m_nLastStrafeSign = 0;
		return;
	}
	
	// Need some speed to detect strafing
	const float flSpeed = vRecords[0].m_vVelocity.Length2D();
	if (flSpeed < 40.0f)
	{
		behavior.m_Strafe.m_bIsCurrentlyCounterStrafing = false;
		behavior.m_Strafe.m_nLastStrafeSign = 0;
		return;
	}
	
	// =========================================================================
	// STEP 1: Build a list of direction changes from the records
	// Direction = which way the velocity yaw is changing (left or right turn)
	// =========================================================================
	struct DirChange
	{
		int nDir;        // -1 = turning left, +1 = turning right
		float flTime;    // When this sample was taken
	};
	
	DirChange changes[16];
	int nChangeCount = 0;
	
	const float flMaxAge = 1.0f;  // Look at last 1.0 seconds (increased from 0.6s for slower strafes)
	
	for (size_t i = 0; i + 1 < vRecords.size() && i < 20; i++)
	{
		const auto& rec0 = vRecords[i];
		const auto& rec1 = vRecords[i + 1];
		
		// Skip if too old
		if (flCurTime - rec0.m_flSimTime > flMaxAge)
			break;
		
		// Skip non-ground samples
		if (rec0.m_iMode != 0 || rec1.m_iMode != 0)
			continue;
		
		// Skip if either velocity is too slow
		if (rec0.m_vVelocity.Length2D() < 30.0f || rec1.m_vVelocity.Length2D() < 30.0f)
			continue;
		
		// Calculate yaw change between these two samples
		const float flYaw0 = Math::VelocityToAngles(rec0.m_vVelocity).y;
		const float flYaw1 = Math::VelocityToAngles(rec1.m_vVelocity).y;
		const float flYawDelta = Math::NormalizeAngle(flYaw0 - flYaw1);
		
		// Need significant yaw change to count as a direction
		// (ignore tiny changes from network jitter)
		if (fabsf(flYawDelta) < 1.5f)
			continue;
		
		// Determine direction: positive yaw delta = turning right, negative = turning left
		const int nDir = (flYawDelta > 0) ? 1 : -1;
		
		if (nChangeCount < 16)
		{
			changes[nChangeCount].nDir = nDir;
			changes[nChangeCount].flTime = rec0.m_flSimTime;
			nChangeCount++;
		}
	}
	
	// =========================================================================
	// TRACK CURRENT DIRECTION AND WHEN IT CHANGED
	// This is used by ShouldPredictCounterStrafe to know if they just changed
	// =========================================================================
	if (nChangeCount > 0)
	{
		const int nCurrentDir = changes[0].nDir;
		
		// Did direction change from last time?
		if (behavior.m_Strafe.m_nLastStrafeSign != 0 && 
		    behavior.m_Strafe.m_nLastStrafeSign != nCurrentDir)
		{
			// Direction just changed! Record when
			behavior.m_Strafe.m_flLastDirectionChangeTime = flCurTime;
		}
		
		behavior.m_Strafe.m_nLastStrafeSign = nCurrentDir;
	}
	
	// =========================================================================
	// STEP 2: Look for the counter-strafe pattern in the direction changes
	// Pattern: DIR_A → DIR_B → DIR_A where DIR_A != DIR_B
	// Each transition must happen within 0.45 seconds (increased from 0.25s)
	// This catches players who strafe with ~0.35s timing
	// =========================================================================
	bool bCounterStrafeDetected = false;
	int nPatternCount = 0;  // How many L-R-L or R-L-R patterns found
	
	// Use learned timing if available, otherwise default to 0.45s
	// This adapts to each player's strafe speed
	float flMaxTransitionTime = 0.45f;
	if (behavior.m_Strafe.m_flAvgReversalTime > 0.05f && behavior.m_Strafe.m_nLeftToRightSamples > 3)
	{
		// Use 1.5x their average reversal time as the max (gives some slack)
		flMaxTransitionTime = std::max(behavior.m_Strafe.m_flAvgReversalTime * 1.5f, 0.45f);
	}
	
	for (int i = 0; i + 2 < nChangeCount; i++)
	{
		const int d0 = changes[i].nDir;
		const int d1 = changes[i + 1].nDir;
		const int d2 = changes[i + 2].nDir;
		
		// Check for reversal pattern: d0 != d1 && d1 != d2 && d0 == d2
		// This means: LEFT-RIGHT-LEFT or RIGHT-LEFT-RIGHT
		if (d0 != d1 && d1 != d2 && d0 == d2)
		{
			// Check timing - each transition should be within max time
			const float flTime01 = changes[i].flTime - changes[i + 1].flTime;
			const float flTime12 = changes[i + 1].flTime - changes[i + 2].flTime;
			
			if (flTime01 > 0.02f && flTime01 < flMaxTransitionTime &&
			    flTime12 > 0.02f && flTime12 < flMaxTransitionTime)
			{
				bCounterStrafeDetected = true;
				nPatternCount++;
				
				// Track timing for this direction change
				behavior.m_Strafe.m_flAvgReversalTime = 
					behavior.m_Strafe.m_flAvgReversalTime * 0.8f + ((flTime01 + flTime12) * 0.5f) * 0.2f;
				
				// Track directional timing (left→right vs right→left)
				if (d0 == -1)  // Started left, went right, back to left
				{
					behavior.m_Strafe.m_flAvgTimeLeftToRight = 
						behavior.m_Strafe.m_flAvgTimeLeftToRight * 0.8f + flTime01 * 0.2f;
					behavior.m_Strafe.m_flAvgTimeRightToLeft = 
						behavior.m_Strafe.m_flAvgTimeRightToLeft * 0.8f + flTime12 * 0.2f;
					behavior.m_Strafe.m_nLeftToRightSamples++;
					behavior.m_Strafe.m_nRightToLeftSamples++;
				}
				else  // Started right, went left, back to right
				{
					behavior.m_Strafe.m_flAvgTimeRightToLeft = 
						behavior.m_Strafe.m_flAvgTimeRightToLeft * 0.8f + flTime01 * 0.2f;
					behavior.m_Strafe.m_flAvgTimeLeftToRight = 
						behavior.m_Strafe.m_flAvgTimeLeftToRight * 0.8f + flTime12 * 0.2f;
					behavior.m_Strafe.m_nRightToLeftSamples++;
					behavior.m_Strafe.m_nLeftToRightSamples++;
				}
				
				// Skip ahead to avoid double-counting overlapping patterns
				i += 2;
			}
		}
	}
	
	// =========================================================================
	// STEP 3: Update counter-strafe state and statistics
	// =========================================================================
	if (bCounterStrafeDetected)
	{
		// Mark as currently counter-strafing
		if (!behavior.m_Strafe.m_bIsCurrentlyCounterStrafing)
		{
			behavior.m_Strafe.m_flCSStartTime = flCurTime;
			behavior.m_Strafe.m_bCSContextRecorded = false;
		}
		
		behavior.m_Strafe.m_bIsCurrentlyCounterStrafing = true;
		behavior.m_Strafe.m_nConsecutiveReversals = nPatternCount;
		behavior.m_Strafe.m_nCurrentReversalStreak = nPatternCount;
		
		// Increment detection count (this is what we remember for future)
		behavior.m_Strafe.m_nCounterStrafeDetections++;
	}
	else
	{
		// Not counter-strafing right now
		if (behavior.m_Strafe.m_bIsCurrentlyCounterStrafing)
		{
			// Was counter-strafing, now stopped - count as a "normal strafe" sample
			behavior.m_Strafe.m_nNormalStrafeDetections++;
		}
		
		behavior.m_Strafe.m_bIsCurrentlyCounterStrafing = false;
		behavior.m_Strafe.m_nConsecutiveReversals = 0;
	}
	
	// =========================================================================
	// STEP 4: Update the overall counter-strafe rate for this player
	// This is what we use to predict future behavior
	// =========================================================================
	const int nTotalSamples = behavior.m_Strafe.m_nCounterStrafeDetections + behavior.m_Strafe.m_nNormalStrafeDetections;
	if (nTotalSamples > 5)
	{
		const float flNewRate = static_cast<float>(behavior.m_Strafe.m_nCounterStrafeDetections) / 
		                        static_cast<float>(nTotalSamples);
		// Smooth update (EMA)
		behavior.m_Strafe.m_flCounterStrafeRate = behavior.m_Strafe.m_flCounterStrafeRate * 0.85f + flNewRate * 0.15f;
	}
	
	// =========================================================================
	// Also track strafe intensity (for circle strafe detection - separate system)
	// =========================================================================
	if (vRecords.size() >= 2)
	{
		const float flYaw0 = Math::VelocityToAngles(vRecords[0].m_vVelocity).y;
		const float flYaw1 = Math::VelocityToAngles(vRecords[1].m_vVelocity).y;
		const float flYawDelta = Math::NormalizeAngle(flYaw0 - flYaw1);
		behavior.m_Strafe.m_flStrafeIntensity = behavior.m_Strafe.m_flStrafeIntensity * 0.95f + fabsf(flYawDelta) * 0.05f;
		
		// Store yaw changes for circle strafe detection
		if (fabsf(flYawDelta) > 2.0f)
		{
			auto& vYawChanges = behavior.m_Strafe.m_vRecentYawChanges;
			auto& vYawTimes = behavior.m_Strafe.m_vRecentYawTimes;
			
			vYawChanges.push_front(flYawDelta);
			vYawTimes.push_front(flCurTime);
			
			if (vYawChanges.size() > 30)
			{
				vYawChanges.pop_back();
				vYawTimes.pop_back();
			}
		}
		
		// =====================================================================
		// CIRCLE STRAFE LEARNING - Track timing per quadrant
		// Quadrant 0=Forward, 1=Right, 2=Back, 3=Left (relative to their movement)
		// =====================================================================
		
		// Determine current quadrant from velocity direction
		// Use the yaw angle to determine which quadrant they're in
		const float flYaw = Math::NormalizeAngle(flYaw0);
		int nQuadrant = 0;
		
		// Convert yaw to quadrant (0-3)
		// -45 to 45 = Forward (0)
		// 45 to 135 = Left (3) - yaw increases counter-clockwise
		// 135 to -135 = Back (2)
		// -135 to -45 = Right (1)
		if (flYaw >= -45.0f && flYaw < 45.0f)
			nQuadrant = 0;  // Forward
		else if (flYaw >= 45.0f && flYaw < 135.0f)
			nQuadrant = 3;  // Left
		else if (flYaw >= -135.0f && flYaw < -45.0f)
			nQuadrant = 1;  // Right
		else
			nQuadrant = 2;  // Back
		
		// Check if quadrant changed
		if (behavior.m_Strafe.m_nLastQuadrant != -1 && 
		    behavior.m_Strafe.m_nLastQuadrant != nQuadrant)
		{
			// Quadrant changed! Record how long they spent in the previous quadrant
			const float flTimeInLastQuadrant = flCurTime - behavior.m_Strafe.m_flLastQuadrantChangeTime;
			
			// Only record if timing is reasonable (0.05s to 0.5s)
			if (flTimeInLastQuadrant > 0.05f && flTimeInLastQuadrant < 0.5f)
			{
				const int nLastQ = behavior.m_Strafe.m_nLastQuadrant;
				
				// Update EMA for this quadrant's timing
				// Use higher weight for more samples (stabilizes over time)
				const float flWeight = behavior.m_Strafe.m_nQuadrantSamples[nLastQ] > 10 ? 0.1f : 0.2f;
				behavior.m_Strafe.m_flQuadrantTime[nLastQ] = 
					behavior.m_Strafe.m_flQuadrantTime[nLastQ] * (1.0f - flWeight) + 
					flTimeInLastQuadrant * flWeight;
				
				// Calculate and store the yaw rate for this quadrant
				// Yaw rate = total yaw change during this quadrant / time in quadrant
				// This gives us degrees per second, which we convert to degrees per tick
				if (behavior.m_Strafe.m_vRecentYawChanges.size() >= 2)
				{
					// Sum up yaw changes that occurred during this quadrant
					float flTotalYawChange = 0.0f;
					int nYawSamples = 0;
					for (size_t j = 0; j < behavior.m_Strafe.m_vRecentYawChanges.size() && j < 10; j++)
					{
						// Only count samples from this quadrant (within the time window)
						if (behavior.m_Strafe.m_vRecentYawTimes.size() > j)
						{
							const float flSampleTime = behavior.m_Strafe.m_vRecentYawTimes[j];
							if (flCurTime - flSampleTime <= flTimeInLastQuadrant + 0.05f)
							{
								flTotalYawChange += behavior.m_Strafe.m_vRecentYawChanges[j];
								nYawSamples++;
							}
						}
					}
					
					if (nYawSamples > 0)
					{
						// Average yaw change per sample (which is roughly per tick)
						const float flYawPerTick = flTotalYawChange / static_cast<float>(nYawSamples);
						
						// Update the quadrant yaw rate with EMA
						behavior.m_Strafe.m_flQuadrantYawRate[nLastQ] = 
							behavior.m_Strafe.m_flQuadrantYawRate[nLastQ] * (1.0f - flWeight) + 
							flYawPerTick * flWeight;
					}
				}
				
				behavior.m_Strafe.m_nQuadrantSamples[nLastQ]++;
			}
			
			behavior.m_Strafe.m_flLastQuadrantChangeTime = flCurTime;
			
			// Determine circle strafe direction
			// Clockwise: 0→1→2→3→0 (Forward→Right→Back→Left→Forward)
			// Counter-clockwise: 0→3→2→1→0
			const int nExpectedCW = (behavior.m_Strafe.m_nLastQuadrant + 1) % 4;
			const int nExpectedCCW = (behavior.m_Strafe.m_nLastQuadrant + 3) % 4;
			
			if (nQuadrant == nExpectedCW)
				behavior.m_Strafe.m_nCircleStrafeDirection = 1;  // Clockwise
			else if (nQuadrant == nExpectedCCW)
				behavior.m_Strafe.m_nCircleStrafeDirection = -1; // Counter-clockwise
		}
		
		behavior.m_Strafe.m_nLastQuadrant = nQuadrant;
		
		// Track if they're circle strafing (consistent yaw changes in same direction)
		if (behavior.m_Strafe.m_vRecentYawChanges.size() >= 5)
		{
			int nPositive = 0, nNegative = 0;
			for (size_t i = 0; i < 5; i++)
			{
				if (behavior.m_Strafe.m_vRecentYawChanges[i] > 1.0f)
					nPositive++;
				else if (behavior.m_Strafe.m_vRecentYawChanges[i] < -1.0f)
					nNegative++;
			}
			
			// Circle strafing = consistent direction (4+ out of 5 same sign)
			behavior.m_Strafe.m_bIsCircleStrafing = (nPositive >= 4 || nNegative >= 4);
			
			if (behavior.m_Strafe.m_bIsCircleStrafing)
			{
				behavior.m_Strafe.m_nCircleStrafeDirection = (nPositive > nNegative) ? 1 : -1;
				behavior.m_Strafe.m_nCircleStrafeSamples++;
				
				// Track average yaw per tick
				float flAvgYaw = 0.0f;
				for (size_t i = 0; i < 5; i++)
					flAvgYaw += behavior.m_Strafe.m_vRecentYawChanges[i];
				flAvgYaw /= 5.0f;
				
				behavior.m_Strafe.m_flCircleStrafeYawPerTick = 
					behavior.m_Strafe.m_flCircleStrafeYawPerTick * 0.9f + flAvgYaw * 0.1f;
				
				// Track variance in yaw rate (for consistency check)
				// Low variance = consistent circle strafing = more predictable
				float flVariance = 0.0f;
				for (size_t i = 0; i < 5; i++)
				{
					const float flDiff = behavior.m_Strafe.m_vRecentYawChanges[i] - flAvgYaw;
					flVariance += flDiff * flDiff;
				}
				flVariance /= 5.0f;
				
				// Update variance EMA
				behavior.m_Strafe.m_flYawRateVariance = 
					behavior.m_Strafe.m_flYawRateVariance * 0.9f + flVariance * 0.1f;
				
				// Store recent yaw rates for variance calculation
				behavior.m_Strafe.m_vRecentYawRates.push_front(flAvgYaw);
				if (behavior.m_Strafe.m_vRecentYawRates.size() > 10)
					behavior.m_Strafe.m_vRecentYawRates.pop_back();
			}
		}
	}
}

// ============================================================================
// CONTEXTUAL COUNTER-STRAFE LEARNING
// Tracks WHEN and WHY players counter-strafe:
// - When shot at (reactive)
// - When they see an enemy (proactive)
// - When teammate is fighting nearby (awareness)
// - When low health (panic)
// - When being healed (confident)
// - At different ranges (close/mid/long)
// - Per class (Heavy doesn't bother, Scout always does)
// ============================================================================
void CMovementSimulation::LearnCounterStrafeContext(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	const float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	auto& ctx = behavior.m_Strafe.m_Context;
	
	// Get player state
	const float flHealth = static_cast<float>(pPlayer->m_iHealth());
	const float flMaxHealth = static_cast<float>(pPlayer->GetMaxHealth());
	const float flHealthPct = flMaxHealth > 0 ? flHealth / flMaxHealth : 1.0f;
	const bool bLowHealth = flHealthPct < 0.35f;
	// Check if being healed: has healers or is overhealed
	const bool bBeingHealed = pPlayer->m_nNumHealers() > 0 || 
	                          pPlayer->InCond(TF_COND_HEALTH_OVERHEALED);
	const int nClass = pPlayer->m_iClass();
	
	// Find closest enemy and distance
	float flClosestEnemyDist = 99999.0f;
	C_TFPlayer* pClosestEnemy = nullptr;
	const int nTeam = pPlayer->m_iTeamNum();
	
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		auto pEnemy = pEntity->As<C_TFPlayer>();
		if (!pEnemy || pEnemy->deadflag() || pEnemy->m_iTeamNum() == nTeam)
			continue;
		
		const float flDist = (pEnemy->m_vecOrigin() - pPlayer->m_vecOrigin()).Length();
		if (flDist < flClosestEnemyDist)
		{
			flClosestEnemyDist = flDist;
			pClosestEnemy = pEnemy;
		}
	}
	
	// Check if teammate is fighting nearby (shooting or being shot at)
	bool bTeammateEngaged = false;
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_TEAMMATES))
	{
		auto pTeammate = pEntity->As<C_TFPlayer>();
		if (!pTeammate || pTeammate == pPlayer || pTeammate->deadflag())
			continue;
		
		const float flTeamDist = (pTeammate->m_vecOrigin() - pPlayer->m_vecOrigin()).Length();
		if (flTeamDist < 600.0f)  // Within 600 units
		{
			// Check if teammate is attacking (has weapon out and shooting)
			auto hWeapon = pTeammate->m_hActiveWeapon();
			if (hWeapon)
			{
				auto pWeaponEnt = I::ClientEntityList->GetClientEntityFromHandle(hWeapon);
				if (pWeaponEnt)
				{
					auto pWeapon = pWeaponEnt->As<C_TFWeaponBase>();
					if (pWeapon && flCurTime - pWeapon->m_flLastFireTime() < 1.0f)
					{
						bTeammateEngaged = true;
						behavior.m_Strafe.m_flLastTeammateEngagedTime = flCurTime;
						break;
					}
				}
			}
		}
	}
	
	// Check if they can see an enemy (simple FOV check)
	bool bCanSeeEnemy = false;
	if (pClosestEnemy && flClosestEnemyDist < 2000.0f)
	{
		Vec3 vToEnemy = pClosestEnemy->m_vecOrigin() - pPlayer->m_vecOrigin();
		vToEnemy.NormalizeInPlace();
		
		Vec3 vForward;
		Math::AngleVectors(pPlayer->GetEyeAngles(), &vForward);
		
		const float flDot = vForward.Dot(vToEnemy);
		if (flDot > 0.5f)  // Within ~60 degree FOV
		{
			bCanSeeEnemy = true;
			behavior.m_Strafe.m_flLastSawEnemyTime = flCurTime;
		}
	}
	
	// =========================================================================
	// RECORD CONTEXT WHEN COUNTER-STRAFE STARTS
	// =========================================================================
	if (behavior.m_Strafe.m_bIsCurrentlyCounterStrafing && !behavior.m_Strafe.m_bCSContextRecorded)
	{
		behavior.m_Strafe.m_bCSContextRecorded = true;
		
		const float flTimeSinceCSStart = flCurTime - behavior.m_Strafe.m_flCSStartTime;
		const float flTimeSinceShotAt = flCurTime - behavior.m_Strafe.m_flLastShotAtTime;
		const float flTimeSinceSawEnemy = flCurTime - behavior.m_Strafe.m_flLastSawEnemyTime;
		const float flTimeSinceTeammate = flCurTime - behavior.m_Strafe.m_flLastTeammateEngagedTime;
		
		// Determine the trigger (what caused them to start counter-strafing)
		// Priority: shot at > saw enemy > teammate engaged > low health > being healed > unprovoked
		bool bFoundTrigger = false;
		
		// Shot at recently (within 0.5s before CS started)
		if (flTimeSinceShotAt < 0.5f && flTimeSinceShotAt < flTimeSinceCSStart + 0.5f)
		{
			ctx.m_nWhenShotAt++;
			bFoundTrigger = true;
		}
		// Saw enemy recently (within 0.3s before CS started)
		else if (flTimeSinceSawEnemy < 0.3f && flTimeSinceSawEnemy < flTimeSinceCSStart + 0.3f)
		{
			ctx.m_nWhenSawEnemy++;
			bFoundTrigger = true;
		}
		// Teammate engaged nearby (within 1s before CS started)
		else if (flTimeSinceTeammate < 1.0f && flTimeSinceTeammate < flTimeSinceCSStart + 1.0f)
		{
			ctx.m_nWhenTeammateEngaged++;
			bFoundTrigger = true;
		}
		// Low health
		else if (bLowHealth)
		{
			ctx.m_nWhenLowHealth++;
			bFoundTrigger = true;
		}
		// Being healed (confident play)
		else if (bBeingHealed)
		{
			ctx.m_nWhenBeingHealed++;
			bFoundTrigger = true;
		}
		
		// No obvious trigger - habitual counter-strafer
		if (!bFoundTrigger)
		{
			ctx.m_nWhenUnprovoked++;
		}
		
		// Track health when counter-strafing
		ctx.m_flAvgHealthWhenCS = ctx.m_flAvgHealthWhenCS * 0.9f + flHealthPct * 100.0f * 0.1f;
		ctx.m_nHealthSamples++;
		
		// Track distance when counter-strafing
		if (pClosestEnemy)
		{
			ctx.m_flAvgDistanceWhenCS = ctx.m_flAvgDistanceWhenCS * 0.9f + flClosestEnemyDist * 0.1f;
			ctx.m_nDistanceSamples++;
			
			// Range-based tracking
			if (flClosestEnemyDist < 400.0f)
				ctx.m_nCSAtCloseRange++;
			else if (flClosestEnemyDist < 800.0f)
				ctx.m_nCSAtMidRange++;
			else
				ctx.m_nCSAtLongRange++;
		}
		
		// Track class-specific counter-strafe rate
		if (nClass >= 0 && nClass < 10)
		{
			ctx.m_flClassCSRate[nClass] = ctx.m_flClassCSRate[nClass] * 0.9f + 1.0f * 0.1f;
			ctx.m_nClassCSSamples[nClass]++;
		}
	}
	
	// =========================================================================
	// UPDATE CONTEXT SAMPLE COUNTS (for rate calculation)
	// Only update when NOT counter-strafing to track "opportunities"
	// =========================================================================
	if (!behavior.m_Strafe.m_bIsCurrentlyCounterStrafing)
	{
		const float flTimeSinceShotAt = flCurTime - behavior.m_Strafe.m_flLastShotAtTime;
		const float flTimeSinceSawEnemy = flCurTime - behavior.m_Strafe.m_flLastSawEnemyTime;
		const float flTimeSinceTeammate = flCurTime - behavior.m_Strafe.m_flLastTeammateEngagedTime;
		
		// Track opportunities where they COULD have counter-strafed but didn't
		// (throttled to avoid over-counting)
		static float s_flLastContextUpdate = 0.0f;
		if (flCurTime - s_flLastContextUpdate > 0.5f)
		{
			s_flLastContextUpdate = flCurTime;
			
			if (flTimeSinceShotAt < 1.0f)
				ctx.m_nShotAtSamples++;
			if (flTimeSinceSawEnemy < 1.0f)
				ctx.m_nSawEnemySamples++;
			if (flTimeSinceTeammate < 1.0f)
				ctx.m_nTeammateEngagedSamples++;
			if (bLowHealth)
				ctx.m_nLowHealthSamples++;
			if (bBeingHealed)
				ctx.m_nBeingHealedSamples++;
			
			// Range-based samples
			if (pClosestEnemy)
			{
				if (flClosestEnemyDist < 400.0f)
					ctx.m_nCloseRangeSamples++;
				else if (flClosestEnemyDist < 800.0f)
					ctx.m_nMidRangeSamples++;
				else
					ctx.m_nLongRangeSamples++;
			}
			
			// Class samples (when NOT counter-strafing)
			if (nClass >= 0 && nClass < 10)
			{
				ctx.m_flClassCSRate[nClass] = ctx.m_flClassCSRate[nClass] * 0.95f;  // Decay toward 0
			}
		}
	}
}

// ============================================================================
// SHOULD PREDICT COUNTER-STRAFE
// Returns true if we should predict this player will counter-strafe
// 
// Simple logic:
// 1. If they're CURRENTLY doing L-R-L pattern → YES
// 2. If they have a HISTORY of counter-strafing (>30% rate) AND 
//    just changed direction recently → YES (predict they'll reverse again)
// 3. Otherwise → NO
//
// This prevents false positives when player is just moving in one direction
// ============================================================================
bool CMovementSimulation::ShouldPredictCounterStrafe(int nEntIndex)
{
	if (nEntIndex < 0 || nEntIndex >= 64)
		return false;
	
	auto it = m_mPlayerBehaviors.find(nEntIndex);
	if (it == m_mPlayerBehaviors.end())
		return false;
	
	auto& behavior = it->second;
	
	// CASE 1: Currently counter-strafing right now (L-R-L pattern detected)
	if (behavior.m_Strafe.m_bIsCurrentlyCounterStrafing)
		return true;
	
	// CASE 2: Has history of counter-strafing AND just changed direction
	const int nTotalSamples = behavior.m_Strafe.m_nCounterStrafeDetections + behavior.m_Strafe.m_nNormalStrafeDetections;
	
	// Need enough history to make a prediction (at least 6 samples)
	if (nTotalSamples < 6)
		return false;
	
	// Need at least 30% counter-strafe rate to predict
	if (behavior.m_Strafe.m_flCounterStrafeRate < 0.30f)
		return false;
	
	// Check if they JUST changed direction
	const float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	const float flTimeSinceChange = flCurTime - behavior.m_Strafe.m_flLastDirectionChangeTime;
	
	// If no direction change recorded, can't predict
	if (behavior.m_Strafe.m_flLastDirectionChangeTime <= 0.0f)
		return false;
	
	// Use their learned reversal time, with a generous buffer
	// Default to 0.25s if no learned data
	float flExpectedReversalTime = 0.25f;
	if (behavior.m_Strafe.m_flAvgReversalTime > 0.05f && 
	    (behavior.m_Strafe.m_nLeftToRightSamples > 3 || behavior.m_Strafe.m_nRightToLeftSamples > 3))
	{
		flExpectedReversalTime = behavior.m_Strafe.m_flAvgReversalTime;
	}
	
	// Predict if they changed direction within (reversal time * 1.5)
	// This gives us a window to catch the pattern
	const float flPredictionWindow = flExpectedReversalTime * 1.5f;
	
	if (flTimeSinceChange > 0.0f && flTimeSinceChange < flPredictionWindow)
		return true;
	
	return false;
}

// ============================================================================
// CONTEXTUAL COUNTER-STRAFE PREDICTION
// Enhanced prediction that considers the current situation
// Returns a likelihood (0-1) that they will counter-strafe given current context
// ============================================================================
float CMovementSimulation::GetCounterStrafeLikelihood(int nEntIndex, C_TFPlayer* pTarget)
{
	if (nEntIndex < 0 || nEntIndex >= 64 || !pTarget)
		return 0.5f;  // Default 50% if no data
	
	auto it = m_mPlayerBehaviors.find(nEntIndex);
	if (it == m_mPlayerBehaviors.end())
		return 0.5f;
	
	auto& behavior = it->second;
	auto& ctx = behavior.m_Strafe.m_Context;
	
	// If currently counter-strafing, very high likelihood to continue
	if (behavior.m_Strafe.m_bIsCurrentlyCounterStrafing)
		return 0.9f;
	
	// Not enough data yet
	const int nTotalSamples = ctx.m_nShotAtSamples + ctx.m_nSawEnemySamples + ctx.m_nLowHealthSamples;
	if (nTotalSamples < 10)
		return behavior.m_Strafe.m_flCounterStrafeRate;  // Fall back to overall rate
	
	// Get current context
	const float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	const float flHealth = static_cast<float>(pTarget->m_iHealth());
	const float flMaxHealth = static_cast<float>(pTarget->GetMaxHealth());
	const float flHealthPct = flMaxHealth > 0 ? flHealth / flMaxHealth : 1.0f;
	const bool bLowHealth = flHealthPct < 0.35f;
	// Check if being healed: has healers or is overhealed
	const bool bBeingHealed = pTarget->m_nNumHealers() > 0 || 
	                          pTarget->InCond(TF_COND_HEALTH_OVERHEALED);
	const int nClass = pTarget->m_iClass();
	
	// Get distance to local player
	float flDistance = 500.0f;
	const auto pLocal = H::Entities->GetLocal();
	if (pLocal)
		flDistance = (pLocal->m_vecOrigin() - pTarget->m_vecOrigin()).Length();
	
	// Calculate weighted likelihood based on current context
	float flLikelihood = 0.0f;
	float flTotalWeight = 0.0f;
	
	// Check if we recently shot at them
	const float flTimeSinceShotAt = flCurTime - behavior.m_Strafe.m_flLastShotAtTime;
	if (flTimeSinceShotAt < 2.0f && ctx.m_nShotAtSamples > 5)
	{
		flLikelihood += ctx.GetCSRateWhenShotAt() * 2.0f;  // High weight - reactive CS
		flTotalWeight += 2.0f;
	}
	
	// Check if they can see us
	const float flTimeSinceSawEnemy = flCurTime - behavior.m_Strafe.m_flLastSawEnemyTime;
	if (flTimeSinceSawEnemy < 1.0f && ctx.m_nSawEnemySamples > 5)
	{
		flLikelihood += ctx.GetCSRateWhenSawEnemy() * 1.5f;
		flTotalWeight += 1.5f;
	}
	
	// Low health context
	if (bLowHealth && ctx.m_nLowHealthSamples > 5)
	{
		flLikelihood += ctx.GetCSRateWhenLowHealth() * 1.2f;
		flTotalWeight += 1.2f;
	}
	
	// Being healed context
	if (bBeingHealed && ctx.m_nBeingHealedSamples > 5)
	{
		flLikelihood += ctx.GetCSRateWhenHealed() * 1.0f;
		flTotalWeight += 1.0f;
	}
	
	// Distance-based likelihood
	flLikelihood += ctx.GetCSRateAtRange(flDistance) * 1.0f;
	flTotalWeight += 1.0f;
	
	// Class-based likelihood
	if (nClass >= 0 && nClass < 10 && ctx.m_nClassCSSamples[nClass] > 5)
	{
		flLikelihood += ctx.GetCSRateForClass(nClass) * 0.8f;
		flTotalWeight += 0.8f;
	}
	
	// Calculate final likelihood
	if (flTotalWeight > 0.0f)
		flLikelihood /= flTotalWeight;
	else
		flLikelihood = behavior.m_Strafe.m_flCounterStrafeRate;
	
	return std::clamp(flLikelihood, 0.0f, 1.0f);
}

// LearnAggression removed - GetPlaystyle() now handles all aggression analysis
// The aggressive/defensive sample counts are still updated in passive learning (Store())
// and in LearnHealthBehavior for directional tracking

// Main behavior update - call this from Store()
void CMovementSimulation::UpdatePlayerBehavior(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return;
	
	const int nEntIndex = pPlayer->entindex();
	if (nEntIndex < 1 || nEntIndex >= 64)
		return;
	
	// Don't update if player is dead
	if (pPlayer->deadflag())
		return;
	
	auto& behavior = GetOrCreateBehavior(nEntIndex);
	
	// Increment sample count
	behavior.m_nSampleCount++;
	
	// Get frame number for throttling
	const int nFrame = I::GlobalVars ? static_cast<int>(I::GlobalVars->framecount) : 0;
	const int nPlayerSlot = nEntIndex & 7;  // Faster than % 8
	
	// Apply time-weighted decay every ~30 seconds (throttled per-player)
	if ((nFrame & 511) == nPlayerSlot)  // Every ~512 frames per player
	{
		float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
		behavior.ApplyDecay(flCurTime);
	}
	
	// =========================================================================
	// PATTERN VERIFICATION - Throttled to every ~0.5 sec per player
	// =========================================================================
	if (behavior.m_nSampleCount > 30 && ((nFrame & 15) == nPlayerSlot))
	{
		int nMatches = 0;
		int nChecks = 0;
		
		// Check aggression prediction
		const int nAggroTotal = behavior.m_Positioning.m_nAggressiveSamples + behavior.m_Positioning.m_nDefensiveSamples;
		if (nAggroTotal > 10)
		{
			const auto pLocal = H::Entities->GetLocal();
			if (pLocal && pLocal != pPlayer)
			{
				Vec3 vToLocal = pLocal->m_vecOrigin() - pPlayer->m_vecOrigin();
				vToLocal.z = 0;
				const float flDistSqr = vToLocal.LengthSqr();
				if (flDistSqr > 10000.0f)  // > 100 units
				{
					const float flDist = sqrtf(flDistSqr);
					vToLocal = vToLocal * (1.0f / flDist);
					Vec3 vVel = pPlayer->m_vecVelocity();
					vVel.z = 0;
					const float flSpeedSqr = vVel.LengthSqr();
					if (flSpeedSqr > 2500.0f)  // > 50 units/s
					{
						vVel = vVel * (1.0f / sqrtf(flSpeedSqr));
						const float flDot = vVel.Dot(vToLocal);
						
						const bool bPredictedAggro = behavior.m_Positioning.m_flAggressionScore > 0.6f;
						const bool bPredictedDefensive = behavior.m_Positioning.m_flAggressionScore < 0.4f;
						const bool bActuallyAggro = flDot > 0.2f;
						const bool bActuallyDefensive = flDot < -0.2f;
						
						if ((bPredictedAggro || bPredictedDefensive) && (bActuallyAggro || bActuallyDefensive))
						{
							nChecks++;
							if ((bPredictedAggro && bActuallyAggro) || (bPredictedDefensive && bActuallyDefensive))
								nMatches++;
						}
					}
				}
			}
		}
		
		// Check strafe pattern prediction
		if (behavior.m_Strafe.m_flStrafeIntensity > 2.0f && behavior.m_Strafe.m_vRecentYawChanges.size() >= 3)
		{
			float flRecentIntensity = 0.0f;
			const size_t nCount = std::min(size_t(3), behavior.m_Strafe.m_vRecentYawChanges.size());
			for (size_t i = 0; i < nCount; i++)
				flRecentIntensity += fabsf(behavior.m_Strafe.m_vRecentYawChanges[i]);
			flRecentIntensity *= 0.333f;  // Faster than /3
			
			nChecks++;
			if (flRecentIntensity > behavior.m_Strafe.m_flStrafeIntensity * 0.5f)
				nMatches++;
		}
		
		// Record the pattern check result
		if (nChecks > 0)
			behavior.RecordPatternCheck(nMatches >= (nChecks + 1) / 2);
	}
	
	// Always learn these (fast, no entity iteration)
	LearnCounterStrafe(nEntIndex, behavior);
	LearnBunnyHop(pPlayer, behavior);
	
	// Throttle per-player using entity index for even distribution
	if ((nFrame & 7) == nPlayerSlot)
	{
		LearnClassBehavior(pPlayer, behavior);
		LearnWeaponAwareness(pPlayer, behavior);
		LearnReactionPattern(pPlayer, behavior);
		LearnHealthBehavior(pPlayer, behavior);
		LearnCounterStrafeContext(pPlayer, behavior);  // Contextual counter-strafe learning
	}
	
	if ((nFrame & 31) == nPlayerSlot)
	{
		LearnCornerPeek(pPlayer, behavior);
		LearnTeamBehavior(pPlayer, behavior);
		LearnObjectiveBehavior(pPlayer, behavior);
	}
	
	// Store position history (limit frequency)
	if ((nFrame & 3) == (nPlayerSlot & 3))
	{
		behavior.m_Positioning.m_vRecentPositions.push_front(pPlayer->m_vecOrigin());
		if (behavior.m_Positioning.m_vRecentPositions.size() > 30)
			behavior.m_Positioning.m_vRecentPositions.pop_back();
	}
	
	behavior.m_vLastKnownPos = pPlayer->m_vecOrigin();
	behavior.m_flLastUpdateTime = pPlayer->m_flSimulationTime();
}

// Find the payload cart entity (cached to avoid expensive iteration)
C_BaseEntity* CMovementSimulation::FindPayloadCart()
{
	// Safety check
	if (!I::EngineClient || !I::EngineClient->IsInGame() || !I::ClientEntityList)
		return nullptr;
	
	// Cache the cart - only search every 5 seconds
	static C_BaseEntity* s_pCachedCart = nullptr;
	static float s_flLastSearch = 0.0f;
	static int s_nCachedCartIndex = -1;
	
	float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	
	// Validate cached cart is still valid
	if (s_pCachedCart && s_nCachedCartIndex > 0)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(s_nCachedCartIndex);
		if (!pEntity || pEntity != s_pCachedCart)
		{
			s_pCachedCart = nullptr;
			s_nCachedCartIndex = -1;
		}
	}
	
	if (s_pCachedCart && flCurTime - s_flLastSearch < 5.0f)
		return s_pCachedCart;
	
	s_flLastSearch = flCurTime;
	s_pCachedCart = nullptr;
	s_nCachedCartIndex = -1;
	
	// Look for func_tracktrain (payload cart) or item_teamflag for CTF
	int nMaxEnts = I::ClientEntityList->GetHighestEntityIndex();
	int nMaxClients = I::EngineClient->GetMaxClients();
	
	for (int i = nMaxClients + 1; i < nMaxEnts && i < 2048; i++)  // Cap iteration
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pEntity)
			continue;
		
		auto pClientClass = pEntity->GetClientClass();
		if (!pClientClass || !pClientClass->m_pNetworkName)
			continue;
		
		const char* szClassName = pClientClass->m_pNetworkName;
		if (strstr(szClassName, "FuncTrackTrain") || strstr(szClassName, "ObjectCartDispenser"))
		{
			s_pCachedCart = pEntity->As<C_BaseEntity>();
			s_nCachedCartIndex = i;
			return s_pCachedCart;
		}
	}
	return nullptr;
}

// Count nearby teammates within radius (optimized - uses squared distance)
int CMovementSimulation::CountNearbyTeammates(C_TFPlayer* pPlayer, float flRadius)
{
	if (!pPlayer || !H::Entities || !I::EngineClient || !I::EngineClient->IsInGame())
		return 0;
	
	int nCount = 0;
	const int nTeam = pPlayer->m_iTeamNum();
	const Vec3 vPos = pPlayer->m_vecOrigin();
	const float flRadiusSqr = flRadius * flRadius;
	
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
	{
		if (!pEntity)
			continue;
		
		auto pOther = pEntity->As<C_TFPlayer>();
		if (!pOther || pOther == pPlayer || pOther->deadflag() || pOther->m_iTeamNum() != nTeam)
			continue;
		
		const Vec3 vDelta = pOther->m_vecOrigin() - vPos;
		if (vDelta.LengthSqr() < flRadiusSqr)
			nCount++;
	}
	
	return nCount;
}

// Count nearby enemies within radius (optimized - uses squared distance)
int CMovementSimulation::CountNearbyEnemies(C_TFPlayer* pPlayer, float flRadius)
{
	if (!pPlayer || !H::Entities || !I::EngineClient || !I::EngineClient->IsInGame())
		return 0;
	
	int nCount = 0;
	const int nTeam = pPlayer->m_iTeamNum();
	const Vec3 vPos = pPlayer->m_vecOrigin();
	const float flRadiusSqr = flRadius * flRadius;
	
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
	{
		if (!pEntity)
			continue;
		
		auto pOther = pEntity->As<C_TFPlayer>();
		if (!pOther || pOther == pPlayer || pOther->deadflag() || pOther->m_iTeamNum() == nTeam)
			continue;
		
		const Vec3 vDelta = pOther->m_vecOrigin() - vPos;
		if (vDelta.LengthSqr() < flRadiusSqr)
			nCount++;
	}
	
	return nCount;
}

// ============================================================================
// BOUNDS ADJUSTMENT - Fix origin compression issues for non-local players
// The game compresses origin values for network transmission, which can cause
// small inaccuracies. Adjusting the hull bounds by 0.125 units compensates.
// ============================================================================
void CMovementSimulation::SetBounds(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return;
	
	// Only adjust for non-local players
	const auto pLocal = H::Entities->GetLocal();
	if (pPlayer == pLocal)
		return;
	
	// Get game rules and view vectors
	auto pGameRules = I::TFGameRules();
	if (!pGameRules)
		return;
	
	auto pViewVectors = pGameRules->GetViewVectors();
	if (!pViewVectors)
		return;
	
	// Adjust bounds inward by 0.125 units to compensate for origin compression
	pViewVectors->m_vHullMin = Vec3(-24, -24, 0) + 0.125f;
	pViewVectors->m_vHullMax = Vec3(24, 24, 82) - 0.125f;
	pViewVectors->m_vDuckHullMin = Vec3(-24, -24, 0) + 0.125f;
	pViewVectors->m_vDuckHullMax = Vec3(24, 24, 62) - 0.125f;
}

void CMovementSimulation::RestoreBounds(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return;
	
	// Only restore for non-local players
	const auto pLocal = H::Entities->GetLocal();
	if (pPlayer == pLocal)
		return;
	
	// Get game rules and view vectors
	auto pGameRules = I::TFGameRules();
	if (!pGameRules)
		return;
	
	auto pViewVectors = pGameRules->GetViewVectors();
	if (!pViewVectors)
		return;
	
	// Restore original bounds
	pViewVectors->m_vHullMin = Vec3(-24, -24, 0);
	pViewVectors->m_vHullMax = Vec3(24, 24, 82);
	pViewVectors->m_vDuckHullMin = Vec3(-24, -24, 0);
	pViewVectors->m_vDuckHullMax = Vec3(24, 24, 62);
}

// Learn health-based behavior (do they retreat when low, push when healed?)
void CMovementSimulation::LearnHealthBehavior(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pPlayer == pLocal)
		return;
	
	float flHealth = static_cast<float>(pPlayer->m_iHealth());
	float flMaxHealth = static_cast<float>(pPlayer->GetMaxHealth());
	if (flMaxHealth <= 0.0f)
		return;
	
	float flHealthPct = flHealth / flMaxHealth;
	bool bLowHealth = flHealthPct < 0.35f;
	bool bBeingHealed = pPlayer->InCond(TF_COND_HEALTH_BUFF) || pPlayer->InCond(TF_COND_RADIUSHEAL);
	
	// Track health history
	behavior.m_Positioning.m_vRecentHealthPct.push_front(flHealthPct);
	if (behavior.m_Positioning.m_vRecentHealthPct.size() > 20)
		behavior.m_Positioning.m_vRecentHealthPct.pop_back();
	
	// Check movement direction relative to enemies
	Vec3 vPlayerPos = pPlayer->m_vecOrigin();
	Vec3 vLocalPos = pLocal->m_vecOrigin();
	Vec3 vToLocal = vLocalPos - vPlayerPos;
	vToLocal.z = 0;
	float flDist = vToLocal.Length();
	
	if (flDist < 100.0f)
		return;
	
	vToLocal = vToLocal * (1.0f / flDist);
	
	Vec3 vVelocity = pPlayer->m_vecVelocity();
	vVelocity.z = 0;
	float flSpeed = vVelocity.Length();
	
	if (flSpeed < 30.0f)
		return;
	
	vVelocity = vVelocity * (1.0f / flSpeed);
	float flDot = vVelocity.Dot(vToLocal);
	
	// Learn low health behavior
	if (bLowHealth)
	{
		if (flDot < -0.2f)
			behavior.m_Positioning.m_nLowHPRetreatSamples++;
		else if (flDot > 0.2f)
			behavior.m_Positioning.m_nLowHPFightSamples++;
		
		int nTotal = behavior.m_Positioning.m_nLowHPRetreatSamples + behavior.m_Positioning.m_nLowHPFightSamples;
		if (nTotal > 5)
		{
			float flRate = static_cast<float>(behavior.m_Positioning.m_nLowHPRetreatSamples) / static_cast<float>(nTotal);
			behavior.m_Positioning.m_flLowHealthRetreatRate = behavior.m_Positioning.m_flLowHealthRetreatRate * 0.9f + flRate * 0.1f;
		}
	}
	
	// Learn healed behavior
	if (bBeingHealed)
	{
		if (flDot > 0.2f)
			behavior.m_Positioning.m_nHealedPushSamples++;
		else
			behavior.m_Positioning.m_nHealedPassiveSamples++;
		
		int nTotal = behavior.m_Positioning.m_nHealedPushSamples + behavior.m_Positioning.m_nHealedPassiveSamples;
		if (nTotal > 5)
		{
			float flRate = static_cast<float>(behavior.m_Positioning.m_nHealedPushSamples) / static_cast<float>(nTotal);
			behavior.m_Positioning.m_flHealedAggroBoost = behavior.m_Positioning.m_flHealedAggroBoost * 0.9f + flRate * 0.1f;
		}
	}
	
	behavior.m_flLastHealth = flHealth;
	behavior.m_bWasBeingHealed = bBeingHealed;
}

// Learn team proximity behavior (do they stick with team or flank alone?)
void CMovementSimulation::LearnTeamBehavior(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	const int nNearbyTeammates = CountNearbyTeammates(pPlayer, 500.0f);
	const bool bNearTeam = nNearbyTeammates >= 2;
	const bool bAlone = nNearbyTeammates == 0;
	
	if (bNearTeam)
		behavior.m_Positioning.m_nNearTeamSamples++;
	if (bAlone)
		behavior.m_Positioning.m_nAloneSamples++;
	
	int nTotal = behavior.m_Positioning.m_nNearTeamSamples + behavior.m_Positioning.m_nAloneSamples;
	if (nTotal > 10)
	{
		behavior.m_Positioning.m_flTeamProximityRate = static_cast<float>(behavior.m_Positioning.m_nNearTeamSamples) / static_cast<float>(nTotal);
		behavior.m_Positioning.m_flSoloPlayRate = static_cast<float>(behavior.m_Positioning.m_nAloneSamples) / static_cast<float>(nTotal);
	}
	
	// =========================================================================
	// CONDITIONAL BEHAVIOR: When grouped with team AND enemy nearby, do they
	// break off to chase or stay grouped?
	// =========================================================================
	const int nNearbyEnemies = CountNearbyEnemies(pPlayer, 800.0f);
	const bool bEnemyNearby = nNearbyEnemies > 0;
	
	// Check if they WERE grouped last tick and now there's an enemy nearby
	if (behavior.m_Positioning.m_bWasNearTeam && bEnemyNearby)
	{
		// Are they still near team or did they break off?
		if (bNearTeam)
			behavior.m_Positioning.m_nWithTeamStayedGrouped++;
		else if (bAlone)
			behavior.m_Positioning.m_nWithTeamChasedAlone++;
		
		// Update the "leaves team to fight" score
		const int nTeamTotal = behavior.m_Positioning.m_nWithTeamChasedAlone + behavior.m_Positioning.m_nWithTeamStayedGrouped;
		if (nTeamTotal > 5)
		{
			const float flRate = static_cast<float>(behavior.m_Positioning.m_nWithTeamChasedAlone) / static_cast<float>(nTeamTotal);
			behavior.m_Positioning.m_flLeavesTeamToFight = behavior.m_Positioning.m_flLeavesTeamToFight * 0.9f + flRate * 0.1f;
		}
	}
	
	// Store current state for next tick
	behavior.m_Positioning.m_bWasNearTeam = bNearTeam;
	
	// Learn outnumbered/advantage behavior
	const bool bOutnumbered = nNearbyEnemies > nNearbyTeammates + 1;
	const bool bAdvantage = nNearbyTeammates > nNearbyEnemies + 1;
	
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pPlayer == pLocal)
		return;
	
	Vec3 vToLocal = pLocal->m_vecOrigin() - pPlayer->m_vecOrigin();
	vToLocal.z = 0;
	float flDist = vToLocal.Length();
	if (flDist < 100.0f)
		return;
	
	vToLocal = vToLocal * (1.0f / flDist);
	
	Vec3 vVel = pPlayer->m_vecVelocity();
	vVel.z = 0;
	float flSpeed = vVel.Length();
	if (flSpeed < 30.0f)
		return;
	
	vVel = vVel * (1.0f / flSpeed);
	float flDot = vVel.Dot(vToLocal);
	
	if (bOutnumbered && flDot < -0.2f)
		behavior.m_Positioning.m_flRetreatWhenOutnumbered = behavior.m_Positioning.m_flRetreatWhenOutnumbered * 0.95f + 0.05f;
	
	if (bAdvantage && flDot > 0.2f)
		behavior.m_Positioning.m_flPushWhenAdvantage = behavior.m_Positioning.m_flPushWhenAdvantage * 0.95f + 0.05f;
}

// Learn objective behavior (payload cart proximity + conditional behavior)
void CMovementSimulation::LearnObjectiveBehavior(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	C_BaseEntity* pCart = FindPayloadCart();
	if (!pCart)
		return;
	
	const Vec3 vPlayerPos = pPlayer->m_vecOrigin();
	const Vec3 vCartPos = pCart->GetAbsOrigin();
	const float flDistToCart = (vCartPos - vPlayerPos).Length();
	const bool bNearCart = flDistToCart < 300.0f;
	
	if (bNearCart)
		behavior.m_Positioning.m_nNearCartSamples++;
	
	if (behavior.m_nSampleCount > 10)
	{
		const float flRate = static_cast<float>(behavior.m_Positioning.m_nNearCartSamples) / static_cast<float>(behavior.m_nSampleCount);
		behavior.m_Positioning.m_flCartProximityRate = behavior.m_Positioning.m_flCartProximityRate * 0.95f + flRate * 0.05f;
	}
	
	// =========================================================================
	// CONDITIONAL BEHAVIOR: When on cart AND enemy nearby, do they leave cart
	// to chase or stay on objective?
	// =========================================================================
	const int nNearbyEnemies = CountNearbyEnemies(pPlayer, 800.0f);
	const bool bEnemyNearby = nNearbyEnemies > 0;
	
	// Check if they WERE on cart last tick and now there's an enemy nearby
	if (behavior.m_Positioning.m_bWasNearCart && bEnemyNearby)
	{
		// Are they still on cart or did they leave to fight?
		if (bNearCart)
			behavior.m_Positioning.m_nOnCartStayedOnCart++;
		else
			behavior.m_Positioning.m_nOnCartChasedEnemy++;
		
		// Update the "leaves cart to fight" score
		const int nCartTotal = behavior.m_Positioning.m_nOnCartChasedEnemy + behavior.m_Positioning.m_nOnCartStayedOnCart;
		if (nCartTotal > 5)
		{
			const float flRate = static_cast<float>(behavior.m_Positioning.m_nOnCartChasedEnemy) / static_cast<float>(nCartTotal);
			behavior.m_Positioning.m_flLeavesCartToFight = behavior.m_Positioning.m_flLeavesCartToFight * 0.9f + flRate * 0.1f;
		}
	}
	
	// Store current state for next tick
	behavior.m_Positioning.m_bWasNearCart = bNearCart;
	
	// =========================================================================
	// OVERALL OBJECTIVE VS FRAGGER SCORE
	// Combines cart proximity, team grouping, and chase behavior
	// =========================================================================
	const int nObjSamples = behavior.m_Positioning.m_nNearCartSamples + behavior.m_Positioning.m_nNearTeamSamples;
	const int nFragSamples = behavior.m_Positioning.m_nOnCartChasedEnemy + behavior.m_Positioning.m_nWithTeamChasedAlone;
	const int nObjTotal = nObjSamples + nFragSamples;
	
	if (nObjTotal > 20)
	{
		// Higher = more of a fragger, Lower = more objective focused
		float flFragRate = static_cast<float>(nFragSamples) / static_cast<float>(nObjTotal);
		
		// Also factor in cart proximity rate (objective players stay on cart more)
		flFragRate = flFragRate * 0.7f + (1.0f - behavior.m_Positioning.m_flCartProximityRate) * 0.3f;
		
		behavior.m_Positioning.m_flObjectiveVsFragger = behavior.m_Positioning.m_flObjectiveVsFragger * 0.95f + flFragRate * 0.05f;
	}
}

// Learn class-specific behavior tendencies
void CMovementSimulation::LearnClassBehavior(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	int nClass = pPlayer->m_iClass();
	behavior.m_nPlayerClass = nClass;
	
	// Set class-based aggression modifier
	// These are baseline expectations - actual behavior will override
	switch (nClass)
	{
	case TF_CLASS_SCOUT:
		behavior.m_flClassAggroModifier = 1.3f;  // Scouts are aggressive flankers
		break;
	case TF_CLASS_SOLDIER:
		behavior.m_flClassAggroModifier = 1.1f;  // Soldiers push
		break;
	case TF_CLASS_PYRO:
		behavior.m_flClassAggroModifier = 1.2f;  // Pyros ambush
		break;
	case TF_CLASS_DEMOMAN:
		behavior.m_flClassAggroModifier = 0.9f;  // Demos hold ground
		break;
	case TF_CLASS_HEAVY:
		behavior.m_flClassAggroModifier = 0.7f;  // Heavies are slow, defensive
		break;
	case TF_CLASS_ENGINEER:
		behavior.m_flClassAggroModifier = 0.5f;  // Engies stay near buildings
		break;
	case TF_CLASS_MEDIC:
		behavior.m_flClassAggroModifier = 0.6f;  // Medics stay back
		break;
	case TF_CLASS_SNIPER:
		behavior.m_flClassAggroModifier = 0.4f;  // Snipers stay far
		break;
	case TF_CLASS_SPY:
		behavior.m_flClassAggroModifier = 1.0f;  // Spies are unpredictable
		break;
	default:
		behavior.m_flClassAggroModifier = 1.0f;
		break;
	}
}


// ============================================================================
// WEAPON AWARENESS - Track when players are attacking + melee/look direction
// ============================================================================
void CMovementSimulation::LearnWeaponAwareness(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	const float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	bool bIsAttacking = false;
	bool bHasMeleeOut = false;
	
	auto hWeapon = pPlayer->m_hActiveWeapon();
	auto pWeapon = I::ClientEntityList ? 
		reinterpret_cast<C_TFWeaponBase*>(I::ClientEntityList->GetClientEntityFromHandle(hWeapon)) : nullptr;
	
	if (pWeapon)
	{
		float flNextPrimaryAttack = pWeapon->m_flNextPrimaryAttack();
		if (flNextPrimaryAttack > flCurTime && flNextPrimaryAttack < flCurTime + 1.0f)
			bIsAttacking = true;
		
		// Check if holding melee weapon
		const int nWeaponSlot = pWeapon->GetSlot();
		bHasMeleeOut = (nWeaponSlot == 2);  // Slot 2 = melee
	}
	
	if (pPlayer->InCond(TF_COND_AIMING))
		bIsAttacking = true;
	if (pPlayer->InCond(TF_COND_TAUNTING))
		bIsAttacking = false;
	
	// Shield charge = definitely running at someone
	if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		bHasMeleeOut = true;
	
	behavior.m_Combat.m_bIsAttacking = bIsAttacking;
	behavior.m_Combat.m_bHasMeleeOut = bHasMeleeOut;
	
	// =========================================================================
	// LOOK DIRECTION - Are they looking at us?
	// =========================================================================
	const auto pLocal = H::Entities->GetLocal();
	bool bIsLookingAtUs = false;
	
	if (pLocal && pLocal != pPlayer)
	{
		const Vec3 vEnemyPos = pPlayer->m_vecOrigin();
		const Vec3 vLocalPos = pLocal->m_vecOrigin();
		Vec3 vToLocal = vLocalPos - vEnemyPos;
		vToLocal.z = 0;
		const float flDist = vToLocal.Length();
		
		if (flDist > 50.0f)
		{
			vToLocal = vToLocal * (1.0f / flDist);
			
			// Get their eye angles and convert to forward vector
			const Vec3 vEyeAngles = pPlayer->GetEyeAngles();
			Vec3 vForward;
			Math::AngleVectors(vEyeAngles, &vForward);
			vForward.z = 0;
			vForward.NormalizeInPlace();
			
			// Dot product: 1 = looking directly at us, 0 = perpendicular, -1 = looking away
			const float flDot = vForward.Dot(vToLocal);
			bIsLookingAtUs = flDot > 0.7f;  // ~45 degree cone
		}
	}
	
	behavior.m_Combat.m_bIsLookingAtUs = bIsLookingAtUs;
	
	// =========================================================================
	// MELEE CHARGE BEHAVIOR - When they have melee out AND looking at us,
	// are they running toward us?
	// =========================================================================
	if (bHasMeleeOut && bIsLookingAtUs && pLocal)
	{
		Vec3 vToLocal = pLocal->m_vecOrigin() - pPlayer->m_vecOrigin();
		vToLocal.z = 0;
		const float flDist = vToLocal.Length();
		
		if (flDist > 100.0f && flDist < 1500.0f)  // In melee chase range
		{
			vToLocal = vToLocal * (1.0f / flDist);
			
			Vec3 vVel = pPlayer->m_vecVelocity();
			vVel.z = 0;
			const float flSpeed = vVel.Length();
			
			if (flSpeed > 100.0f)  // Moving fast enough to be chasing
			{
				vVel = vVel * (1.0f / flSpeed);
				const float flDot = vVel.Dot(vToLocal);
				
				if (flDot > 0.5f)  // Running toward us
					behavior.m_Combat.m_nMeleeChargeSamples++;
				else
					behavior.m_Combat.m_nMeleePassiveSamples++;
				
				const int nTotal = behavior.m_Combat.m_nMeleeChargeSamples + behavior.m_Combat.m_nMeleePassiveSamples;
				if (nTotal > 3)
				{
					const float flRate = static_cast<float>(behavior.m_Combat.m_nMeleeChargeSamples) / static_cast<float>(nTotal);
					behavior.m_Combat.m_flMeleeChargeRate = behavior.m_Combat.m_flMeleeChargeRate * 0.9f + flRate * 0.1f;
				}
			}
		}
	}
	
	// Original attack tracking
	if (bIsAttacking)
	{
		behavior.m_Combat.m_flLastAttackTime = flCurTime;
		behavior.m_Combat.m_nAttackingSamples++;
		
		const float flSpeed = pPlayer->m_vecVelocity().Length2D();
		if (flSpeed < 30.0f)
			behavior.m_Combat.m_nAttackingStillSamples++;
		else
			behavior.m_Combat.m_nAttackingMovingSamples++;
		
		if (behavior.m_Combat.m_nAttackingSamples > 5)
		{
			const float flStillRate = static_cast<float>(behavior.m_Combat.m_nAttackingStillSamples) / 
			                          static_cast<float>(behavior.m_Combat.m_nAttackingSamples);
			behavior.m_Combat.m_flAttackPredictability = behavior.m_Combat.m_flAttackPredictability * 0.9f + flStillRate * 0.1f;
		}
	}
}

// ============================================================================
// REACTION DETECTION - Track how players react when shot at
// ============================================================================
void CMovementSimulation::LearnReactionPattern(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	float flTimeSinceShot = flCurTime - behavior.m_Combat.m_flLastShotAtTime;
	if (flTimeSinceShot <= 0.0f || flTimeSinceShot > 1.0f)
		return;
	
	if (behavior.m_Combat.m_vPosWhenShotAt.IsZero())
		return;
	
	float flMinReactionTime = 0.1f;
	float flMaxReactionTime = 0.5f;
	
	switch (behavior.m_nPlayerClass)
	{
	case TF_CLASS_SCOUT:
		flMinReactionTime = 0.08f; flMaxReactionTime = 0.35f; break;
	case TF_CLASS_SOLDIER:
	case TF_CLASS_DEMOMAN:
		flMinReactionTime = 0.1f; flMaxReactionTime = 0.45f; break;
	case TF_CLASS_HEAVY:
		flMinReactionTime = 0.15f; flMaxReactionTime = 0.6f; break;
	case TF_CLASS_SNIPER:
		flMinReactionTime = 0.12f; flMaxReactionTime = 0.5f; break;
	}
	
	if (flTimeSinceShot < flMinReactionTime || flTimeSinceShot > flMaxReactionTime)
		return;
	
	if (behavior.m_Combat.m_flLastAnalyzedShotTime == behavior.m_Combat.m_flLastShotAtTime)
		return;
	behavior.m_Combat.m_flLastAnalyzedShotTime = behavior.m_Combat.m_flLastShotAtTime;
	
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;
	
	Vec3 vCurPos = pPlayer->m_vecOrigin();
	Vec3 vOldPos = behavior.m_Combat.m_vPosWhenShotAt;
	Vec3 vDelta = vCurPos - vOldPos;
	
	Vec3 vToLocal = pLocal->m_vecOrigin() - vOldPos;
	vToLocal.z = 0;
	float flDistToLocal = vToLocal.Length();
	if (flDistToLocal < 50.0f)
		return;
	
	vToLocal = vToLocal * (1.0f / flDistToLocal);
	Vec3 vRight = Vec3(-vToLocal.y, vToLocal.x, 0);
	
	float flForwardMove = vDelta.Dot(vToLocal);
	float flSideMove = vDelta.Dot(vRight);
	float flVertMove = vDelta.z;
	float flTotalMove = vDelta.Length2D();
	
	behavior.m_Combat.m_nReactionSamples++;
	
	if (flTotalMove < 20.0f && fabsf(flVertMove) < 20.0f)
		behavior.m_Combat.m_nNoReactionCount++;
	else if (flVertMove > 40.0f)
		behavior.m_Combat.m_nDodgeJumpCount++;
	else if (flForwardMove < -30.0f)
		behavior.m_Combat.m_nDodgeBackCount++;
	else if (flSideMove > 30.0f)
		behavior.m_Combat.m_nDodgeRightCount++;
	else if (flSideMove < -30.0f)
		behavior.m_Combat.m_nDodgeLeftCount++;
	else
		behavior.m_Combat.m_nNoReactionCount++;
	
	behavior.m_Combat.m_flAvgReactionTime = behavior.m_Combat.m_flAvgReactionTime * 0.8f + flTimeSinceShot * 0.2f;
	
	int nTotalReactions = behavior.m_Combat.m_nDodgeLeftCount + behavior.m_Combat.m_nDodgeRightCount + 
	                      behavior.m_Combat.m_nDodgeJumpCount + behavior.m_Combat.m_nDodgeBackCount;
	int nTotalSamples = nTotalReactions + behavior.m_Combat.m_nNoReactionCount;
	
	if (nTotalSamples > 3)
	{
		float flReactionRate = static_cast<float>(nTotalReactions) / static_cast<float>(nTotalSamples);
		behavior.m_Combat.m_flReactionToThreat = behavior.m_Combat.m_flReactionToThreat * 0.9f + flReactionRate * 0.1f;
	}
}

// ============================================================================
// BUNNY HOP DETECTION (optimized)
// ============================================================================
void CMovementSimulation::LearnBunnyHop(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	const bool bOnGround = (pPlayer->m_fFlags() & FL_ONGROUND) != 0;
	
	if (bOnGround)
	{
		behavior.m_Strafe.m_nGroundSamples++;
		
		// Detect bhop: just landed after being airborne with good speed
		if (!behavior.m_Strafe.m_bWasOnGround && 
		    behavior.m_Strafe.m_nConsecutiveAirTicks > 3 && 
		    pPlayer->m_vecVelocity().Length2DSqr() > 40000.0f)  // > 200 units/s
		{
			behavior.m_Strafe.m_nBunnyHopSamples++;
		}
		
		behavior.m_Strafe.m_nConsecutiveAirTicks = 0;
	}
	else
	{
		behavior.m_Strafe.m_nAirSamples++;
		behavior.m_Strafe.m_nConsecutiveAirTicks++;
	}
	
	behavior.m_Strafe.m_bWasOnGround = bOnGround;
	
	// Update bhop rate periodically (not every tick)
	const int nTotalSamples = behavior.m_Strafe.m_nAirSamples + behavior.m_Strafe.m_nGroundSamples;
	if (nTotalSamples > 50 && (nTotalSamples & 15) == 0)  // Every 16 samples
	{
		const float flAirRate = static_cast<float>(behavior.m_Strafe.m_nAirSamples) / static_cast<float>(nTotalSamples);
		
		if (flAirRate > 0.4f && behavior.m_Strafe.m_nBunnyHopSamples > 3)
		{
			const float flBhopRate = static_cast<float>(behavior.m_Strafe.m_nBunnyHopSamples) / 
			                         static_cast<float>(behavior.m_Strafe.m_nGroundSamples + 1);
			behavior.m_Strafe.m_flBunnyHopRate = behavior.m_Strafe.m_flBunnyHopRate * 0.95f + 
			                                     std::min(flBhopRate, 1.0f) * 0.05f;
		}
	}
}

// ============================================================================
// CORNER PEEK DETECTION
// ============================================================================
void CMovementSimulation::LearnCornerPeek(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	if (behavior.m_Positioning.m_vRecentPositions.size() < 10)
		return;
	
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pPlayer == pLocal)
		return;
	
	Vec3 vLocalPos = pLocal->m_vecOrigin();
	Vec3 vCurPos = pPlayer->m_vecOrigin();
	
	size_t nIdx1 = std::min(size_t(10), behavior.m_Positioning.m_vRecentPositions.size() - 1);
	size_t nIdx2 = std::min(size_t(20), behavior.m_Positioning.m_vRecentPositions.size() - 1);
	
	if (nIdx2 >= behavior.m_Positioning.m_vRecentPositions.size())
		return;
	
	Vec3 vPos1 = behavior.m_Positioning.m_vRecentPositions[nIdx1];
	Vec3 vPos2 = behavior.m_Positioning.m_vRecentPositions[nIdx2];
	
	float flDistNow = (vCurPos - vLocalPos).Length2D();
	float flDist1 = (vPos1 - vLocalPos).Length2D();
	float flDist2 = (vPos2 - vLocalPos).Length2D();
	
	bool bApproached = flDist1 < flDist2 - 50.0f;
	bool bRetreated = flDistNow > flDist1 + 50.0f;
	
	if (bApproached && bRetreated)
		behavior.m_Strafe.m_nCornerPeekSamples++;
	
	if (behavior.m_nSampleCount > 30)
	{
		float flPeekRate = static_cast<float>(behavior.m_Strafe.m_nCornerPeekSamples) / 
		                   static_cast<float>(behavior.m_nSampleCount / 10);
		behavior.m_Strafe.m_flCornerPeekRate = behavior.m_Strafe.m_flCornerPeekRate * 0.95f + 
		                                        std::min(flPeekRate, 1.0f) * 0.05f;
	}
}
