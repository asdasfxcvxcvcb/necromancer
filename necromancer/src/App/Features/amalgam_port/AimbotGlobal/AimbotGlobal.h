#pragma once

#include "../AmalgamCompat.h"
#include "../../LagRecords/LagRecords.h"

// ============================================
// Bounds Hitboxes (for projectile prediction)
// ============================================

enum BOUNDS_HITBOXES
{
    BOUNDS_HEAD = 0,
    BOUNDS_BODY = 1,
    BOUNDS_FEET = 2
};

// ============================================
// Target Type Enum
// ============================================

Enum(Target, Unknown, Player, Sentry, Dispenser, Teleporter, Sticky, NPC, Bomb)

// ============================================
// Tick Record (for backtrack compatibility)
// ============================================

struct TickRecord
{
    float m_flSimTime = 0.f;
    Vec3 m_vOrigin = {};
    Vec3 m_vMins = {};
    Vec3 m_vMaxs = {};
    bool m_bOnShot = false;
    bool m_bInvalid = false;
    matrix3x4_t m_aBones[128]; // MAXSTUDIOBONES
};

// ============================================
// Target Structure
// ============================================

struct Target_t
{
    C_BaseEntity* m_pEntity = nullptr;
    int m_iTargetType = TargetEnum::Unknown;
    Vec3 m_vPos = {};
    Vec3 m_vAngleTo = {};
    float m_flFOVTo = std::numeric_limits<float>::max();
    float m_flDistTo = std::numeric_limits<float>::max();
    int m_nPriority = 0;
    int m_nAimedHitbox = -1;

    TickRecord* m_pRecord = nullptr;
    bool m_bBacktrack = false;
};

// ============================================
// AimbotGlobal Class
// ============================================

class CAimbotGlobal
{
public:
    void SortTargets(std::vector<Target_t>& targets, int iMethod);
    void SortPriority(std::vector<Target_t>& targets);

    bool PlayerBoneInFOV(C_TFPlayer* pTarget, Vec3 vLocalPos, Vec3 vLocalAngles, 
                         float& flFOVTo, Vec3& vPos, Vec3& vAngleTo, 
                         int iHitboxes = 0b11111);
    
    bool IsHitboxValid(C_BaseEntity* pEntity, int nHitbox, int iHitboxes = 0b11111);
    bool IsHitboxValid(int nHitbox, int iHitboxes = 0b1111);
    
    bool ShouldIgnore(C_BaseEntity* pTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
    int GetPriority(int iIndex);
    bool FriendlyFire();

    bool ShouldAim();
    bool ShouldHoldAttack(C_TFWeaponBase* pWeapon);
    bool ValidBomb(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, C_BaseEntity* pBomb);
};

MAKE_SINGLETON_SCOPED(CAimbotGlobal, AimbotGlobal, F);
