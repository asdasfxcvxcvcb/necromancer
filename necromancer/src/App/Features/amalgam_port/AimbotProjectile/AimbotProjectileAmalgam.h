#pragma once

#include "../AmalgamCompat.h"
#include "../MoveSim/MoveSimAmalgam.h"
#include "../AimbotGlobal/AimbotGlobal.h"
#include "../ProjectileSimulation/ProjectileSimulation.h" // ProjectileInfo (m_tProjInfo)

// ============================================
// Amalgam's current point/flag architecture
// ============================================
// PointFlags: what a direct hitbox offset carries (Regular = flat, Lob = high arc)
// PointType:  how a point is validated when tested
//             (Direct = projectile must hit the target itself,
//              Geometry = projectile lands near a surface point,
//              Air = airdet happens near a point)
// CalculateFlags: which extras CalculateAngle applies
//             (TwoPass = redo the solution from the real spawn position,
//              SetupClip = re-trace the spawn position at the aim angles,
//              AccountDrag = vphysics drag compensation, LobAngle = high arc)

Enum(PointFlags, None = 0, Regular = 1 << 0, Lob = 1 << 1)
Enum(PointType, Direct, Geometry, Air)
Enum(CalculateFlags, None = 0, TwoPass = 1 << 0, SetupClip = 1 << 1, AccountDrag = 1 << 2, LobAngle = 1 << 3, Accuracy = TwoPass | SetupClip | AccountDrag)
Enum(Calculated, Pending, Good, Time, Bad)

// ============================================
// Structures
// ============================================

struct Solution_t
{
    float m_flPitch = 0.f;
    float m_flYaw = 0.f;
    float m_flTime = 0.f;
    int m_iCalculated = CalculatedEnum::Pending;
};

struct Setup_t
{
    Vec3 m_vPoint = {};
    int m_iType = PointTypeEnum::Geometry;
};

struct Point_t
{
    Vec3 m_vPoint = {};
    Solution_t m_tSolution = {};
    int m_iType = PointTypeEnum::Direct;
};

struct Offset_t
{
    Vec3 m_vOffset = {};
    int m_iFlags = PointFlagsEnum::Regular;
};
using Directs_t = std::unordered_map<int, Offset_t>;
using Splashes_t = std::vector<int>;

struct History_t
{
    Vec3 m_vOrigin = {};
    int m_iSimtime = 0;

    History_t() = default;
    History_t(const Vec3& vOrigin, int iSimtime) : m_vOrigin(vOrigin), m_iSimtime(iSimtime) {}
};
struct Direct_t : History_t
{
    float m_flPitch = 0.f;
    float m_flYaw = 0.f;
    float m_flTime = 0.f;
    Vec3 m_vPoint = {};
    int m_iPriority = 0;

    Direct_t() = default;
    Direct_t(const History_t& tHistory, float flPitch, float flYaw, float flTime, const Vec3& vPoint, int iPriority)
        : History_t(tHistory), m_flPitch(flPitch), m_flYaw(flYaw), m_flTime(flTime), m_vPoint(vPoint), m_iPriority(iPriority) {}
};
struct Splash_t : History_t
{
    float m_flTimeTo = 0.f;

    Splash_t() = default;
    Splash_t(const History_t& tHistory, float flTimeTo) : History_t(tHistory), m_flTimeTo(flTimeTo) {}
};
using DirectHistory_t = std::unordered_map<int, std::vector<Direct_t>>;
using SplashHistory_t = std::unordered_map<int, std::vector<Splash_t>>;

// ============================================
// Info Structure (projectile calculation context)
// ============================================

struct Info_t
{
    C_TFPlayer* m_pLocal = nullptr;
    C_TFWeaponBase* m_pWeapon = nullptr;
    Target_t* m_pTarget = nullptr;
    C_BaseEntity* m_pProjectile = nullptr;

    Vec3 m_vLocalEye = {};
    Vec3 m_vTargetEye = {};

    float m_flLatency = 0.f;
    Vec3 m_vHull = {};
    Vec3 m_vOffset = {};
    Vec3 m_vAngFix = {};
    float m_flVelocity = 0.f;
    float m_flGravity = 0.f;
    float m_flRadius = 0.f;
    float m_flRadiusTime = 0.f;
    float m_flBoundsTime = 0.f;
    float m_flOffsetTime = 0.f;
    int m_iSplashRestrict = 0;
    int m_iArmTime = 0;
    float m_flNormalOffset = 0.f;
    bool m_bIgnoreTiming = false;
};

// ============================================
// Projectile Aimbot (Amalgam port, current architecture)
// ============================================
// History-based solver: the simulation loop collects candidate direct points
// and splash detonation windows per tick, then HandleDirect/HandleSplash
// replay each candidate through ProjSim + TestAngle, best-first.

class CAmalgamAimbotProjectile
{
private:
    std::vector<Target_t> GetTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
    std::vector<Target_t> SortTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);

    Directs_t GetDirects();
    Splashes_t GetSplashes();
    void SetupSplashPoints(Vec3& vOrigin, std::vector<Setup_t>& vSplashPoints, int iFlags = CalculateFlagsEnum::None);
    std::vector<Point_t> GetSplashPoints(Vec3 vOrigin, std::vector<Setup_t>& vSplashPoints, int iSimTime, int iFlags = CalculateFlagsEnum::Accuracy, bool bFirst = false);

    void CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& tOut, int iFlags = CalculateFlagsEnum::Accuracy, int iTolerance = -1, float flRoll = 0.f);
    bool TestAngle(const Vec3& vPoint, const Vec3& vAngles, int iSimTime, int iType, int iFlags, bool bSecondTest = false);

    bool HandlePoint(const Vec3& vOrigin, int iSimTime, float flPitch, float flYaw, float flTime, const Vec3& vPoint, int iType = PointTypeEnum::Direct, int iFlags = PointFlagsEnum::Regular);
    bool HandleDirect(DirectHistory_t& mDirectHistory);
    bool HandleSplash(SplashHistory_t& mSplashHistory);

    int CanHit(Target_t& tTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, bool bUpdate = true);
    bool RunMain(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);

    // Auto airblast path (stubbed - Necromancer has no auto airblast integration yet)
    bool CanHit(Target_t& tTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, C_BaseEntity* pProjectile);
    bool TestAngle(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, C_BaseEntity* pProjectile, Target_t& tTarget, Vec3& vPoint, Vec3& vAngles, int iSimTime, bool bSplash, std::vector<Vec3>* pProjectilePath = nullptr);

    bool Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod = Vars::Aimbot::General::AimType.Value);
    void Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod = Vars::Aimbot::General::AimType.Value);

    Info_t m_tInfo = {};
    AmalgamMoveStorage m_tMoveStorage = {};
    ProjectileInfo m_tProjInfo = {};
    std::vector<Setup_t> m_vSplashPoints = {};

    bool m_bLastTickHeld = false;
    bool m_bLastTickAimbotCharge = false;
    bool m_bManualBailLatch = false;

    float m_flTimeTo = std::numeric_limits<float>::max();
    std::vector<Vec3> m_vPlayerPath = {};
    std::vector<Vec3> m_vProjectilePath = {};

    Vec3 m_vAngleTo = {};
    Vec3 m_vPredicted = {};
    Vec3 m_vTarget = {};

    // tick of the last validated (iResult == 1) solution - lets the failure
    // path carry a fresh angle through validation-flicker gaps
    int m_iLastSolutionTick = -1;

    int m_iResult = false;
    bool m_bUpdate = true;

    // set by TestAngle when a flight traced into something other than its
    // point's purpose (direct missed the target, splash never reached its
    // surface) - the one failure mode muzzle roll (neckbreaker) can route
    // around; cleared at every TestAngle entry
    bool m_bObstructed = false;

    // budget of roll angles the neckbreaker search may spend per HandleDirect
    // or HandleSplash evaluation - full rolls across every candidate would
    // multiply the simulation cost by 360/step
    int m_iRollBudget = 0;

public:
    // Main entry point. Claims every projectile-type weapon Necromancer
    // dispatches here (EWeaponType::PROJECTILE), matching Amalgam's Run; the
    // native projectile aimbot only ever runs for weapons that never reach
    // this port.
    bool Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
    void RestoreCancel(C_TFPlayer* pLocal, CUserCmd* pCmd);

    float GetSplashRadius(C_TFWeaponBase* pWeapon, C_TFPlayer* pPlayer, float flScale = 1.f);
    float GetSplashRadius(C_BaseEntity* pProjectile, C_TFWeaponBase* pWeapon = nullptr, C_TFPlayer* pPlayer = nullptr, float flScale = 1.f);

    int m_iLastTickCancel = 0;
};

MAKE_SINGLETON_SCOPED(CAmalgamAimbotProjectile, AmalgamAimbotProjectile, F);
