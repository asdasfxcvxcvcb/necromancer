#include "ShotgunDamagePrediction.h"
#include "../../../SDK/Impl/TraceFilters/TraceFilters.h"

// Fixed spread pellet pattern from Source SDK (tf_fx_shared.cpp)
// 10 pellets - Square pattern
static const Vec3 g_vecFixedWpnSpreadPellets[] = 
{
    Vec3(0, 0, 0),          // First pellet goes down the middle
    Vec3(1, 0, 0),
    Vec3(-1, 0, 0),
    Vec3(0, -1, 0),
    Vec3(0, 1, 0),
    Vec3(0.85f, -0.85f, 0),
    Vec3(0.85f, 0.85f, 0),
    Vec3(-0.85f, -0.85f, 0),
    Vec3(-0.85f, 0.85f, 0),
    Vec3(0, 0, 0),          // Last pellet goes down the middle as well
};

// Item definition indices for shotguns
namespace ShotgunDefIndex
{
    // Scout
    constexpr int Scattergun = 13;
    constexpr int ForceANature = 45;
    constexpr int Shortstop = 220;
    constexpr int SodaPopper = 448;
    constexpr int BabyFacesBlaster = 772;
    constexpr int BackScatter = 1103;
    
    // Multi-class shotguns
    constexpr int Shotgun = 9;          // Stock shotgun
    constexpr int ReserveShooter = 415;
    constexpr int PanicAttack = 1153;
    
    // Heavy
    constexpr int FamilyBusiness = 425;
    
    // Engineer
    constexpr int FrontierJustice = 141;
    constexpr int Widowmaker = 527;
}

float CShotgunDamagePrediction::GetDistanceDamageModifier(float flDistance, bool bHasRampUp, bool bHasRampDown)
{
    // TF2 damage falloff formula from Source SDK:
    // - Point blank (0 units): 150% damage (ramp up)
    // - Optimal (512 units): 100% damage
    // - Max falloff (1024 units): 50% damage (ramp down)
    
    if (flDistance <= SHOTGUN_DAMAGE_RANGE_MIN)
    {
        return bHasRampUp ? SHOTGUN_DAMAGE_RAMP_UP : 1.0f;
    }
    else if (flDistance < SHOTGUN_DAMAGE_RANGE_MID)
    {
        // Ramp up zone: 0-512 units
        // Linear interpolation from 150% to 100%
        if (!bHasRampUp)
            return 1.0f;
            
        float flRatio = flDistance / SHOTGUN_DAMAGE_RANGE_MID;
        return Math::RemapValClamped(flRatio, 0.0f, 1.0f, SHOTGUN_DAMAGE_RAMP_UP, 1.0f);
    }
    else if (flDistance < SHOTGUN_DAMAGE_RANGE_MAX)
    {
        // Ramp down zone: 512-1024 units
        // Linear interpolation from 100% to 50%
        if (!bHasRampDown)
            return 1.0f;
            
        float flRatio = (flDistance - SHOTGUN_DAMAGE_RANGE_MID) / (SHOTGUN_DAMAGE_RANGE_MAX - SHOTGUN_DAMAGE_RANGE_MID);
        return Math::RemapValClamped(flRatio, 0.0f, 1.0f, 1.0f, SHOTGUN_DAMAGE_RAMP_DOWN);
    }
    else
    {
        // Beyond max range: 50% damage
        return bHasRampDown ? SHOTGUN_DAMAGE_RAMP_DOWN : 1.0f;
    }
}

EShotgunType CShotgunDamagePrediction::GetShotgunType(C_TFWeaponBase* pWeapon)
{
    if (!pWeapon)
        return EShotgunType::Unknown;
    
    const int nWeaponID = pWeapon->GetWeaponID();
    const int nDefIndex = pWeapon->m_iItemDefinitionIndex();
    
    // Check by weapon ID first
    switch (nWeaponID)
    {
    case TF_WEAPON_SCATTERGUN:
        // Check specific scattergun variants
        switch (nDefIndex)
        {
        case ShotgunDefIndex::ForceANature:
            return EShotgunType::ForceANature;
        case ShotgunDefIndex::SodaPopper:
            return EShotgunType::SodaPopper;
        case ShotgunDefIndex::BabyFacesBlaster:
            return EShotgunType::BabyFacesBlaster;
        case ShotgunDefIndex::BackScatter:
            return EShotgunType::BackScatter;
        default:
            return EShotgunType::Scattergun;
        }
        
    case TF_WEAPON_HANDGUN_SCOUT_PRIMARY:
        return EShotgunType::Shortstop;
        
    case TF_WEAPON_SODA_POPPER:
        return EShotgunType::SodaPopper;
        
    case TF_WEAPON_PEP_BRAWLER_BLASTER:
        return EShotgunType::BabyFacesBlaster;
        
    case TF_WEAPON_SHOTGUN_PRIMARY:
    case TF_WEAPON_SHOTGUN_SOLDIER:
    case TF_WEAPON_SHOTGUN_PYRO:
    case TF_WEAPON_SHOTGUN_HWG:
        // Check specific shotgun variants
        switch (nDefIndex)
        {
        case ShotgunDefIndex::ReserveShooter:
            return EShotgunType::ReserveShooter;
        case ShotgunDefIndex::PanicAttack:
            return EShotgunType::PanicAttack;
        case ShotgunDefIndex::FamilyBusiness:
            return EShotgunType::FamilyBusiness;
        case ShotgunDefIndex::FrontierJustice:
            return EShotgunType::FrontierJustice;
        case ShotgunDefIndex::Widowmaker:
            return EShotgunType::Widowmaker;
        default:
            return EShotgunType::Shotgun;
        }
        
    case TF_WEAPON_SENTRY_REVENGE:
        return EShotgunType::FrontierJustice;
        
    default:
        return EShotgunType::Unknown;
    }
}

ShotgunStats CShotgunDamagePrediction::GetShotgunStats(EShotgunType type)
{
    ShotgunStats stats = {};
    stats.bHasRampUp = true;
    stats.bHasRampDown = true;
    stats.bMiniCritAirborne = false;
    
    switch (type)
    {
    case EShotgunType::Scattergun:
    case EShotgunType::SodaPopper:
    case EShotgunType::BabyFacesBlaster:
    case EShotgunType::BackScatter:
        stats.flBaseDamagePerPellet = 6.0f;
        stats.nPelletCount = 10;
        stats.flSpread = 0.0675f;  // Base spread
        break;
        
    case EShotgunType::ForceANature:
        stats.flBaseDamagePerPellet = 5.4f;
        stats.nPelletCount = 10;
        stats.flSpread = 0.0675f;
        break;
        
    case EShotgunType::Shortstop:
        stats.flBaseDamagePerPellet = 12.0f;
        stats.nPelletCount = 4;
        stats.flSpread = 0.04f;  // Tighter spread
        break;
        
    case EShotgunType::Shotgun:
    case EShotgunType::FrontierJustice:
    case EShotgunType::Widowmaker:
    case EShotgunType::PanicAttack:
        stats.flBaseDamagePerPellet = 6.0f;
        stats.nPelletCount = 10;
        stats.flSpread = 0.0675f;
        break;
        
    case EShotgunType::ReserveShooter:
        stats.flBaseDamagePerPellet = 6.0f;
        stats.nPelletCount = 10;
        stats.flSpread = 0.0675f;
        stats.bMiniCritAirborne = true;  // Mini-crits airborne targets
        break;
        
    case EShotgunType::FamilyBusiness:
        stats.flBaseDamagePerPellet = 4.5f;  // -15% damage
        stats.nPelletCount = 10;
        stats.flSpread = 0.0675f;
        break;
        
    default:
        // Default shotgun stats
        stats.flBaseDamagePerPellet = 6.0f;
        stats.nPelletCount = 10;
        stats.flSpread = 0.0675f;
        break;
    }
    
    return stats;
}

bool CShotgunDamagePrediction::IsTargetAirborne(C_TFPlayer* pTarget)
{
    if (!pTarget)
        return false;
    
    // Check if target is not on ground
    if (!(pTarget->m_fFlags() & FL_ONGROUND))
        return true;
    
    // Check for blast jump conditions
    if (pTarget->InCond(TF_COND_BLASTJUMPING))
        return true;
    
    // Check for knocked into air condition
    if (pTarget->InCond(TF_COND_KNOCKED_INTO_AIR))
        return true;
    
    return false;
}

bool CShotgunDamagePrediction::CanMiniCritAirborne(C_TFWeaponBase* pWeapon)
{
    if (!pWeapon)
        return false;
    
    // Reserve Shooter mini-crits airborne targets
    if (pWeapon->m_iItemDefinitionIndex() == ShotgunDefIndex::ReserveShooter)
        return true;
    
    return false;
}

ShotgunDamageResult CShotgunDamagePrediction::PredictDamage(
    C_TFPlayer* pLocal,
    C_TFWeaponBase* pWeapon,
    C_TFPlayer* pTarget,
    float flDistance,
    int nPelletsHit,
    bool bCrit,
    bool bMiniCrit)
{
    ShotgunDamageResult result = {};
    
    if (!pLocal || !pWeapon || !pTarget)
        return result;
    
    // Get shotgun type and stats
    EShotgunType shotgunType = GetShotgunType(pWeapon);
    if (shotgunType == EShotgunType::Unknown)
        return result;
    
    ShotgunStats stats = GetShotgunStats(shotgunType);
    
    // Check for Reserve Shooter mini-crit on airborne targets
    if (!bMiniCrit && stats.bMiniCritAirborne && IsTargetAirborne(pTarget))
    {
        bMiniCrit = true;
    }
    
    // Calculate distance modifier
    float flDistanceMod = GetDistanceDamageModifier(flDistance, stats.bHasRampUp, stats.bHasRampDown);
    
    // Calculate base damage per pellet with distance modifier
    float flDamagePerPellet = stats.flBaseDamagePerPellet * flDistanceMod;
    
    // Apply crit/mini-crit multipliers
    // Note: Crits ignore distance falloff in TF2
    if (bCrit)
    {
        // Crits use base damage * 3, no distance falloff
        flDamagePerPellet = stats.flBaseDamagePerPellet * TF_DAMAGE_CRIT_MULTIPLIER;
        result.bIsCrit = true;
    }
    else if (bMiniCrit)
    {
        // Mini-crits: base damage with distance mod * 1.35, no falloff below 100%
        float flMiniCritDistMod = std::max(1.0f, flDistanceMod);  // No falloff, only ramp-up
        flDamagePerPellet = stats.flBaseDamagePerPellet * flMiniCritDistMod * TF_DAMAGE_MINICRIT_MULTIPLIER;
        result.bIsMiniCrit = true;
    }
    
    // Clamp pellets hit to max pellet count
    nPelletsHit = std::min(nPelletsHit, stats.nPelletCount);
    
    // Calculate total damage
    result.flDamagePerPellet = flDamagePerPellet;
    result.nPelletsHit = nPelletsHit;
    result.flTotalDamage = flDamagePerPellet * static_cast<float>(nPelletsHit);
    
    // Calculate pellets required to kill
    int nTargetHealth = pTarget->m_iHealth();
    result.nPelletsRequired = GetPelletsRequiredToKill(pTarget, flDamagePerPellet, bCrit, bMiniCrit);
    
    // Check if we can kill with current pellets
    result.bCanKill = (result.flTotalDamage >= static_cast<float>(nTargetHealth));
    
    // Estimate kill probability based on pellet hit chance
    if (result.nPelletsRequired <= stats.nPelletCount)
    {
        result.flKillProbability = static_cast<float>(nPelletsHit) / static_cast<float>(result.nPelletsRequired);
        result.flKillProbability = std::min(1.0f, result.flKillProbability);
    }
    else
    {
        result.flKillProbability = 0.0f;
    }
    
    return result;
}

int CShotgunDamagePrediction::GetPelletsRequiredToKill(
    C_TFPlayer* pTarget,
    float flDamagePerPellet,
    bool bCrit,
    bool bMiniCrit)
{
    if (!pTarget || flDamagePerPellet <= 0.0f)
        return 999;
    
    int nTargetHealth = pTarget->m_iHealth();
    
    // Calculate pellets needed (ceiling division)
    int nPelletsNeeded = static_cast<int>(ceilf(static_cast<float>(nTargetHealth) / flDamagePerPellet));
    
    return nPelletsNeeded;
}
