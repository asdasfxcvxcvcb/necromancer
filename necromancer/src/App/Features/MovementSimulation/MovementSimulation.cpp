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
	float flStraightFuzzyValue, int iMaxChanges, int iMaxChangeTime, float flMaxSpeed,
	int& iChanges, int& iStart, int& iStaticSign, bool& bStaticZero)
{
	const float flYaw1 = Math::VelocityToAngles(tRecord1.m_vDirection).y;
	const float flYaw2 = Math::VelocityToAngles(tRecord2.m_vDirection).y;
	const float flTime1 = tRecord1.m_flSimTime;
	const float flTime2 = tRecord2.m_flSimTime;
	const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);

	*pYaw = Math::NormalizeAngle(flYaw1 - flYaw2);
	
	if (flMaxSpeed > 0.0f && tRecord1.m_iMode != 1)
		*pYaw *= std::clamp(tRecord1.m_vVelocity.Length2D() / flMaxSpeed, 0.f, 1.f);
	
	if (tRecord1.m_iMode == 1) // Air
		*pYaw /= GetFrictionScale(tRecord1.m_vVelocity.Length2D(), *pYaw, 
			tRecord1.m_vVelocity.z + GetGravity() * TICK_INTERVAL, 0.f, 56.f);
	
	if (fabsf(*pYaw) > 45.f)
		return false;

	const int iLastSign = iStaticSign;
	const int iCurrSign = iStaticSign = *pYaw ? (*pYaw > 0 ? 1 : -1) : iStaticSign;

	const bool iLastZero = bStaticZero;
	const bool iCurrZero = bStaticZero = !*pYaw;

	const bool bChanged = iCurrSign != iLastSign || (iCurrZero && iLastZero);
	const bool bStraight = fabsf(*pYaw) * tRecord1.m_vVelocity.Length2D() * iTicks < flStraightFuzzyValue;

	if (bStart)
	{
		iChanges = 0;
		iStart = TIME_TO_TICKS(flTime1);
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
	if (vRecords.empty())
		return;

	const bool bGroundInit = tStorage.m_bDirectMove;
	const float flMaxSpeed = pPlayer->TeamFortress_CalculateMaxSpeed();
	
	// Cache CFG values once
	const float flGroundFuzzy = CFG::Aimbot_Projectile_Ground_Straight_Fuzzy;
	const float flAirFuzzy = CFG::Aimbot_Projectile_Air_Straight_Fuzzy;
	const int iGroundMaxChanges = CFG::Aimbot_Projectile_Ground_Max_Changes;
	const int iAirMaxChanges = CFG::Aimbot_Projectile_Air_Max_Changes;
	const int iGroundMaxChangeTime = CFG::Aimbot_Projectile_Ground_Max_Change_Time;
	const int iAirMaxChangeTime = CFG::Aimbot_Projectile_Air_Max_Change_Time;

	bool bGround = bGroundInit;
	int iMinimumStrafes = 4;
	float flStraightFuzzyValue = bGround ? flGroundFuzzy : flAirFuzzy;
	int iMaxChanges = bGround ? iGroundMaxChanges : iAirMaxChanges;
	int iMaxChangeTime = bGround ? iGroundMaxChangeTime : iAirMaxChangeTime;

	float flAverageYaw = 0.f;
	int iTicks = 0, iSkips = 0;
	iSamples = std::min(iSamples, static_cast<int>(vRecords.size()));
	
	// Local state for GetYawDifference (not static to avoid cross-player pollution)
	int iChanges = 0, iStart = 0, iStaticSign = 0;
	bool bStaticZero = false;
	
	size_t i = 1;
	for (; i < static_cast<size_t>(iSamples); i++)
	{
		auto& tRecord1 = vRecords[i - 1];
		auto& tRecord2 = vRecords[i];
		
		if (tRecord1.m_iMode != tRecord2.m_iMode)
		{
			iSkips++;
			continue;
		}

		bGround = tRecord1.m_iMode != 1;
		flStraightFuzzyValue = bGround ? flGroundFuzzy : flAirFuzzy;
		iMaxChanges = bGround ? iGroundMaxChanges : iAirMaxChanges;
		iMaxChangeTime = bGround ? iGroundMaxChangeTime : iAirMaxChangeTime;
		iMinimumStrafes = 4 + iMaxChanges;

		float flYaw = 0.f;
		bool bResult = GetYawDifference(tRecord1, tRecord2, !iTicks, &flYaw, 
			flStraightFuzzyValue, iMaxChanges, iMaxChangeTime, flMaxSpeed,
			iChanges, iStart, iStaticSign, bStaticZero);
		
		if (!bResult)
			break;

		flAverageYaw += flYaw;
		iTicks += std::max(TIME_TO_TICKS(tRecord1.m_flSimTime - tRecord2.m_flSimTime), 1);
	}

	if (i <= static_cast<size_t>(iMinimumStrafes + iSkips))
		return;

	int iMinimum = 4; // Minimum samples required
	flAverageYaw /= std::max(iTicks, iMinimum);
	
	if (fabsf(flAverageYaw) < 0.36f)
		return;

	tStorage.m_flAverageYaw = flAverageYaw;
}

// Counter-strafe spam detection removed - was causing false positives with circle strafing
// The normal strafe prediction handles this case adequately

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

	GetAverageYaw(tStorage, iSamples);
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

	float flCorrection = 0.f;
	
	// =========================================================================
	// BEHAVIOR-BASED PREDICTION - Adjust predicted DIRECTION based on playstyle
	// Instead of scaling movement, we adjust WHERE we think they'll go
	// =========================================================================
	PlayerBehavior* pBehavior = tStorage.m_pCachedBehavior;
	float flDirectionBias = 0.0f;  // Positive = predict forward, Negative = predict retreat
	float flStrafeMultiplier = 1.0f;  // How much to trust their strafe pattern
	
	if (pBehavior && pBehavior->m_nSampleCount > 20)
	{
		const float flCurTime = I::GlobalVars->curtime;
		const bool bLowHealth = tStorage.m_bCachedLowHealth;
		const bool bBeingHealed = tStorage.m_bCachedBeingHealed;
		const bool bRecentlyAttacking = (flCurTime - pBehavior->m_Combat.m_flLastAttackTime) < 0.5f;
		
		// Get playstyle score (-1 = defensive, +1 = aggressive)
		const float flPlaystyle = pBehavior->GetPlaystyle();
		
		// =====================================================================
		// IMMEDIATE OVERRIDES - Current state trumps learned behavior
		// =====================================================================
		
		// Melee + looking at us = charging straight, predict FORWARD
		if (pBehavior->m_Combat.m_bHasMeleeOut && pBehavior->m_Combat.m_bIsLookingAtUs)
		{
			flDirectionBias = 0.6f;  // Strong forward prediction - they're charging
			flStrafeMultiplier = 0.2f;  // Almost ignore strafe pattern, they're beelining
		}
		// Currently attacking = focused on aiming, less strafe
		else if (bRecentlyAttacking && pBehavior->m_Combat.m_flAttackPredictability > 0.5f)
		{
			flStrafeMultiplier = 0.4f;  // They stand still when shooting
		}
		
		// =====================================================================
		// HEALTH-BASED DIRECTION PREDICTION
		// =====================================================================
		
		// Low HP + known retreater = predict BACKWARD movement
		if (bLowHealth && pBehavior->m_Positioning.m_flLowHealthRetreatRate > 0.5f)
		{
			flDirectionBias = -0.5f * pBehavior->m_Positioning.m_flLowHealthRetreatRate;  // Strong retreat prediction
		}
		// Low HP + known fighter = predict they'll keep pushing
		else if (bLowHealth && pBehavior->m_Positioning.m_flLowHealthRetreatRate < 0.3f)
		{
			flDirectionBias = 0.25f;  // They fight when hurt
		}
		
		// Being healed + aggressive = predict FORWARD push
		if (bBeingHealed && pBehavior->m_Positioning.m_flHealedAggroBoost > 0.5f)
		{
			flDirectionBias += 0.35f * pBehavior->m_Positioning.m_flHealedAggroBoost;  // Uber push prediction
		}
		
		// =====================================================================
		// PLAYSTYLE-BASED DIRECTION PREDICTION
		// =====================================================================
		
		// Aggressive players approach - predict forward
		if (flPlaystyle > 0.3f)
		{
			flDirectionBias += 0.25f * flPlaystyle;  // Up to +0.25 for very aggressive
		}
		// Defensive players retreat - predict backward
		else if (flPlaystyle < -0.3f)
		{
			flDirectionBias += 0.25f * flPlaystyle;  // Up to -0.25 for very defensive
		}
		
		// =====================================================================
		// CORNER PEEKERS - They'll retreat after peeking
		// =====================================================================
		if (pBehavior->m_Strafe.m_flCornerPeekRate > 0.3f)
		{
			// Check if they recently approached (peeking) - predict retreat
			if (pBehavior->m_Positioning.m_vRecentPositions.size() > 5)
			{
				const auto pLocal = H::Entities->GetLocal();
				if (pLocal)
				{
					Vec3 vLocalPos = pLocal->m_vecOrigin();
					Vec3 vCurPos = tStorage.m_MoveData.m_vecAbsOrigin;
					Vec3 vOldPos = pBehavior->m_Positioning.m_vRecentPositions[4];
					
					float flDistNow = (vCurPos - vLocalPos).Length2D();
					float flDistOld = (vOldPos - vLocalPos).Length2D();
					
					// They approached recently - predict retreat
					if (flDistNow < flDistOld - 30.0f)
					{
						flDirectionBias -= 0.4f * pBehavior->m_Strafe.m_flCornerPeekRate;  // Strong retreat after peek
					}
				}
			}
		}
		
		// =====================================================================
		// STRAFE PATTERN RELIABILITY
		// High strafe intensity = trust the strafe pattern more
		// Low strafe intensity = they move predictably
		// =====================================================================
		if (pBehavior->m_Strafe.m_flStrafeIntensity > 8.0f)
		{
			flStrafeMultiplier = 1.0f + (pBehavior->m_Strafe.m_flStrafeIntensity - 8.0f) * 0.02f;
			flStrafeMultiplier = std::min(flStrafeMultiplier, 1.3f);
		}
		else if (pBehavior->m_Strafe.m_flStrafeIntensity < 2.0f)
		{
			flStrafeMultiplier = 0.7f;  // They barely strafe, predict straight movement
		}
		
		// Bunny hoppers in air = trust strafe pattern more
		if (!tStorage.m_bDirectMove && pBehavior->m_Strafe.m_flBunnyHopRate > 0.3f)
		{
			flStrafeMultiplier *= 1.0f + (0.2f * pBehavior->m_Strafe.m_flBunnyHopRate);
		}
		
		// =====================================================================
		// CLASS-SPECIFIC ADJUSTMENTS
		// =====================================================================
		switch (pBehavior->m_nPlayerClass)
		{
		case TF_CLASS_SCOUT:
			// Scouts strafe a lot, trust the pattern
			flStrafeMultiplier *= 1.1f;
			break;
		case TF_CLASS_HEAVY:
			// Revved heavies barely move
			if (bRecentlyAttacking)
			{
				flStrafeMultiplier = 0.2f;
				flDirectionBias = 0.0f;  // They're stationary
			}
			break;
		case TF_CLASS_SNIPER:
			// Scoped snipers don't move
			if (tStorage.m_bCachedAiming)
			{
				flStrafeMultiplier = 0.1f;
				flDirectionBias = 0.0f;
			}
			break;
		}
		
		// Clamp values - allow stronger biases now
		flDirectionBias = std::clamp(flDirectionBias, -0.7f, 0.7f);
		flStrafeMultiplier = std::clamp(flStrafeMultiplier, 0.1f, 1.5f);
	}
	
	// =========================================================================
	// APPLY MOVEMENT PREDICTION
	// =========================================================================
	
	// Apply counter-strafe spam prediction (A-D spam)
	if (tStorage.m_bCounterStrafeSpam && tStorage.m_bDirectMove)
	{
		tStorage.m_MoveData.m_flForwardMove *= 0.3f;
		tStorage.m_MoveData.m_flSideMove *= 0.3f;
	}
	else if (tStorage.m_flAverageYaw)
	{
		float flMult = flStrafeMultiplier;  // Use behavior-adjusted multiplier
		if (!tStorage.m_bDirectMove && !tStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			flCorrection = 90.f * (tStorage.m_flAverageYaw > 0 ? 1.f : -1.f);
			flMult *= GetFrictionScale(tStorage.m_MoveData.m_vecVelocity.Length2D(), tStorage.m_flAverageYaw,
				tStorage.m_MoveData.m_vecVelocity.z + GetGravity() * TICK_INTERVAL);
		}
		tStorage.m_MoveData.m_vecViewAngles.y += tStorage.m_flAverageYaw * flMult + flCorrection;
	}
	else if (!tStorage.m_bDirectMove)
	{
		tStorage.m_MoveData.m_flForwardMove = tStorage.m_MoveData.m_flSideMove = 0.f;
	}
	
	// =========================================================================
	// APPLY DIRECTION BIAS - Adjust forward/backward prediction
	// Positive bias = predict they move toward us (add forward movement)
	// Negative bias = predict they retreat (reduce forward movement)
	// =========================================================================
	if (fabsf(flDirectionBias) > 0.01f && pBehavior)
	{
		const auto pLocal = H::Entities->GetLocal();
		if (pLocal && pLocal != tStorage.m_pPlayer)
		{
			// Get direction toward local player
			Vec3 vToLocal = pLocal->m_vecOrigin() - tStorage.m_MoveData.m_vecAbsOrigin;
			vToLocal.z = 0;
			float flDist = vToLocal.Length();
			
			if (flDist > 50.0f)
			{
				vToLocal = vToLocal * (1.0f / flDist);
				
				// Convert to movement space
				float flBiasForward = vToLocal.x;
				float flBiasSide = -vToLocal.y;
				Math::FixMovement(flBiasForward, flBiasSide, Vec3(0, 0, 0), tStorage.m_MoveData.m_vecViewAngles);
				
				// Apply bias (scaled by max speed for meaningful adjustment)
				float flBiasStrength = flDirectionBias * tStorage.m_MoveData.m_flMaxSpeed;
				tStorage.m_MoveData.m_flForwardMove += flBiasForward * flBiasStrength;
				tStorage.m_MoveData.m_flSideMove += flBiasSide * flBiasStrength;
			}
		}
	}

	float flOldSpeed = tStorage.m_MoveData.m_flClientMaxSpeed;
	bool bSwimmingTick = tStorage.m_pPlayer->m_nWaterLevel() >= 2;
	if (tStorage.m_pPlayer->m_bDucked() && (tStorage.m_pPlayer->m_fFlags() & FL_ONGROUND) && !bSwimmingTick)
		tStorage.m_MoveData.m_flClientMaxSpeed /= 3.f;

	// Adjust bounds for non-local players to fix origin compression issues
	const bool bIsLocalPlayer = (tStorage.m_pPlayer == H::Entities->GetLocal());
	if (!bIsLocalPlayer)
		SetBounds(tStorage.m_pPlayer);

	I::GameMovement->ProcessMovement(tStorage.m_pPlayer, &tStorage.m_MoveData);

	// Restore bounds after movement
	if (!bIsLocalPlayer)
		RestoreBounds(tStorage.m_pPlayer);

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

	if (tStorage.m_flAverageYaw && !tStorage.m_bCounterStrafeSpam)
	{
		tStorage.m_MoveData.m_vecViewAngles.y -= flCorrection;
	}
	else if (tStorage.m_bDirectMove && !bLastDirectMove
		&& !tStorage.m_MoveData.m_flForwardMove && !tStorage.m_MoveData.m_flSideMove
		&& tStorage.m_MoveData.m_vecVelocity.Length2D() > tStorage.m_MoveData.m_flMaxSpeed * 0.015f)
	{
		// Just landed - set up movement in velocity direction
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

// Learn counter-strafe patterns from yaw history (optimized)
void CMovementSimulation::LearnCounterStrafe(int nEntIndex, PlayerBehavior& behavior)
{
	auto it = m_mRecords.find(nEntIndex);
	if (it == m_mRecords.end() || it->second.size() < 3)
		return;
	
	const auto& vRecords = it->second;
	const auto& rec0 = vRecords[0];
	const auto& rec1 = vRecords[1];
	
	if (rec0.m_iMode != 0 || rec1.m_iMode != 0)  // Ground only
		return;
	
	const float flSpeed0Sqr = rec0.m_vVelocity.Length2DSqr();
	const float flSpeed1Sqr = rec1.m_vVelocity.Length2DSqr();
	if (flSpeed0Sqr < 400.0f || flSpeed1Sqr < 400.0f)  // < 20 units/s
		return;
	
	const float flYaw0 = Math::VelocityToAngles(rec0.m_vVelocity).y;
	const float flYaw1 = Math::VelocityToAngles(rec1.m_vVelocity).y;
	const float flYawDelta = Math::NormalizeAngle(flYaw0 - flYaw1);
	
	// Track strafe intensity (EMA of absolute yaw change)
	behavior.m_Strafe.m_flStrafeIntensity = behavior.m_Strafe.m_flStrafeIntensity * 0.95f + fabsf(flYawDelta) * 0.05f;
	
	// Store yaw change if significant
	if (fabsf(flYawDelta) > 2.0f)
	{
		auto& vYawChanges = behavior.m_Strafe.m_vRecentYawChanges;
		auto& vYawTimes = behavior.m_Strafe.m_vRecentYawTimes;
		
		vYawChanges.push_front(flYawDelta);
		vYawTimes.push_front(rec0.m_flSimTime);
		
		if (vYawChanges.size() > 30)
		{
			vYawChanges.pop_back();
			vYawTimes.pop_back();
		}
		
		// Analyze for counter-strafe pattern (only if we have enough data)
		if (vYawChanges.size() >= 4)
		{
			int nSignChanges = 0;
			int nLastSign = 0;
			
			for (size_t i = 0; i < vYawChanges.size(); i++)
			{
				const int nSign = (vYawChanges[i] > 0.0f) ? 1 : -1;
				if (nLastSign != 0 && nSign != nLastSign)
					nSignChanges++;
				nLastSign = nSign;
			}
			
			if (nSignChanges >= 2)
				behavior.m_Strafe.m_nCounterStrafeSamples++;
			
			if (behavior.m_nSampleCount > 0)
			{
				const float flNewRate = static_cast<float>(behavior.m_Strafe.m_nCounterStrafeSamples) / 
				                        static_cast<float>(behavior.m_nSampleCount);
				behavior.m_Strafe.m_flCounterStrafeRate = behavior.m_Strafe.m_flCounterStrafeRate * 0.95f + flNewRate * 0.05f;
			}
		}
	}
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

// Check if we should predict counter-strafe based on learned behavior
// Made VERY strict to avoid false positives
bool CMovementSimulation::ShouldPredictCounterStrafe(int nEntIndex)
{
	if (nEntIndex < 0 || nEntIndex >= 64)
		return false;
	
	auto it = m_mPlayerBehaviors.find(nEntIndex);
	if (it == m_mPlayerBehaviors.end())
		return false;
	
	auto& behavior = it->second;
	
	// Only predict counter-strafe if:
	// 1. They have a VERY high counter-strafe rate (>60%)
	// 2. We have lots of samples (>50)
	// 3. Their strafe intensity is high (they're actually changing direction a lot)
	return behavior.m_Strafe.m_flCounterStrafeRate > 0.60f && 
	       behavior.m_nSampleCount > 50 &&
	       behavior.m_Strafe.m_flStrafeIntensity > 10.0f;
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
void CMovementSimulation::OnShotFired(int nTargetEntIndex)
{
	if (nTargetEntIndex < 1 || nTargetEntIndex >= 64)
		return;
	
	auto pTarget = I::ClientEntityList->GetClientEntity(nTargetEntIndex);
	if (!pTarget)
		return;
	
	auto pPlayer = pTarget->As<C_TFPlayer>();
	if (!pPlayer || pPlayer->deadflag())
		return;
	
	auto& behavior = GetOrCreateBehavior(nTargetEntIndex);
	behavior.m_Combat.m_flLastShotAtTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	behavior.m_Combat.m_vPosWhenShotAt = pPlayer->m_vecOrigin();
}

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
