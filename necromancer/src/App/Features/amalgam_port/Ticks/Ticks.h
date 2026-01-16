#pragma once

#include "../AmalgamCompat.h"

// ============================================
// Simplified Ticks Class for Projectile Aimbot
// ============================================
// This is a simplified version that only provides
// what the projectile aimbot needs - no doubletap/warp

class CAmalgamTicks
{
private:
    Vec3 m_vShootPos = {};
    Vec3 m_vShootAngle = {};
    bool m_bShootAngle = false;

public:
    // Save the current shoot position (call in CreateMove)
    // Accounts for CrouchWhileAirborne - if IN_DUCK is set while airborne,
    // the actual shoot position will be lower even if FL_DUCKING isn't set yet
    void SaveShootPos(C_TFPlayer* pLocal, CUserCmd* pCmd = nullptr)
    {
        if (!pLocal || !pLocal->IsAlive())
            return;
            
        m_vShootPos = pLocal->GetShootPos();
        
        // Account for CrouchWhileAirborne feature
        // When airborne, ducking is instant - if IN_DUCK is set but FL_DUCKING isn't,
        // the actual shoot position when firing will be at duck height
        // TF2 view heights: standing = 68, ducking = 45, difference = 23 units
        if (pCmd || G::CurrentUserCmd)
        {
            CUserCmd* cmd = pCmd ? pCmd : G::CurrentUserCmd;
            const bool bCurrentlyDucking = (pLocal->m_fFlags() & FL_DUCKING) != 0;
            const bool bWantsToDuck = (cmd->buttons & IN_DUCK) != 0;
            const bool bOnGround = (pLocal->m_fFlags() & FL_ONGROUND) != 0;
            
            if (bWantsToDuck && !bCurrentlyDucking && !bOnGround)
            {
                // Adjust to duck view height (68 -> 45 = -23 units)
                m_vShootPos.z -= 23.0f * pLocal->m_flModelScale();
            }
        }
    }

    // Get the saved shoot position
    Vec3 GetShootPos()
    {
        return m_vShootPos;
    }

    // Save shoot angle (not used for projectile aimbot without doubletap)
    void SaveShootAngle(CUserCmd* pCmd, bool bSendPacket)
    {
        if (bSendPacket)
            m_bShootAngle = false;
        else if (!m_bShootAngle && pCmd)
            m_vShootAngle = pCmd->viewangles, m_bShootAngle = true;
    }

    // Get shoot angle (returns nullptr without doubletap)
    Vec3* GetShootAngle()
    {
        return nullptr; // No doubletap, no saved angle
    }

    // Timing is never unsure without tick manipulation
    bool IsTimingUnsure()
    {
        return false;
    }

    // No tick shifting without doubletap
    int GetTicks(C_TFWeaponBase* pWeapon = nullptr)
    {
        return 0;
    }

    // Reset state
    void Reset()
    {
        m_vShootPos = {};
        m_vShootAngle = {};
        m_bShootAngle = false;
    }

    // Stub functions for compatibility
    void CreateMove(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd, bool* pSendPacket)
    {
        SaveShootPos(pLocal, pCmd);
        SaveShootAngle(pCmd, pSendPacket ? *pSendPacket : true);
    }

    void Start(C_TFPlayer* pLocal, CUserCmd* pCmd)
    {
        // No-op without tick manipulation
    }

    void End(C_TFPlayer* pLocal, CUserCmd* pCmd)
    {
        // No-op without tick manipulation
    }

    bool CanChoke()
    {
        return true; // Always can choke without tick manipulation
    }

    void Draw(C_TFPlayer* pLocal)
    {
        // No UI needed
    }
};

MAKE_SINGLETON_SCOPED(CAmalgamTicks, AmalgamTicks, F);
