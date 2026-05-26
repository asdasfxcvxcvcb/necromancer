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

		// SEOwned duck fix: remove FL_DUCKING (breaks origin Z), set m_bInDuckJump to prevent re-duck
		if (pPlayer->m_fFlags() & FL_DUCKING)
		{
			pPlayer->m_fFlags() &= ~FL_DUCKING;
			// Only set ducked=true if actually ducked — stale FL_DUCKING alone isn't enough
			if (!pPlayer->m_bDucked())
				pPlayer->m_bDucked() = false;  // Explicit: not ducked, just had stale flag
			pPlayer->m_bDucking() = false;
			pPlayer->m_bInDuckJump() = true;  // Prevents re-duck during sim
			pPlayer->m_flDucktime() = 0.f;
			pPlayer->m_flDuckJumpTime() = 0.f;
		}

		// Skip model scale modification — SetBounds already compresses collision hull

		// Lift origin off ground to prevent ground sticking
		if (IsOnGround(pPlayer))
			pPlayer->m_vecOrigin().z += 0.03125f * 3.0f;

		pPlayer->m_vecBaseVelocity() = {};

		// Zero Z velocity on ground (residual downward Z causes false falling)
		if (IsOnGround(pPlayer))
			pPlayer->m_vecVelocity().z = 0.0f;
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
		const Vec3 vVel2D = pPlayer->m_vecVelocity().To2D();
		const float flVelYaw = Math::VectorAngles(vVel2D).y;
		const bool bOnGround = IsOnGround(pPlayer);
		const bool bIsAir = !bOnGround && GetMoveMode(pPlayer) != MoveMode::Swim;
		const float flClassMaxSpeed = std::max(pPlayer->TeamFortress_CalculateMaxSpeed(), 100.f);

		if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
			tMoveData.m_vecViewAngles = pPlayer->GetEyeAngles();
		else if (tMoveStorage.m_bAntiAimDetected)
		{
			float flViewYaw = flVelYaw;
			if (!vRecords.empty() && vRecords.size() >= 2)
			{
				Vec3 vOriginDelta = (vRecords[0].m_vOrigin - vRecords[1].m_vOrigin).To2D();
				if (!vOriginDelta.IsZero())
					flViewYaw = YawFromVector(vOriginDelta);
			}
			tMoveData.m_vecViewAngles = { 0.f, flViewYaw, 0.f };
		}
		else if (tMoveStorage.m_bStrafing)
			tMoveData.m_vecViewAngles = { 0.f, pPlayer->GetEyeAngles().y, 0.f };
		else
			tMoveData.m_vecViewAngles = { 0.f, flVelYaw, 0.f };

		const bool bIsGroundStrafing = tMoveStorage.m_bStrafing && bOnGround && !bIsAir;
		if (!vRecords.empty() && !bIsGroundStrafing)
		{
			Vec3 vDirection = vRecords.front().m_vDirection;

			const float flCurSpeed2D = vVel2D.Length();
			if (!vDirection.IsZero() && flCurSpeed2D > 50.f)
			{
				Vec3 vDir2D = { vDirection.x, vDirection.y, 0.f };
				const float flDirLen = vDir2D.Length();
				if (flDirLen > 0.f)
				{
					const float flDot = (vDir2D.x * vVel2D.x + vDir2D.y * vVel2D.y)
						/ (flDirLen * flCurSpeed2D);
					// Records direction disagrees with current velocity (>72° off).
					// Fall through to velocity-based fallback by zeroing vDirection.
					if (flDot < 0.3f)
						vDirection = {};
				}
			}

			// Air: rescale to MaxSpeed; Ground: use raw magnitude
			if (!vDirection.IsZero())
			{
				if (bIsAir)
				{
					const float flDirLen = vDirection.Length2D();
					if (flDirLen > 0.f && flDirLen < flClassMaxSpeed)
					{
						const float flScale = flClassMaxSpeed / flDirLen;
							vDirection.x *= flScale;
							vDirection.y *= flScale;
					}
				}
				DecomposeWishDir(vDirection, tMoveData.m_vecViewAngles.y, tMoveData.m_flForwardMove, tMoveData.m_flSideMove);
				tMoveData.m_flUpMove = vDirection.z;
			}
			else
			{
				// Records direction zero — derive from velocity
				Vec3 vVelDir = pPlayer->m_vecVelocity().To2D();
				if (!vVelDir.IsZero())
				{
					vVelDir.NormalizeInPlace();
					// Air: MaxSpeed; Ground: actual speed
					const float flWishMag = bIsAir ? flClassMaxSpeed : vVel2D.Length();
					const Vec3 vWishDir = vVelDir * flWishMag;
					DecomposeWishDir(vWishDir, tMoveData.m_vecViewAngles.y, tMoveData.m_flForwardMove, tMoveData.m_flSideMove);
				}
			}
		}
		else
		{
			// No records or grounded strafing — derive from velocity
			Vec3 vVelDir = pPlayer->m_vecVelocity().To2D();
			if (!vVelDir.IsZero())
			{
				vVelDir.NormalizeInPlace();
				// Air/ground-strafe: MaxSpeed; Ground non-strafe: actual speed
				const float flWishMag = (bIsAir || bIsGroundStrafing) ? flClassMaxSpeed : vVel2D.Length();
				const Vec3 vWishDir = vVelDir * flWishMag;
				DecomposeWishDir(vWishDir, tMoveData.m_vecViewAngles.y, tMoveData.m_flForwardMove, tMoveData.m_flSideMove);
			}
		}
	}
	// Velocity zero but have direction history — use last known direction
	else if (!vRecords.empty() && !vRecords.front().m_vDirection.IsZero())
	{
		Vec3 vDirection = vRecords.front().m_vDirection;
		
		tMoveData.m_vecViewAngles = { 0.f, Math::VectorAngles(vDirection).y, 0.f };
		DecomposeWishDir(vDirection, tMoveData.m_vecViewAngles.y, tMoveData.m_flForwardMove, tMoveData.m_flSideMove);
		tMoveData.m_flUpMove = vDirection.z;
	}

	tMoveData.m_vecAngles = tMoveData.m_vecViewAngles;
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
	if (!pPlayer || iSamples <= 0)
		return 0.f;

	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.size() < 3)
		return 0.f;

	const MoveMode iMode = vRecords.front().m_iMode;
	const int iMaxSamples = std::min<int>(iSamples, static_cast<int>(vRecords.size()) - 1);
	float flYawTotal = 0.f;
	int iTickTotal = 0;
	int iRealSamples = 0;
	int iDirectionChanges = 0;
	int iLastSign = 0;

	// Ground: allow up to 8 direction changes (A/D oscillation averages to net yaw)
	// Air: 3 max (air strafes are more sustained)
	const int iMaxDirectionChanges = iMode == MoveMode::Ground ? 8 : 3;

	// Ground: accumulate all samples, let average work (no break on large deltas or too-straight filter)

	for (int i = 0; i < iMaxSamples; i++)
	{
		const auto& tCurrent = vRecords[i];
		const auto& tPrevious = vRecords[i + 1];
		if (tCurrent.m_iMode != iMode || tPrevious.m_iMode != iMode)
			break;

		if (tCurrent.m_bHitWall || tPrevious.m_bHitWall)
			continue;

		if (tCurrent.m_vDirection.IsZero() || tPrevious.m_vDirection.IsZero())
			continue;

		const float flTimeDelta = tCurrent.m_flSimTime - tPrevious.m_flSimTime;
		const int iTicks = std::max(TIME_TO_TICKS(flTimeDelta), 1);

		float flYaw = Math::NormalizeAngle(YawFromVector(tCurrent.m_vDirection) - YawFromVector(tPrevious.m_vDirection));

		// Compression garbage cleaning: air only. Ground noise is same range as real micro-movements.
		if (iMode == MoveMode::Air)
		{
			constexpr float COMPRESSION_GARBAGE = 1.507446f;
			if (std::fabs(flYaw) < 5.f)
			{
				if (flYaw < 0.f)
					flYaw += COMPRESSION_GARBAGE;
				else if (flYaw > 0.f)
					flYaw -= COMPRESSION_GARBAGE;

				if (std::fabs(flYaw) < 0.3f)
					flYaw = 0.f;
			}
		}

		// Ground: don't break on large deltas (sharp turns are real). Air: keep 45° break.
		if (iMode == MoveMode::Air && std::fabs(flYaw) > 45.f)
			break;

		const float flSpeed = std::max(tCurrent.m_vVelocity.Length2D(), 1.f);
		const int iYawSign = sign(flYaw);

		// Ground: count direction changes only, no too-straight filter. Air: keep both.
		const bool bTooStraight = iMode == MoveMode::Air && std::fabs(flYaw) * flSpeed * iTicks < 15.f;
		if ((iLastSign && iYawSign && iYawSign != iLastSign) || bTooStraight)
		{
			iDirectionChanges++;
			if (iDirectionChanges > iMaxDirectionChanges)
				break;
		}

		if (iYawSign)
			iLastSign = iYawSign;

		flYawTotal += flYaw;
		iTickTotal += iTicks;
		iRealSamples++;
	}

	const int iMinSamples = iMode == MoveMode::Ground ? 3 : 5;
	if (iRealSamples < iMinSamples || iTickTotal <= 0)
		return 0.f;

	const float flAverage = flYawTotal / static_cast<float>(iTickTotal);
	// Ground threshold raised: 0.5°/tick minimum to filter out interpolation/prediction noise.
	// Real ground strafes produce 1-5°/tick; noise is 0.01-0.3°/tick.
	if (std::fabs(flAverage) < (iMode == MoveMode::Ground ? 0.5f : 0.12f))
		return 0.f;

	return std::clamp(flAverage, -10.f, 10.f);
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

	const MoveMode iMode = vRecords.front().m_iMode;
	const int iSamples = iMode == MoveMode::Ground ? 12 : 20;
	tMoveStorage.m_flAverageYaw = GetAverageYaw(pPlayer, iSamples);

	// Fallback yaw estimation when GetAverageYaw returns too-small values.
	// Air: origin curvature captures net trajectory; Ground: VelEyeDelta + cross-product for direction.

	// Air curvature fallback: use origin curvature when 2x+ larger than averageYaw
	{
		const bool bAirborne = vRecords.front().m_iMode == MoveMode::Air;
		const float flCurvatureAbs = std::fabs(tMoveStorage.m_flOriginCurvature);
		const bool bExplosiveHighCurve = tMoveStorage.m_bExplosiveLaunch && flCurvatureAbs > 2.0f;
		if (bAirborne && flCurvatureAbs > 0.4f
			&& std::fabs(tMoveStorage.m_flAverageYaw) < flCurvatureAbs * 0.5f
			&& !bExplosiveHighCurve)
		{
			// Scale by 0.7: AirAccelerate already contributes curve; full curvature would over-curve
			tMoveStorage.m_flAverageYaw = tMoveStorage.m_flOriginCurvature * 0.7f;
		}
	}

	// Air VelEyeDelta fallback: estimate yaw from VelEyeDelta when curvature hasn't built up yet
	{
		const bool bAirborne = vRecords.front().m_iMode == MoveMode::Air;
		if (bAirborne
			&& std::fabs(tMoveStorage.m_flAverageYaw) < 0.5f
			&& tMoveStorage.m_flVelEyeDeltaAtInit > 10.f)
		{
			const Vec3 vVel2D = pPlayer->m_vecVelocity().To2D();
			const float flVelLen = vVel2D.Length();
			if (flVelLen > 50.f)
			{
				const float flEyeYaw = pPlayer->GetEyeAngles().y * DEG2RAD(1.f);
				const float flEyeFwdX = cosf(flEyeYaw);
				const float flEyeFwdY = sinf(flEyeYaw);
				// Cross product: vel × eyeFwd gives strafe direction
				const float flCross = vVel2D.x * flEyeFwdY - vVel2D.y * flEyeFwdX;
				const float flDirection = flCross > 0.f ? -1.f : 1.f;

				// Yaw magnitude from VelEyeDelta: 90°→2.0, 45°→1.0, 20°→0.5 deg/tick
				const float flSpeedRatio = std::clamp(flVelLen / std::max(pPlayer->TeamFortress_CalculateMaxSpeed(), 100.f), 0.f, 1.f);
				const float flVelEyeScale = std::clamp(tMoveStorage.m_flVelEyeDeltaAtInit / 90.f, 0.15f, 1.f);
				float flMagnitude = flVelEyeScale * flSpeedRatio * 2.0f;

				// Explosive launch: less predictable but still strafing
				if (tMoveStorage.m_bExplosiveLaunch)
					flMagnitude *= 0.6f;

				const float flEstimatedYaw = flDirection * flMagnitude;
				if (std::fabs(flEstimatedYaw) > std::fabs(tMoveStorage.m_flAverageYaw))
					tMoveStorage.m_flAverageYaw = std::clamp(flEstimatedYaw, -10.f, 10.f);
			}
		}
	}

	// Grounded fallback: direction estimation for strafers with low averageYaw
	if (vRecords.front().m_iMode == MoveMode::Ground)
	{
		const bool bNeedsFallback = std::fabs(tMoveStorage.m_flAverageYaw) < 0.5f
			&& tMoveStorage.m_flVelEyeDeltaAtInit > 10.f;
		const bool bNeedsBoost = std::fabs(tMoveStorage.m_flAverageYaw) > 0.1f
			&& std::fabs(tMoveStorage.m_flAverageYaw) < std::fabs(tMoveStorage.m_flOriginCurvature) * 0.6f
			&& std::fabs(tMoveStorage.m_flOriginCurvature) > 0.5f;

		if (bNeedsFallback || bNeedsBoost)
		{
			const Vec3 vVel2D = pPlayer->m_vecVelocity().To2D();
			const float flVelLen = vVel2D.Length();
			if (flVelLen > 50.f)
			{
				const float flEyeYaw = pPlayer->GetEyeAngles().y * DEG2RAD(1.f);
				const float flEyeFwdX = cosf(flEyeYaw);
				const float flEyeFwdY = sinf(flEyeYaw);
				const float flCross = vVel2D.x * flEyeFwdY - vVel2D.y * flEyeFwdX;
				const float flSpeedRatio = std::clamp(flVelLen / pPlayer->TeamFortress_CalculateMaxSpeed(), 0.f, 1.f);
				const bool bReversing = tMoveStorage.m_flVelEyeDeltaAtInit > 90.f;

				if (bNeedsFallback)
				{
					// Prefer origin curvature (position-based), fall back to cross product
					float flDirection = 0.f;
					if (std::fabs(tMoveStorage.m_flOriginCurvature) > 0.5f)
						flDirection = tMoveStorage.m_flOriginCurvature > 0.f ? 1.f : -1.f;
					else if (!bReversing)
						flDirection = flCross > 0.f ? -1.f : 1.f;

					if (flDirection == 0.f)
					{
						// No reliable direction — leave averageYaw at 0
					}
					else
					{
						// Yaw magnitude: scale by VelEyeDelta and speed
						const float flVelEyeScale = std::clamp(tMoveStorage.m_flVelEyeDeltaAtInit / 90.f, 0.2f, 1.f);
						float flMagnitude = flVelEyeScale * flSpeedRatio * 3.0f;

						// Reversing penalty: VelEyeDelta > 90°, direction uncertain
						if (bReversing)
							flMagnitude *= 0.5f;

						const float flEstimatedYaw = flDirection * flMagnitude;
						if (std::fabs(flEstimatedYaw) > 0.2f)
							tMoveStorage.m_flAverageYaw = std::clamp(flEstimatedYaw, -10.f, 10.f);
					}
				}
				else if (bNeedsBoost)
				{
					// Boost averageYaw toward curvature when velocity oscillation cancelled signal
					const float flCurvSign = tMoveStorage.m_flOriginCurvature > 0.f ? 1.f : -1.f;
					const float flBoosted = flCurvSign * std::fabs(tMoveStorage.m_flOriginCurvature) * 0.8f;
					if (std::fabs(flBoosted) > std::fabs(tMoveStorage.m_flAverageYaw))
						tMoveStorage.m_flAverageYaw = std::clamp(flBoosted, -10.f, 10.f);
				}
			}
		}
	}

	if (!bHitchance)
		return;

	const Vec3 vStart = tMoveStorage.m_MoveData.m_vecAbsOrigin;
	const Vec3 vVelocity = tMoveStorage.m_MoveData.m_vecVelocity;
	CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
	filter.pSkip = pPlayer;

	trace_t trace = {};
	SDK::TraceHull(vStart, vStart + vVelocity * TICK_INTERVAL, pPlayer->m_vecMins(), pPlayer->m_vecMaxs(), SolidMask(pPlayer), &filter, &trace);
	if (trace.DidHit() && trace.plane.normal.z < 0.7f)
		tMoveStorage.m_flAverageYaw = 0.f;
}

bool CMovementSimulation::IsBunnyHopping(C_TFPlayer* pPlayer, float* pCadenceOut)
{
	// Bhop detection: jump→land→jump pattern with <200ms ground contact
	if (pCadenceOut)
		*pCadenceOut = 0.f;

	if (!pPlayer)
		return false;

	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.size() < 4)
		return false;

	// Must have meaningful speed — walking at 10 HU/s isn't bhopping
	const float flSpeed = vRecords.front().m_vVelocity.Length2D();
	if (flSpeed < 100.f)
		return false;

	// Scan history for jump→land→jump cycles (ground contact < 200ms)
	constexpr float MaxGroundContactTime = 0.200f;  // 200ms — about 13 ticks
	constexpr int MaxHistory = 40;
	const int iMax = std::min<int>(static_cast<int>(vRecords.size()) - 1, MaxHistory);

	int iCycles = 0;                // Complete jump→land→jump cycles
	float flCadenceTotal = 0.f;     // Sum of jump-to-jump times (for averaging)
	int iCadenceSamples = 0;
	float flLastTakeoffTime = 0.f;  // SimTime of the last takeoff we found

	// State machine: track ground contact from most recent to oldest
	int iState = 0;  // 0 = looking for land, 1 = on ground measuring, 2 = found takeoff
	float flLandTime = 0.f;
	float flTakeoffTime = 0.f;

	for (int i = 0; i < iMax; i++)
	{
		const auto& tNew = vRecords[i];      // More recent
		const auto& tOld = vRecords[i + 1];  // Older
		const bool bNewGround = tNew.m_iMode == MoveMode::Ground;
		const bool bOldGround = tOld.m_iMode == MoveMode::Ground;

		// Takeoff: was on ground, now in air
		if (!bNewGround && bOldGround)
		{
			flTakeoffTime = tNew.m_flSimTime;

			// If we were measuring ground contact time
			if (iState == 1)
			{
				const float flGroundTime = flLandTime - flTakeoffTime;
				if (flGroundTime >= 0.f && flGroundTime < MaxGroundContactTime)
				{
					// Valid cycle: jump→land→jump with short ground contact
					iCycles++;

					// Cadence = time from this takeoff to the previous takeoff
					if (flLastTakeoffTime > 0.f)
					{
						const float flJumpToJump = flLastTakeoffTime - flTakeoffTime;
						if (flJumpToJump > 0.f && flJumpToJump < 2.0f)
						{
							flCadenceTotal += flJumpToJump;
							iCadenceSamples++;
						}
					}
				}
			}

			flLastTakeoffTime = flTakeoffTime;
			iState = 0;  // Back to looking for next landing
		}
		// Landing: was in air, now on ground
		else if (bNewGround && !bOldGround)
		{
			flLandTime = tNew.m_flSimTime;
			iState = 1;  // Start measuring ground contact duration
		}
		// Still on ground — check if ground contact exceeded max
		else if (bNewGround && bOldGround && iState == 1)
		{
			const float flGroundSoFar = flLandTime - tOld.m_flSimTime;
			if (flGroundSoFar >= MaxGroundContactTime)
			{
				// They stayed on ground too long — this isn't a bhop transition
				iState = 0;
			}
		}
	}

	// Need at least 1 complete cycle to confirm bhop
	if (iCycles < 1)
		return false;

	// Speed must be maintained throughout — bhoppers keep momentum
	const float flMaxSpeed = std::max(pPlayer->TeamFortress_CalculateMaxSpeed(), 230.f);
	if (flSpeed < flMaxSpeed * 0.65f)
		return false;

	// Calculate predicted cadence: average observed jump-to-jump times, or estimate from physics
	if (pCadenceOut)
	{
		if (iCadenceSamples > 0)
		{
			*pCadenceOut = flCadenceTotal / static_cast<float>(iCadenceSamples);
		}
		else
		{
			// Estimate from physics:
			// jump_vel = sqrt(2 * g * 45)
			// air_time = 2 * jump_vel / g
			// cadence = air_time + ~2 ticks ground contact
			static ConVar* sv_gravity = U::ConVars.FindVar("sv_gravity");
			const float flGravity = sv_gravity ? sv_gravity->GetFloat() : 800.f;
			const float flJumpVel = std::sqrtf(2.f * flGravity * 45.f);
			const float flAirTime = 2.f * flJumpVel / flGravity;
			*pCadenceOut = flAirTime + TICK_INTERVAL * 2.f;  // + ~2 tick ground contact
		}
	}

	return true;
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

	// Skip processing when velocity is negligible and on ground
	if (md.m_vecVelocity.Length() < 15.0f && tMoveStorage.m_bDirectMove
		&& !tMoveStorage.m_bStrafing && !tMoveStorage.m_bEvasiveFriction)
		return;

	// --- OF-style manual physics: no ProcessMovement call ---
	// ProcessMovement was the #1 perf bottleneck (virtual call into game movement
	// DLL per tick). At 5s sim time = 300 ticks × N enemies, this killed FPS.
	// OF's approach: do physics manually — gravity, friction, air accel, collision.
	// This is both faster AND more accurate for strafe prediction because we
	// rotate velocity directly instead of hacking viewangles.

	const bool bOnGround = tMoveStorage.m_bDirectMove;
	const bool bAir = !bOnGround;

	// --- Strafe: rotate velocity directly (OF-style) ---
	const bool bSuppressYaw = (bOnGround && tMoveStorage.m_bRigidGround) || tMoveStorage.m_bRecentReversal;

	if (tMoveStorage.m_flAverageYaw && !bSuppressYaw)
	{
		if (bAir)
		{
			// Air strafe: rotate velocity by averageYaw per tick
			// This produces the characteristic Source air-strafe curve naturally
			const float flYawRad = DEG2RAD(tMoveStorage.m_flAverageYaw);
			const float flCos = cosf(flYawRad);
			const float flSin = sinf(flYawRad);
			const float flNewX = md.m_vecVelocity.x * flCos - md.m_vecVelocity.y * flSin;
			const float flNewY = md.m_vecVelocity.x * flSin + md.m_vecVelocity.y * flCos;
			md.m_vecVelocity.x = flNewX;
			md.m_vecVelocity.y = flNewY;

			// Air acceleration: apply wish direction force perpendicular to velocity
			Vec3 vVel2D = md.m_vecVelocity.To2D();
			const float flSpeed2D = vVel2D.Length2D();
			if (flSpeed2D > 10.f)
			{
				vVel2D.NormalizeInPlace();
				// Wish direction = perpendicular to velocity (strafe direction)
				const float flSideSign = sign(tMoveStorage.m_flAverageYaw);
				const float flPerpRad = DEG2RAD(flSideSign * 90.f);
				Vec3 vWishDir = {
					vVel2D.x * cosf(flPerpRad) - vVel2D.y * sinf(flPerpRad),
					vVel2D.x * sinf(flPerpRad) + vVel2D.y * cosf(flPerpRad),
					0.f
				};

				static ConVar* sv_airaccelerate = U::ConVars.FindVar("sv_airaccelerate");
				const float flAirAccel = sv_airaccelerate ? sv_airaccelerate->GetFloat() : 10.f;
				constexpr float flAirSpeedCap = 30.f;
				const float flCurrentSpeed = md.m_vecVelocity.x * vWishDir.x + md.m_vecVelocity.y * vWishDir.y;
				const float flAddSpeed = std::max(0.f, flAirSpeedCap - flCurrentSpeed);
				float flAccelSpeed = flAirAccel * flAirSpeedCap * TICK_INTERVAL;
				if (flAccelSpeed > flAddSpeed)
					flAccelSpeed = flAddSpeed;
				md.m_vecVelocity.x += flAccelSpeed * vWishDir.x;
				md.m_vecVelocity.y += flAccelSpeed * vWishDir.y;
			}
		}
		else
		{
			// Ground strafe: rotate velocity by yaw rate with friction decay
			const float flFriction = GetFrictionScale(pPlayer);
			const float flElapsedTicks = (CFG::Aimbot_Projectile_Max_Simulation_Time - tMoveStorage.m_flTimeToTarget) / TICK_INTERVAL;
			const float flBurstDecay = std::exp(-flElapsedTicks * 0.7f);
			constexpr float flBurstScale = 2.5f;
			float flYawRate = tMoveStorage.m_flAverageYaw;
			if (std::fabs(tMoveStorage.m_flOriginCurvature) > std::fabs(flYawRate))
				flYawRate = tMoveStorage.m_flOriginCurvature;

			const float flYawRad = DEG2RAD(flYawRate * flFriction * flBurstScale * flBurstDecay);
			const float flCos = cosf(flYawRad);
			const float flSin = sinf(flYawRad);
			const float flNewX = md.m_vecVelocity.x * flCos - md.m_vecVelocity.y * flSin;
			const float flNewY = md.m_vecVelocity.x * flSin + md.m_vecVelocity.y * flCos;
			md.m_vecVelocity.x = flNewX;
			md.m_vecVelocity.y = flNewY;
		}
	}

	// --- Gravity ---
	if (bAir)
	{
		static ConVar* sv_gravity = U::ConVars.FindVar("sv_gravity");
		const float flGravity = sv_gravity ? sv_gravity->GetFloat() : 800.f;
		md.m_vecVelocity.z -= flGravity * TICK_INTERVAL;
		if (md.m_vecVelocity.z < -3500.f)
			md.m_vecVelocity.z = -3500.f;
	}
	else
	{
		// Ground: clamp Z velocity
		if (md.m_vecVelocity.z > 100.f)
			md.m_vecVelocity.z = 100.f;
		if (md.m_vecVelocity.z < 0.f)
			md.m_vecVelocity.z = 0.f;

		// Ground friction (Source engine: Friction() in gamemovement.cpp)
		{
			const float flSpeed = md.m_vecVelocity.Length2D();
			if (flSpeed < 0.1f)
			{
				md.m_vecVelocity.x = md.m_vecVelocity.y = 0.f;
			}
			else
			{
				static ConVar* sv_friction = U::ConVars.FindVar("sv_friction");
				static ConVar* sv_stopspeed = U::ConVars.FindVar("sv_stopspeed");
				const float flFriction = sv_friction ? sv_friction->GetFloat() : 4.f;
				const float flStopSpeed = sv_stopspeed ? sv_stopspeed->GetFloat() : 100.f;
				const float flSurfaceFriction = pPlayer->m_surfaceFriction();
				const float flControl = std::max(flSpeed, flStopSpeed);
				const float flDrop = flControl * flFriction * flSurfaceFriction * TICK_INTERVAL;
				const float flNewSpeed = std::max(0.f, flSpeed - flDrop);
				if (flSpeed > 0.f)
				{
					const float flScale = flNewSpeed / flSpeed;
					md.m_vecVelocity.x *= flScale;
					md.m_vecVelocity.y *= flScale;
				}
			}
		}

		// Ground acceleration (Source engine: WalkMove() after Friction())
		// TF2's WalkMove applies sv_accelerate toward wish direction, which counteracts
		// friction. Without this, friction alone decelerates players to zero.
		// When wish speed is below friction threshold, TF2 increases accel to compensate.
		{
			const float flMaxSpeed = std::max(md.m_flMaxSpeed, 100.f);

			// Build wish direction from current velocity direction (player is moving
			// in their wish direction already — we just need to maintain speed)
			Vec3 vVel2D = md.m_vecVelocity.To2D();
			float flSpeed2D = vVel2D.Length2D();

			if (flSpeed2D > 0.1f)
			{
				vVel2D.NormalizeInPlace();

				static ConVar* sv_accelerate = U::ConVars.FindVar("sv_accelerate");
				static ConVar* sv_friction = U::ConVars.FindVar("sv_friction");
				const float flAccel = sv_accelerate ? sv_accelerate->GetFloat() : 10.f;
				const float flFriction = sv_friction ? sv_friction->GetFloat() : 4.f;
				const float flSurfaceFriction = pPlayer->m_surfaceFriction();

				float flWishSpeed = flMaxSpeed;
				float flAccelRate = flAccel;

				// TF2 compensation: when wish speed is below friction threshold,
				// increase acceleration so player can overcome friction
				const float flWishSpeedThreshold = 100.f * flFriction * flSurfaceFriction / flAccel;
				if (flWishSpeed > 0.f && flWishSpeed < flWishSpeedThreshold)
				{
					static ConVar* sv_stopspeed = U::ConVars.FindVar("sv_stopspeed");
					const float flStopSpeed = sv_stopspeed ? sv_stopspeed->GetFloat() : 100.f;
					const float flControl = std::max(flSpeed2D, flStopSpeed);
					flAccelRate = (flControl * flFriction * flSurfaceFriction) / flWishSpeed + 1.f;
				}

				// Accelerate toward wish direction
				const float flCurrentSpeed = md.m_vecVelocity.x * vVel2D.x + md.m_vecVelocity.y * vVel2D.y;
				const float flAddSpeed = std::max(0.f, flWishSpeed - flCurrentSpeed);
				float flAccelSpeed = flAccelRate * flWishSpeed * TICK_INTERVAL * flSurfaceFriction;
				if (flAccelSpeed > flAddSpeed)
					flAccelSpeed = flAddSpeed;

				md.m_vecVelocity.x += flAccelSpeed * vVel2D.x;
				md.m_vecVelocity.y += flAccelSpeed * vVel2D.y;
			}
		}

		// Evasive friction: extra slowdown for rapidly-changing ground direction
		if (tMoveStorage.m_bEvasiveFriction)
		{
			const float flSpeed = md.m_vecVelocity.Length2D();
			if (flSpeed > 0.f)
			{
				static ConVar* sv_friction = U::ConVars.FindVar("sv_friction");
				static ConVar* sv_stopspeed = U::ConVars.FindVar("sv_stopspeed");
				const float flFriction = sv_friction ? sv_friction->GetFloat() : 4.f;
				const float flStopSpeed = sv_stopspeed ? sv_stopspeed->GetFloat() : 100.f;
				const float flSurfaceFriction = pPlayer->m_surfaceFriction();
				const float flNewSpeed = std::max(0.f, flSpeed - std::max(flSpeed, flStopSpeed) * (flFriction * flSurfaceFriction * TICK_INTERVAL));
				if (flSpeed > 0.f)
				{
					const float flScale = flNewSpeed / flSpeed;
					md.m_vecVelocity.x *= flScale;
					md.m_vecVelocity.y *= flScale;
				}
			}
		}

		// Bhop: predict jump on ground contact
		if (tMoveStorage.m_bBunnyHop && !pPlayer->m_bDucked())
		{
			if (md.m_vecVelocity.z < 50.f && md.m_vecVelocity.z > -50.f)
			{
				md.m_vecVelocity.z = 270.f;
				tMoveStorage.m_bDirectMove = false;
				tMoveStorage.m_iBhopSimTicksInAir = 0;
			}
		}

		// Clamp ground velocity to max speed
		{
			const float flMaxSpeed = md.m_flMaxSpeed;
			const float flSpeedSqr = md.m_vecVelocity.Length2DSqr();
			if (flSpeedSqr > flMaxSpeed * flMaxSpeed && flSpeedSqr > 0.f)
			{
				const float flSpeed = std::sqrt(flSpeedSqr);
				const float flScale = flMaxSpeed / flSpeed;
				md.m_vecVelocity.x *= flScale;
				md.m_vecVelocity.y *= flScale;
			}
		}
	}

	// --- Move: origin += velocity * dt ---
	Vec3 vNewPos = md.m_vecAbsOrigin + md.m_vecVelocity * TICK_INTERVAL;

	// --- Collision: hull trace ---
	{
		trace_t trace = {};
		CTraceFilterWorldAndPropsOnlyAmalgam filter = {};

		const Vec3 vCompression(PlayerOriginCompression, PlayerOriginCompression, 0.f);
		Vec3 vMins = pPlayer->m_vecMins() + vCompression;
		Vec3 vMaxs = pPlayer->m_vecMaxs() - vCompression;

		H::AimUtils->TraceHull(md.m_vecAbsOrigin, vNewPos, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &trace);

		if (trace.fraction < 1.0f)
		{
			vNewPos = trace.endpos;

			if (trace.plane.normal.z > 0.7f)
			{
				// Hit ground
				md.m_vecVelocity.z = 0.f;
				tMoveStorage.m_bDirectMove = true;
				vNewPos.z += 0.5f;
			}
			else
			{
				// Hit wall/ceiling — clip velocity along collision plane
				tMoveStorage.m_bDirectMove = false;
				const float flBackoff = md.m_vecVelocity.Dot(trace.plane.normal);
				if (flBackoff < 0.f)
					md.m_vecVelocity = md.m_vecVelocity - (trace.plane.normal * flBackoff);
			}
		}
		else
		{
			// Clear path — not on ground
			tMoveStorage.m_bDirectMove = false;
		}
	}

	md.m_vecAbsOrigin = vNewPos;

	if (pCallback)
		(*pCallback)(md);

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
