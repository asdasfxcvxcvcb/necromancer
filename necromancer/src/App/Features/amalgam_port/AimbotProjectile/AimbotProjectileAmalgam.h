#pragma once

#include "../AmalgamCompat.h"
#include "../AimbotGlobal/AimbotGlobal.h"

// ============================================
// Point Type Enum (for splash prediction)
// ============================================

Enum(PointType, None = 0, Regular = 1 << 0, Obscured = 1 << 1, ObscuredExtra = 1 << 2, ObscuredMulti = 1 << 3)

// ============================================
// Calculation State Enum
// ============================================

Enum(Calculated, Pending, Good, Time, Bad)

// ============================================
// Solution Structure
// ============================================

struct Solution_t
{
    float m_flPitch = 0.f;
    float m_flYaw = 0.f;
    float m_flTime = 0.f;
    int m_iCalculated = CalculatedEnum::Pending;
};

// ============================================
// Point Structure
// ============================================

struct Point_t
{
    Vec3 m_vPoint = {};
    Solution_t m_tSolution = {};
};

// ============================================
// Info Structure (projectile calculation context)
// ============================================

struct Info_t
{
    C_TFPlayer* m_pLocal = nullptr;
    C_TFWeaponBase* m_pWeapon = nullptr;

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
    float m_flBoundingTime = 0.f;
    float m_flOffsetTime = 0.f;
    int m_iSplashCount = 0;
    int m_iSplashMode = 0;
    float m_flPrimeTime = 0;
    int m_iPrimeTime = 0;
};

// ============================================
// Rocket Launcher Aimbot (Amalgam Port)
// Supports: Splash prediction, Neckbreaker, Midpoint aim
// Targets: Players, Buildings (sentries, dispensers, teleporters)
// ============================================

class CAmalgamAimbotProjectile
{
private:
    std::vector<Target_t> GetTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
    std::vector<Target_t> SortTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);

    int CanHit(Target_t& tTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
    bool RunMain(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);

    // For auto airblast (stub)
    bool CanHit(Target_t& tTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, C_BaseEntity* pProjectile);

    bool Aim(Vec3 vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod = Vars::Aimbot::General::AimType.Value);
    void Aim(CUserCmd* pCmd, Vec3& vAngle, int iMethod = Vars::Aimbot::General::AimType.Value);

    bool m_bLastTickHeld = false;

    float m_flTimeTo = std::numeric_limits<float>::max();
    std::vector<Vec3> m_vPlayerPath = {};
    std::vector<Vec3> m_vProjectilePath = {};
    std::vector<DrawBox_t> m_vBoxes = {};

public:
    // Main entry point - only handles rocket launchers
    void Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
    
    // Splash radius calculation
    float GetSplashRadius(C_TFWeaponBase* pWeapon, C_TFPlayer* pPlayer);
    float GetSplashRadius(C_BaseEntity* pProjectile, C_TFWeaponBase* pWeapon = nullptr, C_TFPlayer* pPlayer = nullptr, float flScale = 1.f, C_TFWeaponBase* pAirblast = nullptr);

    // Splash point generation - public for external use
    std::unordered_map<int, Vec3> GetDirectPoints(Target_t& tTarget, C_BaseEntity* pProjectile = nullptr);
    std::vector<Point_t> GetSplashPoints(Target_t& tTarget, std::vector<std::pair<Vec3, int>>& vSpherePoints, int iSimTime);
    void SetupSplashPoints(Target_t& tTarget, std::vector<std::pair<Vec3, int>>& vSpherePoints, std::vector<std::pair<Vec3, Vec3>>& vSimplePoints);
    std::vector<Point_t> GetSplashPointsSimple(Target_t& tTarget, std::vector<std::pair<Vec3, Vec3>>& vSpherePoints, int iSimTime);
    
    // Angle calculation and testing - public for external use
    void CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& out, bool bAccuracy = true);
    bool TestAngle(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, Target_t& tTarget, Vec3& vPoint, Vec3& vAngles, int iSimTime, bool bSplash, bool* pHitSolid = nullptr, std::vector<Vec3>* pProjectilePath = nullptr);
    bool TestAngle(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, C_BaseEntity* pProjectile, Target_t& tTarget, Vec3& vPoint, Vec3& vAngles, int iSimTime, bool bSplash, std::vector<Vec3>* pProjectilePath = nullptr);
    
    // Info structure - public for external setup
    Info_t m_tInfo = {};
    
    // Setup info for a weapon
    void SetupInfo(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
    
    // Compute sphere points for splash prediction (hardcoded to 75 points)
    static std::vector<std::pair<Vec3, int>> ComputeSphere(float flRadius, int iSamples);

    int m_iLastTickCancel = 0;
};

MAKE_SINGLETON_SCOPED(CAmalgamAimbotProjectile, AmalgamAimbotProjectile, F);
