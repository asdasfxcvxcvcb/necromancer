#pragma once
#include "../../../../SDK/SDK.h"
#include "../AmalgamCompat.h"

#include <array>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

// Amalgam-faithful movement simulation, ported from
// Amalgam-master/Amalgam/src/Features/Simulation/MovementSimulation.
// Runs the engine's real ProcessMovement per tick with Amalgam's exact input
// derivation (history records, strafe/average-yaw prediction, timing via
// sim-time deltas). Used by the Amalgam projectile aimbot; Necromancer's own
// CMovementSimulation is untouched and keeps serving its other consumers.
// All tuning values are Amalgam's NOSAVE debug-var defaults, hardcoded so the
// port cannot drift from upstream behavior through menu state (Necromancer's
// CFG defaults for these were tuned for the old native simulator).

namespace MoveSimMoveEnum
{
	enum { Ground = 0, Air = 1, Swim = 2 };
}

// Necromancer's igamemovement.h only exposes ProcessMovement. Amalgam's
// IGameMovement.h additionally declares the member block (player/mv/...) and
// CheckStuck, which the simulation needs. Layout copied 1:1 from Amalgam so
// the vtable slots and member offsets match the engine's CGameMovement.
class CGameMovementAmalgam : public IGameMovement
{
public:
	virtual			~CGameMovementAmalgam(void) {}

	virtual void	ProcessMovement(C_BasePlayer* pPlayer, CMoveData* pMove) = 0;
	virtual void	StartTrackPredictionErrors(C_BasePlayer* pPlayer) = 0;
	virtual void	FinishTrackPredictionErrors(C_BasePlayer* pPlayer) = 0;
	virtual void	DiffPrint(char const* fmt, ...) = 0;
	virtual Vector	GetPlayerMins(bool ducked) const = 0;
	virtual Vector	GetPlayerMaxs(bool ducked) const = 0;
	virtual Vector	GetPlayerViewOffset(bool ducked) const = 0;

	typedef enum
	{
		GROUND = 0,
		STUCK,
		LADDER
	} IntervalType_t;

	virtual void	TracePlayerBBox(const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t& pm) = 0;
	virtual void	TryTouchGround(const Vector& start, const Vector& end, const Vector& mins, const Vector& maxs, unsigned int fMask, int collisionGroup, trace_t& pm) = 0;
	virtual unsigned int	PlayerSolidMask(bool brushOnly = false) = 0;
	virtual void	PlayerMove(void) = 0;
	virtual float	CalcRoll(const QAngle& angles, const Vector& velocity, float rollangle, float rollspeed) = 0;
	virtual	void	DecayPunchAngle(void) = 0;
	virtual void	CheckWaterJump(void) = 0;
	virtual void	WaterMove(void) = 0;
	virtual void	AirAccelerate(Vector& wishdir, float wishspeed, float accel) = 0;
	virtual void	AirMove(void) = 0;
	virtual float	GetAirSpeedCap(void) = 0;
	virtual bool	CanAccelerate() = 0;
	virtual void	Accelerate(Vector& wishdir, float wishspeed, float accel) = 0;
	virtual void	WalkMove(void) = 0;
	virtual void	FullWalkMove() = 0;
	virtual void	OnJump(float fImpulse) = 0;
	virtual void	OnLand(float fVelocity) = 0;
	virtual void	OnTryPlayerMoveCollision(trace_t& tr) = 0;
	virtual Vector	GetPlayerMins(void) const = 0;
	virtual Vector	GetPlayerMaxs(void) const = 0;
	virtual int		GetCheckInterval(IntervalType_t type) = 0;
	virtual bool	CheckJumpButton(void) = 0;
	virtual void    FullTossMove(void) = 0;
	virtual void	FullLadderMove() = 0;
	virtual int		TryPlayerMove(Vector* pFirstDest = NULL, trace_t* pFirstTrace = NULL, float flSlideMultiplier = 0.f) = 0;
	virtual bool	LadderMove(void) = 0;
	virtual bool	OnLadder(trace_t& trace) = 0;
	virtual float	LadderDistance(void) const = 0;
	virtual unsigned int	LadderMask(void) const = 0;
	virtual float	ClimbSpeed(void) const = 0;
	virtual float	LadderLateralMultiplier(void) const = 0;
	virtual int		CheckStuck(void) = 0;
	virtual bool	CheckWater(void) = 0;
	virtual void	CategorizePosition(void) = 0;
	virtual void	CheckParameters(void) = 0;
	virtual	void	ReduceTimers(void) = 0;
	virtual	void	CheckFalling(void) = 0;
	virtual	void	PlayerRoughLandingEffects(float fvol) = 0;
	virtual	void	Duck(void) = 0;
	virtual	void	HandleDuckingSpeedCrop() = 0;
	virtual	void	FinishUnDuck(void) = 0;
	virtual	void	FinishDuck(void) = 0;
	virtual	bool	CanUnduck() = 0;
	virtual CBaseHandle	TestPlayerPosition(const Vector& pos, int collisionGroup, trace_t& pm) = 0;
	virtual void	SetGroundEntity(trace_t* pm) = 0;
	virtual void	StepMove(Vector& vecDestination, trace_t& trace) = 0;
	virtual bool	GameHasLadders() const = 0;

	enum
	{
		MAX_PC_CACHE_SLOTS = 3,
	};

	C_BasePlayer*	player;
	CMoveData*		mv;
	int				m_nOldWaterLevel;
	float			m_flWaterEntryTime;
	int				m_nOnLadder;
	Vector			m_vecForward;
	Vector			m_vecRight;
	Vector			m_vecUp;
	int				m_CachedGetPointContents[102][MAX_PC_CACHE_SLOTS];
	Vector			m_CachedGetPointContentsPoint[102][MAX_PC_CACHE_SLOTS];
	Vector			m_vecProximityMins;
	Vector			m_vecProximityMaxs;
	float			m_fFrameTime;
	int				m_iSpeedCropped;
	float			m_flStuckCheckTime[102][2];
};

class CTFGameMovementAmalgam : public CGameMovementAmalgam
{
public:
	virtual void	PlayerMove() = 0;
	virtual unsigned int	PlayerSolidMask(bool brushOnly = false) = 0;
	virtual void	ProcessMovement(C_BasePlayer* pBasePlayer, CMoveData* pMove) = 0;
	virtual bool	CanAccelerate() = 0;
	virtual bool	CheckJumpButton() = 0;
	virtual int		CheckStuck(void) = 0;
	virtual bool	CheckWater(void) = 0;
	virtual void	WaterMove(void) = 0;
	virtual void	FullWalkMove() = 0;
	virtual void	WalkMove(void) = 0;
	virtual void	AirMove(void) = 0;
	virtual void	FullTossMove(void) = 0;
	virtual void	CategorizePosition(void) = 0;
	virtual void	CheckFalling(void) = 0;
	virtual void	Duck(void) = 0;
	virtual Vector	GetPlayerViewOffset(bool ducked) const = 0;
	virtual float	GetAirSpeedCap(void) = 0;
	virtual void	TracePlayerBBox(const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t& pm) = 0;
	virtual CBaseHandle	TestPlayerPosition(const Vector& pos, int collisionGroup, trace_t& pm) = 0;
	virtual void	StepMove(Vector& vecDestination, trace_t& trace) = 0;
	virtual bool	GameHasLadders() const = 0;
	virtual void	SetGroundEntity(trace_t* pm) = 0;
	virtual void	PlayerRoughLandingEffects(float fvol) = 0;
	virtual void	HandleDuckingSpeedCrop(void) = 0;
	virtual void	CheckWaterJump(void) = 0;

	Vector			m_vecWaterPoint;
	C_TFPlayer*		m_pTFPlayer;
	bool			m_isPassingThroughEnemies;
};

using MoveSimRunTickCallback = const std::function<void(CMoveData&)>;

struct MoveSimRecord
{
	Vec3 m_vDirection = {};
	float m_flSimTime = 0.f;
	int m_iMode = 0;
	Vec3 m_vVelocity = {};
	Vec3 m_vOrigin = {};
	bool m_bInputDirection = false;

	MoveSimRecord() = default;
	MoveSimRecord(const Vec3& vDirection, float flSimTime, int iMode, const Vec3& vVelocity, const Vec3& vOrigin)
		: m_vDirection(vDirection), m_flSimTime(flSimTime), m_iMode(iMode), m_vVelocity(vVelocity), m_vOrigin(vOrigin) {}
};

struct AmalgamMoveStorage
{
	C_TFPlayer* m_pPlayer = nullptr;
	CMoveData m_MoveData = {};
	byte* m_pData = nullptr;

	float m_flAverageYaw = 0.f;
	bool m_bBunnyHop = false;
	int m_iSimTick = 0;

	float m_flSimTime = 0.f;
	float m_flPredictedDelta = 0.f;
	float m_flPredictedSimTime = 0.f;
	bool m_bDirectMove = true;

	bool m_bPredictNetworked = true;
	Vec3 m_vPredictedOrigin = {};

	std::vector<Vec3> m_vPath = {};

	bool m_bFailed = false;
	bool m_bInitFailed = false;

	// Amalgam removes TF_COND_SHIELD_CHARGE from the simulated player once the
	// charge meter drains (CTFPlayerShared::RemoveCond). Necromancer has no
	// RemoveCond, so the sim tracks the expiry here instead.
	bool m_bChargeExpired = false;
};

class CAmalgamMoveSim
{
public:
	void Store();

	bool Initialize(C_BaseEntity* pEntity, AmalgamMoveStorage& tMoveStorage, bool bHitchance = true, bool bStrafe = true, bool bPredict = true);
	bool SetDuck(AmalgamMoveStorage& tMoveStorage, bool bDuck);
	void RunTick(AmalgamMoveStorage& tMoveStorage, bool bPath = true, const MoveSimRunTickCallback* pCallback = nullptr);
	void Restore(AmalgamMoveStorage& tMoveStorage);
	float GetPredictedDelta(C_BaseEntity* pEntity);

private:
	void StoreState(AmalgamMoveStorage& tMoveStorage);
	void ResetState(AmalgamMoveStorage& tMoveStorage);

	void SetupMoveData(AmalgamMoveStorage& tMoveStorage);
	bool CheckStuck(AmalgamMoveStorage& tMoveStorage);
	void GetAverageYaw(AmalgamMoveStorage& tMoveStorage, int iSamples);
	void GetRecentYaw(AmalgamMoveStorage& tMoveStorage, int iSamples);
	bool StrafePrediction(AmalgamMoveStorage& tMoveStorage, bool bStrafe = true, bool bHitchance = false);

	void SetBounds(C_TFPlayer* pPlayer);
	void RestoreBounds(C_TFPlayer* pPlayer);

	// Amalgam's H::Entities.GetDeltaTime/GetChoke equivalents, fed at the top
	// of Store() (Amalgam feeds them from its entity cache in the same stage).
	float GetDeltaTime(int iIndex) const { return iIndex >= 0 && iIndex < 102 ? m_aDeltaTimes[iIndex] : TICK_INTERVAL; }
	int GetChoke(int iIndex) const { return iIndex >= 0 && iIndex < 102 ? m_aChokes[iIndex] : 0; }

	bool m_bOldInPrediction = false;
	bool m_bOldFirstTimePredicted = false;
	float m_flOldFrametime = 0.f;

	std::array<float, 102> m_aSimTimes = {};
	std::array<float, 102> m_aDeltaTimes = {};
	std::array<int, 102> m_aChokes = {};
	std::array<int, 102> m_aSetTicks = {};
	std::array<float, 102> m_aLastSeenTimes = {};

	std::unordered_map<int, std::deque<MoveSimRecord>> m_mRecords = {};
	std::unordered_map<int, std::deque<float>> m_mSimTimes = {};
};

MAKE_SINGLETON_SCOPED(CAmalgamMoveSim, MoveSim, F);
