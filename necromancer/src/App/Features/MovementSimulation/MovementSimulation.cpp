#include "MovementSimulation.h"

#include "../CFG.h"
#include "../amalgam_port/AmalgamCompat.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr size_t MaxMoveRecords = 66;
	constexpr size_t MaxSimTimeRecords = 8;
	constexpr float PlayerOriginCompression = 0.125f;
	constexpr float MicroMoveDistanceSqr = (PlayerOriginCompression * 2.f) * (PlayerOriginCompression * 2.f);
	constexpr float StationarySpeed = 10.f;
	constexpr size_t StationarySamples = 4;
	constexpr float TeleportDistanceSqr = 4096.f * 4096.f;
	constexpr int MaxChokedTicks = 22;

	float g_flOldFrametime = 0.f;
	bool g_bOldInPrediction = false;
	bool g_bOldFirstTimePredicted = false;
	CUserCmd g_MoveSimCmd = {};
	Vec3 g_vOriginalHullMin = {};
	Vec3 g_vOriginalHullMax = {};
	Vec3 g_vOriginalDuckHullMin = {};
	Vec3 g_vOriginalDuckHullMax = {};
	bool g_bBoundsChanged = false;

	float GetFrictionScale(C_TFPlayer* pPlayer)
	{
		static ConVar* sv_friction = U::ConVars.FindVar("sv_friction");
		if (!sv_friction || !pPlayer)
			return 1.f;

		const float flSpeed = pPlayer->m_vecVelocity().Length2D();
		if (flSpeed <= 0.f)
			return 1.f;

		const float flControl = std::max(flSpeed, pPlayer->TeamFortress_CalculateMaxSpeed());
		const float flDrop = flControl * sv_friction->GetFloat() * TICK_INTERVAL;
		return std::clamp((flSpeed - flDrop) / flSpeed, 0.f, 1.f);
	}

	MoveMode GetMoveMode(C_TFPlayer* pPlayer)
	{
		if (pPlayer && pPlayer->m_nWaterLevel() >= 2)
			return MoveMode::Swim;

		return IsOnGround(pPlayer) ? MoveMode::Ground : MoveMode::Air;
	}

	// Amalgam-inspired: models AirAccelerate diminishing returns.
	// When (velocityXY × |yaw|) is high, air-strafing is friction-limited — scale it down.
	// Only active during the upward arc (0 < Vz <= 250): at this speed the player just jumped
	// and is in the typical strafe zone. Explosive launches (Vz > 250) and descent (Vz <= 0)
	// are left at full yaw since AirAccelerate has different effective limits there.
	float GetAirFrictionScale(float flVelocityXY, float flTurn, float flVelocityZ)
	{
		if (flVelocityZ <= 0.f || flVelocityZ > 250.f)
			return 1.f;

		static ConVar* sv_airaccelerate = U::ConVars.FindVar("sv_airaccelerate");
		const float flAccel = std::max(sv_airaccelerate ? sv_airaccelerate->GetFloat() : 10.f, 1.f);
		const float flMin = 50.f * flAccel;
		const float flMax = 150.f * flAccel;
		const float flInput = std::fabs(flVelocityXY * flTurn);

		return Math::RemapValClamped(flInput, flMin, flMax, 1.f, 0.25f);
	}

	Vec3 DirectionFromVelocity(const Vec3& vVelocity)
	{
		// Ground movement must keep move-unit magnitude; a unit vector makes GameMovement under-drive the target.
		return vVelocity.To2D();
	}

	Vec3 NormalizedDirectionFromVelocity(const Vec3& vVelocity)
	{
		Vec3 vDirection = DirectionFromVelocity(vVelocity);
		if (!vDirection.IsZero())
			vDirection.Normalize2D();

		return vDirection;
	}

	float YawFromVector(const Vec3& vVector)
	{
		return Math::VectorAngles(vVector).y;
	}

	void DecomposeWishDir(const Vec3& vWishDir, float flEyeYawDeg, float& flForwardMove, float& flSideMove)
	{
		const float flEyeYaw = flEyeYawDeg * DEG2RAD(1.f);
		const float flFwdX = cosf(flEyeYaw), flFwdY = sinf(flEyeYaw);
		const float flRtX = sinf(flEyeYaw), flRtY = -cosf(flEyeYaw);
		flForwardMove = vWishDir.x * flFwdX + vWishDir.y * flFwdY;
		flSideMove    = vWishDir.x * flRtX  + vWishDir.y * flRtY;
	}

	bool IsUsablePlayer(C_TFPlayer* pPlayer)
	{
		return pPlayer && !pPlayer->IsDormant() && pPlayer->IsAlive() && !IsAGhost(pPlayer);
	}

	bool HasMeaningfulOriginDelta(const Vec3& vFrom, const Vec3& vTo)
	{
		return (vFrom - vTo).Length2DSqr() > MicroMoveDistanceSqr;
	}

	bool IsRecentlyStationary(C_TFPlayer* pPlayer, const std::deque<MoveRecord>& vRecords)
	{
		if (!pPlayer || pPlayer->m_vecVelocity().Length2DSqr() >= StationarySpeed * StationarySpeed)
			return false;

		if (vRecords.empty())
			return true;

		const Vec3 vOrigin = pPlayer->m_vecOrigin();
		const size_t nSamples = std::min(vRecords.size(), StationarySamples);
		for (size_t n = 0; n < nSamples; ++n)
		{
			if (HasMeaningfulOriginDelta(vOrigin, vRecords[n].m_vOrigin))
				return false;
		}

		return true;
	}

	void HandleMovementRecord(C_TFPlayer* pPlayer, std::deque<MoveRecord>& vRecords)
	{
		if (!pPlayer || vRecords.empty())
			return;

		const MoveMode iMode = vRecords.front().m_iMode;
		if (vRecords.size() > 1)
		{
			const auto& tPrevious = vRecords[1];
			
			// Skip TraceHull when velocity is low — wall collision unlikely
			const float flSpeed = tPrevious.m_vVelocity.Length2D();
			if (flSpeed > 50.0f)
			{
				const Vec3 vTraceStart = tPrevious.m_vOrigin;
				const Vec3 vTraceEnd = vTraceStart + tPrevious.m_vVelocity * TICK_INTERVAL;

				CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
				filter.pSkip = pPlayer;

				trace_t trace = {};
				const Vec3 vCompression(PlayerOriginCompression, PlayerOriginCompression, 0.f);
				SDK::TraceHull(vTraceStart, vTraceEnd, pPlayer->m_vecMins() + vCompression, pPlayer->m_vecMaxs() - vCompression, SolidMask(pPlayer), &filter, &trace);

				if (trace.DidHit() && trace.plane.normal.z < 0.7f)
				{
					// Match Amalgam: clear ALL stale history on wall hit.
					// The old fallback-from-history approach would chain zero
					// directions on complex geometry (corridors/corners),
					// causing the sim to think the enemy stopped moving.
					// Keep only the current (newest) record and let it
					// re-derive direction from actual velocity next frame.
					vRecords.erase(vRecords.begin() + 1, vRecords.end());
					return;
				}
			}
		}

		if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			vRecords.front().m_vDirection = NormalizedDirectionFromVelocity(pPlayer->m_vecVelocity()) * 450.f;
			return;
		}

		if (iMode == MoveMode::Air)
		{
			// Air: use velocity direction; if 2D velocity near zero, preserve last ground direction for momentum
			const float flSpeed2D = pPlayer->m_vecVelocity().Length2D();
			if (flSpeed2D > 50.f)
			{
				vRecords.front().m_vDirection = NormalizedDirectionFromVelocity(pPlayer->m_vecVelocity()) * pPlayer->TeamFortress_CalculateMaxSpeed();
			}
			else if (vRecords.size() > 1)
			{
				// Search backward for last valid direction (ground or air record)
				for (size_t i = 1; i < vRecords.size(); i++)
				{
					const auto& tPrev = vRecords[i];
					if (tPrev.m_iMode == MoveMode::Ground && !tPrev.m_vDirection.IsZero())
					{
						vRecords.front().m_vDirection = tPrev.m_vDirection;
						break;
					}
					if (tPrev.m_iMode == MoveMode::Air && !tPrev.m_vDirection.IsZero())
					{
						vRecords.front().m_vDirection = tPrev.m_vDirection;
						break;
					}
				}
			}
			return;
		}

		if (iMode == MoveMode::Swim)
			vRecords.front().m_vDirection *= 2.f;
	}
}

bool CMovementSimulation::StoreState(MoveStorage& tMoveStorage)
{
	if (!tMoveStorage.m_pPlayer || !I::MemAlloc)
		return false;

	auto pMap = GetPredDescMap(tMoveStorage.m_pPlayer);
	if (!pMap)
		return false;

	const size_t iSize = GetIntermediateDataSize(tMoveStorage.m_pPlayer);
	if (!iSize)
		return false;

	tMoveStorage.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Alloc(iSize));
	if (!tMoveStorage.m_pData)
		return false;

	CPredictionCopy copy(PC_EVERYTHING, tMoveStorage.m_pData, PC_DATA_PACKED, tMoveStorage.m_pPlayer, PC_DATA_NORMAL);
	copy.TransferData("CMovementSimulation::StoreState", tMoveStorage.m_pPlayer->entindex(), pMap);
	return true;
}

void CMovementSimulation::ResetState(MoveStorage& tMoveStorage)
{
	if (!tMoveStorage.m_pData || !I::MemAlloc)
		return;

	if (tMoveStorage.m_pPlayer)
	{
		auto pMap = GetPredDescMap(tMoveStorage.m_pPlayer);
		if (pMap)
		{
			CPredictionCopy copy(PC_EVERYTHING, tMoveStorage.m_pPlayer, PC_DATA_NORMAL, tMoveStorage.m_pData, PC_DATA_PACKED);
			copy.TransferData("CMovementSimulation::ResetState", tMoveStorage.m_pPlayer->entindex(), pMap);
		}
	}

	I::MemAlloc->Free(tMoveStorage.m_pData);
	tMoveStorage.m_pData = nullptr;
}

void CMovementSimulation::Store()
{
	if (!I::ClientEntityList || !I::EngineClient || !I::EngineClient->IsInGame() || !I::EngineClient->IsConnected())
	{
		m_mRecords.clear();
		m_mSimTimes.clear();
		return;
	}

	C_TFPlayer* pLocal = H::Entities ? H::Entities->GetLocal() : nullptr;
	const int nMaxClients = std::min(I::ClientEntityList->GetHighestEntityIndex(), I::GlobalVars ? I::GlobalVars->maxClients : 64);

	for (int n = 1; n <= nMaxClients; n++)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(n);
		
		// Early exit: skip null/dormant entities before any processing
		if (!pEntity || pEntity->IsDormant())
		{
			m_mRecords[n].clear();
			m_mSimTimes[n].clear();
			continue;
		}
		
		C_TFPlayer* pPlayer = pEntity->As<C_TFPlayer>();
		auto& vRecords = m_mRecords[n];

		if (!IsUsablePlayer(pPlayer) || pPlayer == pLocal)
		{
			vRecords.clear();
			m_mSimTimes[n].clear();
			continue;
		}

		// Keep micro-movement records when simtime advances and origin moved past compression noise.
		// — they might start moving next frame and we need their direction history
		const float flSimTime = pPlayer->m_flSimulationTime();
		const Vec3 vOrigin = pPlayer->m_vecOrigin();
		if (!vRecords.empty() && std::fabs(vRecords.front().m_flSimTime - flSimTime) <= 0.0001f)
			continue;

		const bool bOriginMoved = vRecords.empty() || HasMeaningfulOriginDelta(vOrigin, vRecords.front().m_vOrigin);
		const bool bIsStationary = pPlayer->m_vecVelocity().Length2D() < StationarySpeed && !bOriginMoved;
		if (bIsStationary)
		{
			// Record a zero-direction entry so IsRecentlyStationary detects the stop immediately.
			// Without this, old moving records persist and IsRecentlyStationary sees origin
			// deltas from when the player WAS moving, returning false for several frames.
			MoveRecord tStatRecord = {};
			tStatRecord.m_vDirection = {};
			tStatRecord.m_flSimTime = flSimTime;
			tStatRecord.m_iMode = GetMoveMode(pPlayer);
			tStatRecord.m_vVelocity = {};
			tStatRecord.m_vOrigin = vOrigin;
			tStatRecord.m_flEyeYaw = pPlayer->GetEyeAngles().y;
			tStatRecord.m_flEyePitch = pPlayer->GetEyeAngles().x;
			vRecords.push_front(tStatRecord);
			while (vRecords.size() > MaxMoveRecords)
				vRecords.pop_back();
			continue;
		}

		if (!vRecords.empty() && (vOrigin - vRecords.front().m_vOrigin).LengthSqr() > TeleportDistanceSqr)
		{
			vRecords.clear();
			m_mSimTimes[n].clear();
		}

		if (!vRecords.empty())
		{
			const float flDelta = flSimTime - vRecords.front().m_flSimTime;
			if (flDelta <= 0.f)
			{
				vRecords.clear();
				m_mSimTimes[n].clear();
				continue;
			}

			auto& vSimTimes = m_mSimTimes[n];
			vSimTimes.push_front(flDelta);
			while (vSimTimes.size() > MaxSimTimeRecords)
				vSimTimes.pop_back();
		}

		Vec3 vVelocity = pPlayer->m_vecVelocity();

		// RijiN duck velocity spike fix: Z origin shift > 15 units on ground = duck transition, not real Z velocity
		if (!vRecords.empty())
		{
			const float flOriginDeltaZ = (vOrigin - vRecords.front().m_vOrigin).z;
			if (IsOnGround(pPlayer) && std::fabs(flOriginDeltaZ) > 15.f)
				vVelocity.z = 0.f;
		}

		// Clamp ground velocity to max speed (stale/interpolated velocity can exceed max)
		if (IsOnGround(pPlayer))
		{
			constexpr float flMaxPossibleSpeed = 520.f;
			const float flSpeedSqr = vVelocity.Length2DSqr();
			if (flSpeedSqr > flMaxPossibleSpeed * flMaxPossibleSpeed)
			{
				const float flMaxSpeed = SDK::MaxSpeed(pPlayer);
				const float flMaxSpeedSqr = flMaxSpeed * flMaxSpeed;
				if (flSpeedSqr > flMaxSpeedSqr)
				{
					const float flSpeed = std::sqrt(flSpeedSqr);
					vVelocity.x *= flMaxSpeed / flSpeed;
					vVelocity.y *= flMaxSpeed / flSpeed;
				}
			}
		}

		MoveRecord tRecord = {};
		tRecord.m_vDirection = DirectionFromVelocity(vVelocity);
		
		// If velocity is near zero but origin is still changing (interpolation artifact),
		// preserve previous direction to avoid false stops.
		// If origin hasn't moved, this is a REAL stop — don't preserve direction.
		if (tRecord.m_vDirection.IsZero() && !vRecords.empty() && !vRecords.front().m_vDirection.IsZero() && bOriginMoved)
		{
			tRecord.m_vDirection = vRecords.front().m_vDirection;
		}
		
		tRecord.m_flSimTime = flSimTime;
		tRecord.m_iMode = GetMoveMode(pPlayer);
		tRecord.m_vVelocity = vVelocity;
		tRecord.m_vOrigin = vOrigin;

		// Record eye angles for antiaim detection and strafe analysis
		{
			const Vec3 vEye = pPlayer->GetEyeAngles();
			tRecord.m_flEyeYaw = vEye.y;
			tRecord.m_flEyePitch = vEye.x;

			// Compute velocity-eye yaw divergence
			const float flSpeed2D = vVelocity.Length2D();
			if (flSpeed2D > 10.f)
			{
				Vec3 vVelDir = vVelocity; vVelDir.z = 0.f;
				Vec3 vVelAng = {};
				Math::VectorAngles(vVelDir, vVelAng);
				tRecord.m_flVelEyeDelta = std::fabs(Math::NormalizeAngle(vVelAng.y - vEye.y));
			}
		}

		// Evasion detection: origin-based direction deltas (immune to antiaim/interpolation)
		if (IsOnGround(pPlayer) && vRecords.size() >= 4)
		{
			int iEvasionCount = 0;
			int iHighDeltaCount = 0;
			int iReversalCount = 0;  // Actual direction reversals (>90° origin yaw flip)
			constexpr int EvasionRequiredSamples = 4;

			for (int i = 0; i < EvasionRequiredSamples && i < static_cast<int>(vRecords.size()) - 1; i++)
			{
				const auto& tCur = vRecords[i];
				const auto& tPrev = vRecords[i + 1];

				// Use origin deltas for direction change detection
				Vec3 vCurDelta = (tCur.m_vOrigin - tPrev.m_vOrigin).To2D();
				if (vCurDelta.IsZero())
					continue;

				const float flCurYaw = YawFromVector(vCurDelta);

				// Get previous origin delta for comparison
				if (i + 2 >= static_cast<int>(vRecords.size()))
					continue;
				Vec3 vPrevDelta = (tPrev.m_vOrigin - vRecords[i + 2].m_vOrigin).To2D();
				if (vPrevDelta.IsZero())
					continue;

				const float flPrevYaw = YawFromVector(vPrevDelta);
				const float flYawDelta = std::fabs(Math::NormalizeAngle(flCurYaw - flPrevYaw));

				// Direction reversal or high yaw delta = evasive
				const int iCurSign = sign(Math::NormalizeAngle(flCurYaw - flPrevYaw));
				const int iPrevSign = i > 0 ? sign(Math::NormalizeAngle(
					YawFromVector((vRecords[i - 1].m_vOrigin - tCur.m_vOrigin).To2D()) - flCurYaw)) : 0;
				if ((iPrevSign && iCurSign && iCurSign != iPrevSign) || flYawDelta >= 15.f)
					iEvasionCount++;

				if (flYawDelta >= 25.f)
					iHighDeltaCount++;

				// Track actual reversals (>90° origin yaw flip) — only these warrant zero-vel
				if (flYawDelta > 90.f)
					iReversalCount++;
			}

			if (iEvasionCount >= EvasionRequiredSamples)
				tRecord.m_bEvasiveFriction = true;

			// Only zero velocity for actual reversals, not sustained strafing
			if (iReversalCount >= 2)
				tRecord.m_bEvasiveZeroVel = true;
		}

		vRecords.push_front(tRecord);
		while (vRecords.size() > MaxMoveRecords)
			vRecords.pop_back();

		HandleMovementRecord(pPlayer, vRecords);
	}
}

bool CMovementSimulation::Initialize(C_TFPlayer* pPlayer)
{
	if (m_CurrentStorage.m_pData)
		Restore(m_CurrentStorage);

	m_CurrentStorage = {};
	return Initialize(pPlayer, m_CurrentStorage, false, true);
}

bool CMovementSimulation::Initialize(C_TFPlayer* pPlayer, MoveStorage& tMoveStorage, bool bHitchance, bool bStrafe)
{
	tMoveStorage = {};
	tMoveStorage.m_pPlayer = pPlayer;

	if (!IsUsablePlayer(pPlayer) || !I::GlobalVars || !I::Prediction || !I::MoveHelper || !I::GameMovement)
	{
		tMoveStorage.m_bFailed = true;
		tMoveStorage.m_bInitFailed = true;
		return false;
	}

	// Stationary enemies: Initialize succeeds so callers can use GetOrigin/Restore/splash/multipoint/lob
	{
		C_TFPlayer* pLocal = H::Entities ? H::Entities->GetLocal() : nullptr;
		const auto itRecords = m_mRecords.find(pPlayer->entindex());
		const std::deque<MoveRecord> vEmptyRecords = {};
		const auto& vRecords = itRecords != m_mRecords.end() ? itRecords->second : vEmptyRecords;
		tMoveStorage.m_bStationary = (pPlayer != pLocal && IsRecentlyStationary(pPlayer, vRecords));
	}

	g_flOldFrametime = I::GlobalVars->frametime;
	g_bOldInPrediction = I::Prediction->m_bInPrediction;
	g_bOldFirstTimePredicted = I::Prediction->m_bFirstTimePredicted;

	tMoveStorage.m_bPredictNetworked = !bHitchance;
	I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;
	I::Prediction->m_bInPrediction = true;
	I::Prediction->m_bFirstTimePredicted = false;

	if (!StoreState(tMoveStorage))
	{
		tMoveStorage.m_bFailed = true;
		tMoveStorage.m_bInitFailed = true;
		I::GlobalVars->frametime = g_flOldFrametime;
		I::Prediction->m_bInPrediction = g_bOldInPrediction;
		I::Prediction->m_bFirstTimePredicted = g_bOldFirstTimePredicted;
		return false;
	}

	I::MoveHelper->SetHost(pPlayer);
	g_MoveSimCmd = {};
	pPlayer->SetCurrentCommand(&g_MoveSimCmd);

	if (C_TFPlayer* pLocal = H::Entities ? H::Entities->GetLocal() : nullptr; pPlayer != pLocal)
	{
		// Explicitly backup values that CPredictionCopy might not restore (rendered netvars)
		tMoveStorage.m_flOldModelScale = pPlayer->m_flModelScale();
		tMoveStorage.m_vOldOrigin = pPlayer->m_vecOrigin();
		tMoveStorage.m_bHasBackup = true;

		if (Vec3* pAverage = g_AmalgamEntitiesExt.GetAvgVelocity(pPlayer->entindex()); pAverage && !pAverage->IsZero())
			pPlayer->m_vecVelocity() = *pAverage;

		// Duck fix: remove FL_DUCKING (breaks origin Z in ProcessMovement) — Amalgam approach
		if (pPlayer->m_bDucked() = (pPlayer->m_bDucked() || (pPlayer->m_fFlags() & FL_DUCKING) != 0))
		{
			pPlayer->m_fFlags() &= ~FL_DUCKING;
			pPlayer->m_flDucktime() = 0.f;
			pPlayer->m_flDuckJumpTime() = 0.f;
			pPlayer->m_bDucking() = false;
			pPlayer->m_bInDuckJump() = false;
		}

		// Skip model scale modification — SetBounds already compresses collision hull

		// Lift origin off ground to prevent ground sticking
		if (IsOnGround(pPlayer))
			pPlayer->m_vecOrigin().z += 0.03125f * 3.0f;

		pPlayer->m_vecBaseVelocity() = {};

		// Step fix: clamp Z to <= 0 on ground — preserves upward velocity for jumps (Amalgam approach)
		if (IsOnGround(pPlayer))
			pPlayer->m_vecVelocity().z = std::min(pPlayer->m_vecVelocity().z, 0.f);
		else
			pPlayer->m_hGroundEntity() = nullptr;

		// Nudge near-zero XY velocity to prevent prediction stall — but NOT for
		// stationary players, or the sim will predict movement for enemies that stopped.
		if (!tMoveStorage.m_bStationary)
		{
			if (fabsf(pPlayer->m_vecVelocity().x) < 0.01f)
				pPlayer->m_vecVelocity().x = 0.015f;

			if (fabsf(pPlayer->m_vecVelocity().y) < 0.01f)
				pPlayer->m_vecVelocity().y = 0.015f;
		}
	}

	float flCadenceOut = 0.f;
	tMoveStorage.m_bBunnyHop = IsBunnyHopping(pPlayer, &flCadenceOut);
	tMoveStorage.m_flBhopCadence = flCadenceOut;
	tMoveStorage.m_iBhopSimTicksInAir = 0;
	tMoveStorage.m_iBhopSimTicksOnGround = 0;

	// Propagate evasion detection from newest record, and detect antiaim/strafing
	{
		const auto itRecords = m_mRecords.find(pPlayer->entindex());
		if (itRecords != m_mRecords.end() && !itRecords->second.empty())
		{
			tMoveStorage.m_bEvasiveFriction = itRecords->second.front().m_bEvasiveFriction;
			// Zero velocity for ACTUAL reversals only (not sustained strafing)
			if (itRecords->second.front().m_bEvasiveZeroVel && pPlayer != (H::Entities ? H::Entities->GetLocal() : nullptr))
			{
				pPlayer->m_vecVelocity().x = 0.f;
				pPlayer->m_vecVelocity().y = 0.f;
			}

			// Antiaim detection: eye angles jitter wildly while origin trajectory is smooth
			tMoveStorage.m_bAntiAimDetected = IsAntiAiming(pPlayer);

			// Strafe detection: VelEyeDelta > 10° means lateral movement (pressing A/D)
			tMoveStorage.m_flVelEyeDeltaAtInit = itRecords->second.front().m_flVelEyeDelta;
			tMoveStorage.m_bStrafing = tMoveStorage.m_flVelEyeDeltaAtInit > 10.f;

			// Origin curvature: turning rate from position history (immune to antiaim/interpolation)
			tMoveStorage.m_flOriginCurvature = GetOriginCurvature(pPlayer);

			// Recent reversal: current velocity opposes older mean direction
			{
				const auto& vRecs = itRecords->second;
				if (vRecs.size() >= 5)
				{
					const Vec3 vRecent = vRecs[0].m_vVelocity.To2D();
					const float flRecLen = vRecent.Length();
					Vec3 vOlderSum = {};
					int iOlder = 0;
					for (size_t i = 2; i < std::min<size_t>(vRecs.size(), 6); ++i)
					{
						vOlderSum.x += vRecs[i].m_vVelocity.x;
						vOlderSum.y += vRecs[i].m_vVelocity.y;
						iOlder++;
					}
					if (flRecLen > 30.f && iOlder >= 3)
					{
						const float flOlderLen = std::sqrt(vOlderSum.x * vOlderSum.x + vOlderSum.y * vOlderSum.y);
						if (flOlderLen > 30.f)
						{
							const float flDot = (vRecent.x * vOlderSum.x + vRecent.y * vOlderSum.y)
								/ (flRecLen * flOlderLen);
							// Cosine < -0.3 == > ~107° between current and older mean dir
							tMoveStorage.m_bRecentReversal = (flDot < -0.3f);
						}
					}
				}
			}

			// Rigid ground: suppress yaw for straight-line prediction.
			// When VelEyeDelta < 20°, no curvature, no reversal, no lateral vel.
			// Slow players (< 200 HU/s) are also rigid — they're standing/creeping, not strafing.
			tMoveStorage.m_bRigidGround = false;
			if (!tMoveStorage.m_bRecentReversal
				&& tMoveStorage.m_flVelEyeDeltaAtInit < 20.f
				&& std::fabs(tMoveStorage.m_flOriginCurvature) < 0.3f)
			{
				const float flSpeed2D = pPlayer->m_vecVelocity().Length2D();
				if (flSpeed2D < 200.f)
				{
					// Slow player — not strafing, just standing/creeping
					tMoveStorage.m_bRigidGround = true;
				}
				else
				{
					// Fast player — check lateral velocity to confirm straight-line movement
					const Vec3 vVel2D = pPlayer->m_vecVelocity().To2D();
					const float flEyeYaw = pPlayer->GetEyeAngles().y * DEG2RAD(1.f);
					const float flEyeRightX = sinf(flEyeYaw);
					const float flEyeRightY = -cosf(flEyeYaw);
					const float flLateralSpeed = std::fabs(vVel2D.x * flEyeRightX + vVel2D.y * flEyeRightY);
					// Lateral vel > 50 HU/s means micro-adjustments — don't freeze
					if (flLateralSpeed < 50.f)
						tMoveStorage.m_bRigidGround = true;
				}
			}

			// Explosive launch: speed far exceeds class max (rocket/sticky jump)
			{
				const bool bAirborne = !IsOnGround(pPlayer) && GetMoveMode(pPlayer) != MoveMode::Swim;
				if (bAirborne)
				{
					const float flSpeed2D = pPlayer->m_vecVelocity().Length2D();
					const float flClassMax = pPlayer->TeamFortress_CalculateMaxSpeed();
					if (flSpeed2D > flClassMax * 1.5f && flClassMax > 0.f)
						tMoveStorage.m_bExplosiveLaunch = true;
				}
			}
		}
	}

	SetupMoveData(tMoveStorage, bStrafe);

	// Log the sim inputs for diagnostic purposes
	tMoveStorage.m_flForwardMove = tMoveStorage.m_MoveData.m_flForwardMove;
	tMoveStorage.m_flSideMove = tMoveStorage.m_MoveData.m_flSideMove;

	if (bStrafe)
		StrafePrediction(tMoveStorage, bHitchance);

	tMoveStorage.m_vPath.push_back(tMoveStorage.m_MoveData.m_vecAbsOrigin);

	const int nChokedTicks = GetChokedTicks(pPlayer);
	for (int i = 0; i < nChokedTicks && !tMoveStorage.m_bFailed; i++)
		RunTick(tMoveStorage, false, nullptr);

	tMoveStorage.m_vPath.clear();
	tMoveStorage.m_vPath.push_back(tMoveStorage.m_MoveData.m_vecAbsOrigin);
	return !tMoveStorage.m_bFailed && !tMoveStorage.m_bInitFailed;
}

void CMovementSimulation::SetupMoveData(MoveStorage& tMoveStorage, bool bStrafe)
{
	C_TFPlayer* pPlayer = tMoveStorage.m_pPlayer;
	C_TFPlayer* pLocal = H::Entities ? H::Entities->GetLocal() : nullptr;
	auto& tMoveData = tMoveStorage.m_MoveData;
	const int iIndex = pPlayer->entindex();
	auto& vRecords = m_mRecords[iIndex];

	tMoveData = {};
	tMoveData.m_bFirstRunOfFunctions = false;
	tMoveData.m_bGameCodeMovedPlayer = false;
	tMoveData.m_nPlayerHandle = pPlayer->GetRefEHandle();
	tMoveData.m_vecVelocity = pPlayer->m_vecVelocity();
	tMoveData.m_vecAbsOrigin = pPlayer->m_vecOrigin();
	tMoveData.m_flMaxSpeed = SDK::MaxSpeed(pPlayer);
	tMoveData.m_flClientMaxSpeed = tMoveData.m_flMaxSpeed;
	tMoveData.m_nButtons = 0;
	tMoveData.m_nOldButtons = 0;

	CUserCmd* pCmd = G::CurrentUserCmd ? G::CurrentUserCmd : G::LastUserCmd;
	if (pPlayer == pLocal && pCmd)
	{
		tMoveData.m_vecViewAngles = pCmd->viewangles;
		tMoveData.m_flForwardMove = pCmd->forwardmove;
		tMoveData.m_flSideMove = pCmd->sidemove;
		tMoveData.m_flUpMove = pCmd->upmove;
		tMoveData.m_nButtons = pCmd->buttons;
	}
	else if (!tMoveData.m_vecVelocity.To2D().IsZero())
	{
		// View angles from velocity direction — always use velocity yaw (Amalgam approach).
		// Shield charge is the one exception where eye angles actually matter for movement.
		// Antiaim players have fake eye angles so we never trust them for direction.
		if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
			tMoveData.m_vecViewAngles = pPlayer->GetEyeAngles();
		else
			tMoveData.m_vecViewAngles = { 0.f, Math::VectorAngles(tMoveData.m_vecVelocity).y, 0.f };

		// Forward/side from records direction (Amalgam pattern: direction in world space
		// → DummyCmd → FixMovement rotates to view-relative space)
		if (!vRecords.empty() && !vRecords.front().m_vDirection.IsZero())
		{
			const Vec3& vDir = vRecords.front().m_vDirection;
			g_MoveSimCmd.forwardmove = vDir.x;
			g_MoveSimCmd.sidemove   = -vDir.y;
			g_MoveSimCmd.upmove     = vDir.z;
			H::AimUtils->FixMovement(&g_MoveSimCmd, {}, tMoveData.m_vecViewAngles);
			tMoveData.m_flForwardMove = g_MoveSimCmd.forwardmove;
			tMoveData.m_flSideMove    = g_MoveSimCmd.sidemove;
			tMoveData.m_flUpMove      = g_MoveSimCmd.upmove;
		}
		else
		{
			// No records — derive wish direction from current velocity at appropriate magnitude
			const bool bIsAir = !IsOnGround(pPlayer) && pPlayer->m_nWaterLevel() < 2;
			const float flClassMax = std::max(pPlayer->TeamFortress_CalculateMaxSpeed(), 100.f);
			Vec3 vVelDir = pPlayer->m_vecVelocity().To2D();
			if (!vVelDir.IsZero())
			{
				const float flSpeed = vVelDir.Length2D();
				vVelDir.NormalizeInPlace();
				const float flWishMag = bIsAir ? flClassMax : flSpeed;
				g_MoveSimCmd.forwardmove = vVelDir.x * flWishMag;
				g_MoveSimCmd.sidemove   = -(vVelDir.y * flWishMag);
				g_MoveSimCmd.upmove     = 0.f;
				H::AimUtils->FixMovement(&g_MoveSimCmd, {}, tMoveData.m_vecViewAngles);
				tMoveData.m_flForwardMove = g_MoveSimCmd.forwardmove;
				tMoveData.m_flSideMove    = g_MoveSimCmd.sidemove;
			}
		}
	}
	else if (!vRecords.empty() && !vRecords.front().m_vDirection.IsZero())
	{
		// Velocity zero but have direction history — use last known direction
		const Vec3& vDir = vRecords.front().m_vDirection;
		tMoveData.m_vecViewAngles = { 0.f, Math::VectorAngles(vDir).y, 0.f };
		g_MoveSimCmd.forwardmove = vDir.x;
		g_MoveSimCmd.sidemove   = -vDir.y;
		g_MoveSimCmd.upmove     = vDir.z;
		H::AimUtils->FixMovement(&g_MoveSimCmd, {}, tMoveData.m_vecViewAngles);
		tMoveData.m_flForwardMove = g_MoveSimCmd.forwardmove;
		tMoveData.m_flSideMove    = g_MoveSimCmd.sidemove;
		tMoveData.m_flUpMove      = g_MoveSimCmd.upmove;
	}

	// Set both angle fields — ProcessMovement reads m_vecOldAngles for delta calculations
	tMoveData.m_vecAngles = tMoveData.m_vecOldAngles = tMoveData.m_vecViewAngles;
	tMoveData.m_outStepHeight = 0.f;
	tMoveData.m_vecConstraintCenter = pPlayer->m_vecConstraintCenter();
	tMoveData.m_flConstraintRadius = pPlayer->m_flConstraintRadius();
	tMoveData.m_flConstraintWidth = pPlayer->m_flConstraintWidth();
	tMoveData.m_flConstraintSpeedFactor = pPlayer->m_flConstraintSpeedFactor();

	tMoveStorage.m_flSimTime = pPlayer->m_flSimulationTime();
	tMoveStorage.m_flPredictedDelta = GetPredictedDelta(pPlayer);
	tMoveStorage.m_flPredictedSimTime = tMoveStorage.m_flSimTime + tMoveStorage.m_flPredictedDelta;
	tMoveStorage.m_flTimeToTarget = CFG::Aimbot_Projectile_Max_Simulation_Time;  // Use menu CFG for yaw decay horizon (not choked-tick estimate)
	tMoveStorage.m_vPredictedOrigin = tMoveData.m_vecAbsOrigin;
	tMoveStorage.m_bDirectMove = IsOnGround(pPlayer) || GetMoveMode(pPlayer) == MoveMode::Swim || !bStrafe;
}

float CMovementSimulation::GetAverageYaw(C_TFPlayer* pPlayer, int iSamples)
{
	// OF-style: simple velocity-yaw delta averaging.
	// Average the yaw change between consecutive velocity vectors — no fuzzy filters,
	// no compression garbage removal, no direction-change caps. Fast and accurate.
	if (!pPlayer || iSamples <= 0)
		return 0.f;

	auto& vRecords = m_mRecords[pPlayer->entindex()];
	const int nMax = std::min(iSamples, static_cast<int>(vRecords.size()) - 1);
	if (nMax < 4)
		return 0.f;

	float flTotal = 0.f;
	int nSamples = 0;

	for (int i = 0; i < nMax; i++)
	{
		Vec3 vDir1 = vRecords[i].m_vVelocity;     vDir1.z = 0.f;
		Vec3 vDir2 = vRecords[i + 1].m_vVelocity; vDir2.z = 0.f;

		const float flLen1 = vDir1.Length2D();
		const float flLen2 = vDir2.Length2D();

		// Skip near-stationary frames — no meaningful yaw signal below 30 HU/s
		if (flLen1 < 30.f || flLen2 < 30.f)
			continue;

		const float flYaw1 = RAD2DEG(atan2f(vDir1.y, vDir1.x));
		const float flYaw2 = RAD2DEG(atan2f(vDir2.y, vDir2.x));
		float flDelta = flYaw1 - flYaw2;
		while (flDelta > 180.f)  flDelta -= 360.f;
		while (flDelta < -180.f) flDelta += 360.f;

		flTotal += flDelta;
		nSamples++;
	}

	if (nSamples < 4)
		return 0.f;

	const float flAvg = flTotal / static_cast<float>(nSamples);
	if (std::fabs(flAvg) < 0.05f)
		return 0.f;

	return std::clamp(flAvg, -10.f, 10.f);
}

void CMovementSimulation::StrafePrediction(MoveStorage& tMoveStorage, bool bHitchance)
{
	C_TFPlayer* pPlayer = tMoveStorage.m_pPlayer;
	if (!pPlayer)
		return;

	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.empty())
	{
		tMoveStorage.m_flAverageYaw = 0.f;
		return;
	}

	// Sample counts: ground 12 (faster converging), air 20 (more history for sustained strafes)
	const MoveMode iMode = vRecords.front().m_iMode;
	const int iSamples = iMode == MoveMode::Ground ? 12 : 20;
	tMoveStorage.m_flAverageYaw = GetAverageYaw(pPlayer, iSamples);

	// Suppress yaw only for explosive launches (rocket/sticky jump) — genuinely unpredictable trajectory.
	// RigidGround and RecentReversal suppression removed: GetAverageYaw naturally returns ~0
	// for straight walkers (constant velocity direction → near-zero yaw delta).
	// Amalgam does not suppress based on movement state either.
	if (tMoveStorage.m_bExplosiveLaunch)
		tMoveStorage.m_flAverageYaw = 0.f;

	if (!bHitchance)
		return;

	// NOTE: Amalgam does NOT zero averageYaw based on wall traces.
	// The old wall-trace zeroing (TraceHull + plane.normal.z < 0.7f check) was causing
	// airborne players near walls/corners to have their averageYaw zeroed, which then
	// zeroed their movement input in RunTick, making them fall straight down instead
	// of continuing their strafe arc. Removed — GetAverageYaw naturally handles this.
}

bool CMovementSimulation::IsBunnyHopping(C_TFPlayer* pPlayer, float* pCadenceOut)
{
	// Bhop detection from mode transitions + velocity filter.
	//
	// Key insight: a bhop jump has velocity.z in [200, 320] (TF2 jump impulse ~270).
	// Explosive launches (rocket/sticky) have velocity.z >> 400.
	// Edge falls have velocity.z < 100 at the ground→air transition.
	//
	// Records are newest-first. Takeoff = ground(i+1)→air(i).
	// Landing = air(i+1)→ground(i). We need 1 confirmed short-contact cycle.
	if (pCadenceOut)
		*pCadenceOut = 0.f;

	if (!pPlayer)
		return false;

	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.size() < 4)
		return false;

	constexpr float JumpVzMin = 200.f;   // Below this = edge fall, not a jump
	constexpr float JumpVzMax = 400.f;   // Above this = explosive launch, not a bhop
	constexpr float MaxGroundContact = 0.3f;

	const int nMax = std::min<int>(66, static_cast<int>(vRecords.size()) - 1);

	int nValidCycles = 0;
	float flPendingTakeoffTime = -1.f;
	float flPendingTakeoffVz = 0.f;

	for (int i = 0; i < nMax; i++)
	{
		const bool bNewerGround = (vRecords[i].m_iMode == MoveMode::Ground);
		const bool bOlderGround = (vRecords[i + 1].m_iMode == MoveMode::Ground);

		if (!bNewerGround && bOlderGround)
		{
			// Takeoff (ground→air): record time + velocity.z to filter out non-jump transitions
			const float flVz = vRecords[i].m_vVelocity.z;
			if (flVz >= JumpVzMin && flVz <= JumpVzMax)
			{
				flPendingTakeoffTime = vRecords[i].m_flSimTime;
				flPendingTakeoffVz = flVz;
			}
			else
			{
				// Explosive launch or edge fall — not a bhop takeoff, reset
				flPendingTakeoffTime = -1.f;
			}
		}
		else if (bNewerGround && !bOlderGround && flPendingTakeoffTime >= 0.f)
		{
			// Landing that preceded the takeoff — short ground contact = bhop cycle
			const float flGroundContact = flPendingTakeoffTime - vRecords[i].m_flSimTime;
			if (flGroundContact >= 0.f && flGroundContact <= MaxGroundContact)
			{
				nValidCycles++;
				if (pCadenceOut && nValidCycles == 1)
					*pCadenceOut = flGroundContact;
			}
			flPendingTakeoffTime = -1.f;
		}
	}

	// 1 confirmed cycle is enough — auto-bhop is very consistent once detected
	return nValidCycles >= 1;
}

int CMovementSimulation::GetChokedTicks(C_TFPlayer* pPlayer) const
{
	if (!pPlayer)
		return 0;

	const int nIndex = pPlayer->entindex();
	auto it = m_mSimTimes.find(nIndex);
	if (it != m_mSimTimes.end() && !it->second.empty())
		return std::clamp(TIME_TO_TICKS(it->second.front()) - 1, 0, MaxChokedTicks);

	auto itRecords = m_mRecords.find(nIndex);
	if (itRecords == m_mRecords.end() || itRecords->second.size() < 2)
		return 0;

	const float flDelta = itRecords->second[0].m_flSimTime - itRecords->second[1].m_flSimTime;
	return std::clamp(TIME_TO_TICKS(flDelta) - 1, 0, MaxChokedTicks);
}

void CMovementSimulation::RunTick()
{
	RunTick(m_CurrentStorage, true, nullptr);
}

void CMovementSimulation::RunTick(MoveStorage& tMoveStorage, bool bPath, const RunTickCallback* pCallback)
{
	C_TFPlayer* pPlayer = tMoveStorage.m_pPlayer;
	if (!pPlayer || tMoveStorage.m_bFailed || tMoveStorage.m_bInitFailed || !I::GlobalVars)
	{
		tMoveStorage.m_bFailed = true;
		return;
	}

	auto& md = tMoveStorage.m_MoveData;

	// Ensure prediction state is set — ProcessMovement reads these
	I::Prediction->m_bInPrediction = true;
	I::Prediction->m_bFirstTimePredicted = false;
	I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;
	SetBounds(pPlayer);

	// Strafe: rotate view angle per tick so ProcessMovement's AirAccelerate/WalkMove
	// computes the correct wish direction for the player's turning rate.
	// Air: +90° correction ensures wishdir is perpendicular to velocity (max AirAccelerate efficiency).
	// Ground: no correction needed — ProcessMovement handles walk acceleration directly.
	float flCorrection = 0.f;
	if (tMoveStorage.m_flAverageYaw)
	{
		float flMult = 1.f;
		if (!tMoveStorage.m_bDirectMove && !pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			flCorrection = 90.f * sign(tMoveStorage.m_flAverageYaw);
			if (CFG::Aimbot_Projectile_Friction_Flags & 2)  // RunReduce
				flMult = GetAirFrictionScale(md.m_vecVelocity.Length2D(), tMoveStorage.m_flAverageYaw, md.m_vecVelocity.z + SDKUtils::GetGravity() * TICK_INTERVAL);
		}
		md.m_vecViewAngles.y += tMoveStorage.m_flAverageYaw * flMult + flCorrection;
	}
	else if (!tMoveStorage.m_bDirectMove && !tMoveStorage.m_bBunnyHop)
	{
		// Amalgam: when airborne and averageYaw=0, zero movement input.
		// Without strafe prediction, stale forwardmove/sidemove would cause
		// ProcessMovement to AirAccelerate in a random direction. Zeroing
		// makes the sim apply only gravity, which is more accurate.
		// EXCEPTION: bhoppers are actively air-strafing even when averageYaw=0
		// (records might not have enough history yet). Zeroing their input would
		// kill AirAccelerate speed gain, predicting them at walking speed instead
		// of bhop speed. Keep their existing forwardmove/sidemove alive.
		md.m_flForwardMove = 0.f;
		md.m_flSideMove = 0.f;
	}

	// Duck speed penalty (ProcessMovement expects this pre-applied)
	const float flOldSpeed = md.m_flClientMaxSpeed;
	if (pPlayer->m_bDucked() && IsOnGround(pPlayer) && pPlayer->m_nWaterLevel() < 2)
		md.m_flClientMaxSpeed /= 3.f;

	// Bhop: inject IN_JUMP after the measured ground contact cadence.
	// Auto-bhop scripts often jump on the same tick they land (cadence ≈ 0),
	// but some players have a slight delay (0.1-0.14s). We track ground ticks
	// and only inject the jump after the measured cadence has elapsed.
	if (tMoveStorage.m_bBunnyHop && IsOnGround(pPlayer) && !pPlayer->m_bDucked())
	{
		tMoveStorage.m_iBhopSimTicksOnGround++;
		const int nCadenceTicks = TIME_TO_TICKS(tMoveStorage.m_flBhopCadence);
		if (tMoveStorage.m_iBhopSimTicksOnGround > nCadenceTicks)
		{
			md.m_nOldButtons = 0;
			md.m_nButtons |= IN_JUMP;
		}
	}
	else
	{
		// Airborne or ducked — reset ground counter
		tMoveStorage.m_iBhopSimTicksOnGround = 0;
	}

	// ProcessMovement: handles ALL physics — gravity, friction, air accel, walk accel,
	// crouching, zooming, water, ladders, ramps, etc. This is the actual TF2 game code.
	I::GameMovement->ProcessMovement(pPlayer, &md);
	if (pCallback)
		(*pCallback)(md);

	md.m_flClientMaxSpeed = flOldSpeed;

	const bool bLastDirectMove = tMoveStorage.m_bDirectMove;
	tMoveStorage.m_bDirectMove = IsOnGround(pPlayer) || pPlayer->m_nWaterLevel() >= 2;

	// Un-apply the air correction (cumulative averageYaw rotation is intentionally kept)
	if (tMoveStorage.m_flAverageYaw)
		md.m_vecViewAngles.y -= flCorrection;
	else if (tMoveStorage.m_bDirectMove && !bLastDirectMove
		&& !md.m_flForwardMove && !md.m_flSideMove
		&& md.m_vecVelocity.Length2D() > md.m_flMaxSpeed * 0.015f)
	{
		// Player just landed with no explicit move input — re-derive forward/side from velocity
		Vec3 vDir = md.m_vecVelocity.Normalized2D() * 450.f;
		g_MoveSimCmd.forwardmove = vDir.x;
		g_MoveSimCmd.sidemove = -vDir.y;
		H::AimUtils->FixMovement(&g_MoveSimCmd, {}, md.m_vecViewAngles);
		md.m_flForwardMove = g_MoveSimCmd.forwardmove;
		md.m_flSideMove = g_MoveSimCmd.sidemove;
	}

	RestoreBounds(pPlayer);

	tMoveStorage.m_flSimTime += TICK_INTERVAL;
	tMoveStorage.m_flTimeToTarget = std::max(0.f, tMoveStorage.m_flTimeToTarget - TICK_INTERVAL);
	tMoveStorage.m_bPredictNetworked = tMoveStorage.m_flSimTime >= tMoveStorage.m_flPredictedSimTime;
	if (tMoveStorage.m_bPredictNetworked)
	{
		tMoveStorage.m_vPredictedOrigin = md.m_vecAbsOrigin;
		tMoveStorage.m_flPredictedSimTime += tMoveStorage.m_flPredictedDelta;
	}

	if (bPath)
		tMoveStorage.m_vPath.push_back(md.m_vecAbsOrigin);
}


void CMovementSimulation::Restore()
{
	Restore(m_CurrentStorage);
	m_CurrentStorage = {};
}

void CMovementSimulation::Restore(MoveStorage& tMoveStorage)
{
	if (tMoveStorage.m_pPlayer)
	{
		RestoreBounds(tMoveStorage.m_pPlayer);

		// Explicitly restore model scale and origin — rendered netvars that CPredictionCopy may miss
		if (tMoveStorage.m_bHasBackup)
		{
			tMoveStorage.m_pPlayer->m_flModelScale() = tMoveStorage.m_flOldModelScale;
			tMoveStorage.m_pPlayer->m_vecOrigin() = tMoveStorage.m_vOldOrigin;
		}

		tMoveStorage.m_pPlayer->SetCurrentCommand(nullptr);
	}

	if (I::MoveHelper)
		I::MoveHelper->SetHost(nullptr);

	ResetState(tMoveStorage);

	if (I::Prediction)
	{
		I::Prediction->m_bInPrediction = g_bOldInPrediction;
		I::Prediction->m_bFirstTimePredicted = g_bOldFirstTimePredicted;
	}

	if (I::GlobalVars)
		I::GlobalVars->frametime = g_flOldFrametime;
}

void CMovementSimulation::SetBounds(C_TFPlayer* pPlayer)
{
	if (!pPlayer || g_bBoundsChanged || !I::TFGameRules())
		return;

	CTFGameRules* pRules = I::TFGameRules();
	if (!pRules || !pRules->GetViewVectors())
		return;

	auto pViewVectors = pRules->GetViewVectors();
	g_vOriginalHullMin = pViewVectors->m_vHullMin;
	g_vOriginalHullMax = pViewVectors->m_vHullMax;
	g_vOriginalDuckHullMin = pViewVectors->m_vDuckHullMin;
	g_vOriginalDuckHullMax = pViewVectors->m_vDuckHullMax;

	const Vec3 vCompression(PlayerOriginCompression, PlayerOriginCompression, 0.f);
	pViewVectors->m_vHullMin = pPlayer->m_vecMins() + vCompression;
	pViewVectors->m_vHullMax = pPlayer->m_vecMaxs() - vCompression;
	pViewVectors->m_vDuckHullMin = pPlayer->m_vecMins() + vCompression;
	pViewVectors->m_vDuckHullMax = pPlayer->m_vecMaxs() - vCompression;
	g_bBoundsChanged = true;
}

void CMovementSimulation::RestoreBounds(C_TFPlayer* pPlayer)
{
	if (!pPlayer || !g_bBoundsChanged || !I::TFGameRules())
		return;

	CTFGameRules* pRules = I::TFGameRules();
	if (!pRules || !pRules->GetViewVectors())
		return;

	auto pViewVectors = pRules->GetViewVectors();
	pViewVectors->m_vHullMin = g_vOriginalHullMin;
	pViewVectors->m_vHullMax = g_vOriginalHullMax;
	pViewVectors->m_vDuckHullMin = g_vOriginalDuckHullMin;
	pViewVectors->m_vDuckHullMax = g_vOriginalDuckHullMax;
	g_bBoundsChanged = false;
}

float CMovementSimulation::GetPredictedDelta(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return TICK_INTERVAL;

	auto& vSimTimes = m_mSimTimes[pPlayer->entindex()];
	if (vSimTimes.empty())
		return TICK_INTERVAL;

	float flTotal = 0.f;
	for (const float flDelta : vSimTimes)
		flTotal += flDelta;

	return std::clamp(flTotal / static_cast<float>(vSimTimes.size()), TICK_INTERVAL, TICKS_TO_TIME(MaxChokedTicks + 1));
}

void CMovementSimulation::ClearRecords()
{
	if (m_CurrentStorage.m_pData)
		Restore(m_CurrentStorage);

	m_CurrentStorage = {};
	m_mRecords.clear();
	m_mSimTimes.clear();
}

bool CMovementSimulation::IsAntiAiming(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return false;

	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.size() < 6)
		return false;

	// Antiaim heuristic: eye angles jitter wildly while origin trajectory is smooth

	constexpr int iSamples = 6;
	const int iMax = std::min<int>(iSamples, static_cast<int>(vRecords.size()) - 1);

	// Compute eye yaw deltas
	float flEyeYawVariance = 0.f;
	int iEyeSamples = 0;
	for (int i = 0; i < iMax; i++)
	{
		const float flDelta = std::fabs(Math::NormalizeAngle(vRecords[i].m_flEyeYaw - vRecords[i + 1].m_flEyeYaw));
		flEyeYawVariance += flDelta * flDelta;
		iEyeSamples++;
	}
	if (iEyeSamples < 3)
		return false;
	flEyeYawVariance /= static_cast<float>(iEyeSamples);

	// Compute origin-trajectory yaw deltas
	float flOriginYawVariance = 0.f;
	int iOriginSamples = 0;
	for (int i = 0; i < iMax; i++)
	{
		Vec3 vCurDelta = (vRecords[i].m_vOrigin - vRecords[i + 1].m_vOrigin).To2D();
		if (vCurDelta.IsZero())
			continue;

		if (i + 2 >= static_cast<int>(vRecords.size()))
			break;
		Vec3 vPrevDelta = (vRecords[i + 1].m_vOrigin - vRecords[i + 2].m_vOrigin).To2D();
		if (vPrevDelta.IsZero())
			continue;

		const float flCurYaw = YawFromVector(vCurDelta);
		const float flPrevYaw = YawFromVector(vPrevDelta);
		const float flDelta = std::fabs(Math::NormalizeAngle(flCurYaw - flPrevYaw));
		flOriginYawVariance += flDelta * flDelta;
		iOriginSamples++;
	}
	if (iOriginSamples < 2)
		return false;
	flOriginYawVariance /= static_cast<float>(iOriginSamples);

	// Antiaim: eye variance >> origin variance, or eye jitter >30° RMS with smooth trajectory
	constexpr float flEyeVarianceThreshold = 900.f;   // ~30° RMS
	constexpr float flOriginVarianceMax = 225.f;       // ~15° RMS (smooth trajectory)
	constexpr float flRatioThreshold = 4.f;            // eye variance >= 4x origin variance

	if (flEyeYawVariance > flEyeVarianceThreshold && flOriginYawVariance < flOriginVarianceMax)
		return true;

	if (flOriginYawVariance > 0.f && flEyeYawVariance / flOriginYawVariance > flRatioThreshold)
		return true;

	return false;
}

float CMovementSimulation::GetOriginCurvature(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return 0.f;

	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.size() < 4)
		return 0.f;

	// Compute average turning rate from origin position history (immune to antiaim/interpolation)
	constexpr int iSamples = 8;
	const int iMax = std::min<int>(iSamples, static_cast<int>(vRecords.size()) - 2);

	float flTotalYawDelta = 0.f;
	int iValidSamples = 0;
	int iLastSign = 0;
	int iDirectionChanges = 0;

	for (int i = 0; i < iMax; i++)
	{
		Vec3 vCurDelta = (vRecords[i].m_vOrigin - vRecords[i + 1].m_vOrigin).To2D();
		if (vCurDelta.IsZero())
			continue;

		Vec3 vPrevDelta = (vRecords[i + 1].m_vOrigin - vRecords[i + 2].m_vOrigin).To2D();
		if (vPrevDelta.IsZero())
			continue;

		if (vCurDelta.LengthSqr() < 1.f || vPrevDelta.LengthSqr() < 1.f)
			continue;

		const float flCurYaw = YawFromVector(vCurDelta);
		const float flPrevYaw = YawFromVector(vPrevDelta);
		float flDelta = Math::NormalizeAngle(flCurYaw - flPrevYaw);

		if (std::fabs(flDelta) < 0.1f)
			flDelta = 0.f;

		if (std::fabs(flDelta) > 90.f)
			break;

		const int iSign = sign(flDelta);
		if (iLastSign && iSign && iSign != iLastSign)
		{
			iDirectionChanges++;
			if (iDirectionChanges > 4)
				break;
		}
		if (iSign)
			iLastSign = iSign;

		flTotalYawDelta += flDelta;
		iValidSamples++;
	}

	if (iValidSamples < 2)
		return 0.f;

	const float flAverage = flTotalYawDelta / static_cast<float>(iValidSamples);
	if (std::fabs(flAverage) < 0.02f)
		return 0.f;

	return std::clamp(flAverage, -10.f, 10.f);
}
