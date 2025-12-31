#include "SmartShotgunAimbot.h"
#include "../../../SDK/Impl/TraceFilters/TraceFilters.h"
#include "../SeedPred/SeedPred.h"
#include "../CFG.h"

bool CSmartShotgunAimbot::CanDoubleTap(const SmartShotgunConfig& config)
{
    // Check if double tap is enabled
    if (!config.bDoubleTapEnabled)
        return false;
    
    // Check if we have enough stored ticks for double tap
    if (config.nDoubleTapTicksStored < config.nDoubleTapTicksRequired)
        return false;
    
    return true;
}

bool CSmartShotgunAimbot::IsRapidFireReady(const SmartShotgunConfig& config)
{
    return config.bRapidFireReady;
}

int CSmartShotgunAimbot::EstimatePelletsCanHit(
    C_TFPlayer* pLocal,
    C_TFWeaponBase* pWeapon,
    C_TFPlayer* pTarget)
{
    if (!pLocal || !pWeapon || !pTarget)
        return 0;
    
    // Get pellet world positions from no-spread prediction
    std::vector<Vec3> pelletPositions;
    GetNoSpreadPelletPositions(pLocal, pWeapon, pelletPositions);
    
    // Count how many would hit the target
    return CountPelletsHittingTarget(pLocal, pTarget, pelletPositions);
}

void CSmartShotgunAimbot::GetNoSpreadPelletPositions(
    C_TFPlayer* pLocal,
    C_TFWeaponBase* pWeapon,
    std::vector<Vec3>& outWorldPositions)
{
    outWorldPositions.clear();
    
    if (!pLocal || !pWeapon)
        return;
    
    // Get player shoot position and angles
    Vec3 vShootPos = pLocal->GetShootPos();
    Vec3 vAngles = I::EngineClient->GetViewAngles();
    
    // Calculate forward, right, up vectors
    Vec3 vForward, vRight, vUp;
    Math::AngleVectors(vAngles, &vForward, &vRight, &vUp);
    
    // Trace distance for shotgun
    const float flTraceDistance = 8192.0f;
    
    // Get weapon spread
    const float flSpread = pWeapon->GetWeaponSpread();
    
    // Get bullets per shot (with attribute hook)
    int nBulletsPerShot = pWeapon->GetWeaponInfo()->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_nBulletsPerShot;
    nBulletsPerShot = static_cast<int>(SDKUtils::AttribHookValue(static_cast<float>(nBulletsPerShot), "mult_bullets_per_shot", pWeapon));
    
    // Check if fixed spread is enabled (tf_use_fixed_weaponspreads)
    static ConVar* tf_use_fixed_weaponspreads = I::CVar->FindVar("tf_use_fixed_weaponspreads");
    const bool bFixedSpread = tf_use_fixed_weaponspreads && tf_use_fixed_weaponspreads->GetBool();
    
    // Get seed from SeedPred if active
    int nSeed = F::SeedPred->GetSeed();
    
    // Calculate end position for each pellet
    for (int iBullet = 0; iBullet < nBulletsPerShot; iBullet++)
    {
        float x = 0.0f;
        float y = 0.0f;
        
        if (bFixedSpread)
        {
            // Use fixed spread pattern (deterministic)
            // Pattern: center, right, left, down, up, corners, center
            static const Vec3 fixedPattern[] = {
                Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, -1, 0), Vec3(0, 1, 0),
                Vec3(0.85f, -0.85f, 0), Vec3(0.85f, 0.85f, 0), Vec3(-0.85f, -0.85f, 0), Vec3(-0.85f, 0.85f, 0), Vec3(0, 0, 0)
            };
            const float flScalar = 0.5f;
            x = fixedPattern[iBullet % 10].x * flScalar;
            y = fixedPattern[iBullet % 10].y * flScalar;
        }
        else
        {
            // Use SeedPred to calculate random spread
            SDKUtils::RandomSeed(nSeed++);
            
            // First pellet can be perfect if weapon hasn't fired recently
            if (iBullet == 0)
            {
                const float flTimeSinceLastShot = (pLocal->m_nTickBase() * TICK_INTERVAL) - pWeapon->m_flLastFireTime();
                if (flTimeSinceLastShot > 1.25f)
                {
                    // Perfect shot - no spread
                    x = 0.0f;
                    y = 0.0f;
                }
                else
                {
                    x = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
                    y = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
                }
            }
            else
            {
                x = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
                y = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
            }
        }
        
        // Apply spread to direction
        Vec3 vDir = vForward + (vRight * x * flSpread) + (vUp * y * flSpread);
        vDir.Normalize();
        
        // Calculate end position (where the pellet would go)
        Vec3 vEndPos = vShootPos + (vDir * flTraceDistance);
        
        outWorldPositions.push_back(vEndPos);
    }
}

int CSmartShotgunAimbot::CountPelletsHittingTarget(
    C_TFPlayer* pLocal,
    C_TFPlayer* pTarget,
    const std::vector<Vec3>& pelletEndPositions)
{
    if (!pLocal || !pTarget || pelletEndPositions.empty())
        return 0;
    
    int nHitCount = 0;
    Vec3 vShootPos = pLocal->GetShootPos();
    
    // Get target bounding box for intersection test
    Vec3 vTargetOrigin = pTarget->GetAbsOrigin();
    Vec3 vTargetMins = pTarget->m_vecMins() + vTargetOrigin;
    Vec3 vTargetMaxs = pTarget->m_vecMaxs() + vTargetOrigin;
    
    // Expand the box slightly to account for hitboxes being larger than collision
    const float flPadding = 8.0f;
    vTargetMins -= Vec3(flPadding, flPadding, 0);
    vTargetMaxs += Vec3(flPadding, flPadding, flPadding);
    
    for (const Vec3& pelletEnd : pelletEndPositions)
    {
        // Check if the pellet ray intersects the target's bounding box
        Vec3 vDir = pelletEnd - vShootPos;
        vDir.Normalize();
        
        // Simple ray-AABB intersection test
        float tmin = 0.0f;
        float tmax = 8192.0f;
        
        bool bHit = true;
        for (int i = 0; i < 3 && bHit; i++)
        {
            float origin = (i == 0) ? vShootPos.x : (i == 1) ? vShootPos.y : vShootPos.z;
            float dir = (i == 0) ? vDir.x : (i == 1) ? vDir.y : vDir.z;
            float minB = (i == 0) ? vTargetMins.x : (i == 1) ? vTargetMins.y : vTargetMins.z;
            float maxB = (i == 0) ? vTargetMaxs.x : (i == 1) ? vTargetMaxs.y : vTargetMaxs.z;
            
            if (fabsf(dir) < 0.0001f)
            {
                // Ray is parallel to slab
                if (origin < minB || origin > maxB)
                    bHit = false;
            }
            else
            {
                float t1 = (minB - origin) / dir;
                float t2 = (maxB - origin) / dir;
                
                if (t1 > t2) std::swap(t1, t2);
                
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                
                if (tmin > tmax)
                    bHit = false;
            }
        }
        
        if (bHit && tmin >= 0.0f)
        {
            nHitCount++;
        }
    }
    
    return nHitCount;
}

SmartShotDecision CSmartShotgunAimbot::ShouldShoot(
    C_TFPlayer* pLocal,
    C_TFWeaponBase* pWeapon,
    C_TFPlayer* pTarget,
    const SmartShotgunConfig& config)
{
    SmartShotDecision decision = {};
    decision.bShouldShoot = false;
    decision.bWaitForMorePellets = false;
    decision.szReason = "Unknown";
    
    if (!pLocal || !pWeapon || !pTarget)
    {
        decision.szReason = "Invalid entities";
        return decision;
    }
    
    // Check if this is a shotgun
    EShotgunType shotgunType = g_ShotgunDmgPred.GetShotgunType(pWeapon);
    if (shotgunType == EShotgunType::Unknown)
    {
        // Not a shotgun - allow shooting (let other systems handle it)
        decision.bShouldShoot = true;
        decision.szReason = "Not a shotgun - pass through";
        return decision;
    }
    
    // Get shotgun stats
    ShotgunStats stats = g_ShotgunDmgPred.GetShotgunStats(shotgunType);
    
    // Calculate distance to target
    float flDistance = pLocal->GetShootPos().DistTo(pTarget->GetCenter());
    
    // Determine if we're using crits
    bool bUsingCrit = config.bCanCrit || config.bHoldingCritKey;
    bool bMiniCrit = false;
    
    // Check for Reserve Shooter mini-crit on airborne targets
    if (stats.bMiniCritAirborne && g_ShotgunDmgPred.IsTargetAirborne(pTarget))
    {
        bMiniCrit = true;
    }
    
    // Calculate damage per pellet
    float flDistanceMod = g_ShotgunDmgPred.GetDistanceDamageModifier(flDistance, stats.bHasRampUp, stats.bHasRampDown);
    float flDamagePerPellet = stats.flBaseDamagePerPellet * flDistanceMod;
    
    // Apply crit/mini-crit multipliers
    if (bUsingCrit)
    {
        flDamagePerPellet = stats.flBaseDamagePerPellet * TF_DAMAGE_CRIT_MULTIPLIER;
        decision.bUsingCrit = true;
    }
    else if (bMiniCrit)
    {
        float flMiniCritDistMod = std::max(1.0f, flDistanceMod);
        flDamagePerPellet = stats.flBaseDamagePerPellet * flMiniCritDistMod * TF_DAMAGE_MINICRIT_MULTIPLIER;
    }
    
    // Get target health
    decision.flTargetHealth = static_cast<float>(pTarget->m_iHealth());
    
    // Calculate pellets needed to kill
    int nPelletsToKill = g_ShotgunDmgPred.GetPelletsRequiredToKill(pTarget, flDamagePerPellet, bUsingCrit, bMiniCrit);
    decision.nPelletsRequired = nPelletsToKill;
    
    // Estimate how many pellets can currently hit
    int nPelletsCanHit = EstimatePelletsCanHit(pLocal, pWeapon, pTarget);
    decision.nCurrentPelletsCanHit = nPelletsCanHit;
    
    // Calculate expected damage
    decision.flExpectedDamage = flDamagePerPellet * static_cast<float>(nPelletsCanHit);
    
    // Check if user can double tap
    bool bCanDT = CanDoubleTap(config);
    bool bRapidFireReady = IsRapidFireReady(config);
    decision.bUsingDoubleTap = bCanDT && bRapidFireReady;
    
    // Determine minimum pellet threshold based on mode
    // For double tap: 50% = 5 pellets
    // For normal: 10% = 1 pellet (minimum 1)
    int nMinThreshold;
    if (decision.bUsingDoubleTap)
    {
        nMinThreshold = static_cast<int>(ceilf(static_cast<float>(stats.nPelletCount) * config.flDoubleTapMinPelletPercent));
    }
    else
    {
        // For normal mode, require at least 1 pellet to hit
        nMinThreshold = 1;
    }
    decision.nPelletsToWaitFor = nMinThreshold;
    
    // SHOOT if we can hit at least the minimum threshold of pellets
    if (nPelletsCanHit >= nMinThreshold)
    {
        decision.bShouldShoot = true;
        decision.szReason = decision.bUsingDoubleTap ? "DT: Met pellet threshold" : "Normal: Met pellet threshold";
        return decision;
    }
    
    // Not enough pellets visible - wait
    decision.bWaitForMorePellets = true;
    decision.szReason = decision.bUsingDoubleTap ? "DT: Waiting for pellets" : "Normal: Waiting for pellets";
    return decision;
}
