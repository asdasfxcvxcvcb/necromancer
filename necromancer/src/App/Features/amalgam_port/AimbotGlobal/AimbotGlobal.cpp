#include "AimbotGlobal.h"
#include "../Ticks/Ticks.h"
#include "../../Players/Players.h"

void CAimbotGlobal::SortTargets(std::vector<Target_t>& targets, int iMethod)
{
    std::ranges::sort(targets, [&](const Target_t& a, const Target_t& b) -> bool
    {
        switch (iMethod)
        {
        case Vars::Aimbot::General::TargetSelectionEnum::FOV:
            return a.m_flFOVTo < b.m_flFOVTo;
        case Vars::Aimbot::General::TargetSelectionEnum::Distance:
            return a.m_flDistTo < b.m_flDistTo;
        default:
            return false;
        }
    });
}

void CAimbotGlobal::SortPriority(std::vector<Target_t>& targets)
{
    std::ranges::stable_sort(targets, [](const Target_t& a, const Target_t& b) -> bool
    {
        return a.m_nPriority > b.m_nPriority;
    });
}

bool CAimbotGlobal::PlayerBoneInFOV(C_TFPlayer* pTarget, Vec3 vLocalPos, Vec3 vLocalAngles,
                                    float& flFOVTo, Vec3& vPos, Vec3& vAngleTo, int iHitboxes)
{
    if (!pTarget)
        return false;

    // Amalgam gates acquisition by its single General::AimFOV (default 180).
    // The projectile module's knob here is the projectile FOV - the same value
    // the native aimbot targets with and the FOV circle draws. Gating by the
    // general FOV instead made most off-center targets invisible to this port.
    const float flAimFOV = CFG::Aimbot_Projectile_FOV;

    // Dormant/no-bones fallback: center eligibility (Amalgam's EntityCenterInFOV)
    auto fCenter = [&]() -> bool
    {
        vPos = pTarget->GetCenter();
        vAngleTo = Math::CalcAngle(vLocalPos, vPos);
        flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
        return flFOVTo < flAimFOV;
    };

    matrix3x4_t bones[128];
    if (!pTarget->SetupBones(bones, 128, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
        return fCenter();

    // GetModel returns model_t*, so the hitbox set goes through ModelInfoClient
    auto pModel = pTarget->GetModel();
    auto pHDR = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
    auto pSet = pHDR ? pHDR->pHitboxSet(pTarget->m_nHitboxSet()) : nullptr;
    if (!pSet)
        return fCenter();

    // Amalgam scans every valid hitbox, accepts the target if any of them is
    // inside the cone, and reports the closest one as the initial aim position
    float flMinFOV = 180.f;
    for (int nHitbox = 0; nHitbox < pSet->numhitboxes; nHitbox++)
    {
        if (!IsHitboxValid(pTarget, nHitbox, iHitboxes))
            continue;

        auto pBox = pSet->pHitbox(nHitbox);
        if (!pBox)
            continue;

        Vec3 vCurPos;
        Math::VectorTransform((pBox->bbmin + pBox->bbmax) * 0.5f, bones[pBox->bone], vCurPos);

        const Vec3 vCurAngleTo = Math::CalcAngle(vLocalPos, vCurPos);
        const float flCurFOVTo = Math::CalcFov(vLocalAngles, vCurAngleTo);
        if (flCurFOVTo < flMinFOV)
        {
            vPos = vCurPos;
            vAngleTo = vCurAngleTo;
            flFOVTo = flMinFOV = flCurFOVTo;
        }
    }

    return flMinFOV < flAimFOV;
}

bool CAimbotGlobal::IsHitboxValid(C_BaseEntity* pEntity, int nHitbox, int iHitboxes)
{
    // Map hitbox index to hitbox flags
    switch (nHitbox)
    {
    case 0: // HITBOX_HEAD
        return iHitboxes & (1 << 0);
    case 1: // HITBOX_PELVIS
    case 2: // HITBOX_SPINE_0
    case 3: // HITBOX_SPINE_1
    case 4: // HITBOX_SPINE_2
    case 5: // HITBOX_SPINE_3
        return iHitboxes & (1 << 1);
    default:
        return iHitboxes & (1 << 2);
    }
}

bool CAimbotGlobal::IsHitboxValid(int nHitbox, int iHitboxes)
{
    switch (nHitbox)
    {
    case BOUNDS_HEAD:
        return iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::Head;
    case BOUNDS_BODY:
        return iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::Body;
    case BOUNDS_FEET:
        return iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::Feet;
    }
    return false;
}

bool CAimbotGlobal::ShouldIgnore(C_BaseEntity* pTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
    if (!pTarget || !pLocal)
        return true;

    // Don't target self
    if (pTarget == pLocal)
        return true;

    // Check if it's a player
    if (IsPlayer(pTarget))
    {
        auto pPlayer = pTarget->As<C_TFPlayer>();
        
        // Dead players
        if (!IsAlive(pPlayer) || IsAGhost(pPlayer))
            return true;

        if (!FriendlyFire() && pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
            return false;

        // Invulnerable
        if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invulnerable)
        {
            if (pPlayer->InCond(TF_COND_INVULNERABLE) || 
                pPlayer->InCond(TF_COND_INVULNERABLE_CARD_EFFECT) ||
                pPlayer->InCond(TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED) ||
                pPlayer->InCond(TF_COND_INVULNERABLE_USER_BUFF) ||
                pPlayer->InCond(TF_COND_PHASE))
                return true;
        }

        // Invisible
        if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invisible)
        {
            if (pPlayer->m_flInvisibility() >= 1.0f)
                return true;
        }

        // Taunting
        if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Taunting)
        {
            if (pPlayer->InCond(TF_COND_TAUNTING))
                return true;
        }

        // Friends/Ignored players (using SEOwnedDE's player list system)
        if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends)
        {
            PlayerPriority priority{};
            if (F::Players->GetInfo(pPlayer->entindex(), priority))
            {
                if (priority.Ignored)
                    return true;
            }
        }
    }

    return false;
}

int CAimbotGlobal::GetPriority(int iIndex)
{
    // TODO: Implement priority system based on player list
    return 0;
}

bool CAimbotGlobal::FriendlyFire()
{
    static ConVar* mp_friendlyfire = I::CVar->FindVar("mp_friendlyfire");
    return mp_friendlyfire && mp_friendlyfire->GetBool();
}

bool CAimbotGlobal::ShouldAim()
{
    // Amalgam's original logic: for Plain/Silent aim types, only aim when we can actually attack
    // This prevents the aimbot from "pre-aiming" when we can't shoot yet
    // NOTE: The aimbot key check is done in RunMain, not here!
    switch (Vars::Aimbot::General::AimType.Value)
    {
    case Vars::Aimbot::General::AimTypeEnum::Plain:
    case Vars::Aimbot::General::AimTypeEnum::Silent:
        // Only aim if we can attack OR we're reloading (queued attack) OR timing is unsure
        if (!G::bCanPrimaryAttack && !G::bReloading && !F::AmalgamTicks->IsTimingUnsure())
            return false;
    }

    return true;
}

bool CAimbotGlobal::ShouldHoldAttack(C_TFWeaponBase* pWeapon)
{
    if (!pWeapon)
        return false;

    // Amalgam's original logic: hold attack for minigun when we can't attack yet
    // and the user was holding attack last tick
    // This is controlled by AimHoldsFire setting in Amalgam
    // For SEOwnedDE, we'll use a simplified version
    switch (pWeapon->GetWeaponID())
    {
    case TF_WEAPON_MINIGUN:
        // Only hold if user was attacking and we can't attack yet
        if (!G::bCanPrimaryAttack && G::LastUserCmd && (G::LastUserCmd->buttons & IN_ATTACK) && Vars::Aimbot::General::AimType.Value && !pWeapon->IsInReload())
            return true;
        break;
    }

    return false;
}

bool CAimbotGlobal::ValidBomb(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, C_BaseEntity* pBomb)
{
    // TODO: Implement bomb validation for MvM
    return false;
}
