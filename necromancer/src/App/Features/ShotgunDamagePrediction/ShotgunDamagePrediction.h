#pragma once
#include "../../../SDK/SDK.h"

// TF2 Damage Falloff Constants (from Source SDK)
// Distance-based damage ramp: 
// - 0 units (point blank): 150% damage
// - 512 units (optimal): 100% damage  
// - 1024 units (max falloff): 50% damage
constexpr float SHOTGUN_DAMAGE_RANGE_MIN = 0.0f;      // Point blank
constexpr float SHOTGUN_DAMAGE_RANGE_MID = 512.0f;    // Optimal distance
constexpr float SHOTGUN_DAMAGE_RANGE_MAX = 1024.0f;   // Max falloff distance

constexpr float SHOTGUN_DAMAGE_RAMP_UP = 1.5f;        // 150% at point blank
constexpr float SHOTGUN_DAMAGE_RAMP_DOWN = 0.5f;      // 50% at max range

// Note: TF_DAMAGE_CRIT_MULTIPLIER and TF_DAMAGE_MINICRIT_MULTIPLIER are defined in SDK/TF2/tf_shareddefs.h

// Shotgun types and their base stats
enum class EShotgunType
{
    Unknown = 0,
    // Scout
    Scattergun,             // 6 damage per pellet, 10 pellets = 60 base
    ForceANature,           // 5.4 damage per pellet, 10 pellets = 54 base
    Shortstop,              // 12 damage per pellet, 4 pellets = 48 base
    SodaPopper,             // 6 damage per pellet, 10 pellets = 60 base
    BabyFacesBlaster,       // 6 damage per pellet, 10 pellets = 60 base
    BackScatter,            // 6 damage per pellet, 10 pellets = 60 base
    
    // Soldier/Pyro/Heavy/Engineer
    Shotgun,                // 6 damage per pellet, 10 pellets = 60 base
    ReserveShooter,         // 6 damage per pellet, 10 pellets = 60 base
    FamilyBusiness,         // 4.5 damage per pellet, 10 pellets = 45 base
    PanicAttack,            // 6 damage per pellet, 10 pellets = 60 base
    
    // Engineer
    FrontierJustice,        // 6 damage per pellet, 10 pellets = 60 base
    Widowmaker,             // 6 damage per pellet, 10 pellets = 60 base
};

// Shotgun stats structure
struct ShotgunStats
{
    float flBaseDamagePerPellet;
    int nPelletCount;
    float flSpread;
    bool bHasRampUp;        // Some weapons don't have damage ramp-up
    bool bHasRampDown;      // Some weapons don't have damage falloff
    bool bMiniCritAirborne; // Reserve Shooter mini-crits airborne targets
};

// Damage prediction result
struct ShotgunDamageResult
{
    float flTotalDamage;
    float flDamagePerPellet;
    int nPelletsHit;
    int nPelletsRequired;   // Pellets needed to kill
    float flKillProbability;
    bool bCanKill;
    bool bIsCrit;
    bool bIsMiniCrit;
};

class CShotgunDamagePrediction
{
public:
    // Get singleton instance
    static CShotgunDamagePrediction& Instance()
    {
        static CShotgunDamagePrediction instance;
        return instance;
    }

    // Main prediction functions
    ShotgunDamageResult PredictDamage(
        C_TFPlayer* pLocal,
        C_TFWeaponBase* pWeapon,
        C_TFPlayer* pTarget,
        float flDistance,
        int nPelletsHit,
        bool bCrit,
        bool bMiniCrit
    );

    // Calculate damage falloff multiplier based on distance
    float GetDistanceDamageModifier(float flDistance, bool bHasRampUp = true, bool bHasRampDown = true);

    // Get shotgun type from weapon
    EShotgunType GetShotgunType(C_TFWeaponBase* pWeapon);

    // Get shotgun stats
    ShotgunStats GetShotgunStats(EShotgunType type);

    // Calculate pellets needed to kill target
    int GetPelletsRequiredToKill(
        C_TFPlayer* pTarget,
        float flDamagePerPellet,
        bool bCrit,
        bool bMiniCrit
    );

    // Check if target is airborne (for Reserve Shooter mini-crit)
    bool IsTargetAirborne(C_TFPlayer* pTarget);

    // Check if weapon can mini-crit airborne targets
    bool CanMiniCritAirborne(C_TFWeaponBase* pWeapon);



private:
    CShotgunDamagePrediction() = default;
    ~CShotgunDamagePrediction() = default;
    CShotgunDamagePrediction(const CShotgunDamagePrediction&) = delete;
    CShotgunDamagePrediction& operator=(const CShotgunDamagePrediction&) = delete;
};

// Global accessor
#define g_ShotgunDmgPred CShotgunDamagePrediction::Instance()
