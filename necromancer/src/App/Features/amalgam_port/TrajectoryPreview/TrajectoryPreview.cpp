#include "TrajectoryPreview.h"
#include "../../../../SDK/Helpers/Draw/Draw.h"

void CTrajectoryPreview::ProjectileTrace(C_TFPlayer* pPlayer, C_TFWeaponBase* pWeapon, bool bQuick)
{
    // Check if feature is enabled
    if (!CFG::Visuals_Trajectory_Preview_Active || CFG::Visuals_Trajectory_Preview_Style == 0)
        return;

    // Clean screenshot - don't draw trajectory preview
    if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
        return;

    // Get current view angles for real-time preview
    Vec3 vAngles = I::EngineClient->GetViewAngles();
    int iFlags = bQuick ? ProjSimEnum::Trace | ProjSimEnum::InitCheck | ProjSimEnum::Quick 
                        : ProjSimEnum::Trace | ProjSimEnum::InitCheck;

    // Validate player and weapon
    if (!pPlayer || !pWeapon)
        return;

    // Skip flamethrower (doesn't make sense to show trajectory)
    if (pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER)
        return;

    // Get projectile info - clear path first to prevent memory buildup
    ProjectileInfo tProjInfo = {};
    tProjInfo.m_vPath.clear();
    tProjInfo.m_vPath.reserve(400); // Pre-allocate to avoid reallocations
    
    if (!F::ProjSim.GetInfo(pPlayer, pWeapon, vAngles, tProjInfo, iFlags, -1.f))
        return;
    
    if (!F::ProjSim.Initialize(tProjInfo))
        return;

    // Setup trace filter
    CGameTrace trace = {};
    CTraceFilterCollideable filter = {};
    filter.pSkip = pPlayer;
    int nMask = MASK_SOLID;
    F::ProjSim.SetupTrace(filter, nMask, pWeapon, 0, bQuick);

    Vec3* pNormal = nullptr;
    
    // Simulate projectile path
    for (int n = 1; n <= TIME_TO_TICKS(tProjInfo.m_flLifetime); n++)
    {
        Vec3 Old = F::ProjSim.GetOrigin();
        F::ProjSim.RunTick(tProjInfo);
        Vec3 New = F::ProjSim.GetOrigin();

        SDK::TraceHull(Old, New, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, nMask, &filter, &trace);
        F::ProjSim.SetupTrace(filter, nMask, pWeapon, n, bQuick);
        
        if (trace.DidHit())
        {
            pNormal = &trace.plane.normal;
            if (trace.startsolid)
                *pNormal = F::ProjSim.GetVelocity().Normalized();
            break;
        }
    }

    // Need at least some path points
    if (tProjInfo.m_vPath.empty())
        return;

    // Add final position
    tProjInfo.m_vPath.push_back(trace.endpos);

    // Draw the trajectory path
    int iStyle = CFG::Visuals_Trajectory_Preview_Style;
    
    // Draw trajectory with single color (always visible through walls)
    if (CFG::Color_Trajectory.a > 0)
        H::Draw->RenderPath(tProjInfo.m_vPath, CFG::Color_Trajectory, false, iStyle);

    // Draw impact box
    if (CFG::Visuals_Trajectory_Preview_Box && pNormal)
    {
        const float flSize = std::max(tProjInfo.m_vHull.x, 1.f);
        const Vec3 vSize = { 1.f, flSize, flSize };
        Vec3 vBoxAngles = Math::VectorAngles(*pNormal);

        if (CFG::Color_Trajectory.a > 0)
            H::Draw->RenderBox(trace.endpos, vSize * -1, vSize, vBoxAngles, CFG::Color_Trajectory, false);
    }
}
