#include "MoveSimAmalgam.h"

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
	constexpr int MOVE_SIM_RECENT_SAMPLES = 8;        // newest records feeding the recency yaw estimate
	constexpr int MOVE_SIM_RECENT_MIN = 3;            // fewer valid deltas than this -> keep the long average
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
			CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
			filter.pSkip = pPlayer;

			SDK::TraceHull(pLastRecord->m_vOrigin, pLastRecord->m_vOrigin + pLastRecord->m_vVelocity * TICK_INTERVAL,
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
			SDK::FixMovement(&g_DummyCmd, bLocal && G::CurrentUserCmd ? G::CurrentUserCmd->viewangles : pPlayer->GetEyeAngles(), {});
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
			else if (Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
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

		static ConVar* sv_airaccelerate = U::ConVars.FindVar("sv_airaccelerate");
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

void CAmalgamMoveSim::StoreState(AmalgamMoveStorage& tMoveStorage)
{
	if (!tMoveStorage.m_pPlayer || !I::MemAlloc)
		return;

	auto pMap = GetPredDescMap(tMoveStorage.m_pPlayer);
	if (!pMap)
		return;

	const size_t iSize = GetIntermediateDataSize(tMoveStorage.m_pPlayer);
	if (!iSize)
		return;

	tMoveStorage.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Alloc(iSize));

	// Amalgam uses PC_NETWORKED_ONLY here (the native sim uses PC_EVERYTHING).
	CPredictionCopy copy(PC_NETWORKED_ONLY, tMoveStorage.m_pData, PC_DATA_PACKED, tMoveStorage.m_pPlayer, PC_DATA_NORMAL);
	copy.TransferData("AmalgamMoveSimStore", tMoveStorage.m_pPlayer->entindex(), pMap);
}

void CAmalgamMoveSim::ResetState(AmalgamMoveStorage& tMoveStorage)
{
	if (!tMoveStorage.m_pData)
		return;

	if (tMoveStorage.m_pPlayer)
	{
		if (auto pMap = GetPredDescMap(tMoveStorage.m_pPlayer))
		{
			CPredictionCopy copy(PC_NETWORKED_ONLY, tMoveStorage.m_pPlayer, PC_DATA_NORMAL, tMoveStorage.m_pData, PC_DATA_PACKED);
			copy.TransferData("AmalgamMoveSimReset", tMoveStorage.m_pPlayer->entindex(), pMap);
		}
	}

	if (I::MemAlloc)
		I::MemAlloc->Free(tMoveStorage.m_pData);
	tMoveStorage.m_pData = nullptr;
}

void CAmalgamMoveSim::Store()
{
	if (!I::EngineClient || !I::EngineClient->IsInGame() || I::EngineClient->IsPlayingDemo() || !I::GlobalVars)
		return;

	const auto& vPlayers = g_AmalgamEntitiesExt.GetGroup(EntityEnum::PlayerAll);

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

bool CAmalgamMoveSim::Initialize(C_BaseEntity* pEntity, AmalgamMoveStorage& tMoveStorage, bool bHitchance, bool bStrafe, bool bPredict)
{
	if (!pEntity || !IsPlayer(pEntity) || !pEntity->As<C_TFPlayer>()->IsAlive())
	{
		tMoveStorage.m_bInitFailed = tMoveStorage.m_bFailed = true;
		return false;
	}

	auto pPlayer = pEntity->As<C_TFPlayer>();
	tMoveStorage.m_pPlayer = pPlayer;

	// store vars
	m_bOldInPrediction = I::Prediction->m_bInPrediction;
	m_bOldFirstTimePredicted = I::Prediction->m_bFirstTimePredicted;
	m_flOldFrametime = I::GlobalVars->frametime;

	// store restore data
	StoreState(tMoveStorage);

	// the hacks that make it work
	I::MoveHelper->SetHost(pPlayer);
	pPlayer->SetCurrentCommand(&g_DummyCmd);
	pPlayer->m_nPlayerState() = TF_STATE_ACTIVE;
	if (pPlayer->m_MoveType() == MOVETYPE_NONE || pPlayer->m_MoveType() == MOVETYPE_OBSERVER)
		pPlayer->m_MoveType() = MOVETYPE_WALK;

	if (auto pAvgVelocity = g_AmalgamEntitiesExt.GetAvgVelocity(pPlayer->entindex()))
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
	else if (Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
		tMoveStorage.m_bBunnyHop = true;

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
	SetupMoveData(tMoveStorage);
	if (CheckStuck(tMoveStorage))
	{
		tMoveStorage.m_bFailed = true;
		return true;
	}

	// calculate strafe if desired
	if (!StrafePrediction(tMoveStorage, bStrafe, bHitchance))
	{
		tMoveStorage.m_bFailed = true;
		return false;
	}

	tMoveStorage.m_vPath = { tMoveStorage.m_MoveData.m_vecAbsOrigin };
	if (bPredict)
	{
		for (int i = 0; i < GetChoke(pPlayer->entindex()); i++)
			RunTick(tMoveStorage);
	}

	return true;
}

void CAmalgamMoveSim::SetupMoveData(AmalgamMoveStorage& tMoveStorage)
{
	tMoveStorage.m_MoveData.m_bFirstRunOfFunctions = false;
	tMoveStorage.m_MoveData.m_bGameCodeMovedPlayer = false;
	tMoveStorage.m_MoveData.m_nPlayerHandle = tMoveStorage.m_pPlayer->GetRefEHandle();

	tMoveStorage.m_MoveData.m_vecAbsOrigin = tMoveStorage.m_pPlayer->m_vecOrigin();
	tMoveStorage.m_MoveData.m_vecVelocity = tMoveStorage.m_pPlayer->m_vecVelocity();
	tMoveStorage.m_MoveData.m_flMaxSpeed = SDK::MaxSpeed(tMoveStorage.m_pPlayer);
	tMoveStorage.m_MoveData.m_flClientMaxSpeed = tMoveStorage.m_MoveData.m_flMaxSpeed;

	if (!tMoveStorage.m_MoveData.m_vecVelocity.To2D().IsZero())
	{
		const int iIndex = tMoveStorage.m_pPlayer->entindex();
		if (iIndex == I::EngineClient->GetLocalPlayer() && G::CurrentUserCmd)
			tMoveStorage.m_MoveData.m_vecViewAngles = G::CurrentUserCmd->viewangles;
		else
		{
			if (!tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
				tMoveStorage.m_MoveData.m_vecViewAngles = { 0.f, Math::VectorAngles(tMoveStorage.m_MoveData.m_vecVelocity).y, 0.f };
			else
				tMoveStorage.m_MoveData.m_vecViewAngles = g_AmalgamEntitiesExt.GetEyeAngles(iIndex);
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
				SDK::FixMovement(&g_DummyCmd, {}, tMoveStorage.m_MoveData.m_vecViewAngles);
				tMoveStorage.m_MoveData.m_flForwardMove = g_DummyCmd.forwardmove;
				tMoveStorage.m_MoveData.m_flSideMove = g_DummyCmd.sidemove;
				tMoveStorage.m_MoveData.m_flUpMove = g_DummyCmd.upmove;
			}
		}
	}

	tMoveStorage.m_MoveData.m_vecAngles = tMoveStorage.m_MoveData.m_vecOldAngles = tMoveStorage.m_MoveData.m_vecViewAngles;
	if (auto pConstraintEntity = tMoveStorage.m_pPlayer->m_hConstraintEntity().Get())
		tMoveStorage.m_MoveData.m_vecConstraintCenter = pConstraintEntity->m_vecOrigin();
	else
		tMoveStorage.m_MoveData.m_vecConstraintCenter = tMoveStorage.m_pPlayer->m_vecConstraintCenter();
	tMoveStorage.m_MoveData.m_flConstraintRadius = tMoveStorage.m_pPlayer->m_flConstraintRadius();
	tMoveStorage.m_MoveData.m_flConstraintWidth = tMoveStorage.m_pPlayer->m_flConstraintWidth();
	tMoveStorage.m_MoveData.m_flConstraintSpeedFactor = tMoveStorage.m_pPlayer->m_flConstraintSpeedFactor();

	tMoveStorage.m_flPredictedDelta = GetPredictedDelta(tMoveStorage.m_pPlayer);
	tMoveStorage.m_flSimTime = tMoveStorage.m_pPlayer->m_flSimulationTime();
	tMoveStorage.m_flPredictedSimTime = tMoveStorage.m_flSimTime + tMoveStorage.m_flPredictedDelta;
	tMoveStorage.m_vPredictedOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
	tMoveStorage.m_bDirectMove = IsOnGround(tMoveStorage.m_pPlayer) || IsSwimming(tMoveStorage.m_pPlayer);
}

bool CAmalgamMoveSim::CheckStuck(AmalgamMoveStorage& tMoveStorage)
{
	auto pGameMovement = reinterpret_cast<CTFGameMovementAmalgam*>(I::GameMovement);
	pGameMovement->player = tMoveStorage.m_pPlayer;
	pGameMovement->m_pTFPlayer = tMoveStorage.m_pPlayer;
	pGameMovement->mv = &tMoveStorage.m_MoveData;
	SetBounds(tMoveStorage.m_pPlayer);
	const bool bReturn = pGameMovement->CheckStuck() != 0;
	RestoreBounds(tMoveStorage.m_pPlayer);
	return bReturn;
}

void CAmalgamMoveSim::GetAverageYaw(AmalgamMoveStorage& tMoveStorage, int iSamples)
{
	auto pPlayer = tMoveStorage.m_pPlayer;
	const bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();
	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.empty())
		return;

	bool bGround = tMoveStorage.m_bDirectMove;
	int iMinimumStrafes = 4;
	const float flMaxSpeed = SDK::MaxSpeed(tMoveStorage.m_pPlayer, false, true);
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
		if (auto pLocal = g_AmalgamEntitiesExt.GetLocal())
			flDistance = pLocal->m_vecOrigin().DistTo(tMoveStorage.m_pPlayer->m_vecOrigin());
		iMinimum = flDistance < flLowMinimumDistance ? flLowMinimumSamples : Math::RemapVal(flDistance, flLowMinimumDistance, flHighMinimumDistance, flLowMinimumSamples + 1, flHighMinimumSamples);
	}

	flAverageYaw /= std::max(float(iTicks), iMinimum);
	if (fabsf(flAverageYaw) < 0.36f)
		return;

	tMoveStorage.m_flAverageYaw = flAverageYaw;
}

// recency yaw estimate: mean of the newest iSamples tick-span-normalized
// velocity-yaw deltas. The long-window flat average extrapolates a constant
// turn rate through curve reversals and blends opposite directions; the
// newest deltas track smooth micro-turning (validated offline on the 11-04
// capture dataset: 30.9u -> 19.0u mean miss). Falls back to the long average
// when too few valid deltas exist.
void CAmalgamMoveSim::GetRecentYaw(AmalgamMoveStorage& tMoveStorage, int iSamples)
{
	auto pPlayer = tMoveStorage.m_pPlayer;
	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.empty())
		return;

	iSamples = std::min(iSamples, int(vRecords.size()) - 1);
	if (iSamples < MOVE_SIM_RECENT_MIN)
		return;

	float flAverageYaw = 0.f;
	int iTicks = 0, iValid = 0;
	for (int i = 0; i < iSamples; i++)
	{
		auto& tRecord1 = vRecords[i];
		auto& tRecord2 = vRecords[i + 1];
		if (tRecord1.m_iMode != tRecord2.m_iMode)
			continue;
		if (tRecord1.m_vVelocity.Length2D() < 25.f || tRecord2.m_vVelocity.Length2D() < 25.f)
			continue;

		const float flYaw1 = Math::VectorAngles(tRecord1.m_vDirection).y;
		const float flYaw2 = Math::VectorAngles(tRecord2.m_vDirection).y;
		const int iTickSpan = std::max(TIME_TO_TICKS(tRecord1.m_flSimTime - tRecord2.m_flSimTime), 1);
		const float flYaw = Math::NormalizeAngle(flYaw1 - flYaw2) / iTickSpan;
		if (fabsf(flYaw) > 45.f)
			continue;

		flAverageYaw += flYaw;
		iTicks += iTickSpan;
		iValid++;
	}

	if (iValid < MOVE_SIM_RECENT_MIN)
		return;

	flAverageYaw /= iValid;
	if (fabsf(flAverageYaw) < 0.36f)
		return;

	const float flSpeed = std::max(tMoveStorage.m_MoveData.m_vecVelocity.Length2D(), 200.f);
	const float flMaxYaw = tMoveStorage.m_bDirectMove
		? MOVE_SIM_GROUND_MAX_YAW
		: (MOVE_SIM_AIR_MAX_YAW_SPEED / flSpeed) * 57.29578f;
	tMoveStorage.m_flAverageYaw = std::clamp(flAverageYaw, -flMaxYaw, flMaxYaw);
}

bool CAmalgamMoveSim::StrafePrediction(AmalgamMoveStorage& tMoveStorage, bool bStrafe, bool bHitchance)
{
	// Amalgam gates this on its StrafePrediction var (Air = 0b01, Ground = 0b10);
	// our menu checkboxes carry the same split (defaults on, matching Amalgam)
	const int iStrafePrediction = (CFG::Aimbot_Projectile_Air_Strafe_Prediction ? 0b01 : 0)
	                            | (CFG::Aimbot_Projectile_Ground_Strafe_Prediction ? 0b10 : 0);
	if (bStrafe && iStrafePrediction & (tMoveStorage.m_bDirectMove ? 0b10 : 0b01))
	{
		const int iStrafeSamples = tMoveStorage.m_bDirectMove ? MOVE_SIM_GROUND_SAMPLES : MOVE_SIM_AIR_SAMPLES;

		GetAverageYaw(tMoveStorage, iStrafeSamples);

		// the recency estimate tracks smooth micro-turning that the flat
		// long-window average blends away; it overrides when it has enough
		// fresh deltas, otherwise the long estimate above stays
		GetRecentYaw(tMoveStorage, MOVE_SIM_RECENT_SAMPLES);

		// really hope this doesn't work like shit (amalgam)
		if (bHitchance && !tMoveStorage.m_pPlayer->m_vecVelocity().IsZero() && Vars::Aimbot::Projectile::HitChance.Value)
		{
			const auto& vRecords = m_mRecords[tMoveStorage.m_pPlayer->entindex()];
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
				if (tMoveStorage.m_MoveData.m_flMaxSpeed)
					flYaw *= std::clamp(pRecord1.m_vVelocity.Length2D() / tMoveStorage.m_MoveData.m_flMaxSpeed, 0.f, 1.f);

				if ((i + 1) % iStrafeSamples == 0 || i == iSamples - 1)
				{
					flAverageYaw /= i % iStrafeSamples + 1;
					if (fabsf(tMoveStorage.m_flAverageYaw - flAverageYaw) > 0.5f)
						flCurrentChance -= 1.f / ((iSamples - 1) / float(iStrafeSamples) + 1);
					flAverageYaw = 0.f;
				}
			}

			if (flCurrentChance < Vars::Aimbot::Projectile::HitChance.Value / 100)
				return false;
		}
	}

	if (!tMoveStorage.m_bDirectMove)
	{
		if (!tMoveStorage.m_flAverageYaw)
			tMoveStorage.m_MoveData.m_flForwardMove = tMoveStorage.m_MoveData.m_flSideMove = 0.f;
		else
		{
			const auto& vRecords = m_mRecords[tMoveStorage.m_pPlayer->entindex()];
			if (!vRecords.empty())
			{
				auto& tRecord = vRecords.front();
				Vec3 vDirection = tRecord.m_vVelocity;
				if (!vDirection.IsZero() && tRecord.m_bInputDirection)
				{	// fix inputs for turning
					const int iSign = sign(tMoveStorage.m_flAverageYaw);
					const float flForward = tMoveStorage.m_MoveData.m_flForwardMove, flSide = tMoveStorage.m_MoveData.m_flSideMove;
					tMoveStorage.m_MoveData.m_flForwardMove = -flSide * iSign, tMoveStorage.m_MoveData.m_flSideMove = flForward * iSign;
				}
			}
		}
	}

	return true;
}

bool CAmalgamMoveSim::SetDuck(AmalgamMoveStorage& tMoveStorage, bool bDuck) // this only touches origin, bounds
{
	if (bDuck == tMoveStorage.m_pPlayer->m_bDucked())
		return true;

	auto pGameRules = I::TFGameRules();
	auto pViewVectors = pGameRules ? pGameRules->GetViewVectors() : nullptr;
	const float flScale = tMoveStorage.m_pPlayer->m_flModelScale();

	if (!IsOnGround(tMoveStorage.m_pPlayer))
	{
		const Vec3 vHullMins = (pViewVectors ? pViewVectors->m_vHullMin : Vec3(-24, -24, 0)) * flScale;
		const Vec3 vHullMaxs = (pViewVectors ? pViewVectors->m_vHullMax : Vec3(24, 24, 82)) * flScale;
		const Vec3 vDuckHullMins = (pViewVectors ? pViewVectors->m_vDuckHullMin : Vec3(-24, -24, 0)) * flScale;
		const Vec3 vDuckHullMaxs = (pViewVectors ? pViewVectors->m_vDuckHullMax : Vec3(24, 24, 62)) * flScale;

		if (bDuck)
			tMoveStorage.m_MoveData.m_vecAbsOrigin += (vHullMaxs - vHullMins) - (vDuckHullMaxs - vDuckHullMins);
		else
		{
			const Vec3 vOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin - ((vHullMaxs - vHullMins) - (vDuckHullMaxs - vDuckHullMins));

			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
			SDK::TraceHull(vOrigin, vOrigin, vHullMins, vHullMaxs, SolidMask(tMoveStorage.m_pPlayer), &filter, &trace);
			if (trace.DidHit())
				return false;

			tMoveStorage.m_MoveData.m_vecAbsOrigin = vOrigin;
		}
	}
	tMoveStorage.m_pPlayer->m_bDucked() = bDuck;

	return true;
}

void CAmalgamMoveSim::SetBounds(C_TFPlayer* pPlayer)
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

void CAmalgamMoveSim::RestoreBounds(C_TFPlayer* pPlayer)
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

void CAmalgamMoveSim::RunTick(AmalgamMoveStorage& tMoveStorage, bool bPath, const MoveSimRunTickCallback* pCallback)
{
	if (tMoveStorage.m_bFailed || !tMoveStorage.m_pPlayer || !IsPlayer(tMoveStorage.m_pPlayer))
		return;

	// make sure frametime and prediction vars are right
	I::Prediction->m_bInPrediction = true;
	I::Prediction->m_bFirstTimePredicted = false;
	I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;
	SetBounds(tMoveStorage.m_pPlayer);

	if (tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE) && !tMoveStorage.m_bChargeExpired)
	{
		static auto tf_demoman_charge_drain_time = U::ConVars.FindVar("tf_demoman_charge_drain_time");

		const float flDrainTime = tf_demoman_charge_drain_time
			? SDKUtils::AttribHookValue(tf_demoman_charge_drain_time->GetFloat(), "mod_charge_time", tMoveStorage.m_pPlayer)
			: 10.f;
		tMoveStorage.m_pPlayer->m_flChargeMeter() -= TICK_INTERVAL * 100.f / flDrainTime;

		if (tMoveStorage.m_pPlayer->m_flChargeMeter() <= 0.f)
		{
			// amalgam: RemoveCond(TF_COND_SHIELD_CHARGE) — Necromancer has no
			// RemoveCond, so the sim tracks the expiry via m_bChargeExpired and
			// refreshes max speed the same way.
			tMoveStorage.m_bChargeExpired = true;
			tMoveStorage.m_MoveData.m_flMaxSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed = SDK::MaxSpeed(tMoveStorage.m_pPlayer);
			tMoveStorage.m_pPlayer->m_flMaxspeed() = tMoveStorage.m_MoveData.m_flMaxSpeed;
		}
	}

	tMoveStorage.m_iSimTick++;

	float flCorrection = 0.f;
	if (tMoveStorage.m_flAverageYaw)
	{
		float flMult = 1.f;
		if (!tMoveStorage.m_bDirectMove && !(tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE) && !tMoveStorage.m_bChargeExpired))
		{
			flCorrection = 90.f * sign(tMoveStorage.m_flAverageYaw);
			if (MOVE_SIM_FRICTION_FLAGS & 0b01)
				flMult = GetFrictionScale(tMoveStorage.m_MoveData.m_vecVelocity.Length2D(), tMoveStorage.m_flAverageYaw, tMoveStorage.m_MoveData.m_vecVelocity.z + SDKUtils::GetGravity() * TICK_INTERVAL);
		}
		// humans straighten out: fade the extrapolated turn to straight over
		// MOVE_SIM_DECAY_TICKS (the +90 air wishdir alignment is input
		// construction, not turn rate, so it does not decay)
		const float flDecay = std::exp(-float(tMoveStorage.m_iSimTick) / MOVE_SIM_DECAY_TICKS);
		tMoveStorage.m_MoveData.m_vecViewAngles.y += tMoveStorage.m_flAverageYaw * flMult * flDecay + flCorrection;
	}

	const float flOldSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed;
	if (tMoveStorage.m_pPlayer->m_bDucked() && IsOnGround(tMoveStorage.m_pPlayer) && !IsSwimming(tMoveStorage.m_pPlayer))
		tMoveStorage.m_MoveData.m_flClientMaxSpeed /= 3;

	if (tMoveStorage.m_bBunnyHop && IsOnGround(tMoveStorage.m_pPlayer) && !tMoveStorage.m_pPlayer->m_bDucked())
	{
		tMoveStorage.m_MoveData.m_nOldButtons = 0;
		tMoveStorage.m_MoveData.m_nButtons |= IN_JUMP;
	}

	I::GameMovement->ProcessMovement(tMoveStorage.m_pPlayer, &tMoveStorage.m_MoveData);
	if (pCallback)
		(*pCallback)(tMoveStorage.m_MoveData);

	tMoveStorage.m_MoveData.m_flClientMaxSpeed = flOldSpeed;

	tMoveStorage.m_flSimTime = ROUND_TO_TICKS(tMoveStorage.m_flSimTime + TICK_INTERVAL);
	tMoveStorage.m_bPredictNetworked = TIME_TO_TICKS(tMoveStorage.m_flSimTime) >= TIME_TO_TICKS(tMoveStorage.m_flPredictedSimTime);
	if (tMoveStorage.m_bPredictNetworked)
	{
		tMoveStorage.m_vPredictedOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
		tMoveStorage.m_flPredictedSimTime += tMoveStorage.m_flPredictedDelta;
	}
	const bool bLastbDirectMove = tMoveStorage.m_bDirectMove;
	tMoveStorage.m_bDirectMove = IsOnGround(tMoveStorage.m_pPlayer) || IsSwimming(tMoveStorage.m_pPlayer);

	if (tMoveStorage.m_flAverageYaw)
		tMoveStorage.m_MoveData.m_vecViewAngles.y -= flCorrection;

	if (tMoveStorage.m_bDirectMove && !bLastbDirectMove)
	{
		if (tMoveStorage.m_flAverageYaw)
			tMoveStorage.m_flAverageYaw *= -1;
		else if (!tMoveStorage.m_MoveData.m_flForwardMove && !tMoveStorage.m_MoveData.m_flSideMove
			&& tMoveStorage.m_MoveData.m_vecVelocity.Length2D() > tMoveStorage.m_MoveData.m_flMaxSpeed * 0.015f)
		{
			Vec3 vDirection = tMoveStorage.m_MoveData.m_vecVelocity.Normalized2D() * 520.f;
			g_DummyCmd.forwardmove = vDirection.x, g_DummyCmd.sidemove = -vDirection.y;
			SDK::FixMovement(&g_DummyCmd, {}, tMoveStorage.m_MoveData.m_vecViewAngles);
			tMoveStorage.m_MoveData.m_flForwardMove = g_DummyCmd.forwardmove, tMoveStorage.m_MoveData.m_flSideMove = g_DummyCmd.sidemove;
		}
	}

	RestoreBounds(tMoveStorage.m_pPlayer);

	if (bPath)
		tMoveStorage.m_vPath.push_back(tMoveStorage.m_MoveData.m_vecAbsOrigin);
}

void CAmalgamMoveSim::Restore(AmalgamMoveStorage& tMoveStorage)
{
	if (tMoveStorage.m_bInitFailed || !tMoveStorage.m_pPlayer)
		return;

	if (I::MoveHelper)
		I::MoveHelper->SetHost(nullptr);
	tMoveStorage.m_pPlayer->SetCurrentCommand(nullptr);

	ResetState(tMoveStorage);

	if (I::Prediction)
	{
		I::Prediction->m_bInPrediction = m_bOldInPrediction;
		I::Prediction->m_bFirstTimePredicted = m_bOldFirstTimePredicted;
	}
	if (I::GlobalVars)
		I::GlobalVars->frametime = m_flOldFrametime;
}

float CAmalgamMoveSim::GetPredictedDelta(C_BaseEntity* pEntity)
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
