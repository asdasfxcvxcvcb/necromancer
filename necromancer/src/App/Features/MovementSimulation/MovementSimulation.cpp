#include "MovementSimulation.h"
#include "../LagRecords/LagRecords.h"
#include "../CFG.h"
#include "../amalgam_port/AmalgamCompat.h"
#include <numeric>

static CUserCmd s_tDummyCmd = {};

// Cached ConVars for performance
static ConVar* s_pSvGravity = nullptr;
static ConVar* s_pSvAirAccelerate = nullptr;

// Helper to get gravity (cached)
static inline float GetGravity()
{
	if (!s_pSvGravity)
		s_pSvGravity = I::CVar->FindVar("sv_gravity");
	return s_pSvGravity ? s_pSvGravity->GetFloat() : 800.0f;
}

// Helper for air friction scaling
static inline float GetFrictionScale(float flVelocityXY, float flTurn, float flVelocityZ, float flMin = 50.f, float flMax = 150.f)
{
	if (0.f >= flVelocityZ || flVelocityZ > 250.f)
		return 1.f;

	if (!s_pSvAirAccelerate)
		s_pSvAirAccelerate = I::CVar->FindVar("sv_airaccelerate");
	float flScale = s_pSvAirAccelerate ? std::max(s_pSvAirAccelerate->GetFloat(), 1.f) : 10.f;
	flMin *= flScale;
	flMax *= flScale;

	return Math::RemapValClamped(fabsf(flVelocityXY * flTurn), flMin, flMax, 1.f, 0.25f);
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

	// Get player list once instead of iterating twice
	const auto& vPlayers = H::Entities->GetGroup(EEntGroup::PLAYERS_ALL);
	
	for (const auto pEntity : vPlayers)
	{
		if (!pEntity)
			continue;

		auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer)
			continue;
			
		int nEntIndex = pPlayer->entindex();
		if (nEntIndex < 1 || nEntIndex >= 64)
			continue;
		
		// Cache commonly accessed values
		bool bIsDead = pPlayer->deadflag();
		bool bIsGhost = pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE);
		bool bIsLocal = (pPlayer == pLocal);
		float flSimTime = pPlayer->m_flSimulationTime();
		float flOldSimTime = pPlayer->m_flOldSimulationTime();
		float flDeltaTime = flSimTime - flOldSimTime;
		
		auto& vRecords = m_mRecords[nEntIndex];
		auto& vSimTimes = m_mSimTimes[nEntIndex];

		// Clear records for dead/ghost/zero velocity players
		if (bIsDead || bIsGhost)
		{
			vRecords.clear();
			vSimTimes.clear();
			continue;
		}
		
		Vec3 vVelocity = pPlayer->m_vecVelocity();
		
		if (vVelocity.IsZero())
		{
			vRecords.clear();
			continue;
		}

		// Skip if simulation time hasn't changed
		if (flDeltaTime <= 0.0f)
			continue;

		Vec3 vOrigin = pPlayer->m_vecOrigin();
		Vec3 vDirection = vVelocity;
		vDirection.z = 0;

		// IsSwimming = m_nWaterLevel() >= 2
		bool bSwimming = pPlayer->m_nWaterLevel() >= 2;
		int iMode = bSwimming ? 2 : (pPlayer->m_fFlags() & FL_ONGROUND) ? 0 : 1;

		vRecords.emplace_front(MoveData{
			vDirection,
			flSimTime,
			iMode,
			vVelocity,
			vOrigin
		});

		if (vRecords.size() > 66)
			vRecords.pop_back();

		// Handle shield charge and direction normalization
		auto& tCurRecord = vRecords.front();
		float flMaxSpeed = pPlayer->TeamFortress_CalculateMaxSpeed();
		
		if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			tCurRecord.m_vDirection = vVelocity;
			tCurRecord.m_vDirection.z = 0;
			tCurRecord.m_vDirection.NormalizeInPlace();
			tCurRecord.m_vDirection = tCurRecord.m_vDirection * flMaxSpeed;
		}
		else
		{
			// All modes: normalize direction to maxspeed for consistency
			Vec3 vNorm = vVelocity;
			vNorm.z = 0;
			if (!vNorm.IsZero())
			{
				vNorm.NormalizeInPlace();
				tCurRecord.m_vDirection = vNorm * flMaxSpeed;
				if (tCurRecord.m_iMode == 2) // Swimming gets 2x
					tCurRecord.m_vDirection = tCurRecord.m_vDirection * 2.0f;
			}
		}

		// Store simulation time deltas (skip local player)
		if (!bIsLocal)
		{
			vSimTimes.push_front(flDeltaTime);
			if (vSimTimes.size() > static_cast<size_t>(CFG::Aimbot_Projectile_Delta_Count))
				vSimTimes.pop_back();
			
			// Update behavior learning for this player
			UpdatePlayerBehavior(pPlayer);
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

// Detect counter-strafe spam (A-D spam) on ground
// DISABLED FOR NOW - was causing false positives with circle strafing
// TODO: Implement proper A-D spam detection that doesn't trigger on circle strafes
void CMovementSimulation::DetectCounterStrafeSpam(MoveStorage& tStorage)
{
	// Disabled - the detection was too aggressive and broke circle strafe prediction
	// Counter-strafe spam is rare enough that we can just let the normal strafe prediction handle it
	(void)tStorage;
	return;
}

bool CMovementSimulation::StrafePrediction(MoveStorage& tStorage, int iSamples)
{
	if (tStorage.m_bDirectMove)
	{
		if (!CFG::Aimbot_Projectile_Ground_Strafe_Prediction)
			return false;
		
		// Also detect counter-strafe spam on ground
		DetectCounterStrafeSpam(tStorage);
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
	float flBehaviorMod = 1.0f;
	
	// =========================================================================
	// BEHAVIOR-BASED PREDICTION MODIFIERS (optimized - minimal checks)
	// =========================================================================
	int nEntIndex = tStorage.m_pPlayer->entindex();
	
	// Quick bounds check and get behavior pointer
	PlayerBehavior* pBehavior = (nEntIndex > 0 && nEntIndex < 64) ? 
		GetPlayerBehavior(nEntIndex) : nullptr;
	
	// Only apply behavior if we have enough confidence (>20% = ~20 samples)
	if (pBehavior && pBehavior->m_nSampleCount > 20)
	{
		const float flCurTime = I::GlobalVars->curtime;
		
		// Cache player state once
		const int nMaxHealth = tStorage.m_pPlayer->GetMaxHealth();
		const float flHealthPct = (nMaxHealth > 0) ? 
			static_cast<float>(tStorage.m_pPlayer->m_iHealth()) / static_cast<float>(nMaxHealth) : 1.0f;
		
		const bool bLowHealth = flHealthPct < 0.35f;
		const bool bBeingHealed = tStorage.m_pPlayer->InCond(TF_COND_HEALTH_BUFF) || 
		                          tStorage.m_pPlayer->InCond(TF_COND_RADIUSHEAL);
		
		// === FAST PATH: Most impactful modifiers only ===
		
		// WEAPON AWARENESS: If they're attacking, they're more predictable
		const float flTimeSinceAttack = flCurTime - pBehavior->m_flLastAttackTime;
		const bool bRecentlyAttacking = flTimeSinceAttack < 0.5f;
		
		if (bRecentlyAttacking && pBehavior->m_flAttackPredictability > 0.4f)
		{
			// They tend to stand still when shooting - reduce movement prediction
			flBehaviorMod *= 0.4f + (0.6f * (1.0f - pBehavior->m_flAttackPredictability));
		}
		
		// REACTION DETECTION: If we just shot at them, predict their dodge
		const float flTimeSinceShot = flCurTime - pBehavior->m_flLastShotAtTime;
		if (flTimeSinceShot > 0.0f && flTimeSinceShot < pBehavior->m_flAvgReactionTime + 0.2f)
		{
			// They might be about to dodge - if they have a strong pattern, predict it
			if (pBehavior->m_flReactionToThreat > 0.5f && pBehavior->m_nReactionSamples > 5)
			{
				// They usually react - increase movement prediction slightly
				flBehaviorMod *= 1.1f;
			}
			else if (pBehavior->m_flReactionToThreat < 0.3f)
			{
				// They usually don't react - they'll keep doing what they were doing
				flBehaviorMod *= 0.9f;
			}
		}
		
		// Low health retreat (most common behavior change)
		if (bLowHealth && pBehavior->m_flLowHealthRetreatRate > 0.5f)
			flBehaviorMod *= 0.5f + (0.5f * (1.0f - pBehavior->m_flLowHealthRetreatRate));
		
		// Healed aggro boost
		if (bBeingHealed && pBehavior->m_flHealedAggroBoost > 0.5f)
			flBehaviorMod *= 1.0f + (0.4f * pBehavior->m_flHealedAggroBoost);
		
		// Class-based adjustments (simple switch, no entity iteration)
		switch (pBehavior->m_nPlayerClass)
		{
		case TF_CLASS_SCOUT:
			flBehaviorMod *= 0.9f;
			break;
		case TF_CLASS_HEAVY:
			// Heavies are VERY predictable when revved up
			if (bRecentlyAttacking)
				flBehaviorMod *= 0.6f;
			else
				flBehaviorMod *= 1.15f;
			break;
		case TF_CLASS_SNIPER:
			// Snipers are very predictable when scoped
			if (tStorage.m_pPlayer->InCond(TF_COND_AIMING))
				flBehaviorMod *= 0.3f;  // Almost stationary
			else if (pBehavior->m_flAggressionScore < 0.3f)
				flBehaviorMod *= 0.7f;
			break;
		}
		
		// Aggression score (simple check)
		if (pBehavior->m_flAggressionScore < 0.3f)
			flBehaviorMod *= 0.8f;
		else if (pBehavior->m_flAggressionScore > 0.7f)
			flBehaviorMod *= 1.1f;
		
		// Bunny hoppers are less predictable in the air
		if (pBehavior->m_flBunnyHopRate > 0.3f && !tStorage.m_bDirectMove)
		{
			// They bhop a lot - harder to predict air movement
			flBehaviorMod *= 1.0f + (0.3f * pBehavior->m_flBunnyHopRate);
		}
		
		// Corner peekers will retreat - reduce forward prediction
		if (pBehavior->m_flCornerPeekRate > 0.3f)
		{
			// They peek and retreat - expect them to back off
			flBehaviorMod *= 0.7f + (0.3f * (1.0f - pBehavior->m_flCornerPeekRate));
		}
		
		// High strafe intensity = harder to predict
		if (pBehavior->m_flStrafeIntensity > 5.0f)
		{
			flBehaviorMod *= 1.0f + (0.1f * std::min(pBehavior->m_flStrafeIntensity / 15.0f, 1.0f));
		}
		
		// Clamp
		flBehaviorMod = std::clamp(flBehaviorMod, 0.2f, 1.5f);
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
		float flMult = 1.f;
		if (!tStorage.m_bDirectMove && !tStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			flCorrection = 90.f * (tStorage.m_flAverageYaw > 0 ? 1.f : -1.f);
			flMult = GetFrictionScale(tStorage.m_MoveData.m_vecVelocity.Length2D(), tStorage.m_flAverageYaw,
				tStorage.m_MoveData.m_vecVelocity.z + GetGravity() * TICK_INTERVAL);
		}
		tStorage.m_MoveData.m_vecViewAngles.y += tStorage.m_flAverageYaw * flMult + flCorrection;
	}
	else if (!tStorage.m_bDirectMove)
	{
		tStorage.m_MoveData.m_flForwardMove = tStorage.m_MoveData.m_flSideMove = 0.f;
	}
	
	// Apply behavior modifier to movement inputs
	if (flBehaviorMod != 1.0f && !tStorage.m_bCounterStrafeSpam)
	{
		tStorage.m_MoveData.m_flForwardMove *= flBehaviorMod;
		tStorage.m_MoveData.m_flSideMove *= flBehaviorMod;
	}

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

	if (tStorage.m_flAverageYaw && !tStorage.m_bCounterStrafeSpam)
	{
		tStorage.m_MoveData.m_vecViewAngles.y -= flCorrection;
	}
	else if (tStorage.m_bDirectMove && !bLastDirectMove
		&& !tStorage.m_MoveData.m_flForwardMove && !tStorage.m_MoveData.m_flSideMove
		&& tStorage.m_MoveData.m_vecVelocity.Length2D() > tStorage.m_MoveData.m_flMaxSpeed * 0.015f)
	{
		// Just landed - set up movement in velocity direction using FixMovement
		Vec3 vDirection = tStorage.m_MoveData.m_vecVelocity;
		vDirection.z = 0;
		vDirection.NormalizeInPlace();
		vDirection = vDirection * 450.f;
		
		// Use FixMovement to convert world-space direction to view-space movement
		float flForwardMove = vDirection.x;
		float flSideMove = -vDirection.y;
		Math::FixMovement(flForwardMove, flSideMove, Vec3(0, 0, 0), tStorage.m_MoveData.m_vecViewAngles);
		
		tStorage.m_MoveData.m_flForwardMove = flForwardMove * flBehaviorMod;
		tStorage.m_MoveData.m_flSideMove = flSideMove * flBehaviorMod;
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
	auto& vSimTimes = m_mSimTimes[pPlayer->entindex()];
	if (!vSimTimes.empty())
	{
		// Average mode
		return std::reduce(vSimTimes.begin(), vSimTimes.end()) / vSimTimes.size();
	}
	return TICK_INTERVAL;
}

// ============================================================================
// PLAYER BEHAVIOR LEARNING
// ============================================================================

PlayerBehavior* CMovementSimulation::GetPlayerBehavior(int nEntIndex)
{
	if (nEntIndex < 0 || nEntIndex >= 64)
		return nullptr;
	return &m_mPlayerBehaviors[nEntIndex];
}

// Learn counter-strafe patterns from yaw history
void CMovementSimulation::LearnCounterStrafe(int nEntIndex, PlayerBehavior& behavior)
{
	auto& vRecords = m_mRecords[nEntIndex];
	if (vRecords.size() < 3)
		return;
	
	// Calculate yaw delta between recent records
	auto& rec0 = vRecords[0];
	auto& rec1 = vRecords[1];
	
	if (rec0.m_iMode != 0 || rec1.m_iMode != 0)  // Ground only
		return;
	if (rec0.m_vVelocity.Length2D() < 20.0f || rec1.m_vVelocity.Length2D() < 20.0f)
		return;
	
	float flYaw0 = Math::VelocityToAngles(rec0.m_vVelocity).y;
	float flYaw1 = Math::VelocityToAngles(rec1.m_vVelocity).y;
	float flYawDelta = Math::NormalizeAngle(flYaw0 - flYaw1);
	
	// Track strafe intensity (average absolute yaw change)
	behavior.m_flStrafeIntensity = behavior.m_flStrafeIntensity * 0.95f + fabsf(flYawDelta) * 0.05f;
	
	// Store yaw change if significant
	if (fabsf(flYawDelta) > 2.0f)
	{
		behavior.m_vRecentYawChanges.push_front(flYawDelta);
		behavior.m_vRecentYawTimes.push_front(rec0.m_flSimTime);
		
		// Keep last 30 samples (~0.5 seconds of history)
		if (behavior.m_vRecentYawChanges.size() > 30)
		{
			behavior.m_vRecentYawChanges.pop_back();
			behavior.m_vRecentYawTimes.pop_back();
		}
	}
	
	// Analyze for counter-strafe pattern
	if (behavior.m_vRecentYawChanges.size() >= 4)
	{
		int nSignChanges = 0;
		int nLastSign = 0;
		float flTotalPeriod = 0.0f;
		int nPeriodSamples = 0;
		
		for (size_t i = 0; i < behavior.m_vRecentYawChanges.size(); i++)
		{
			int nSign = (behavior.m_vRecentYawChanges[i] > 0.0f) ? 1 : -1;
			if (nLastSign != 0 && nSign != nLastSign)
			{
				nSignChanges++;
				// Calculate period between this change and previous
				if (i > 0 && i < behavior.m_vRecentYawTimes.size())
				{
					float flTimeDelta = behavior.m_vRecentYawTimes[0] - behavior.m_vRecentYawTimes[i];
					if (flTimeDelta > 0.0f && nSignChanges > 0)
					{
						flTotalPeriod += flTimeDelta / nSignChanges;
						nPeriodSamples++;
					}
				}
			}
			nLastSign = nSign;
		}
		
		// Update counter-strafe rate (exponential moving average)
		bool bIsCounterStrafing = nSignChanges >= 2;
		if (bIsCounterStrafing)
			behavior.m_nCounterStrafeSamples++;
		
		// Calculate rate with smoothing (use m_nSampleCount which is incremented in UpdatePlayerBehavior)
		if (behavior.m_nSampleCount > 0)
		{
			float flNewRate = static_cast<float>(behavior.m_nCounterStrafeSamples) / 
			                  static_cast<float>(behavior.m_nSampleCount);
			behavior.m_flCounterStrafeRate = behavior.m_flCounterStrafeRate * 0.95f + flNewRate * 0.05f;
		}
		
		// Update average strafe period
		if (nPeriodSamples > 0)
		{
			float flAvgPeriod = flTotalPeriod / nPeriodSamples;
			behavior.m_flAvgStrafePeriod = behavior.m_flAvgStrafePeriod * 0.9f + flAvgPeriod * 0.1f;
		}
	}
}

// Learn aggression (moving toward vs away from local player)
void CMovementSimulation::LearnAggression(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pPlayer == pLocal)
		return;
	
	Vec3 vPlayerPos = pPlayer->m_vecOrigin();
	Vec3 vLocalPos = pLocal->m_vecOrigin();
	Vec3 vToLocal = vLocalPos - vPlayerPos;
	vToLocal.z = 0;
	float flDistToLocal = vToLocal.Length();
	
	if (flDistToLocal < 100.0f)  // Too close to measure
		return;
	
	vToLocal = vToLocal * (1.0f / flDistToLocal);  // Normalize
	
	Vec3 vVelocity = pPlayer->m_vecVelocity();
	vVelocity.z = 0;
	float flSpeed = vVelocity.Length();
	
	if (flSpeed < 50.0f)  // Not moving enough
		return;
	
	vVelocity = vVelocity * (1.0f / flSpeed);  // Normalize
	
	// Dot product: positive = moving toward local, negative = moving away
	float flDot = vVelocity.Dot(vToLocal);
	
	if (flDot > 0.3f)
		behavior.m_nAggressiveSamples++;
	else if (flDot < -0.3f)
		behavior.m_nDefensiveSamples++;
	
	// Calculate aggression score
	int nTotal = behavior.m_nAggressiveSamples + behavior.m_nDefensiveSamples;
	if (nTotal > 10)
	{
		float flNewScore = static_cast<float>(behavior.m_nAggressiveSamples) / static_cast<float>(nTotal);
		behavior.m_flAggressionScore = behavior.m_flAggressionScore * 0.95f + flNewScore * 0.05f;
	}
}

// Main behavior update - call this from Store()
void CMovementSimulation::UpdatePlayerBehavior(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return;
	
	int nEntIndex = pPlayer->entindex();
	if (nEntIndex < 1 || nEntIndex >= 64)
		return;
	
	// Don't update if player is dead
	if (pPlayer->deadflag())
		return;
	
	auto& behavior = m_mPlayerBehaviors[nEntIndex];
	
	// Increment sample count here so it applies to ALL learning, not just counter-strafe
	behavior.m_nSampleCount++;
	
	// Always learn these (fast, no entity iteration)
	LearnCounterStrafe(nEntIndex, behavior);
	LearnClassBehavior(pPlayer, behavior);
	LearnWeaponAwareness(pPlayer, behavior);
	LearnReactionPattern(pPlayer, behavior);
	LearnBunnyHop(pPlayer, behavior);
	
	// Throttle expensive operations per-player using their entity index
	// This spreads the load across frames instead of doing all players at once
	int nFrame = I::GlobalVars ? static_cast<int>(I::GlobalVars->framecount) : 0;
	int nPlayerSlot = nEntIndex % 8;  // 8 slots for throttling
	
	if ((nFrame % 8) == nPlayerSlot)
	{
		// These access local player - do every 8 frames per player
		LearnAggression(pPlayer, behavior);
		LearnHealthBehavior(pPlayer, behavior);
		LearnCornerPeek(pPlayer, behavior);
	}
	
	if ((nFrame % 32) == nPlayerSlot)
	{
		// These iterate entities - do every 32 frames per player (~0.5 sec)
		LearnTeamBehavior(pPlayer, behavior);
		LearnObjectiveBehavior(pPlayer, behavior);
	}
	
	// Store position history (cheap)
	behavior.m_vRecentPositions.push_front(pPlayer->m_vecOrigin());
	if (behavior.m_vRecentPositions.size() > 30)
		behavior.m_vRecentPositions.pop_back();
	
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
	return behavior.m_flCounterStrafeRate > 0.60f && 
	       behavior.m_nSampleCount > 50 &&
	       behavior.m_flStrafeIntensity > 10.0f;
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

// Count nearby teammates within radius
int CMovementSimulation::CountNearbyTeammates(C_TFPlayer* pPlayer, float flRadius)
{
	if (!pPlayer || !H::Entities)
		return 0;
	
	// Safety check
	if (!I::EngineClient || !I::EngineClient->IsInGame())
		return 0;
	
	int nCount = 0;
	int nTeam = pPlayer->m_iTeamNum();
	Vec3 vPos = pPlayer->m_vecOrigin();
	float flRadiusSqr = flRadius * flRadius;
	
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
	{
		if (!pEntity)
			continue;
		
		auto pOther = pEntity->As<C_TFPlayer>();
		if (!pOther || pOther == pPlayer)
			continue;
		
		// Extra safety - check entity index is valid
		int nIdx = pOther->entindex();
		if (nIdx < 1 || nIdx > 64)
			continue;
		
		if (pOther->deadflag() || pOther->m_iTeamNum() != nTeam)
			continue;
		
		Vec3 vDelta = pOther->m_vecOrigin() - vPos;
		if (vDelta.LengthSqr() < flRadiusSqr)
			nCount++;
	}
	
	return nCount;
}

// Count nearby enemies within radius
int CMovementSimulation::CountNearbyEnemies(C_TFPlayer* pPlayer, float flRadius)
{
	if (!pPlayer || !H::Entities)
		return 0;
	
	// Safety check
	if (!I::EngineClient || !I::EngineClient->IsInGame())
		return 0;
	
	int nCount = 0;
	int nTeam = pPlayer->m_iTeamNum();
	Vec3 vPos = pPlayer->m_vecOrigin();
	float flRadiusSqr = flRadius * flRadius;
	
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
	{
		if (!pEntity)
			continue;
		
		auto pOther = pEntity->As<C_TFPlayer>();
		if (!pOther || pOther == pPlayer)
			continue;
		
		// Extra safety - check entity index is valid
		int nIdx = pOther->entindex();
		if (nIdx < 1 || nIdx > 64)
			continue;
		
		if (pOther->deadflag() || pOther->m_iTeamNum() == nTeam)
			continue;
		
		Vec3 vDelta = pOther->m_vecOrigin() - vPos;
		if (vDelta.LengthSqr() < flRadiusSqr)
			nCount++;
	}
	
	return nCount;
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
	behavior.m_vRecentHealthPct.push_front(flHealthPct);
	if (behavior.m_vRecentHealthPct.size() > 20)
		behavior.m_vRecentHealthPct.pop_back();
	
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
	float flDot = vVelocity.Dot(vToLocal);  // Positive = toward enemy
	
	// Learn low health behavior
	if (bLowHealth)
	{
		if (flDot < -0.2f)  // Retreating
			behavior.m_nLowHPRetreatSamples++;
		else if (flDot > 0.2f)  // Still fighting
			behavior.m_nLowHPFightSamples++;
		
		int nTotal = behavior.m_nLowHPRetreatSamples + behavior.m_nLowHPFightSamples;
		if (nTotal > 5)
		{
			float flRate = static_cast<float>(behavior.m_nLowHPRetreatSamples) / static_cast<float>(nTotal);
			behavior.m_flLowHealthRetreatRate = behavior.m_flLowHealthRetreatRate * 0.9f + flRate * 0.1f;
		}
	}
	
	// Learn healed behavior
	if (bBeingHealed)
	{
		if (flDot > 0.2f)  // Pushing while healed
			behavior.m_nHealedPushSamples++;
		else
			behavior.m_nHealedPassiveSamples++;
		
		int nTotal = behavior.m_nHealedPushSamples + behavior.m_nHealedPassiveSamples;
		if (nTotal > 5)
		{
			float flRate = static_cast<float>(behavior.m_nHealedPushSamples) / static_cast<float>(nTotal);
			behavior.m_flHealedAggroBoost = behavior.m_flHealedAggroBoost * 0.9f + flRate * 0.1f;
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
	
	int nNearbyTeammates = CountNearbyTeammates(pPlayer, 500.0f);  // Within 500 units
	bool bNearTeam = nNearbyTeammates >= 2;
	bool bAlone = nNearbyTeammates == 0;
	
	if (bNearTeam)
		behavior.m_nNearTeamSamples++;
	if (bAlone)
		behavior.m_nAloneSamples++;
	
	int nTotal = behavior.m_nNearTeamSamples + behavior.m_nAloneSamples;
	if (nTotal > 10)
	{
		behavior.m_flTeamProximityRate = static_cast<float>(behavior.m_nNearTeamSamples) / static_cast<float>(nTotal);
		behavior.m_flSoloPlayRate = static_cast<float>(behavior.m_nAloneSamples) / static_cast<float>(nTotal);
	}
	
	// Learn outnumbered/advantage behavior
	int nNearbyEnemies = CountNearbyEnemies(pPlayer, 600.0f);
	bool bOutnumbered = nNearbyEnemies > nNearbyTeammates + 1;
	bool bAdvantage = nNearbyTeammates > nNearbyEnemies + 1;
	
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
	
	// Track behavior when outnumbered
	if (bOutnumbered && flDot < -0.2f)
	{
		behavior.m_flRetreatWhenOutnumbered = behavior.m_flRetreatWhenOutnumbered * 0.95f + 0.05f;
	}
	
	// Track behavior when advantage
	if (bAdvantage && flDot > 0.2f)
	{
		behavior.m_flPushWhenAdvantage = behavior.m_flPushWhenAdvantage * 0.95f + 0.05f;
	}
}

// Learn objective behavior (payload cart proximity)
void CMovementSimulation::LearnObjectiveBehavior(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	// Use the cached cart finder
	C_BaseEntity* pCart = FindPayloadCart();
	if (!pCart)
		return;
	
	Vec3 vPlayerPos = pPlayer->m_vecOrigin();
	Vec3 vCartPos = pCart->GetAbsOrigin();
	float flDistToCart = (vCartPos - vPlayerPos).Length();
	
	// Near cart = within 300 units
	bool bNearCart = flDistToCart < 300.0f;
	
	if (bNearCart)
		behavior.m_nNearCartSamples++;
	
	// Calculate cart proximity rate
	if (behavior.m_nSampleCount > 10)
	{
		float flRate = static_cast<float>(behavior.m_nNearCartSamples) / static_cast<float>(behavior.m_nSampleCount);
		behavior.m_flCartProximityRate = behavior.m_flCartProximityRate * 0.95f + flRate * 0.05f;
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
// WEAPON AWARENESS - Track when players are attacking
// Players are more predictable when shooting (especially snipers, heavies)
// ============================================================================
void CMovementSimulation::LearnWeaponAwareness(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	
	// Check if player is currently attacking
	// Also check animation activity for more reliable detection
	bool bIsAttacking = false;
	
	// Check weapon activity - more reliable than buttons
	auto hWeapon = pPlayer->m_hActiveWeapon();
	auto pWeapon = I::ClientEntityList ? 
		reinterpret_cast<C_TFWeaponBase*>(I::ClientEntityList->GetClientEntityFromHandle(hWeapon)) : nullptr;
	
	if (pWeapon)
	{
		// Check if weapon is in firing sequence
		float flNextPrimaryAttack = pWeapon->m_flNextPrimaryAttack();
		
		// If next attack time is in the future and close, they just fired
		if (flNextPrimaryAttack > flCurTime && flNextPrimaryAttack < flCurTime + 1.0f)
		{
			bIsAttacking = true;
		}
	}
	
	// Also check conditions that indicate attacking
	if (pPlayer->InCond(TF_COND_AIMING))  // Sniper scoped, heavy revved
		bIsAttacking = true;
	if (pPlayer->InCond(TF_COND_TAUNTING))  // Not attacking, but stationary
		bIsAttacking = false;
	
	// Update attack state
	behavior.m_bIsAttacking = bIsAttacking;
	
	if (bIsAttacking)
	{
		behavior.m_flLastAttackTime = flCurTime;
		behavior.m_nAttackingSamples++;
		
		// Check if they're standing still while attacking
		float flSpeed = pPlayer->m_vecVelocity().Length2D();
		if (flSpeed < 30.0f)
			behavior.m_nAttackingStillSamples++;
		else
			behavior.m_nAttackingMovingSamples++;
		
		// Calculate attack predictability (how often they stand still when shooting)
		if (behavior.m_nAttackingSamples > 5)
		{
			float flStillRate = static_cast<float>(behavior.m_nAttackingStillSamples) / 
			                    static_cast<float>(behavior.m_nAttackingSamples);
			behavior.m_flAttackPredictability = behavior.m_flAttackPredictability * 0.9f + flStillRate * 0.1f;
		}
	}
}

// ============================================================================
// REACTION DETECTION - Track how players react when shot at
// Call OnShotFired() when you fire a projectile at someone
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
	
	auto& behavior = m_mPlayerBehaviors[nTargetEntIndex];
	
	// Record when and where we shot at them
	behavior.m_flLastShotAtTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	behavior.m_vPosWhenShotAt = pPlayer->m_vecOrigin();
}

void CMovementSimulation::LearnReactionPattern(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	float flCurTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	
	// Only analyze if we recently shot at them (within 1 second)
	float flTimeSinceShot = flCurTime - behavior.m_flLastShotAtTime;
	if (flTimeSinceShot <= 0.0f || flTimeSinceShot > 1.0f)
		return;
	
	// Need valid shot position
	if (behavior.m_vPosWhenShotAt.IsZero())
		return;
	
	// Class-based reaction timing windows - faster classes react quicker
	float flMinReactionTime = 0.1f;
	float flMaxReactionTime = 0.5f;
	
	switch (behavior.m_nPlayerClass)
	{
	case TF_CLASS_SCOUT:
		flMinReactionTime = 0.08f;
		flMaxReactionTime = 0.35f;
		break;
	case TF_CLASS_SOLDIER:
	case TF_CLASS_DEMOMAN:
		flMinReactionTime = 0.1f;
		flMaxReactionTime = 0.45f;
		break;
	case TF_CLASS_HEAVY:
		flMinReactionTime = 0.15f;
		flMaxReactionTime = 0.6f;
		break;
	case TF_CLASS_SNIPER:
		flMinReactionTime = 0.12f;
		flMaxReactionTime = 0.5f;
		break;
	}
	
	// Only analyze once per shot (within class-specific reaction window)
	if (flTimeSinceShot < flMinReactionTime || flTimeSinceShot > flMaxReactionTime)
		return;
	
	// Check if we already analyzed this shot (stored per-player in behavior)
	if (behavior.m_flLastAnalyzedShotTime == behavior.m_flLastShotAtTime)
		return;
	behavior.m_flLastAnalyzedShotTime = behavior.m_flLastShotAtTime;
	
	// Get local player for direction reference
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;
	
	Vec3 vCurPos = pPlayer->m_vecOrigin();
	Vec3 vOldPos = behavior.m_vPosWhenShotAt;
	Vec3 vDelta = vCurPos - vOldPos;
	
	// Calculate movement relative to local player's position
	Vec3 vToLocal = pLocal->m_vecOrigin() - vOldPos;
	vToLocal.z = 0;
	float flDistToLocal = vToLocal.Length();
	if (flDistToLocal < 50.0f)
		return;
	
	vToLocal = vToLocal * (1.0f / flDistToLocal);  // Normalize
	
	// Get perpendicular (left/right) direction
	Vec3 vRight = Vec3(-vToLocal.y, vToLocal.x, 0);
	
	// Analyze their movement
	float flForwardMove = vDelta.Dot(vToLocal);   // Positive = toward us, negative = away
	float flSideMove = vDelta.Dot(vRight);        // Positive = their right, negative = their left
	float flVertMove = vDelta.z;
	float flTotalMove = vDelta.Length2D();
	
	behavior.m_nReactionSamples++;
	
	// Classify reaction
	if (flTotalMove < 20.0f && fabsf(flVertMove) < 20.0f)
	{
		// Didn't move much - no reaction
		behavior.m_nNoReactionCount++;
	}
	else if (flVertMove > 40.0f)
	{
		// Jumped
		behavior.m_nDodgeJumpCount++;
	}
	else if (flForwardMove < -30.0f)
	{
		// Retreated
		behavior.m_nDodgeBackCount++;
	}
	else if (flSideMove > 30.0f)
	{
		// Dodged right (from their perspective, left from ours)
		behavior.m_nDodgeRightCount++;
	}
	else if (flSideMove < -30.0f)
	{
		// Dodged left
		behavior.m_nDodgeLeftCount++;
	}
	else
	{
		behavior.m_nNoReactionCount++;
	}
	
	// Update average reaction time
	behavior.m_flAvgReactionTime = behavior.m_flAvgReactionTime * 0.8f + flTimeSinceShot * 0.2f;
	
	// Update reaction to threat score
	int nTotalReactions = behavior.m_nDodgeLeftCount + behavior.m_nDodgeRightCount + 
	                      behavior.m_nDodgeJumpCount + behavior.m_nDodgeBackCount;
	int nTotalSamples = nTotalReactions + behavior.m_nNoReactionCount;
	
	if (nTotalSamples > 3)
	{
		float flReactionRate = static_cast<float>(nTotalReactions) / static_cast<float>(nTotalSamples);
		behavior.m_flReactionToThreat = behavior.m_flReactionToThreat * 0.9f + flReactionRate * 0.1f;
	}
}



// ============================================================================
// BUNNY HOP DETECTION - Track if player frequently jumps to maintain speed
// Bhoppers are harder to hit because they're unpredictable in the air
// ============================================================================
void CMovementSimulation::LearnBunnyHop(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	bool bOnGround = (pPlayer->m_fFlags() & FL_ONGROUND) != 0;
	float flSpeed = pPlayer->m_vecVelocity().Length2D();
	
	// Track air vs ground time
	if (bOnGround)
	{
		behavior.m_nGroundSamples++;
		
		// Detect bhop: they were in air, now on ground, and still moving fast
		// Real bhoppers land and immediately jump again
		if (!behavior.m_bWasOnGround && behavior.m_nConsecutiveAirTicks > 3 && flSpeed > 200.0f)
		{
			behavior.m_nBunnyHopSamples++;
		}
		
		behavior.m_nConsecutiveAirTicks = 0;
	}
	else
	{
		behavior.m_nAirSamples++;
		behavior.m_nConsecutiveAirTicks++;
	}
	
	behavior.m_bWasOnGround = bOnGround;
	
	// Calculate bhop rate
	int nTotalSamples = behavior.m_nAirSamples + behavior.m_nGroundSamples;
	if (nTotalSamples > 50)
	{
		// Bhop rate = how often they're in the air with high speed
		float flAirRate = static_cast<float>(behavior.m_nAirSamples) / static_cast<float>(nTotalSamples);
		
		// If they're in the air a lot (>40%) and have bhop samples, they're a bhopper
		if (flAirRate > 0.4f && behavior.m_nBunnyHopSamples > 3)
		{
			float flBhopRate = static_cast<float>(behavior.m_nBunnyHopSamples) / 
			                   static_cast<float>(behavior.m_nGroundSamples + 1);
			behavior.m_flBunnyHopRate = behavior.m_flBunnyHopRate * 0.95f + 
			                            std::min(flBhopRate, 1.0f) * 0.05f;
		}
	}
}

// ============================================================================
// CORNER PEEK DETECTION - Track if player peeks corners then retreats
// Peekers are predictable - they'll retreat after peeking
// ============================================================================
void CMovementSimulation::LearnCornerPeek(C_TFPlayer* pPlayer, PlayerBehavior& behavior)
{
	if (!pPlayer)
		return;
	
	// Need position history
	if (behavior.m_vRecentPositions.size() < 10)
		return;
	
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pPlayer == pLocal)
		return;
	
	Vec3 vLocalPos = pLocal->m_vecOrigin();
	Vec3 vCurPos = pPlayer->m_vecOrigin();
	
	// Get positions from ~0.3 and ~0.5 seconds ago
	size_t nIdx1 = std::min(size_t(10), behavior.m_vRecentPositions.size() - 1);
	size_t nIdx2 = std::min(size_t(20), behavior.m_vRecentPositions.size() - 1);
	
	if (nIdx2 >= behavior.m_vRecentPositions.size())
		return;
	
	Vec3 vPos1 = behavior.m_vRecentPositions[nIdx1];  // Recent
	Vec3 vPos2 = behavior.m_vRecentPositions[nIdx2];  // Older
	
	// Calculate distances to local player
	float flDistNow = (vCurPos - vLocalPos).Length2D();
	float flDist1 = (vPos1 - vLocalPos).Length2D();
	float flDist2 = (vPos2 - vLocalPos).Length2D();
	
	// Corner peek pattern: got closer, then retreated
	// Old -> Recent: got closer (dist decreased)
	// Recent -> Now: retreated (dist increased)
	bool bApproached = flDist1 < flDist2 - 50.0f;  // Got at least 50 units closer
	bool bRetreated = flDistNow > flDist1 + 50.0f;  // Then retreated at least 50 units
	
	if (bApproached && bRetreated)
	{
		behavior.m_nCornerPeekSamples++;
	}
	
	// Calculate corner peek rate
	if (behavior.m_nSampleCount > 30)
	{
		float flPeekRate = static_cast<float>(behavior.m_nCornerPeekSamples) / 
		                   static_cast<float>(behavior.m_nSampleCount / 10);  // Normalize
		behavior.m_flCornerPeekRate = behavior.m_flCornerPeekRate * 0.95f + 
		                              std::min(flPeekRate, 1.0f) * 0.05f;
	}
}
