#pragma once

#include "../../../../SDK/SDK.h"

// ============================================
// Simplified Ticks Class for Projectile Aimbot
// ============================================
// This is a simplified version that only provides
// what the projectile aimbot needs - no doubletap/warp

class CProjectileTicks
{
private:
    Vec3 m_vShootPos = {};
    bool m_bTimingUnsure = false;

public:
    // Save the current shoot position (call in CreateMove)
    // Accounts for CrouchWhileAirborne - if IN_DUCK is set while airborne,
    // the actual shoot position will be lower even if FL_DUCKING isn't set yet
    void SaveShootPos(C_TFPlayer* pLocal, const CUserCmd* pCmd = nullptr)
    {
        if (!pLocal || !pLocal->IsAlive())
            return;

        // Amalgam's Ticks::SaveShootPos gate (m_iShiftedTicks == m_iShiftStart):
        // a doubletap batch must aim every command from the position captured at
        // the batch's first command, or the simulated solution drifts per batch
        // command and the fired shot's timing no longer matches the validation
        if (Shifting::bShifting && Shifting::nCurrentShiftTick)
            return;

        // Use centralized duck prediction (handles CrouchWhileAirborne)
        m_vShootPos = SDKUtils::GetPredictedShootPos(pLocal, pCmd);
    }

    // Get the saved shoot position
    Vec3 GetShootPos()
    {
        return m_vShootPos;
    }

    bool IsTimingUnsure()
    {
        if (!Shifting::bShifting || Shifting::bShiftingWarp)
        {
            m_bTimingUnsure = false;
            return false;
        }

        if (!Shifting::nCurrentShiftTick)
        {
            const auto pWeapon = H::Entities->GetWeapon();
            m_bTimingUnsure = pWeapon && pWeapon->IsInReload();
        }
        return m_bTimingUnsure;
    }

    // Reset state
    void Reset()
    {
        m_vShootPos = {};
        m_bTimingUnsure = false;
    }
};

MAKE_SINGLETON_SCOPED(CProjectileTicks, ProjectileTicks, F);
