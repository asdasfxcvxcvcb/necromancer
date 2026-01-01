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
    void SaveShootPos(C_TFPlayer* pLocal)
    {
        if (pLocal && pLocal->IsAlive())
            m_vShootPos = pLocal->GetShootPos();
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
        SaveShootPos(pLocal);
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

// Alias for compatibility with Amalgam code
namespace F {
    inline CAmalgamTicks& Ticks = *F::AmalgamTicks;
}
