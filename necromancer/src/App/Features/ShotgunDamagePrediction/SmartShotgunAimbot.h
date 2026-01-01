#pragma once
#include "ShotgunDamagePrediction.h"
#include "../../../SDK/SDK.h"

// Smart Shotgun Aimbot Configuration
struct SmartShotgunConfig
{
    // Double tap settings
    int nDoubleTapTicksRequired;    // Ticks needed for double tap
    int nDoubleTapTicksStored;      // Current stored ticks
    bool bDoubleTapEnabled;
    
    // Pellet thresholds
    float flDoubleTapMinPelletPercent;  // Min pellet % for double tap (default 50%)
    float flNormalMinPelletPercent;     // Min pellet % for normal shot (default 10%)
    
    // Crit settings
    bool bCanCrit;                  // User can crit (kritz, etc.)
    bool bHoldingCritKey;           // User is holding crit key
    
    // Rapid fire check
    bool bRapidFireReady;           // Is rapid fire ready to shoot
};

// Smart shot decision result
struct SmartShotDecision
{
    bool bShouldShoot;
    bool bWaitForMorePellets;
    int nCurrentPelletsCanHit;
    int nPelletsRequired;
    int nPelletsToWaitFor;
    float flExpectedDamage;
    float flTargetHealth;
    bool bUsingDoubleTap;
    bool bUsingCrit;
    const char* szReason;
};

class CSmartShotgunAimbot
{
public:
    static CSmartShotgunAimbot& Instance()
    {
        static CSmartShotgunAimbot instance;
        return instance;
    }

    // Main decision function - determines if we should shoot
    SmartShotDecision ShouldShoot(
        C_TFPlayer* pLocal,
        C_TFWeaponBase* pWeapon,
        C_TFPlayer* pTarget,
        const SmartShotgunConfig& config
    );

    // Check if user can double tap
    bool CanDoubleTap(const SmartShotgunConfig& config);

    // Check if rapid fire is ready for double tap
    bool IsRapidFireReady(const SmartShotgunConfig& config);

    // Estimate pellets that can hit target based on no-spread data
    int EstimatePelletsCanHit(
        C_TFPlayer* pLocal,
        C_TFWeaponBase* pWeapon,
        C_TFPlayer* pTarget
    );

    // Get pellet positions from no-spread prediction
    void GetNoSpreadPelletPositions(
        C_TFPlayer* pLocal,
        C_TFWeaponBase* pWeapon,
        std::vector<Vec3>& outWorldPositions
    );

    // Count how many pellets would hit target hitbox
    int CountPelletsHittingTarget(
        C_TFPlayer* pLocal,
        C_TFPlayer* pTarget,
        const std::vector<Vec3>& pelletEndPositions
    );

private:
    CSmartShotgunAimbot() = default;
    ~CSmartShotgunAimbot() = default;
    CSmartShotgunAimbot(const CSmartShotgunAimbot&) = delete;
    CSmartShotgunAimbot& operator=(const CSmartShotgunAimbot&) = delete;
};

#define g_SmartShotgun CSmartShotgunAimbot::Instance()
