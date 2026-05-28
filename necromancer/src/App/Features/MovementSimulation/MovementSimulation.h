#pragma once
#include "../../../SDK/SDK.h"

#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

enum class MoveMode
{
	Ground,
	Air,
	Swim
};

using RunTickCallback = std::function<void(CMoveData&)>;

struct MoveRecord
{
	Vec3 m_vDirection = {};
	float m_flSimTime = 0.f;
	MoveMode m_iMode = MoveMode::Ground;
	Vec3 m_vVelocity = {};
	Vec3 m_vOrigin = {};
	bool m_bHitWall = false;
	bool m_bEvasiveFriction = false;   // RijiN: rapidly changing direction on ground — apply extra friction
	bool m_bEvasiveZeroVel = false;    // RijiN: extremely erratic movement — zero velocity

	// Eye angle tracking for antiaim detection
	float m_flEyeYaw = 0.f;
	float m_flEyePitch = 0.f;
	float m_flVelEyeDelta = 0.f;       // |velocity yaw - eye yaw|, 0-180 degrees
};

struct MoveStorage
{
	C_TFPlayer* m_pPlayer = nullptr;
	CMoveData m_MoveData = {};
	byte* m_pData = nullptr;

	float m_flAverageYaw = 0.f;
	bool m_bEvasiveFriction = false;   // RijiN: apply extra friction for erratic ground movement
	bool m_bBunnyHop = false;

	float m_flOldModelScale = 0.f;    // Backed up model scale for explicit restore
	Vec3 m_vOldOrigin = {};           // Backed up origin for explicit restore
	bool m_bHasBackup = false;        // Whether we have backup values to restore
	float m_flBhopCadence = 0.f;        // Measured ground contact time before jump (seconds)
	int m_iBhopSimTicksInAir = 0;       // Ticks airborne in the sim (for jump timing)
	int m_iBhopSimTicksOnGround = 0;    // Ticks on ground in the sim (for cadence-delayed jump)

	float m_flSimTime = 0.f;
	float m_flPredictedDelta = 0.f;
	float m_flPredictedSimTime = 0.f;
	float m_flTimeToTarget = 0.f;       // Remaining sim time to target (for ground yaw decay)
	bool m_bDirectMove = false;
	bool m_bPredictNetworked = false;

	Vec3 m_vPredictedOrigin = {};
	std::vector<Vec3> m_vPath = {};

	bool m_bFailed = false;
	bool m_bInitFailed = false;
	bool m_bStationary = false;

	// --- Antiaim / strafe detection state (for logging and prediction) ---
	bool m_bAntiAimDetected = false;     // Eye angles appear fake (jittery while origin is smooth)
	bool m_bStrafing = false;            // VelEyeDelta > 45° — player is actively strafing
	float m_flVelEyeDeltaAtInit = 0.f;   // VelEyeDelta at sim init time
	float m_flOriginCurvature = 0.f;     // Origin-trajectory curvature (deg/tick) — immune to antiaim
	float m_flForwardMove = 0.f;         // Sim input at init (for logging)
	float m_flSideMove = 0.f;            // Sim input at init (for logging)

	// Phase 3 — rigid-turn mode for grounded players. When true, the per-tick
	// avg-yaw rotation in RunTick is suppressed so the sim produces straight-line
	// trajectories that match real "stiff" human movement (walking/peeking).
	// Decided once at init: ON unless strong sustained strafe evidence.
	bool m_bRigidGround = false;
	// Phase 5 — recent velocity reversal detected (player just changed direction,
	// e.g. peek-out → peek-in). Used to disable yaw rotation and shorten horizon.
	bool m_bRecentReversal = false;
	// Phase 6 — explosive launch detected (airborne + speed > class_max * 1.5).
	// Player was rocket/sticky jumped — trajectory is fundamentally unpredictable.
	// Shortens prediction horizon and disables yaw averaging.
	bool m_bExplosiveLaunch = false;
};

class CMovementSimulation
{
public:
	void Store();

	bool Initialize(C_TFPlayer* pPlayer);
	bool Initialize(C_TFPlayer* pPlayer, MoveStorage& tMoveStorage, bool bHitchance = false, bool bStrafe = true);

	void RunTick();
	void RunTick(MoveStorage& tMoveStorage, bool bPath = true, const RunTickCallback* pCallback = nullptr);

	void Restore();
	void Restore(MoveStorage& tMoveStorage);

	inline const Vec3& GetOrigin() const { return m_CurrentStorage.m_MoveData.m_vecAbsOrigin; }
	inline const Vec3& GetVelocity() const { return m_CurrentStorage.m_MoveData.m_vecVelocity; }
	inline bool HasFailed() const { return m_CurrentStorage.m_bFailed || m_CurrentStorage.m_bInitFailed; }
	inline bool IsStationary() const { return m_CurrentStorage.m_bStationary; }
	inline const MoveStorage& GetStorage() const { return m_CurrentStorage; }

	void ClearRecords();
	float GetPredictedDelta(C_TFPlayer* pPlayer);

private:
	bool StoreState(MoveStorage& tMoveStorage);
	void ResetState(MoveStorage& tMoveStorage);

	void SetupMoveData(MoveStorage& tMoveStorage, bool bStrafe);
	float GetAverageYaw(C_TFPlayer* pPlayer, int iSamples);
	void StrafePrediction(MoveStorage& tMoveStorage, bool bHitchance);

	bool IsBunnyHopping(C_TFPlayer* pPlayer, float* pCadenceOut = nullptr);
	int GetChokedTicks(C_TFPlayer* pPlayer) const;

	// Antiaim detection: check if eye angles are unreliable
	bool IsAntiAiming(C_TFPlayer* pPlayer);
	// Origin-trajectory curvature: turning rate from position history (immune to fake angles)
	float GetOriginCurvature(C_TFPlayer* pPlayer);

	void SetBounds(C_TFPlayer* pPlayer);
	void RestoreBounds(C_TFPlayer* pPlayer);

private:
	MoveStorage m_CurrentStorage = {};
	std::unordered_map<int, std::deque<MoveRecord>> m_mRecords = {};
	std::unordered_map<int, std::deque<float>> m_mSimTimes = {};
};

MAKE_SINGLETON_SCOPED(CMovementSimulation, MovementSimulation, F);
