#include "MoveSim.h"

#include "../CFG.h"

#include <algorithm>
#include <numeric>

// Amalgam's tuning values (Vars.h NOSAVE debug-var defaults). Hardcoded on
// purpose: Necromancer's CFG entries for these were tuned for the old native
// simulator and differ from upstream, and wiring them live would let menu
// state desync this port from Amalgam's behavior.
namespace
{
	constexpr int MOVE_SIM_STRAFE_PREDICTION = 0b11;          // Air | Ground
	constexpr int MOVE_SIM_FRICTION_FLAGS = 0b01;             // RunReduce (CalculateIncrease = 0b10, off in Amalgam)

	constexpr int MOVE_SIM_GROUND_SAMPLES = 33;
	constexpr float MOVE_SIM_GROUND_STRAIGHT_FUZZY = 100.f;
	constexpr float MOVE_SIM_GROUND_LOW_MIN_SAMPLES = 16.f;
	constexpr float MOVE_SIM_GROUND_HIGH_MIN_SAMPLES = 33.f;
	constexpr float MOVE_SIM_GROUND_LOW_MIN_DISTANCE = 0.f;
	constexpr float MOVE_SIM_GROUND_HIGH_MIN_DISTANCE = 1000.f;
	constexpr int MOVE_SIM_GROUND_MAX_CHANGES = 0;
	constexpr int MOVE_SIM_GROUND_MAX_CHANGE_TIME = 0;

	constexpr int MOVE_SIM_AIR_SAMPLES = 33;
	constexpr float MOVE_SIM_AIR_STRAIGHT_FUZZY = 0.f;
	constexpr float MOVE_SIM_AIR_LOW_MIN_SAMPLES = 16.f;
	constexpr float MOVE_SIM_AIR_HIGH_MIN_SAMPLES = 16.f;
	constexpr float MOVE_SIM_AIR_LOW_MIN_DISTANCE = 100000.f;
	constexpr float MOVE_SIM_AIR_HIGH_MIN_DISTANCE = 100000.f;
	constexpr int MOVE_SIM_AIR_MAX_CHANGES = 2;
	constexpr int MOVE_SIM_AIR_MAX_CHANGE_TIME = 16;

	constexpr int MOVE_SIM_DELTA_COUNT = 5;
	constexpr int MOVE_SIM_DELTA_MODE = 0;                    // 0 = Average, 1 = Max

	constexpr size_t MOVE_SIM_MAX_RECORDS = 66;
	const Vec3 MOVE_SIM_ORIGIN_COMPRESSION = { 0.125f, 0.125f, 0.125f };

	// 11-04 capture-dataset tuning (see necromancer-movesim-feedback-dataset memory):
	// the flat long-window average extrapolates a constant turn rate and diverges on
	// smooth micro-turning (validated offline: mean miss 30.9u -> 19.0u on 64 captures)
	constexpr float MOVE_SIM_DECAY_TICKS = 26.4f;     // 0.4s: extrapolated turn fades to straight over this
	constexpr float MOVE_SIM_GROUND_MAX_YAW = 15.f;   // sanity clamp deg/tick (ground turns are eye-yaw driven)
	constexpr float MOVE_SIM_AIR_MAX_YAW_SPEED = 30.f; // engine air speed cap: max turn = cap/speed rad per tick
	constexpr float MOVE_SIM_STALE_VEL_2D = 10.f;     // below this the netvar velocity is treated as stale while air-ducked

	CUserCmd g_DummyCmd = {};

	int GetMoveMode(C_TFPlayer* pPlayer)
	{
		if (IsSwimming(pPlayer))
			return MoveSimMoveEnum::Swim;

		if (IsOnGround(pPlayer))
			return MoveSimMoveEnum::Ground;

		return MoveSimMoveEnum::Air;
	}

	void HandleMovement(C_TFPlayer* pPlayer, MoveSimRecord* pLastRecord, MoveSimRecord& tCurRecord, std::deque<MoveSimRecord>& vRecords)
	{
		const bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();

		if (pLastRecord && !pPlayer->IsDormant())
		{
			// does this eat up fps? i can't tell currently (amalgam)
			CGameTrace trace = {};
			CTraceFilterWorldAndProps filter = {};
			filter.pSkip = pPlayer;

			SDKUtils::TraceHull(pLastRecord->m_vOrigin, pLastRecord->m_vOrigin + pLastRecord->m_vVelocity * TICK_INTERVAL,
				pPlayer->m_vecMins() + MOVE_SIM_ORIGIN_COMPRESSION, pPlayer->m_vecMaxs() - MOVE_SIM_ORIGIN_COMPRESSION,
				SolidMask(pPlayer), &filter, &trace);
			if (trace.DidHit() && trace.plane.normal.z < 0.707f)
			{
				pLastRecord = nullptr;
				vRecords.resize(1);
			}
		}

		if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			g_DummyCmd.forwardmove = 520.f;
			g_DummyCmd.sidemove = 0.f;
			SDKUtils::FixMovement(&g_DummyCmd, bLocal && G::CurrentUserCmd ? G::CurrentUserCmd->viewangles : pPlayer->GetEyeAngles(), {});
			tCurRecord.m_vDirection.x = g_DummyCmd.forwardmove;
			tCurRecord.m_vDirection.y = -g_DummyCmd.sidemove;
			return;
		}

		tCurRecord.m_bInputDirection = bLocal;
		switch (tCurRecord.m_iMode)
		{
		case MoveSimMoveEnum::Ground:
			if (!bLocal)
				break;
			else if (CFG::Misc_Bunnyhop && G::OriginalCmd.buttons & IN_JUMP)
				tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized2D() * 520.f, tCurRecord.m_bInputDirection = false;
			break;
		case MoveSimMoveEnum::Air:
			if (!bLocal)
				break;
			else if (tCurRecord.m_vDirection.To2D().IsZero() || pLastRecord && tCurRecord.m_vVelocity.To2D() == pLastRecord->m_vVelocity.To2D())
				tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized2D() * 520.f, tCurRecord.m_bInputDirection = false;
			break;
		case MoveSimMoveEnum::Swim:
			if (!bLocal)
				tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized() * 520.f, tCurRecord.m_bInputDirection = false;
			break;
		}
	}

	float GetFrictionScale(float flVelocityXY, float flTurn, float flVelocityZ, float flMin = 50.f, float flMax = 150.f)
	{
		if (0.f >= flVelocityZ || flVelocityZ > 250.f)
			return 1.f;

		static ConVar* sv_airaccelerate = I::CVar->FindVar("sv_airaccelerate");
		const float flScale = std::max(sv_airaccelerate ? sv_airaccelerate->GetFloat() : 1.f, 1.f);
		flMin *= flScale, flMax *= flScale;

		// entity friction will be 0.25f if velocity is between 0.f and 250.f
		return Math::RemapVal(fabsf(flVelocityXY * flTurn), flMin, flMax, 1.f, 0.25f);
	}

	bool GetYawDifference(MoveSimRecord& tRecord1, MoveSimRecord& tRecord2, bool bStart, float* pYaw, float flStraightFuzzyValue, int iMaxChanges = 0, int iMaxChangeTime = 0, float flMaxSpeed = 0.f)
	{
		const float flYaw1 = Math::VectorAngles(tRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(tRecord2.m_vDirection).y;
		const float flTime1 = tRecord1.m_flSimTime, flTime2 = tRecord2.m_flSimTime;
		const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);

		*pYaw = Math::NormalizeAngle(flYaw1 - flYaw2);
		if (flMaxSpeed && tRecord1.m_iMode != MoveSimMoveEnum::Air)
			*pYaw *= std::clamp(tRecord1.m_vVelocity.Length2D() / flMaxSpeed, 0.f, 1.f);
		if (MOVE_SIM_FRICTION_FLAGS & 0b10 && tRecord1.m_iMode == MoveSimMoveEnum::Air)
			*pYaw /= GetFrictionScale(tRecord1.m_vVelocity.Length2D(), *pYaw, tRecord1.m_vVelocity.z + SDKUtils::GetGravity() * TICK_INTERVAL, 0.f, 56.f);
		if (fabsf(*pYaw) > 45.f)
			return false;

		static int iChanges, iStart;

		static int iStaticSign = 0;
		const int iLastSign = iStaticSign;
		const int iCurrSign = iStaticSign = *pYaw ? sign(*pYaw) : iStaticSign;

		static bool bStaticZero = false;
		const bool iLastZero = bStaticZero;
		const bool iCurrZero = bStaticZero = !*pYaw;

		const bool bChanged = iCurrSign != iLastSign || iCurrZero && iLastZero;
		const bool bStraight = fabsf(*pYaw) * tRecord1.m_vVelocity.Length2D() * iTicks < flStraightFuzzyValue; // dumb way to get straight bool

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
}

void CMoveSim::StoreState(MoveSimStorage& tMoveSimStorage)
{
	if (!tMoveSimStorage.m_pPlayer || !I::MemAlloc)
		return;

	auto pMap = GetPredDescMap(tMoveSimStorage.m_pPlayer);
	if (!pMap)
		return;

	const size_t iSize = GetIntermediateDataSize(tMoveSimStorage.m_pPlayer);
	if (!iSize)
		return;

	tMoveSimStorage.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Alloc(iSize));

	// Amalgam uses PC_NETWORKED_ONLY here (the native sim uses PC_EVERYTHING).
	CPredictionCopy copy(PC_NETWORKED_ONLY, tMoveSimStorage.m_pData, PC_DATA_PACKED, tMoveSimStorage.m_pPlayer, PC_DATA_NORMAL);
	copy.TransferData("MoveSimStore", tMoveSimStorage.m_pPlayer->entindex(), pMap);
}

void CMoveSim::ResetState(MoveSimStorage& tMoveSimStorage)
{
	if (!tMoveSimStorage.m_pData)
		return;

	if (tMoveSimStorage.m_pPlayer)
	{
		if (auto pMap = GetPredDescMap(tMoveSimStorage.m_pPlayer))
		{
			CPredictionCopy copy(PC_NETWORKED_ONLY, tMoveSimStorage.m_pPlayer, PC_DATA_NORMAL, tMoveSimStorage.m_pData, PC_DATA_PACKED);
			copy.TransferData("MoveSimReset", tMoveSimStorage.m_pPlayer->entindex(), pMap);
		}
	}

	if (I::MemAlloc)
		I::MemAlloc->Free(tMoveSimStorage.m_pData);
	tMoveSimStorage.m_pData = nullptr;
}

void CMoveSim::Store()
{
	if (!I::EngineClient || !I::EngineClient->IsInGame() || I::EngineClient->IsPlayingDemo() || !I::GlobalVars)
		return;

	const auto& vPlayers = g_EntityMotionExt.GetGroup(EntityEnum::PlayerAll);

	// Feed the per-entity sim-time deltas and chokes (Amalgam's H::Entities
	// arrays, updated in the same frame stage before MoveSim.Store()).
	{
		static int iStaticTickcount = I::GlobalVars->tickcount;
		const int iLag = I::GlobalVars->tickcount - iStaticTickcount - 1;
		iStaticTickcount = I::GlobalVars->tickcount;

		for (C_BaseEntity* pEntity : vPlayers)
		{
			auto pPlayer = pEntity->As<C_TFPlayer>();
			const int n = pPlayer->entindex();
			if (n <= 0 || n >= 102 || n == I::EngineClient->GetLocalPlayer())
				continue;

			const float flOldSimTime = m_aSimTimes[n];
			const float flSimTime = m_aSimTimes[n] = pPlayer->m_flSimulationTime();
			if (flSimTime > flOldSimTime)
			{
				m_aDeltaTimes[n] = TICKS_TO_TIME(std::clamp(TIME_TO_TICKS(flSimTime - flOldSimTime) - iLag, 1, 24));
				m_aSetTicks[n] = I::GlobalVars->tickcount;
			}
			else
				m_aDeltaTimes[n] = 0.f;

			if (!pPlayer->IsDormant())
			{
				m_aChokes[n] = I::GlobalVars->tickcount - m_aSetTicks[n];
				m_aLastSeenTimes[n] = I::GlobalVars->curtime;
			}
			else if (m_aLastSeenTimes[n] > 0.f)
				m_aChokes[n] = TIME_TO_TICKS(I::GlobalVars->curtime - m_aLastSeenTimes[n]);
		}
	}

	for (C_BaseEntity* pEntity : vPlayers)
	{
		auto pPlayer = pEntity->As<C_TFPlayer>();
		auto& vRecords = m_mRecords[pPlayer->entindex()];
		MoveSimRecord* pLastRecord = !vRecords.empty() ? &vRecords.front() : nullptr;

		Vec3 vVelocity = pPlayer->m_vecVelocity();
		const Vec3 vOrigin = pPlayer->m_vecOrigin();

		// air-duck rocket jumps: the velocity netvar reads zero while ducked
		// airborne (capture dataset 11-04) - rebuild it from recent origin
		// movement instead of wiping the whole record history every tick
		if (vVelocity.IsZero() && !pPlayer->IsDormant() && !IsOnGround(pPlayer) && pPlayer->m_bDucked() && pLastRecord)
		{
			const int iTicks = std::max(TIME_TO_TICKS(pPlayer->m_flSimulationTime() - pLastRecord->m_flSimTime), 1);
			const Vec3 vRebuilt = (vOrigin - pLastRecord->m_vOrigin) * (1.0f / (iTicks * TICK_INTERVAL));
			if (vRebuilt.Length2D() >= MOVE_SIM_STALE_VEL_2D)
				vVelocity = vRebuilt;
		}

		if (!pPlayer->IsAlive() || IsAGhost(pPlayer) || vVelocity.IsZero())
		{
			vRecords.clear();
			continue;
		}
		else if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer() || !GetDeltaTime(pPlayer->entindex()) && !pPlayer->IsDormant())
			continue;

		Vec3 vDirection = vVelocity.To2D();

		vRecords.emplace_front(vDirection, pPlayer->m_flSimulationTime(), GetMoveMode(pPlayer), vVelocity, vOrigin);
		MoveSimRecord& tCurRecord = vRecords.front();
		if (vRecords.size() > MOVE_SIM_MAX_RECORDS)
			vRecords.pop_back();

		HandleMovement(pPlayer, pLastRecord, tCurRecord, vRecords);
	}

	for (C_BaseEntity* pEntity : vPlayers)
	{
		auto pPlayer = pEntity->As<C_TFPlayer>();
		auto& vSimTimes = m_mSimTimes[pPlayer->entindex()];
		if (pEntity->entindex() == I::EngineClient->GetLocalPlayer() || !pPlayer->IsAlive() || IsAGhost(pPlayer) || pPlayer->IsDormant())
		{
			vSimTimes.clear();
			continue;
		}

		const float flDeltaTime = GetDeltaTime(pPlayer->entindex());
		if (!flDeltaTime)
			continue;

		vSimTimes.push_front(flDeltaTime);
		if (vSimTimes.size() > MOVE_SIM_DELTA_COUNT)
			vSimTimes.pop_back();
	}
}

bool CMoveSim::Initialize(C_BaseEntity* pEntity, MoveSimStorage& tMoveSimStorage, bool bHitchance, bool bStrafe, bool bPredict)
{
	if (!pEntity || !IsPlayer(pEntity) || !pEntity->As<C_TFPlayer>()->IsAlive())
	{
		tMoveSimStorage.m_bInitFailed = tMoveSimStorage.m_bFailed = true;
		return false;
	}

	auto pPlayer = pEntity->As<C_TFPlayer>();
	tMoveSimStorage.m_pPlayer = pPlayer;

	// store vars
	m_bOldInPrediction = I::Prediction->m_bInPrediction;
	m_bOldFirstTimePredicted = I::Prediction->m_bFirstTimePredicted;
	m_flOldFrametime = I::GlobalVars->frametime;

	// store restore data
	StoreState(tMoveSimStorage);

	// the hacks that make it work
	I::MoveHelper->SetHost(pPlayer);
	pPlayer->SetCurrentCommand(&g_DummyCmd);
	pPlayer->m_nPlayerState() = TF_STATE_ACTIVE;
	if (pPlayer->m_MoveType() == MOVETYPE_NONE || pPlayer->m_MoveType() == MOVETYPE_OBSERVER)
		pPlayer->m_MoveType() = MOVETYPE_WALK;

	if (auto pAvgVelocity = g_EntityMotionExt.GetAvgVelocity(pPlayer->entindex()))
		pPlayer->m_vecVelocity() = *pAvgVelocity; // only use average velocity here

	pPlayer->m_bDucked() = IsDucking(pPlayer) && !pPlayer->IsDormant();
	pPlayer->m_fFlags() &= ~FL_DUCKING; // breaks origin's z if FL_DUCKING is not removed
	pPlayer->m_flDucktime() = 0.f;
	pPlayer->m_flDuckJumpTime() = 0.f;
	pPlayer->m_bDucking() = false;
	pPlayer->m_bInDuckJump() = false;

	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		pPlayer->m_vecBaseVelocity() = Vec3(); // residual basevelocity causes issues
		if (IsOnGround(pPlayer) && !pPlayer->IsDormant())
			pPlayer->m_vecVelocity().z = std::min(pPlayer->m_vecVelocity().z, 0.f); // step fix
		else
			pPlayer->m_hGroundEntity() = nullptr; // fix for velocity.z being set to 0 even if in air
	}
	else if (CFG::Misc_Bunnyhop && G::OriginalCmd.buttons & IN_JUMP)
		tMoveSimStorage.m_bBunnyHop = true;

	// air-duck stale velocity: the avg-velocity ext reads zero along with the
	// netvar while ducked airborne, so derive the starting velocity from the
	// record history (kept alive by Store()'s stale-velocity handling)
	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer() && !IsOnGround(pPlayer) && pPlayer->m_bDucked()
		&& pPlayer->m_vecVelocity().To2D().Length2D() < MOVE_SIM_STALE_VEL_2D)
	{
		auto& vRecords = m_mRecords[pPlayer->entindex()];
		if (vRecords.size() >= 2)
		{
			const int iTicks = std::max(TIME_TO_TICKS(vRecords[0].m_flSimTime - vRecords[1].m_flSimTime), 1);
			const Vec3 vRebuilt = (vRecords[0].m_vOrigin - vRecords[1].m_vOrigin) * (1.0f / (iTicks * TICK_INTERVAL));
			if (vRebuilt.Length2D() >= MOVE_SIM_STALE_VEL_2D)
				pPlayer->m_vecVelocity() = vRebuilt;
		}
	}

	// setup move data
	SetupMoveData(tMoveSimStorage);
	if (CheckStuck(tMoveSimStorage))
	{
		tMoveSimStorage.m_bFailed = true;
		return true;
	}

	// calculate strafe if desired
	if (!StrafePrediction(tMoveSimStorage, bStrafe, bHitchance))
	{
		tMoveSimStorage.m_bFailed = true;
		return false;
	}

	tMoveSimStorage.m_vPath = { tMoveSimStorage.m_MoveData.m_vecAbsOrigin };
	if (bPredict)
	{
		for (int i = 0; i < GetChoke(pPlayer->entindex()); i++)
			RunTick(tMoveSimStorage);
	}

	return true;
}

void CMoveSim::SetupMoveData(MoveSimStorage& tMoveSimStorage)
{
	tMoveSimStorage.m_MoveData.m_bFirstRunOfFunctions = false;
	tMoveSimStorage.m_MoveData.m_bGameCodeMovedPlayer = false;
	tMoveSimStorage.m_MoveData.m_nPlayerHandle = tMoveSimStorage.m_pPlayer->GetRefEHandle();

	tMoveSimStorage.m_MoveData.m_vecAbsOrigin = tMoveSimStorage.m_pPlayer->m_vecOrigin();
	tMoveSimStorage.m_MoveData.m_vecVelocity = tMoveSimStorage.m_pPlayer->m_vecVelocity();
	tMoveSimStorage.m_MoveData.m_flMaxSpeed = SDKUtils::MaxSpeed(tMoveSimStorage.m_pPlayer);
	tMoveSimStorage.m_MoveData.m_flClientMaxSpeed = tMoveSimStorage.m_MoveData.m_flMaxSpeed;

	if (!tMoveSimStorage.m_MoveData.m_vecVelocity.To2D().IsZero())
	{
		const int iIndex = tMoveSimStorage.m_pPlayer->entindex();
		if (iIndex == I::EngineClient->GetLocalPlayer() && G::CurrentUserCmd)
			tMoveSimStorage.m_MoveData.m_vecViewAngles = G::CurrentUserCmd->viewangles;
		else
		{
			if (!tMoveSimStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
				tMoveSimStorage.m_MoveData.m_vecViewAngles = { 0.f, Math::VectorAngles(tMoveSimStorage.m_MoveData.m_vecVelocity).y, 0.f };
			else
				tMoveSimStorage.m_MoveData.m_vecViewAngles = g_EntityMotionExt.GetEyeAngles(iIndex);
		}

		const auto& vRecords = m_mRecords[iIndex];
		if (!vRecords.empty())
		{
			auto& tRecord = vRecords.front();
			if (!tRecord.m_vDirection.IsZero())
			{
				g_DummyCmd.forwardmove = tRecord.m_vDirection.x;
				g_DummyCmd.sidemove = -tRecord.m_vDirection.y;
				g_DummyCmd.upmove = tRecord.m_vDirection.z;
				SDKUtils::FixMovement(&g_DummyCmd, {}, tMoveSimStorage.m_MoveData.m_vecViewAngles);
				tMoveSimStorage.m_MoveData.m_flForwardMove = g_DummyCmd.forwardmove;
				tMoveSimStorage.m_MoveData.m_flSideMove = g_DummyCmd.sidemove;
				tMoveSimStorage.m_MoveData.m_flUpMove = g_DummyCmd.upmove;
			}
		}
	}

	tMoveSimStorage.m_MoveData.m_vecAngles = tMoveSimStorage.m_MoveData.m_vecOldAngles = tMoveSimStorage.m_MoveData.m_vecViewAngles;
	if (auto pConstraintEntity = tMoveSimStorage.m_pPlayer->m_hConstraintEntity().Get())
		tMoveSimStorage.m_MoveData.m_vecConstraintCenter = pConstraintEntity->m_vecOrigin();
	else
		tMoveSimStorage.m_MoveData.m_vecConstraintCenter = tMoveSimStorage.m_pPlayer->m_vecConstraintCenter();
	tMoveSimStorage.m_MoveData.m_flConstraintRadius = tMoveSimStorage.m_pPlayer->m_flConstraintRadius();
	tMoveSimStorage.m_MoveData.m_flConstraintWidth = tMoveSimStorage.m_pPlayer->m_flConstraintWidth();
	tMoveSimStorage.m_MoveData.m_flConstraintSpeedFactor = tMoveSimStorage.m_pPlayer->m_flConstraintSpeedFactor();

	tMoveSimStorage.m_flPredictedDelta = GetPredictedDelta(tMoveSimStorage.m_pPlayer);
	tMoveSimStorage.m_flSimTime = tMoveSimStorage.m_pPlayer->m_flSimulationTime();
	tMoveSimStorage.m_flPredictedSimTime = tMoveSimStorage.m_flSimTime + tMoveSimStorage.m_flPredictedDelta;
	tMoveSimStorage.m_vPredictedOrigin = tMoveSimStorage.m_MoveData.m_vecAbsOrigin;
	tMoveSimStorage.m_bDirectMove = IsOnGround(tMoveSimStorage.m_pPlayer) || IsSwimming(tMoveSimStorage.m_pPlayer);
}

bool CMoveSim::CheckStuck(MoveSimStorage& tMoveSimStorage)
{
	auto pGameMovement = reinterpret_cast<CTFGameMovementSim*>(I::GameMovement);
	pGameMovement->player = tMoveSimStorage.m_pPlayer;
	pGameMovement->m_pTFPlayer = tMoveSimStorage.m_pPlayer;
	pGameMovement->mv = &tMoveSimStorage.m_MoveData;
	SetBounds(tMoveSimStorage.m_pPlayer);
	const bool bReturn = pGameMovement->CheckStuck() != 0;
	RestoreBounds(tMoveSimStorage.m_pPlayer);
	return bReturn;
}

void CMoveSim::GetAverageYaw(MoveSimStorage& tMoveSimStorage, int iSamples)
{
	auto pPlayer = tMoveSimStorage.m_pPlayer;
	const bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();
	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.empty())
		return;

	bool bGround = tMoveSimStorage.m_bDirectMove;
	int iMinimumStrafes = 4;
	const float flMaxSpeed = SDKUtils::MaxSpeed(tMoveSimStorage.m_pPlayer, false, true);
	const float flLowMinimumDistance = bGround ? MOVE_SIM_GROUND_LOW_MIN_DISTANCE : MOVE_SIM_AIR_LOW_MIN_DISTANCE;
	const float flLowMinimumSamples = bGround ? MOVE_SIM_GROUND_LOW_MIN_SAMPLES : MOVE_SIM_AIR_LOW_MIN_SAMPLES;
	const float flHighMinimumDistance = bGround ? MOVE_SIM_GROUND_HIGH_MIN_DISTANCE : MOVE_SIM_AIR_HIGH_MIN_DISTANCE;
	const float flHighMinimumSamples = bGround ? MOVE_SIM_GROUND_HIGH_MIN_SAMPLES : MOVE_SIM_AIR_HIGH_MIN_SAMPLES;

	float flAverageYaw = 0.f;
	int iTicks = 0, iSkips = 0;
	iSamples = std::min(iSamples, int(vRecords.size()));
	size_t i = 1;
	for (; i < iSamples; i++)
	{
		auto& tRecord1 = vRecords[i - 1];
		auto& tRecord2 = vRecords[i];
		if (tRecord1.m_iMode != tRecord2.m_iMode)
		{
			iSkips++;
			continue;
		}

		bGround = tRecord1.m_iMode != MoveSimMoveEnum::Air;
		const float flStraightFuzzyValue = bGround ? MOVE_SIM_GROUND_STRAIGHT_FUZZY : MOVE_SIM_AIR_STRAIGHT_FUZZY;
		const int iMaxChanges = bLocal ? 0 : bGround ? MOVE_SIM_GROUND_MAX_CHANGES : MOVE_SIM_AIR_MAX_CHANGES;
		const int iMaxChangeTime = bLocal ? 0 : bGround ? MOVE_SIM_GROUND_MAX_CHANGE_TIME : MOVE_SIM_AIR_MAX_CHANGE_TIME;
		iMinimumStrafes = 4 + iMaxChanges;

		float flYaw = 0.f;
		const bool bResult = GetYawDifference(tRecord1, tRecord2, !iTicks, &flYaw, flStraightFuzzyValue, iMaxChanges, iMaxChangeTime, flMaxSpeed);
		if (!bResult)
			break;

		flAverageYaw += flYaw;
		iTicks += std::max(TIME_TO_TICKS(tRecord1.m_flSimTime - tRecord2.m_flSimTime), 1);
	}
	if (i <= size_t(iMinimumStrafes + iSkips)) // valid strafes not high enough
		return;

	float iMinimum = flLowMinimumSamples;
	if (!bLocal)
	{
		float flDistance = 0.f;
		if (auto pLocal = g_EntityMotionExt.GetLocal())
			flDistance = pLocal->m_vecOrigin().DistTo(tMoveSimStorage.m_pPlayer->m_vecOrigin());
		iMinimum = flDistance < flLowMinimumDistance ? flLowMinimumSamples : Math::RemapVal(flDistance, flLowMinimumDistance, flHighMinimumDistance, flLowMinimumSamples + 1, flHighMinimumSamples);
	}

	flAverageYaw /= std::max(float(iTicks), iMinimum);
	if (fabsf(flAverageYaw) < 0.36f)
		return;

	tMoveSimStorage.m_flAverageYaw = flAverageYaw;
}

bool CMoveSim::StrafePrediction(MoveSimStorage& tMoveSimStorage, bool bStrafe, bool bHitchance)
{
	// Amalgam gates this on its StrafePrediction var (Air = 0b01, Ground = 0b10);
	// our menu checkboxes carry the same split (defaults on, matching Amalgam)
	const int iStrafePrediction = (CFG::Aimbot_Projectile_Air_Strafe_Prediction ? 0b01 : 0)
	                            | (CFG::Aimbot_Projectile_Ground_Strafe_Prediction ? 0b10 : 0);
	if (bStrafe && iStrafePrediction & (tMoveSimStorage.m_bDirectMove ? 0b10 : 0b01))
	{
		const int iStrafeSamples = tMoveSimStorage.m_bDirectMove ? MOVE_SIM_GROUND_SAMPLES : MOVE_SIM_AIR_SAMPLES;

		GetAverageYaw(tMoveSimStorage, iStrafeSamples);

		// the recency estimate tracks smooth micro-turning that the flat
		// long-window average blends away; it overrides when it has enough
		// fresh deltas, otherwise the long estimate above stays.
		// Necromancer-only addition - Amalgam predicts with the long average
		// alone, so this is toggleable for exact upstream parity
		// really hope this doesn't work like shit (amalgam)
		if (bHitchance && !tMoveSimStorage.m_pPlayer->m_vecVelocity().IsZero() && CFG::Aimbot_Amalgam_Projectile_HitChance)
		{
			const auto& vRecords = m_mRecords[tMoveSimStorage.m_pPlayer->entindex()];
			const auto iSamples = vRecords.size();

			float flCurrentChance = 1.f, flAverageYaw = 0.f;
			for (size_t i = 0; i < iSamples; i++)
			{
				if (vRecords.size() <= i + 2)
					break;

				const auto& pRecord1 = vRecords[i], & pRecord2 = vRecords[i + 1];
				const float flYaw1 = Math::VectorAngles(pRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(pRecord2.m_vDirection).y;
				const float flTime1 = pRecord1.m_flSimTime, flTime2 = pRecord2.m_flSimTime;
				const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);

				float flYaw = Math::NormalizeAngle(flYaw1 - flYaw2) / iTicks;
				flAverageYaw += flYaw;
				if (tMoveSimStorage.m_MoveData.m_flMaxSpeed)
					flYaw *= std::clamp(pRecord1.m_vVelocity.Length2D() / tMoveSimStorage.m_MoveData.m_flMaxSpeed, 0.f, 1.f);

				if ((i + 1) % iStrafeSamples == 0 || i == iSamples - 1)
				{
					flAverageYaw /= i % iStrafeSamples + 1;
					if (fabsf(tMoveSimStorage.m_flAverageYaw - flAverageYaw) > 0.5f)
						flCurrentChance -= 1.f / ((iSamples - 1) / float(iStrafeSamples) + 1);
					flAverageYaw = 0.f;
				}
			}

			if (flCurrentChance < CFG::Aimbot_Amalgam_Projectile_HitChance / 100)
				return false;
		}
	}

	if (!tMoveSimStorage.m_bDirectMove)
	{
		if (!tMoveSimStorage.m_flAverageYaw)
			tMoveSimStorage.m_MoveData.m_flForwardMove = tMoveSimStorage.m_MoveData.m_flSideMove = 0.f;
		else
		{
			const auto& vRecords = m_mRecords[tMoveSimStorage.m_pPlayer->entindex()];
			if (!vRecords.empty())
			{
				auto& tRecord = vRecords.front();
				Vec3 vDirection = tRecord.m_vVelocity;
				if (!vDirection.IsZero() && tRecord.m_bInputDirection)
				{	// fix inputs for turning
					const int iSign = sign(tMoveSimStorage.m_flAverageYaw);
					const float flForward = tMoveSimStorage.m_MoveData.m_flForwardMove, flSide = tMoveSimStorage.m_MoveData.m_flSideMove;
					tMoveSimStorage.m_MoveData.m_flForwardMove = -flSide * iSign, tMoveSimStorage.m_MoveData.m_flSideMove = flForward * iSign;
				}
			}
		}
	}

	return true;
}

bool CMoveSim::SetDuck(MoveSimStorage& tMoveSimStorage, bool bDuck) // this only touches origin, bounds
{
	if (bDuck == tMoveSimStorage.m_pPlayer->m_bDucked())
		return true;

	auto pGameRules = I::TFGameRules();
	auto pViewVectors = pGameRules ? pGameRules->GetViewVectors() : nullptr;
	const float flScale = tMoveSimStorage.m_pPlayer->m_flModelScale();

	if (!IsOnGround(tMoveSimStorage.m_pPlayer))
	{
		const Vec3 vHullMins = (pViewVectors ? pViewVectors->m_vHullMin : Vec3(-24, -24, 0)) * flScale;
		const Vec3 vHullMaxs = (pViewVectors ? pViewVectors->m_vHullMax : Vec3(24, 24, 82)) * flScale;
		const Vec3 vDuckHullMins = (pViewVectors ? pViewVectors->m_vDuckHullMin : Vec3(-24, -24, 0)) * flScale;
		const Vec3 vDuckHullMaxs = (pViewVectors ? pViewVectors->m_vDuckHullMax : Vec3(24, 24, 62)) * flScale;

		if (bDuck)
			tMoveSimStorage.m_MoveData.m_vecAbsOrigin += (vHullMaxs - vHullMins) - (vDuckHullMaxs - vDuckHullMins);
		else
		{
			const Vec3 vOrigin = tMoveSimStorage.m_MoveData.m_vecAbsOrigin - ((vHullMaxs - vHullMins) - (vDuckHullMaxs - vDuckHullMins));

			CGameTrace trace = {};
			CTraceFilterWorldAndProps filter = {};
			SDKUtils::TraceHull(vOrigin, vOrigin, vHullMins, vHullMaxs, SolidMask(tMoveSimStorage.m_pPlayer), &filter, &trace);
			if (trace.DidHit())
				return false;

			tMoveSimStorage.m_MoveData.m_vecAbsOrigin = vOrigin;
		}
	}
	tMoveSimStorage.m_pPlayer->m_bDucked() = bDuck;

	return true;
}

void CMoveSim::SetBounds(C_TFPlayer* pPlayer)
{
	if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
		return;

	// fixes issues with origin compression
	if (auto pGameRules = I::TFGameRules())
	{
		if (auto pViewVectors = pGameRules->GetViewVectors())
		{
			pViewVectors->m_vHullMin = Vec3(-24, -24, 0) + MOVE_SIM_ORIGIN_COMPRESSION;
			pViewVectors->m_vHullMax = Vec3(24, 24, 82) - MOVE_SIM_ORIGIN_COMPRESSION;
			pViewVectors->m_vDuckHullMin = Vec3(-24, -24, 0) + MOVE_SIM_ORIGIN_COMPRESSION;
			pViewVectors->m_vDuckHullMax = Vec3(24, 24, 62) - MOVE_SIM_ORIGIN_COMPRESSION;
		}
	}
}

void CMoveSim::RestoreBounds(C_TFPlayer* pPlayer)
{
	if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
		return;

	if (auto pGameRules = I::TFGameRules())
	{
		if (auto pViewVectors = pGameRules->GetViewVectors())
		{
			pViewVectors->m_vHullMin = Vec3(-24, -24, 0);
			pViewVectors->m_vHullMax = Vec3(24, 24, 82);
			pViewVectors->m_vDuckHullMin = Vec3(-24, -24, 0);
			pViewVectors->m_vDuckHullMax = Vec3(24, 24, 62);
		}
	}
}

void CMoveSim::RunTick(MoveSimStorage& tMoveSimStorage, bool bPath, const MoveSimRunTickCallback* pCallback)
{
	if (tMoveSimStorage.m_bFailed || !tMoveSimStorage.m_pPlayer || !IsPlayer(tMoveSimStorage.m_pPlayer))
		return;

	// make sure frametime and prediction vars are right
	I::Prediction->m_bInPrediction = true;
	I::Prediction->m_bFirstTimePredicted = false;
	I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;
	SetBounds(tMoveSimStorage.m_pPlayer);

	if (tMoveSimStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE) && !tMoveSimStorage.m_bChargeExpired)
	{
		static auto tf_demoman_charge_drain_time = I::CVar->FindVar("tf_demoman_charge_drain_time");

		const float flDrainTime = tf_demoman_charge_drain_time
			? SDKUtils::AttribHookValue(tf_demoman_charge_drain_time->GetFloat(), "mod_charge_time", tMoveSimStorage.m_pPlayer)
			: 10.f;
		tMoveSimStorage.m_pPlayer->m_flChargeMeter() -= TICK_INTERVAL * 100.f / flDrainTime;

		if (tMoveSimStorage.m_pPlayer->m_flChargeMeter() <= 0.f)
		{
			// amalgam: RemoveCond(TF_COND_SHIELD_CHARGE) — Necromancer has no
			// RemoveCond, so the sim tracks the expiry via m_bChargeExpired and
			// refreshes max speed the same way.
			tMoveSimStorage.m_bChargeExpired = true;
			tMoveSimStorage.m_MoveData.m_flMaxSpeed = tMoveSimStorage.m_MoveData.m_flClientMaxSpeed = SDKUtils::MaxSpeed(tMoveSimStorage.m_pPlayer);
			tMoveSimStorage.m_pPlayer->m_flMaxspeed() = tMoveSimStorage.m_MoveData.m_flMaxSpeed;
		}
	}

	tMoveSimStorage.m_iSimTick++;

	float flCorrection = 0.f;
	if (tMoveSimStorage.m_flAverageYaw)
	{
		float flMult = 1.f;
		if (!tMoveSimStorage.m_bDirectMove && !(tMoveSimStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE) && !tMoveSimStorage.m_bChargeExpired))
		{
			flCorrection = 90.f * sign(tMoveSimStorage.m_flAverageYaw);
			if (MOVE_SIM_FRICTION_FLAGS & 0b01)
				flMult = GetFrictionScale(tMoveSimStorage.m_MoveData.m_vecVelocity.Length2D(), tMoveSimStorage.m_flAverageYaw, tMoveSimStorage.m_MoveData.m_vecVelocity.z + SDKUtils::GetGravity() * TICK_INTERVAL);
		}
		// humans straighten out: fade the extrapolated turn to straight over
		// MOVE_SIM_DECAY_TICKS (the +90 air wishdir alignment is input
		// construction, not turn rate, so it does not decay)
		const float flDecay = std::exp(-float(tMoveSimStorage.m_iSimTick) / MOVE_SIM_DECAY_TICKS);
		tMoveSimStorage.m_MoveData.m_vecViewAngles.y += tMoveSimStorage.m_flAverageYaw * flMult * flDecay + flCorrection;
	}

	const float flOldSpeed = tMoveSimStorage.m_MoveData.m_flClientMaxSpeed;
	if (tMoveSimStorage.m_pPlayer->m_bDucked() && IsOnGround(tMoveSimStorage.m_pPlayer) && !IsSwimming(tMoveSimStorage.m_pPlayer))
		tMoveSimStorage.m_MoveData.m_flClientMaxSpeed /= 3;

	if (tMoveSimStorage.m_bBunnyHop && IsOnGround(tMoveSimStorage.m_pPlayer) && !tMoveSimStorage.m_pPlayer->m_bDucked())
	{
		tMoveSimStorage.m_MoveData.m_nOldButtons = 0;
		tMoveSimStorage.m_MoveData.m_nButtons |= IN_JUMP;
	}

	I::GameMovement->ProcessMovement(tMoveSimStorage.m_pPlayer, &tMoveSimStorage.m_MoveData);
	if (pCallback)
		(*pCallback)(tMoveSimStorage.m_MoveData);

	tMoveSimStorage.m_MoveData.m_flClientMaxSpeed = flOldSpeed;

	tMoveSimStorage.m_flSimTime = ROUND_TO_TICKS(tMoveSimStorage.m_flSimTime + TICK_INTERVAL);
	tMoveSimStorage.m_bPredictNetworked = TIME_TO_TICKS(tMoveSimStorage.m_flSimTime) >= TIME_TO_TICKS(tMoveSimStorage.m_flPredictedSimTime);
	if (tMoveSimStorage.m_bPredictNetworked)
	{
		tMoveSimStorage.m_vPredictedOrigin = tMoveSimStorage.m_MoveData.m_vecAbsOrigin;
		tMoveSimStorage.m_flPredictedSimTime += tMoveSimStorage.m_flPredictedDelta;
	}
	const bool bLastbDirectMove = tMoveSimStorage.m_bDirectMove;
	tMoveSimStorage.m_bDirectMove = IsOnGround(tMoveSimStorage.m_pPlayer) || IsSwimming(tMoveSimStorage.m_pPlayer);

	if (tMoveSimStorage.m_flAverageYaw)
		tMoveSimStorage.m_MoveData.m_vecViewAngles.y -= flCorrection;

	if (tMoveSimStorage.m_bDirectMove && !bLastbDirectMove)
	{
		if (tMoveSimStorage.m_flAverageYaw)
			tMoveSimStorage.m_flAverageYaw *= -1;
		else if (!tMoveSimStorage.m_MoveData.m_flForwardMove && !tMoveSimStorage.m_MoveData.m_flSideMove
			&& tMoveSimStorage.m_MoveData.m_vecVelocity.Length2D() > tMoveSimStorage.m_MoveData.m_flMaxSpeed * 0.015f)
		{
			Vec3 vDirection = tMoveSimStorage.m_MoveData.m_vecVelocity.Normalized2D() * 520.f;
			g_DummyCmd.forwardmove = vDirection.x, g_DummyCmd.sidemove = -vDirection.y;
			SDKUtils::FixMovement(&g_DummyCmd, {}, tMoveSimStorage.m_MoveData.m_vecViewAngles);
			tMoveSimStorage.m_MoveData.m_flForwardMove = g_DummyCmd.forwardmove, tMoveSimStorage.m_MoveData.m_flSideMove = g_DummyCmd.sidemove;
		}
	}

	RestoreBounds(tMoveSimStorage.m_pPlayer);

	if (bPath)
		tMoveSimStorage.m_vPath.push_back(tMoveSimStorage.m_MoveData.m_vecAbsOrigin);
}

void CMoveSim::Restore(MoveSimStorage& tMoveSimStorage)
{
	if (tMoveSimStorage.m_bInitFailed || !tMoveSimStorage.m_pPlayer)
		return;

	if (I::MoveHelper)
		I::MoveHelper->SetHost(nullptr);
	tMoveSimStorage.m_pPlayer->SetCurrentCommand(nullptr);

	ResetState(tMoveSimStorage);

	if (I::Prediction)
	{
		I::Prediction->m_bInPrediction = m_bOldInPrediction;
		I::Prediction->m_bFirstTimePredicted = m_bOldFirstTimePredicted;
	}
	if (I::GlobalVars)
		I::GlobalVars->frametime = m_flOldFrametime;
}

float CMoveSim::GetPredictedDelta(C_BaseEntity* pEntity)
{
	auto& vSimTimes = m_mSimTimes[pEntity->entindex()];
	if (!vSimTimes.empty())
	{
		switch (MOVE_SIM_DELTA_MODE)
		{
		case 0: return std::accumulate(vSimTimes.begin(), vSimTimes.end(), 0.f) / vSimTimes.size();
		case 1: return *std::max_element(vSimTimes.begin(), vSimTimes.end());
		}
	}
	return TICK_INTERVAL;
}
