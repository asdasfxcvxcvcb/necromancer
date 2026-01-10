#pragma once

#include "../AmalgamCompat.h"

// ============================================
// Projectile Simulation Flags
// ============================================

Enum(ProjSim,
    None = 0,
    Trace = 1 << 0,         // Trace when doing GetProjectileFireSetup
    InitCheck = 1 << 1,     // Validate starting position
    Quick = 1 << 2,         // Use interpolation
    NoRandomAngles = 1 << 3, // Don't do angle stuff for aimbot
    PredictCmdNum = 1 << 4,  // Use crithack to predict command number
    MaxSpeed = 1 << 5        // Default projectile speeds to their maximum
)

// ============================================
// Projectile Info Structure
// ============================================

struct ProjectileInfo
{
    C_TFPlayer* m_pOwner = nullptr;
    C_TFWeaponBase* m_pWeapon = nullptr;
    uint32_t m_uType = 0;

    Vec3 m_vPos = {};
    Vec3 m_vAng = {};
    Vec3 m_vHull = {};

    float m_flVelocity = 0.f;
    float m_flGravity = 0.f;
    float m_flLifetime = 60.f;

    std::vector<Vec3> m_vPath = {};

    int m_iFlags = 0;
};

// ============================================
// Projectile Simulation Class
// ============================================

class CAmalgamProjectileSimulation
{
private:
    bool GetInfoMain(C_TFPlayer* pPlayer, C_TFWeaponBase* pWeapon, Vec3 vAngles, ProjectileInfo& tProjInfo, int iFlags, float flAutoCharge);

    const objectparams_t m_tPhysDefaultObjectParams = {
        NULL,
        1.0,    // mass
        1.0,    // inertia
        0.1f,   // damping
        0.1f,   // rotdamping
        0.05f,  // rotIntertiaLimit
        "DEFAULT",
        NULL,   // game data
        0.f,    // volume
        1.0f,   // drag coefficient
        true,   // enable collisions?
    };

public:
    bool GetInfo(C_TFPlayer* pPlayer, C_TFWeaponBase* pWeapon, Vec3 vAngles, ProjectileInfo& tProjInfo, int iFlags = ProjSimEnum::Trace | ProjSimEnum::InitCheck, float flAutoCharge = -1.f);
    void SetupTrace(CTraceFilterCollideable& filter, int& nMask, C_TFWeaponBase* pWeapon, int nTick = 0, bool bQuick = false);

    void GetInfo(C_BaseEntity* pProjectile, ProjectileInfo& tProjInfo);
    void SetupTrace(CTraceFilterCollideable& filter, int& nMask, C_BaseEntity* pProjectile);

    bool Initialize(ProjectileInfo& tProjInfo, bool bSimulate = true, bool bWorld = false);
    void RunTick(ProjectileInfo& tProjInfo, bool bPath = true);
    Vec3 GetOrigin();
    Vec3 GetVelocity();

    // Get weapon and owner from projectile entity
    inline std::pair<C_TFWeaponBase*, C_TFPlayer*> GetEntities(C_BaseEntity* pProjectile)
    {
        std::pair<C_TFWeaponBase*, C_TFPlayer*> paReturn = { nullptr, nullptr };
        if (!pProjectile)
            return paReturn;
        
        switch (pProjectile->GetClassId())
        {
        case ETFClassIds::CBaseGrenade:
        case ETFClassIds::CTFWeaponBaseGrenadeProj:
        case ETFClassIds::CTFWeaponBaseMerasmusGrenade:
        case ETFClassIds::CTFGrenadePipebombProjectile:
        case ETFClassIds::CTFStunBall:
        case ETFClassIds::CTFBall_Ornament:
        case ETFClassIds::CTFProjectile_Jar:
        case ETFClassIds::CTFProjectile_Cleaver:
        case ETFClassIds::CTFProjectile_JarGas:
        case ETFClassIds::CTFProjectile_JarMilk:
        case ETFClassIds::CTFProjectile_SpellBats:
        case ETFClassIds::CTFProjectile_SpellKartBats:
        case ETFClassIds::CTFProjectile_SpellMeteorShower:
        case ETFClassIds::CTFProjectile_SpellMirv:
        case ETFClassIds::CTFProjectile_SpellPumpkin:
        case ETFClassIds::CTFProjectile_SpellSpawnBoss:
        case ETFClassIds::CTFProjectile_SpellSpawnHorde:
        case ETFClassIds::CTFProjectile_SpellSpawnZombie:
        case ETFClassIds::CTFProjectile_SpellTransposeTeleport:
        case ETFClassIds::CTFProjectile_Throwable:
        case ETFClassIds::CTFProjectile_ThrowableBreadMonster:
        case ETFClassIds::CTFProjectile_ThrowableBrick:
        case ETFClassIds::CTFProjectile_ThrowableRepel:
        {
            paReturn.first = pProjectile->As<C_TFGrenadePipebombProjectile>()->m_hOriginalLauncher()->As<C_TFWeaponBase>();
            paReturn.second = pProjectile->As<C_TFWeaponBaseGrenadeProj>()->m_hThrower()->As<C_TFPlayer>();
            break;
        }
        case ETFClassIds::CTFBaseRocket:
        case ETFClassIds::CTFFlameRocket:
        case ETFClassIds::CTFProjectile_Arrow:
        case ETFClassIds::CTFProjectile_GrapplingHook:
        case ETFClassIds::CTFProjectile_HealingBolt:
        case ETFClassIds::CTFProjectile_Rocket:
        case ETFClassIds::CTFProjectile_BallOfFire:
        case ETFClassIds::CTFProjectile_MechanicalArmOrb:
        case ETFClassIds::CTFProjectile_SentryRocket:
        case ETFClassIds::CTFProjectile_SpellFireball:
        case ETFClassIds::CTFProjectile_SpellLightningOrb:
        case ETFClassIds::CTFProjectile_SpellKartOrb:
        case ETFClassIds::CTFProjectile_EnergyBall:
        case ETFClassIds::CTFProjectile_Flare:
        {
            paReturn.first = pProjectile->As<C_TFBaseRocket>()->m_hLauncher()->As<C_TFWeaponBase>();
            paReturn.second = paReturn.first ? paReturn.first->m_hOwner()->As<C_TFPlayer>() : nullptr;
            break;
        }
        case ETFClassIds::CTFBaseProjectile:
        case ETFClassIds::CTFProjectile_EnergyRing:
        {
            paReturn.first = pProjectile->As<C_TFBaseProjectile>()->m_hLauncher()->As<C_TFWeaponBase>();
            paReturn.second = paReturn.first ? paReturn.first->m_hOwner()->As<C_TFPlayer>() : nullptr;
            break;
        }
        }
        return paReturn;
    }

    // Get projectile velocity
    inline Vec3 GetVelocity(C_BaseEntity* pProjectile)
    {
        if (!pProjectile)
            return {};
        
        switch (pProjectile->GetClassId())
        {
        case ETFClassIds::CTFProjectile_Rocket:
        case ETFClassIds::CTFProjectile_SentryRocket:
        case ETFClassIds::CTFProjectile_EnergyBall:
            if (!pProjectile->As<C_TFProjectile_Rocket>()->m_iDeflected())
                return pProjectile->As<C_TFProjectile_Rocket>()->m_vInitialVelocity();
            break;
        case ETFClassIds::CTFProjectile_Arrow:
            if (!pProjectile->As<C_TFProjectile_Rocket>()->m_iDeflected())
                return { 
                    pProjectile->As<C_TFProjectile_Rocket>()->m_vInitialVelocity().x,
                    pProjectile->As<C_TFProjectile_Rocket>()->m_vInitialVelocity().y,
                    pProjectile->GetAbsVelocity().z
                };
            break;
        }
        return pProjectile->GetAbsVelocity();
    }

    // Get projectile gravity multiplier
    inline float GetGravity(C_BaseEntity* pProjectile, C_TFWeaponBase* pWeapon = nullptr)
    {
        if (!pProjectile)
            return 0.f;
        
        float flReturn = 0.f;
        
        static auto sv_gravity = U::ConVars.FindVar("sv_gravity");
        float flGravity = sv_gravity ? sv_gravity->GetFloat() / 800.f : 1.f;
        
        switch (pProjectile->GetClassId())
        {
        case ETFClassIds::CBaseGrenade:
        case ETFClassIds::CTFWeaponBaseGrenadeProj:
        case ETFClassIds::CTFWeaponBaseMerasmusGrenade:
        case ETFClassIds::CTFGrenadePipebombProjectile:
        case ETFClassIds::CTFStunBall:
        case ETFClassIds::CTFBall_Ornament:
        case ETFClassIds::CTFProjectile_Jar:
        case ETFClassIds::CTFProjectile_Cleaver:
        case ETFClassIds::CTFProjectile_JarGas:
        case ETFClassIds::CTFProjectile_JarMilk:
        case ETFClassIds::CTFProjectile_SpellBats:
        case ETFClassIds::CTFProjectile_SpellKartBats:
        case ETFClassIds::CTFProjectile_SpellMeteorShower:
        case ETFClassIds::CTFProjectile_SpellMirv:
        case ETFClassIds::CTFProjectile_SpellPumpkin:
        case ETFClassIds::CTFProjectile_SpellSpawnBoss:
        case ETFClassIds::CTFProjectile_SpellSpawnHorde:
        case ETFClassIds::CTFProjectile_SpellSpawnZombie:
        case ETFClassIds::CTFProjectile_SpellTransposeTeleport:
        case ETFClassIds::CTFProjectile_Throwable:
        case ETFClassIds::CTFProjectile_ThrowableBreadMonster:
        case ETFClassIds::CTFProjectile_ThrowableBrick:
        case ETFClassIds::CTFProjectile_ThrowableRepel:
        case ETFClassIds::CTFProjectile_SpellFireball:
            flReturn = 1.f;
            break;
        case ETFClassIds::CTFProjectile_HealingBolt:
            flReturn = 0.2f * flGravity;
            break;
        case ETFClassIds::CTFProjectile_Flare:
            flReturn = (pWeapon && pWeapon->As<CTFFlareGun>()->GetFlareGunType() == FLAREGUN_GRORDBORT ? 0.45f : 0.3f) * flGravity;
            break;
        case ETFClassIds::CTFProjectile_Arrow:
            flReturn = CanHeadshot(pProjectile->As<C_TFProjectile_Arrow>())
                ? Math::RemapVal(pProjectile->As<C_TFProjectile_Arrow>()->m_vInitialVelocity().Length(), 1800.f, 2600.f, 0.5f, 0.1f) * flGravity
                : 0.2f * flGravity;
            break;
        }
        return flReturn;
    }

    IPhysicsEnvironment* m_pEnv = nullptr;
    IPhysicsObject* m_pObj = nullptr;
};

MAKE_SINGLETON_SCOPED(CAmalgamProjectileSimulation, AmalgamProjSim, F);

// Alias for compatibility with Amalgam code
namespace F {
    inline CAmalgamProjectileSimulation& ProjSim = *F::AmalgamProjSim;
}
