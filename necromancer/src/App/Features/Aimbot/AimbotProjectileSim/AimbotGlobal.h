#pragma once

#include "../../CFG.h"
#include "../../LagRecords/LagRecords.h"

constexpr float AIM_ASSIST_STRENGTH = 25.f;

namespace AimTypeEnum
{
	enum AimTypeEnum { Off = 0, Plain = 1, Smooth = 2, Silent = 3, Locking = 4, Assistive = 5 };
}
namespace TargetSelectionEnum
{
	enum TargetSelectionEnum { FOV = 0, Distance = 1 };
}
namespace TargetEnum
{
	enum TargetEnum { Unknown, Player, Sentry, Dispenser, Teleporter, Sticky, NPC, Bomb };
}
namespace HitboxesEnum
{
	enum HitboxesEnum { Auto = 1 << 0, Head = 1 << 1, Body = 1 << 2, Feet = 1 << 3, BodyaimIfLethal = 1 << 4, PrioritizeFeet = 1 << 5 };
}
namespace ModifiersEnum
{
	enum ModifiersEnum { ChargeWeapon = 1 << 0, CancelCharge = 1 << 1, UseArmTime = 1 << 2, AirSplash = 1 << 3, LobAngles = 1 << 4, TargetDormant = 1 << 5 };
}

inline int AimTypeValue()
{
	return CFG::Aimbot_Projectile_Aim_Type == 1 ? AimTypeEnum::Silent : AimTypeEnum::Plain;
}

inline int ProjectileHitboxFlags()
{
	return (CFG::Aimbot_Amalgam_Projectile_Hitbox_Auto ? HitboxesEnum::Auto : 0) |
	       (CFG::Aimbot_Amalgam_Projectile_Hitbox_Head ? HitboxesEnum::Head : 0) |
	       (CFG::Aimbot_Amalgam_Projectile_Hitbox_Body ? HitboxesEnum::Body : 0) |
	       (CFG::Aimbot_Amalgam_Projectile_Hitbox_Feet ? HitboxesEnum::Feet : 0) |
	       (CFG::Aimbot_Amalgam_Projectile_Hitbox_BodyaimLethal ? HitboxesEnum::BodyaimIfLethal : 0) |
	       (CFG::Aimbot_Amalgam_Projectile_Hitbox_PrioritizeFeet ? HitboxesEnum::PrioritizeFeet : 0);
}

inline int ProjectileModifierFlags()
{
	return (CFG::Aimbot_Amalgam_Projectile_Mod_ChargeWeapon ? ModifiersEnum::ChargeWeapon : 0) |
	       ModifiersEnum::CancelCharge |
	       (CFG::Aimbot_Amalgam_Projectile_Mod_UseArmTime ? ModifiersEnum::UseArmTime : 0) |
	       (CFG::Aimbot_Amalgam_Projectile_Mod_AirSplash ? ModifiersEnum::AirSplash : 0) |
	       (CFG::Aimbot_Amalgam_Projectile_Mod_LobAngles ? ModifiersEnum::LobAngles : 0);
}

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
