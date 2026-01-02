#pragma once

// ============================================
// Amalgam to SEOwnedDE Compatibility Layer
// ============================================
// This header provides type aliases, macros, and helper wrappers
// to allow Amalgam's code to work with SEOwnedDE's infrastructure
// WITHOUT modifying the original Amalgam code.

#include "../../../SDK/SDK.h"
#include "../CFG.h"
#include "../LagRecords/LagRecords.h"

// ============================================
// Class ID Compatibility
// ============================================
// Amalgam uses ETFClassID, SEOwnedDE uses ETFClassIds
// Create an alias so Amalgam code works unchanged

using ETFClassID = ETFClassIds;

// ============================================
// Trace Filter Compatibility
// ============================================
// SEOwnedDE has a simple CTraceFilterWorldAndPropsOnly in IEngineTrace.h
// We use CTraceFilterWorldAndPropsOnlyAmalgam for the full Amalgam implementation
// This alias allows Amalgam code to use the original name in the amalgam_port folder

// Forward declare the Amalgam-style filter (defined in SDK/Impl/TraceFilters/TraceFilters.h)
class CTraceFilterWorldAndPropsOnlyAmalgam;

// ============================================
// Coordinate System Constants
// ============================================

#ifndef MAX_COORD_INTEGER
#define MAX_COORD_INTEGER (16384)
#endif

#ifndef COORD_EXTENT
#define COORD_EXTENT (2 * MAX_COORD_INTEGER)
#endif

#ifndef MAX_TRACE_LENGTH
#define MAX_TRACE_LENGTH (1.732050807569f * COORD_EXTENT)
#endif

#ifndef CONTENTS_NOSTARTSOLID
#define CONTENTS_NOSTARTSOLID 0x100
#endif

// TF2 class constants (if not defined)
#ifndef TF_CLASS_HEAVY
#define TF_CLASS_HEAVY 6
#endif

// TF2 condition constants (if not defined)
#ifndef TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED
#define TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGE
#endif

// ============================================
// FNV1A Hash (Amalgam uses this for model identification)
// ============================================

namespace FNV1A
{
    constexpr uint32_t FNV_PRIME_32 = 0x01000193;
    constexpr uint32_t FNV_OFFSET_32 = 0x811c9dc5;

    constexpr uint32_t Hash32Const(const char* str, uint32_t hash = FNV_OFFSET_32)
    {
        return (*str == '\0') ? hash : Hash32Const(str + 1, (hash ^ static_cast<uint32_t>(*str)) * FNV_PRIME_32);
    }

    inline uint32_t Hash32(const char* str)
    {
        uint32_t hash = FNV_OFFSET_32;
        while (*str)
        {
            hash ^= static_cast<uint32_t>(*str++);
            hash *= FNV_PRIME_32;
        }
        return hash;
    }
}

// ============================================
// Type Aliases (Amalgam -> SEOwnedDE)
// ============================================

// Type aliases - Amalgam uses non-prefixed names, SEOwnedDE uses C_ prefix
// Note: CBaseEntity is forward declared in shareddefs.h, so we can't alias it
using CTFPlayer = C_TFPlayer;
using CTFWeaponBase = C_TFWeaponBase;
// CBaseEntity alias removed - conflicts with forward declaration in SDK
using CBaseAnimating = C_BaseAnimating;
using CBaseCombatWeapon = C_BaseCombatWeapon;
using CBaseObject = C_BaseObject;
using CBaseCombatCharacter = C_BaseCombatCharacter;

// ============================================
// Helper Functions for Entity State Checks
// (Amalgam uses member functions, SEOwnedDE uses different patterns)
// ============================================

// Check if entity is a player
inline bool IsPlayer(C_BaseEntity* pEntity)
{
    if (!pEntity)
        return false;
    return pEntity->GetClassId() == ETFClassID::CTFPlayer;
}

// Check if entity is world
inline bool IsWorld(C_BaseEntity* pEntity)
{
    if (!pEntity)
        return false;
    return pEntity->GetClassId() == ETFClassID::CWorld;
}

// Check if player is alive
inline bool IsAlive(C_TFPlayer* pPlayer)
{
    if (!pPlayer)
        return false;
    return pPlayer->m_lifeState() == LIFE_ALIVE;
}

// Check if player is a ghost (Halloween mode)
inline bool IsAGhost(C_TFPlayer* pPlayer)
{
    if (!pPlayer)
        return false;
    return pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE);
}

// Check if player is on ground
inline bool IsOnGround(C_TFPlayer* pPlayer)
{
    if (!pPlayer)
        return false;
    return (pPlayer->m_fFlags() & FL_ONGROUND) != 0;
}

// Check if player is swimming
inline bool IsSwimming(C_TFPlayer* pPlayer)
{
    if (!pPlayer)
        return false;
    return pPlayer->m_nWaterLevel() >= 2;
}

// Check if player is ducking
inline bool IsDucking(C_TFPlayer* pPlayer)
{
    if (!pPlayer)
        return false;
    return pPlayer->m_bDucked() || (pPlayer->m_fFlags() & FL_DUCKING) != 0;
}

// Get solid mask for player
inline unsigned int SolidMask(C_TFPlayer* pPlayer)
{
    return MASK_PLAYERSOLID;
}

// Building type enum - already defined in SDK/TF2/tf_shareddefs.h
// OBJ_DISPENSER, OBJ_TELEPORTER, OBJ_SENTRYGUN, OBJ_ATTACHMENT_SAPPER

// Building type checks
inline bool IsSentrygun(C_BaseEntity* pEntity)
{
    if (!pEntity)
        return false;
    auto pObj = pEntity->As<C_BaseObject>();
    return pObj && pObj->m_iObjectType() == OBJ_SENTRYGUN;
}

inline bool IsDispenser(C_BaseEntity* pEntity)
{
    if (!pEntity)
        return false;
    auto pObj = pEntity->As<C_BaseObject>();
    return pObj && pObj->m_iObjectType() == OBJ_DISPENSER;
}

inline bool IsTeleporter(C_BaseEntity* pEntity)
{
    if (!pEntity)
        return false;
    auto pObj = pEntity->As<C_BaseObject>();
    return pObj && pObj->m_iObjectType() == OBJ_TELEPORTER;
}



// ============================================
// Math Helper Functions
// ============================================

// Sign function - returns -1, 0, or 1
template<typename T>
inline int sign(T val)
{
    return (T(0) < val) - (val < T(0));
}

// Flare gun types
enum EFlareGunType
{
    FLAREGUN_NORMAL = 0,
    FLAREGUN_DETONATE,
    FLAREGUN_GRORDBORT,
    FLAREGUN_SCORCHSHOT
};

namespace Math
{
    // RemapVal - maps a value from one range to another
    inline float RemapVal(float val, float A, float B, float C, float D, bool bClamp = true)
    {
        if (A == B)
            return val >= B ? D : C;
        float t = (val - A) / (B - A);
        if (bClamp)
            t = std::clamp(t, 0.f, 1.f);
        return C + (D - C) * t;
    }
    
    // VectorAngles - single argument version that returns Vec3
    inline Vec3 VectorAngles(const Vec3& forward)
    {
        Vec3 angles;
        ::Math::VectorAngles(forward, angles);
        return angles;
    }
    
    // RotatePoint - Amalgam's exact implementation
    // Rotates a point around an origin by the given angles (pitch, yaw, roll)
    inline Vec3 RotatePoint(Vec3 vPoint, Vec3 vOrigin, Vec3 vAngles)
    {
        vPoint -= vOrigin;

        float sp, sy, sr, cp, cy, cr;
        SinCos(DEG2RAD(vAngles.x), &sp, &cp);
        SinCos(DEG2RAD(vAngles.y), &sy, &cy);
        SinCos(DEG2RAD(vAngles.z), &sr, &cr);

        Vec3 vX = {
            cy * cp,
            cy * sp * sr - sy * cr,
            cy * sp * cr + sy * sr
        };
        Vec3 vY = {
            sy * cp,
            sy * sp * sr + cy * cr,
            sy * sp * cr - cy * sr
        };
        Vec3 vZ = {
            -sp,
            cp * sr,
            cp * cr
        };

        return Vec3(vX.Dot(vPoint), vY.Dot(vPoint), vZ.Dot(vPoint)) + vOrigin;
    }
    
    // SolveQuadratic - solves ax^2 + bx + c = 0
    inline std::vector<float> SolveQuadratic(float a, float b, float c)
    {
        float flRoot = powf(b, 2.f) - 4 * a * c;
        if (flRoot < 0)
            return {};

        a *= 2;
        b = -b;
        return { (b + sqrtf(flRoot)) / a, (b - sqrtf(flRoot)) / a };
    }
    
    // SolveCubic - solves x^3 + bx^2 + cx + d = 0
    inline float SolveCubic(float b, float c, float d)
    {
        float p = c - powf(b, 2) / 3;
        float q = 2 * powf(b, 3) / 27 - b * c / 3 + d;

        if (p == 0.f)
            return powf(q, 1.f / 3);
        if (q == 0.f)
            return 0.f;

        float t = sqrtf(fabsf(p) / 3);
        float g = q / (2.f / 3) / (p * t);
        if (p > 0.f)
            return -2 * t * sinhf(asinhf(g) / 3) - b / 3;

        if (4 * powf(p, 3) + 27 * powf(q, 2) < 0.f)
            return 2 * t * cosf(acosf(g) / 3) - b / 3;
        if (q > 0.f)
            return -2 * t * coshf(acoshf(-g) / 3) - b / 3;
        return 2 * t * coshf(acoshf(g) / 3) - b / 3;
    }
    
    // SolveQuartic - solves ax^4 + bx^3 + cx^2 + dx + e = 0
    inline std::vector<float> SolveQuartic(float a, float b, float c, float d, float e)
    {
        std::vector<float> vRoots = {};

        b /= a, c /= a, d /= a, e /= a;
        float p = c - powf(b, 2) / (8.f / 3);
        float q = powf(b, 3) / 8 - b * c / 2 + d;
        float m = SolveCubic(
            p,
            powf(p, 2) / 4 + powf(b, 4) / (256.f / 3) - e + b * d / 4 - powf(b, 2) * c / 16,
            -powf(q, 2) / 8
        );
        if (m < 0.f)
            return vRoots;

        float sqrt_2m = sqrtf(2 * m);
        if (q == 0.f)
        {
            if (-m - p > 0.f)
            {
                float flDelta = sqrtf(2 * (-m - p));
                vRoots.push_back(-b / 4 + (sqrt_2m - flDelta) / 2);
                vRoots.push_back(-b / 4 - (sqrt_2m - flDelta) / 2);
                vRoots.push_back(-b / 4 + (sqrt_2m + flDelta) / 2);
                vRoots.push_back(-b / 4 - (sqrt_2m + flDelta) / 2);
            }
            if (-m - p == 0.f)
            {
                vRoots.push_back(-b / 4 - sqrt_2m / 2);
                vRoots.push_back(-b / 4 + sqrt_2m / 2);
            }
        }
        else
        {
            if (-m - p + q / sqrt_2m >= 0.f)
            {
                float flDelta = sqrtf(2 * (-m - p + q / sqrt_2m));
                vRoots.push_back((-sqrt_2m + flDelta) / 2 - b / 4);
                vRoots.push_back((-sqrt_2m - flDelta) / 2 - b / 4);
            }
            if (-m - p - q / sqrt_2m >= 0.f)
            {
                float flDelta = sqrtf(2 * (-m - p - q / sqrt_2m));
                vRoots.push_back((sqrt_2m + flDelta) / 2 - b / 4);
                vRoots.push_back((sqrt_2m - flDelta) / 2 - b / 4);
            }
        }
        return vRoots;
    }
}

// ============================================
// CCollisionProperty - Collision property helper
// ============================================

MAKE_SIGNATURE(CCollisionProperty_CalcNearestPoint, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 49 8B D8 48 8B F9 4C 8D 44 24", 0x0);

class CCollisionProperty
{
public:
    void CalcNearestPoint(const Vec3& vecWorldPt, Vec3* pVecNearestWorldPt)
    {
        reinterpret_cast<void(__thiscall*)(void*, const Vec3&, Vec3*)>(Signatures::CCollisionProperty_CalcNearestPoint.Get())(this, vecWorldPt, pVecNearestWorldPt);
    }
};

// ============================================
// Datamap types (typedescription_t, datamap_t, etc.) are already defined in SDK/TF2/datamap.h

// ============================================
// GetPredDescMap - Virtual function index 15
// ============================================

inline datamap_t* GetPredDescMap(C_BaseEntity* pEntity)
{
    if (!pEntity)
        return nullptr;
    
    // Virtual function index 15 in CBaseEntity
    return reinterpret_cast<datamap_t*(__thiscall*)(void*)>(Memory::GetVFunc(pEntity, 15))(pEntity);
}

// ============================================
// GetIntermediateDataSize
// ============================================

inline size_t GetIntermediateDataSize(C_BaseEntity* pEntity)
{
    auto pMap = GetPredDescMap(pEntity);
    if (!pMap)
        return 4096; // Fallback size
    return std::max(pMap->packed_size, 4);
}

// Projectile types
using CTFGrenadePipebombProjectile = C_TFGrenadePipebombProjectile;
using CTFProjectile_Arrow = C_TFProjectile_Arrow;
using CTFProjectile_Rocket = C_TFProjectile_Rocket;
using CTFBaseRocket = C_TFBaseRocket;
using CTFWeaponBaseGrenadeProj = C_TFWeaponBaseGrenadeProj;
// CTFBaseProjectile - C_TFBaseProjectile doesn't exist in SEOwnedDE, use C_BaseProjectile
using CTFBaseProjectile = C_BaseProjectile;

// ============================================
// Stub Classes for Missing SDK Types
// ============================================

// C_TFFlareGun stub - SEOwnedDE doesn't have this class
class C_TFFlareGun : public C_TFWeaponBase
{
public:
    int GetFlareGunType()
    {
        return static_cast<int>(SDKUtils::AttribHookValue(0.f, "set_weapon_mode", this));
    }
};

// Arrow CanHeadshot helper
inline bool CanHeadshot(C_TFProjectile_Arrow* pArrow)
{
    if (!pArrow)
        return false;
    // Arrows can headshot unless they're repair bolts
    return pArrow->m_iProjectileType() != TF_PROJECTILE_BUILDING_REPAIR_BOLT;
}

// GetHitboxInfo wrapper - Amalgam signature: (bones, hitbox, center, mins, maxs)
// SEOwnedDE signature: (hitbox, center, mins, maxs, matrix)
inline void GetHitboxInfoFromBones(C_BaseAnimating* pEntity, matrix3x4_t* pBones, int nHitbox, Vec3* pCenter, Vec3* pMins, Vec3* pMaxs)
{
    if (!pEntity || !pBones)
        return;
    
    auto pModel = pEntity->GetModel();
    if (!pModel)
        return;
    
    auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
    if (!pHDR)
        return;
    
    auto pSet = pHDR->pHitboxSet(pEntity->m_nHitboxSet());
    if (!pSet)
        return;
    
    auto pBox = pSet->pHitbox(nHitbox);
    if (!pBox)
        return;
    
    if (pMins)
        *pMins = pBox->bbmin;
    if (pMaxs)
        *pMaxs = pBox->bbmax;
    if (pCenter)
        Math::VectorTransform((pBox->bbmin + pBox->bbmax) * 0.5f, pBones[pBox->bone], *pCenter);
}

// Weapon types
using CTFFlareGun = C_TFFlareGun;
using CTFPipebombLauncher = C_TFPipebombLauncher;
using CTFGrenadeLauncher = C_TFGrenadeLauncher;
using CTFSniperRifle = C_TFSniperRifle;

// ============================================
// Amalgam's Enum Macro
// ============================================

#ifndef Enum
#define Enum(name, ...) \
    namespace name##Enum { enum name##Enum { __VA_ARGS__ }; }
#endif

// ============================================
// Amalgam's ADD_FEATURE Macro -> SEOwnedDE's MAKE_SINGLETON_SCOPED
// ============================================

#ifndef ADD_FEATURE
#define ADD_FEATURE(ClassName, InstanceName) \
    MAKE_SINGLETON_SCOPED(ClassName, InstanceName, F)
#endif

// ============================================
// Time/Tick Macros
// ============================================

#ifndef TICK_INTERVAL
#define TICK_INTERVAL (I::GlobalVars->interval_per_tick)
#endif

#ifndef TIME_TO_TICKS
#define TIME_TO_TICKS(t) (static_cast<int>(0.5f + static_cast<float>(t) / TICK_INTERVAL))
#endif

#ifndef TICKS_TO_TIME
#define TICKS_TO_TIME(t) (TICK_INTERVAL * static_cast<float>(t))
#endif

// ============================================
// Physics Constants
// ============================================

// k_flMaxVelocity and k_flMaxAngularVelocity are already defined in SDK/TF2/vphysics.h
constexpr float TF_ARROW_MAX_CHARGE_TIME = 1.0f;

// ============================================
// Entity Group Enum (Amalgam style)
// ============================================

enum class EntityEnum
{
    Invalid = -1,
    PlayerAll,
    PlayerEnemy,
    PlayerTeam,
    BuildingAll,
    BuildingEnemy,
    BuildingTeam,
    WorldProjectile,
    WorldNPC
};

// ============================================
// Amalgam's H::Entities Wrapper
// ============================================

class CAmalgamEntitiesHelper
{
private:
    // Per-entity tracking data - limited to max 64 players
    std::unordered_map<int, float> m_mLastSimTime;
    std::unordered_map<int, float> m_mDeltaTime;
    std::unordered_map<int, int> m_mChoke;
    std::unordered_map<int, Vec3> m_mAvgVelocity;
    std::unordered_map<int, bool> m_mLagCompensation;

public:
    C_TFPlayer* GetLocal() { return H::Entities ? H::Entities->GetLocal() : nullptr; }
    C_TFWeaponBase* GetWeapon() { return H::Entities ? H::Entities->GetWeapon() : nullptr; }
    
    // Clear all tracking data (call on map change/disconnect)
    void Clear()
    {
        m_mLastSimTime.clear();
        m_mDeltaTime.clear();
        m_mChoke.clear();
        m_mAvgVelocity.clear();
        m_mLagCompensation.clear();
    }
    
    // Entity group iteration - maps Amalgam's EntityEnum to SEOwnedDE's EEntGroup
    const std::vector<C_BaseEntity*>& GetGroup(EntityEnum eGroup)
    {
        static std::vector<C_BaseEntity*> empty;
        
        // Safety check
        if (!H::Entities)
            return empty;
        
        switch (eGroup)
        {
        case EntityEnum::PlayerAll:
            return H::Entities->GetGroup(EEntGroup::PLAYERS_ALL);
        case EntityEnum::PlayerEnemy:
            return H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES);
        case EntityEnum::PlayerTeam:
            return H::Entities->GetGroup(EEntGroup::PLAYERS_TEAMMATES);
        case EntityEnum::BuildingAll:
            return H::Entities->GetGroup(EEntGroup::BUILDINGS_ALL);
        case EntityEnum::BuildingEnemy:
            return H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES);
        case EntityEnum::BuildingTeam:
            return H::Entities->GetGroup(EEntGroup::BUILDINGS_TEAMMATES);
        case EntityEnum::WorldProjectile:
            return H::Entities->GetGroup(EEntGroup::PROJECTILES_ALL);
        case EntityEnum::WorldNPC:
            return empty; // NPCs not tracked in SEOwnedDE
        default:
            return empty;
        }
    }
    
    // Friend/party checks
    bool IsFriend(int iIndex)
    {
        auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
        if (pEntity)
        {
            auto pBaseEntity = pEntity->As<C_BaseEntity>();
            if (pBaseEntity && IsPlayer(pBaseEntity))
                return static_cast<C_TFPlayer*>(pBaseEntity)->IsPlayerOnSteamFriendsList();
        }
        return false;
    }
    
    bool InParty(int iIndex)
    {
        // Check TF2 party system
        return false; // TODO: Implement party check via CTFPartyClient
    }
    
    // Choke tracking - tracks how many ticks since last update
    // Calculate from delta time like Amalgam does
    int GetChoke(int iIndex)
    {
        float flDeltaTime = GetDeltaTime(iIndex);
        if (flDeltaTime > 0.f)
            return std::max(TIME_TO_TICKS(flDeltaTime) - 1, 0);
        return 0;
    }
    
    void SetChoke(int iIndex, int iChoke)
    {
        m_mChoke[iIndex] = iChoke;
    }
    
    // Delta time tracking - time between simulation updates
    // Calculate directly from player's simulation times (like Amalgam does)
    float GetDeltaTime(int iIndex)
    {
        // Safety checks
        if (!I::ClientEntityList)
            return TICK_INTERVAL;
        
        auto pClientEntity = I::ClientEntityList->GetClientEntity(iIndex);
        if (!pClientEntity)
            return TICK_INTERVAL;
        
        auto pPlayer = pClientEntity->As<C_TFPlayer>();
        if (!pPlayer || !IsPlayer(pPlayer))
            return TICK_INTERVAL;
        
        float flSimTime = pPlayer->m_flSimulationTime();
        float flOldSimTime = pPlayer->m_flOldSimulationTime();
        
        // Calculate delta ticks - 0 means no update this frame (standing still)
        int iDeltaTicks = std::clamp(TIME_TO_TICKS(flSimTime - flOldSimTime), 0, 24);
        
        return TICKS_TO_TIME(iDeltaTicks);
    }
    
    void UpdateDeltaTime(int iIndex, float flSimTime)
    {
        if (m_mLastSimTime.contains(iIndex))
        {
            float flDelta = flSimTime - m_mLastSimTime[iIndex];
            if (flDelta > 0.f)
            {
                m_mDeltaTime[iIndex] = flDelta;
                m_mChoke[iIndex] = TIME_TO_TICKS(flDelta) - 1;
            }
            else
            {
                m_mDeltaTime[iIndex] = 0.f;
            }
        }
        m_mLastSimTime[iIndex] = flSimTime;
    }
    
    // Average velocity tracking
    // Returns nullptr if no average is stored - MoveSim will use raw velocity
    Vec3* GetAvgVelocity(int iIndex)
    {
        if (!I::EngineClient || iIndex == I::EngineClient->GetLocalPlayer())
            return nullptr; // Don't use average for local player
        
        // Only return if we have a stored average
        if (m_mAvgVelocity.contains(iIndex))
            return &m_mAvgVelocity[iIndex];
        
        return nullptr;
    }
    
    void SetAvgVelocity(int iIndex, const Vec3& vVel)
    {
        m_mAvgVelocity[iIndex] = vVel;
    }
    
    // Lag compensation state
    bool GetLagCompensation(int iIndex)
    {
        return m_mLagCompensation.contains(iIndex) ? m_mLagCompensation[iIndex] : false;
    }
    
    void SetLagCompensation(int iIndex, bool bState)
    {
        m_mLagCompensation[iIndex] = bState;
    }
    
    // Eye angles tracking
    Vec3 GetEyeAngles(int iIndex)
    {
        auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
        if (pEntity)
        {
            auto pBaseEntity = pEntity->As<C_BaseEntity>();
            if (pBaseEntity && IsPlayer(pBaseEntity))
                return static_cast<C_TFPlayer*>(pBaseEntity)->GetEyeAngles();
        }
        return {};
    }
    
    // Clear tracking data for disconnected players
    void ClearPlayer(int iIndex)
    {
        m_mLastSimTime.erase(iIndex);
        m_mDeltaTime.erase(iIndex);
        m_mChoke.erase(iIndex);
        m_mAvgVelocity.erase(iIndex);
        m_mLagCompensation.erase(iIndex);
    }
};

// Global instance for Amalgam-style entity helper
// This extends SEOwnedDE's H::Entities with additional tracking data
inline CAmalgamEntitiesHelper g_AmalgamEntitiesExt;

// ============================================
// Amalgam's I:: Interface Wrappers
// ============================================

// Forward declarations for missing interfaces
class IMemAlloc;

// Memory allocator - get from tier0.dll
inline IMemAlloc* GetMemAlloc()
{
    static IMemAlloc* pMemAlloc = nullptr;
    if (!pMemAlloc)
    {
        pMemAlloc = *reinterpret_cast<IMemAlloc**>(GetProcAddress(GetModuleHandleA("tier0.dll"), "g_pMemAlloc"));
    }
    return pMemAlloc;
}

// IMemAlloc interface definition
class IMemAlloc
{
public:
    virtual void* Alloc(size_t nSize) = 0;
    virtual void* Realloc(void* pMem, size_t nSize) = 0;
    virtual void Free(void* pMem) = 0;
};

// CViewVectors is already defined in SDK/TF2/shareddefs.h

// ============================================
// CTFGameRules - TF2 Game Rules
// ============================================

// Signature to get TFGameRules pointer
MAKE_SIGNATURE(TFGameRules, "client.dll", "48 8B 0D ? ? ? ? 4C 8B C3 48 8B D7 48 8B 01 FF 90 ? ? ? ? 84 C0", 0x0);

class CTFGameRules
{
public:
    // GetViewVectors - virtual function index 31
    CViewVectors* GetViewVectors()
    {
        return reinterpret_cast<CViewVectors*(__thiscall*)(void*)>(Memory::GetVFunc(this, 31))(this);
    }
};

namespace I {
    // Memory allocator interface
    inline IMemAlloc* MemAlloc = GetMemAlloc();
    
    // Note: I::MoveHelper is already defined via MAKE_INTERFACE_SIGNATURE in imovehelper.h
    // Note: I::Physics and I::PhysicsCollision are already defined via MAKE_INTERFACE_VERSION in vphysics.h
    
    // TF Game Rules - access via signature
    inline CTFGameRules* TFGameRules()
    {
        static auto addr = Signatures::TFGameRules.Get();
        if (!addr)
            return nullptr;
        return *reinterpret_cast<CTFGameRules**>(Memory::RelToAbs(addr));
    }
}

// IMoveHelper is already defined in SDK/TF2/imovehelper.h
// I::MoveHelper is available via MAKE_INTERFACE_SIGNATURE

// ============================================
// CPredictionCopy - for saving/restoring entity state
// ============================================

// Signature for CPredictionCopy::TransferData
MAKE_SIGNATURE(CPredictionCopy_TransferData, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 8B 3D", 0x0);

#define PC_DATA_PACKED true
#define PC_DATA_NORMAL false

enum
{
    PC_EVERYTHING = 0,
    PC_NON_NETWORKED_ONLY,
    PC_NETWORKED_ONLY,
};

typedef void (*FN_FIELD_COMPARE)(const char* classname, const char* fieldname, const char* fieldtype,
    bool networked, bool noterrorchecked, bool differs, bool withintolerance, const char* value);

class CPredictionCopy
{
public:
    typedef enum
    {
        DIFFERS = 0,
        IDENTICAL,
        WITHINTOLERANCE,
    } difftype_t;

    CPredictionCopy(int type, void* dest, bool dest_packed, void const* src, bool src_packed,
        bool counterrors = false, bool reporterrors = false, bool performcopy = true,
        bool describefields = false, FN_FIELD_COMPARE func = nullptr)
    {
        m_nType = type;
        m_pDest = dest;
        m_pSrc = src;
        m_nDestOffsetIndex = dest_packed ? TD_OFFSET_PACKED : TD_OFFSET_NORMAL;
        m_nSrcOffsetIndex = src_packed ? TD_OFFSET_PACKED : TD_OFFSET_NORMAL;
        m_bErrorCheck = counterrors;
        m_bReportErrors = reporterrors;
        m_bPerformCopy = performcopy;
        m_bDescribeFields = describefields;

        m_pCurrentField = nullptr;
        m_pCurrentMap = nullptr;
        m_pCurrentClassName = nullptr;
        m_bShouldReport = false;
        m_bShouldDescribe = false;
        m_nErrorCount = 0;

        m_FieldCompareFunc = func;
    }

    int TransferData(const char* operation, int entindex, datamap_t* dmap)
    {
        using TransferDataFn = int(__thiscall*)(void*, const char*, int, datamap_t*);
        static auto fn = reinterpret_cast<TransferDataFn>(Signatures::CPredictionCopy_TransferData.Get());
        return fn(this, operation, entindex, dmap);
    }

public:
    int m_nType;
    void* m_pDest;
    void const* m_pSrc;
    int m_nDestOffsetIndex;
    int m_nSrcOffsetIndex;

    bool m_bErrorCheck;
    bool m_bReportErrors;
    bool m_bDescribeFields;
    typedescription_t* m_pCurrentField;
    char const* m_pCurrentClassName;
    datamap_t* m_pCurrentMap;
    bool m_bShouldReport;
    bool m_bShouldDescribe;
    int m_nErrorCount;
    bool m_bPerformCopy;

    FN_FIELD_COMPARE m_FieldCompareFunc;

    typedescription_t* m_pWatchField;
    char const* m_pOperation;
};

// ============================================
// Amalgam's U:: Utility Wrappers
// ============================================

namespace U {
    class CConVarsHelper
    {
    public:
        ConVar* FindVar(const char* name) { return I::CVar->FindVar(name); }
    };
    
    inline CConVarsHelper ConVars;
    
    class CHooksHelper
    {
    public:
        std::unordered_map<std::string, void*> m_mHooks;
    };
    
    inline CHooksHelper Hooks;
}

// ============================================
// Amalgam's SDK:: Namespace Functions
// ============================================

// Valve random max constant
#define VALVE_RAND_MAX 0x7FFF

namespace SDK
{
    // Random functions
    inline void RandomSeed(unsigned int seed)
    {
        SDKUtils::RandomSeed(seed);
    }
    
    inline float RandomFloat(float min = 0.f, float max = 1.f)
    {
        return SDKUtils::RandomFloat(min, max);
    }
    
    inline int RandomInt(int min = 0, int max = VALVE_RAND_MAX)
    {
        return SDKUtils::RandomInt(min, max);
    }
    
    inline float StdRandomFloat(float min, float max)
    {
        // Standard library random for non-deterministic randomness
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(min, max);
        return dis(gen);
    }
    
    inline int StdRandomInt(int min, int max)
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(min, max);
        return dis(gen);
    }
    
    // Trace wrappers
    inline void Trace(const Vec3& start, const Vec3& end, int mask, ITraceFilter* filter, CGameTrace* trace)
    {
        Ray_t ray;
        ray.Init(start, end);
        I::EngineTrace->TraceRay(ray, mask, filter, trace);
    }
    
    inline void TraceHull(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs, int mask, ITraceFilter* filter, CGameTrace* trace)
    {
        Ray_t ray;
        ray.Init(start, end, mins, maxs);
        I::EngineTrace->TraceRay(ray, mask, filter, trace);
    }
    
    // Visibility checks
    inline bool VisPosWorld(C_BaseEntity* pSkip, C_BaseEntity* pTarget, const Vec3& from, const Vec3& to, int mask)
    {
        CGameTrace trace;
        CTraceFilterWorldOnly filter;
        
        Ray_t ray;
        ray.Init(from, to);
        I::EngineTrace->TraceRay(ray, mask, &filter, &trace);
        
        return trace.fraction >= 1.0f || trace.m_pEnt == pTarget;
    }
    
    inline bool VisPos(C_BaseEntity* pSkip, C_BaseEntity* pTarget, const Vec3& from, const Vec3& to, int mask = MASK_SHOT | CONTENTS_GRATE)
    {
        CGameTrace trace;
        CTraceFilterHitscan filter;
        filter.m_pIgnore = pSkip;
        
        Ray_t ray;
        ray.Init(from, to);
        I::EngineTrace->TraceRay(ray, mask, &filter, &trace);
        
        return trace.fraction >= 1.0f || trace.m_pEnt == pTarget;
    }
    
    inline bool VisPosCollideable(C_BaseEntity* pSkip, C_BaseEntity* pTarget, const Vec3& from, const Vec3& to, int mask = MASK_SHOT | CONTENTS_GRATE)
    {
        CGameTrace trace;
        CTraceFilterHitscan filter;
        filter.m_pIgnore = pSkip;
        
        Ray_t ray;
        ray.Init(from, to);
        I::EngineTrace->TraceRay(ray, mask, &filter, &trace);
        
        return trace.fraction >= 1.0f || trace.m_pEnt == pTarget;
    }
    
    // Attack check - matches Amalgam's SDK::IsAttacking
    inline int IsAttacking(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd, bool bTickBase = false)
    {
        if (!pLocal || !pWeapon || pCmd->weaponselect)
            return 0;
        
        float flTickBase = bTickBase ? I::GlobalVars->curtime : TICKS_TO_TIME(pLocal->m_nTickBase());
        
        switch (pWeapon->GetWeaponID())
        {
        case TF_WEAPON_COMPOUND_BOW:
            // Huntsman fires when you RELEASE attack while charging
            return !(pCmd->buttons & IN_ATTACK) && pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f;
            
        case TF_WEAPON_PIPEBOMBLAUNCHER:
        {
            // Sticky launcher fires when you release attack while charging, or when fully charged
            float flCharge = pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f 
                ? flTickBase - pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() : 0.f;
            const float flAmount = Math::RemapVal(flCharge, 0.f, SDKUtils::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon), 0.f, 1.f);
            return (!(pCmd->buttons & IN_ATTACK) && flAmount > 0.f) || flAmount >= 1.f;
        }
        
        case TF_WEAPON_CANNON:
        {
            // Loose Cannon - check for mortar mode
            float flMortar = SDKUtils::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
            if (!flMortar)
                return G::bCanPrimaryAttack && (pCmd->buttons & IN_ATTACK) ? 1 : (G::bReloading && (pCmd->buttons & IN_ATTACK) ? 2 : 0);
            
            float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f 
                ? flMortar - (pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() - flTickBase) : 0.f;
            const float flAmount = Math::RemapVal(flCharge, 0.f, flMortar, 0.f, 1.f);
            return (!(pCmd->buttons & IN_ATTACK) && flAmount > 0.f) || flAmount >= 1.f;
        }
        
        case TF_WEAPON_MINIGUN:
        {
            // Minigun: only attacking when in FIRING or SPINNING state with ammo
            // This is EXACTLY how Amalgam does it
            switch (pWeapon->As<C_TFMinigun>()->m_iWeaponState())
            {
            case AC_STATE_FIRING:
            case AC_STATE_SPINNING:
                if (pWeapon->HasPrimaryAmmoForShot())
                    return G::bCanPrimaryAttack && (pCmd->buttons & IN_ATTACK) ? 1 : (G::bReloading && (pCmd->buttons & IN_ATTACK) ? 2 : 0);
            }
            return 0;
        }
        
        default:
            // Regular weapons - fire when pressing attack and can attack
            // Return 2 if reloading (queued attack)
            if (pCmd->buttons & IN_ATTACK)
                return G::bCanPrimaryAttack ? 1 : (G::bReloading ? 2 : 0);
            return 0;
        }
    }
    
    // Movement fix
    inline void FixMovement(CUserCmd* pCmd, const Vec3& vTargetAngles)
    {
        Vec3 vMove = { pCmd->forwardmove, pCmd->sidemove, pCmd->upmove };
        Vec3 vMoveAng = {};
        Math::VectorAngles(vMove, vMoveAng);
        
        float flSpeed = vMove.Length();
        float flYaw = DEG2RAD(vTargetAngles.y - pCmd->viewangles.y + vMoveAng.y);
        
        pCmd->forwardmove = cosf(flYaw) * flSpeed;
        pCmd->sidemove = sinf(flYaw) * flSpeed;
    }
    
    inline void FixMovement(CUserCmd* pCmd, const Vec3& vOldAngles, const Vec3& vNewAngles)
    {
        Vec3 vMove = { pCmd->forwardmove, pCmd->sidemove, pCmd->upmove };
        Vec3 vMoveAng = {};
        Math::VectorAngles(vMove, vMoveAng);
        
        float flSpeed = vMove.Length();
        float flYaw = DEG2RAD(vNewAngles.y - vOldAngles.y + vMoveAng.y);
        
        pCmd->forwardmove = cosf(flYaw) * flSpeed;
        pCmd->sidemove = sinf(flYaw) * flSpeed;
    }
    
    // Attribute hook - uses SEOwnedDE's implementation
    inline float AttribHookValue(float flBase, const char* szAttr, C_TFWeaponBase* pWeapon)
    {
        return SDKUtils::AttribHookValue(flBase, szAttr, pWeapon);
    }
    
    inline int AttribHookValue(int iBase, const char* szAttr, C_TFWeaponBase* pWeapon)
    {
        return static_cast<int>(SDKUtils::AttribHookValue(static_cast<float>(iBase), szAttr, pWeapon));
    }
    
    // Max speed calculation
    inline float MaxSpeed(C_TFPlayer* pPlayer, bool bIgnoreCrouch = false, bool bIgnoreSpecialAbility = false)
    {
        if (!pPlayer)
            return 0.f;
        return pPlayer->TeamFortress_CalculateMaxSpeed(bIgnoreSpecialAbility);
    }
    
    // Projectile fire setup
    inline void GetProjectileFireSetup(C_TFPlayer* pPlayer, const Vec3& vAngles, const Vec3& vOffset, Vec3& vPosOut, Vec3& vAngOut, bool bPipes = false, bool bQuick = false, bool bAllowFlip = true)
    {
        SDKUtils::GetProjectileFireSetupRebuilt(pPlayer, vOffset, vAngles, vPosOut, vAngOut, bPipes);
    }
    
    // Output/debug logging
    inline void Output(const char* szTag, const char* szMsg = nullptr, Color_t color = {255, 255, 255, 255}, bool bLog = false)
    {
        if (bLog && szMsg)
            I::CVar->ConsoleColorPrintf({color.r, color.g, color.b, color.a}, "[%s] %s\n", szTag, szMsg);
    }
    
    // Seed file line hash (for random seed calculation)
    inline int SeedFileLineHash(int nSeed, const char* szFile, int nLine)
    {
        // Amalgam's implementation
        unsigned int uHash = nSeed;
        for (const char* p = szFile; *p; ++p)
            uHash = (uHash >> 1) + ((uHash & 1) << 31) + *p;
        return static_cast<int>(uHash + nLine);
    }
}

// ============================================
// Amalgam's G:: Global State
// ============================================

// Extend SEOwnedDE's G:: namespace with Amalgam's globals
// NOTE: Attacking, Throwing, LastUserCmd, OriginalCmd are now defined in SDK/SDK.h
namespace G
{
    // Aliases for Amalgam names -> SEOwnedDE names (using references)
    // These allow Amalgam code to use G::Reloading while SEOwnedDE uses G::bReloading
    inline bool& Reloading = bReloading;
    inline bool& CanPrimaryAttack = bCanPrimaryAttack;
    inline bool& CanSecondaryAttack = bCanSecondaryAttack;
    inline bool& CanHeadshot = bCanHeadshot;
    inline bool& SilentAngles = bSilentAngles;
    inline bool& PSilentAngles = bPSilentAngles;
    
    // Additional Amalgam globals (not in SDK.h)
    inline float Lerp = 0.015f;
    inline EWeaponType PrimaryWeaponType = EWeaponType::OTHER;
    inline EWeaponType SecondaryWeaponType = EWeaponType::OTHER;
    
    // Aim target tracking
    struct AimTarget_t
    {
        int m_iEntIndex = 0;
        int m_iTickCount = 0;
        int m_iDuration = 32;
    };
    
    struct AimPoint_t
    {
        Vec3 m_vOrigin = {};
        int m_iTickCount = 0;
        int m_iDuration = 32;
    };
    
    inline AimTarget_t AimTarget = {};
    inline AimPoint_t AimPoint = {};
    
    // Anti-aim state
    inline bool AntiAim = false;
    inline bool Choking = false;
    
    // Random seed access
    inline int* RandomSeed()
    {
        return SDKUtils::RandomSeed();
    }
    
    // Sync with SEOwnedDE's globals (call this in CreateMove)
    inline void SyncFromSEOwned()
    {
        Attacking = ::G::bFiring ? 1 : 0;
        Reloading = ::G::bReloading;
        CanPrimaryAttack = ::G::bCanPrimaryAttack;
        CanSecondaryAttack = ::G::bCanSecondaryAttack;
        CanHeadshot = ::G::bCanHeadshot;
        SilentAngles = ::G::bSilentAngles;
        PSilentAngles = ::G::bPSilentAngles;
    }
}

// ============================================
// Amalgam's Vars:: Config System
// ============================================
// This maps Amalgam's Vars::X::Y.Value to SEOwnedDE's CFG::X_Y

namespace Vars
{
    namespace Aimbot
    {
        namespace General
        {
            namespace AimTypeEnum {
                enum { Off = 0, Plain = 1, Smooth = 2, Silent = 3, Locking = 4, Assistive = 5 };
            }
            namespace TargetSelectionEnum {
                enum { FOV = 0, Distance = 1 };
            }
            namespace TargetEnum {
                enum {
                    Players = 1 << 0, Sentry = 1 << 1, Dispenser = 1 << 2, Teleporter = 1 << 3,
                    Stickies = 1 << 4, NPCs = 1 << 5, Bombs = 1 << 6,
                    Building = Sentry | Dispenser | Teleporter
                };
            }
            namespace IgnoreEnum {
                enum {
                    Friends = 1 << 0, Party = 1 << 1, Unprioritized = 1 << 2, Invulnerable = 1 << 3,
                    Invisible = 1 << 4, Unsimulated = 1 << 5, DeadRinger = 1 << 6, Vaccinator = 1 << 7,
                    Disguised = 1 << 8, Taunting = 1 << 9, Team = 1 << 10
                };
            }
            
            // Dynamic config wrappers that read from CFG::
            // AimType needs both get and set because Amalgam code temporarily modifies it
            // CFG::Aimbot_Projectile_Aim_Type: 0=Plain, 1=Silent
            // Maps to Amalgam's AimTypeEnum: Plain=1, Silent=3
            // NOTE: Override is reset each frame in CreateMove
            struct AimTypeWrapper { 
                mutable int m_iOverride = -1; // -1 means use CFG value
                int get() const { 
                    if (m_iOverride >= 0) return m_iOverride;
                    // Map: 0 (Plain) -> AimTypeEnum::Plain (1), 1 (Silent) -> AimTypeEnum::Silent (3)
                    return CFG::Aimbot_Projectile_Aim_Type == 1 ? AimTypeEnum::Silent : AimTypeEnum::Plain; 
                }
                void set(int val) { m_iOverride = val; }
                void Reset() { m_iOverride = -1; }
                __declspec(property(get=get, put=set)) int Value; 
            } inline AimType;
            struct TargetSelectionWrapper { int get() const { return CFG::Aimbot_Projectile_Sort; } __declspec(property(get=get)) int Value; } inline TargetSelection;
            struct TargetWrapper { int get() const { return (CFG::Aimbot_Target_Players ? TargetEnum::Players : 0) | (CFG::Aimbot_Target_Buildings ? TargetEnum::Building : 0); } __declspec(property(get=get)) int Value; } inline Target;
            struct IgnoreWrapper { int get() const { return (CFG::Aimbot_Ignore_Friends ? IgnoreEnum::Friends : 0) | (CFG::Aimbot_Ignore_Invisible ? IgnoreEnum::Invisible : 0) | (CFG::Aimbot_Ignore_Invulnerable ? IgnoreEnum::Invulnerable : 0) | (CFG::Aimbot_Ignore_Taunting ? IgnoreEnum::Taunting : 0); } __declspec(property(get=get)) int Value; } inline Ignore;
            struct AimFOVWrapper { float get() const { return CFG::Aimbot_Projectile_FOV; } __declspec(property(get=get)) float Value; } inline AimFOV;
            struct AutoShootWrapper { bool get() const { return CFG::Aimbot_AutoShoot; } __declspec(property(get=get)) bool Value; } inline AutoShoot;
            struct { float Value = 25.f; } inline AssistStrength;
            struct { int Value = 4; } inline TickTolerance;
        }
        
        namespace Projectile
        {
            namespace StrafePredictionEnum { enum { Off = 0, Air = 1 << 0, Ground = 1 << 1 }; }
            namespace SplashPredictionEnum { enum { Off = 0, Include = 1, Prefer = 2, Only = 3 }; }
            namespace HitboxesEnum { enum { Auto = 1 << 0, Head = 1 << 1, Body = 1 << 2, Feet = 1 << 3, BodyaimIfLethal = 1 << 4, PrioritizeFeet = 1 << 5 }; }
            namespace ModifiersEnum { enum { UsePrimeTime = 1 << 0, ChargeWeapon = 1 << 1 }; }
            namespace SplashModeEnum { enum { Multi = 0, Single = 1 }; }
            namespace RocketSplashModeEnum { enum { Regular = 0, SpecialLight = 1, SpecialHeavy = 2 }; }
            namespace DeltaModeEnum { enum { Average = 0, Max = 1 }; }
            namespace MovesimFrictionFlagsEnum { enum { RunReduce = 1 << 0, CalculateIncrease = 1 << 1 }; }
            
            struct HitboxesWrapper { int get() const { 
                return (CFG::Aimbot_Amalgam_Projectile_Hitbox_Auto ? HitboxesEnum::Auto : 0) |
                       (CFG::Aimbot_Amalgam_Projectile_Hitbox_Head ? HitboxesEnum::Head : 0) |
                       (CFG::Aimbot_Amalgam_Projectile_Hitbox_Body ? HitboxesEnum::Body : 0) |
                       (CFG::Aimbot_Amalgam_Projectile_Hitbox_Feet ? HitboxesEnum::Feet : 0) |
                       (CFG::Aimbot_Amalgam_Projectile_Hitbox_BodyaimLethal ? HitboxesEnum::BodyaimIfLethal : 0) |
                       (CFG::Aimbot_Amalgam_Projectile_Hitbox_PrioritizeFeet ? HitboxesEnum::PrioritizeFeet : 0);
            } __declspec(property(get=get)) int Value; } inline Hitboxes;
            struct ModifiersWrapper { int get() const { 
                return (CFG::Aimbot_Amalgam_Projectile_Mod_PrimeTime ? ModifiersEnum::UsePrimeTime : 0) |
                       (CFG::Aimbot_Amalgam_Projectile_Mod_ChargeWeapon ? ModifiersEnum::ChargeWeapon : 0);
            } __declspec(property(get=get)) int Value; } inline Modifiers;
            struct MaxSimulationTimeWrapper { float get() const { return CFG::Aimbot_Projectile_Max_Simulation_Time; } __declspec(property(get=get)) float Value; } inline MaxSimulationTime;
            struct HitChanceWrapper { float get() const { return static_cast<float>(CFG::Aimbot_Amalgam_Projectile_HitChance); } __declspec(property(get=get)) float Value; } inline HitChance;
            struct SplashRadiusWrapper { float get() const { return static_cast<float>(CFG::Aimbot_Amalgam_Projectile_SplashRadius); } __declspec(property(get=get)) float Value; } inline SplashRadius;
            struct { float Value = 0.f; } inline AutoRelease; // Disabled - kept for compatibility
            struct { int Value = CFG::Aimbot_Projectile_Ground_Samples; } inline GroundSamples;
            struct { int Value = CFG::Aimbot_Projectile_Air_Samples; } inline AirSamples;
            struct { float Value = CFG::Aimbot_Projectile_Ground_Straight_Fuzzy; } inline GroundStraightFuzzyValue;
            struct { float Value = CFG::Aimbot_Projectile_Air_Straight_Fuzzy; } inline AirStraightFuzzyValue;
            struct { int Value = CFG::Aimbot_Projectile_Ground_Max_Changes; } inline GroundMaxChanges;
            struct { int Value = CFG::Aimbot_Projectile_Air_Max_Changes; } inline AirMaxChanges;
            struct { int Value = CFG::Aimbot_Projectile_Ground_Max_Change_Time; } inline GroundMaxChangeTime;
            struct { int Value = CFG::Aimbot_Projectile_Air_Max_Change_Time; } inline AirMaxChangeTime;
            struct { float Value = CFG::Aimbot_Projectile_Ground_Low_Min_Distance; } inline GroundLowMinimumDistance;
            struct { float Value = CFG::Aimbot_Projectile_Ground_Low_Min_Samples; } inline GroundLowMinimumSamples;
            struct { float Value = CFG::Aimbot_Projectile_Ground_High_Min_Distance; } inline GroundHighMinimumDistance;
            struct { float Value = CFG::Aimbot_Projectile_Ground_High_Min_Samples; } inline GroundHighMinimumSamples;
            struct { float Value = CFG::Aimbot_Projectile_Air_Low_Min_Distance; } inline AirLowMinimumDistance;
            struct { float Value = CFG::Aimbot_Projectile_Air_Low_Min_Samples; } inline AirLowMinimumSamples;
            struct { float Value = CFG::Aimbot_Projectile_Air_High_Min_Distance; } inline AirHighMinimumDistance;
            struct { float Value = CFG::Aimbot_Projectile_Air_High_Min_Samples; } inline AirHighMinimumSamples;
            struct { int Value = CFG::Aimbot_Projectile_Delta_Count; } inline DeltaCount;
            struct { int Value = CFG::Aimbot_Projectile_Delta_Mode; } inline DeltaMode;
            struct { int Value = CFG::Aimbot_Projectile_Friction_Flags; } inline MovesimFrictionFlags;
            struct { float Value = 5.f; } inline VerticalShift;
            struct SplashPointsDirectWrapper { int get() const { return CFG::Aimbot_Amalgam_Projectile_SplashPoints; } __declspec(property(get=get)) int Value; } inline SplashPointsDirect;
            struct SplashPointsArcWrapper { int get() const { return CFG::Aimbot_Amalgam_Projectile_SplashPoints; } __declspec(property(get=get)) int Value; } inline SplashPointsArc;
            struct { int Value = 100; } inline SplashCountDirect;
            struct { int Value = 5; } inline SplashCountArc;
            struct { float Value = -1.f; } inline SplashRotateX;
            struct { float Value = -1.f; } inline SplashRotateY;
            struct { int Value = 10; } inline SplashTraceInterval;
            struct { int Value = 1; } inline SplashNormalSkip;
            struct { int Value = SplashModeEnum::Multi; } inline SplashMode;
            struct RocketSplashModeWrapper { int get() const { return CFG::Aimbot_Amalgam_Projectile_RocketSplashMode; } __declspec(property(get=get)) int Value; } inline RocketSplashMode;
            struct { bool Value = true; } inline SplashGrates;
            struct { float Value = 0.f; } inline DragOverride;
            struct { float Value = 0.f; } inline TimeOverride;
            struct { float Value = 50.f; } inline HuntsmanLerp;
            struct { float Value = 100.f; } inline HuntsmanLerpLow;
            struct { float Value = 0.f; } inline HuntsmanAdd;
            struct { float Value = 0.f; } inline HuntsmanAddLow;
            struct { float Value = 5.f; } inline HuntsmanClamp;
            struct { bool Value = false; } inline HuntsmanPullPoint;
            struct { int Value = 5; } inline VelocityAverageCount;
            struct NeckbreakerWrapper { bool get() const { return CFG::Aimbot_Projectile_Neckbreaker; } __declspec(property(get=get)) bool Value; } inline Neckbreaker;
            struct NeckbreakerStepWrapper { int get() const { return CFG::Aimbot_Projectile_NeckbreakerStep; } __declspec(property(get=get)) int Value; } inline NeckbreakerStep;
            struct MidpointAimWrapper { bool get() const { return CFG::Aimbot_Projectile_Midpoint_Aim; } __declspec(property(get=get)) bool Value; } inline MidpointAim;
            struct MidpointMaxDistanceWrapper { float get() const { return CFG::Aimbot_Projectile_Midpoint_Max_Distance; } __declspec(property(get=get)) float Value; } inline MidpointMaxDistance;
        }
        
        namespace Healing
        {
            namespace HealPriorityEnum { enum { None = 0, PrioritizeTeam = 1, PrioritizeFriends = 2, FriendsOnly = 3 }; }
            struct { int Value = HealPriorityEnum::None; } inline HealPriority;
            struct { bool Value = false; } inline AutoArrow;
            struct { bool Value = false; } inline AutoSandvich;
            struct { bool Value = false; } inline AutoRepair;
        }
    }
    
    namespace Doubletap
    {
        struct { bool Value = false; } inline Doubletap;
        struct { bool Value = false; } inline Warp;
        struct { bool Value = false; } inline RechargeTicks;
        struct { bool Value = true; } inline AntiWarp;
        struct { int Value = 22; } inline TickLimit;
        struct { int Value = 22; } inline WarpRate;
        struct { int Value = 24; } inline RechargeLimit;
        struct { int Value = 0; } inline PassiveRecharge;
    }
    
    namespace Misc
    {
        namespace Movement { struct { bool Value = CFG::Misc_Bunnyhop; } inline Bunnyhop; }
        namespace Game { struct { bool Value = CFG::Misc_AntiCheat_Enabled; } inline AntiCheatCompatibility; }
    }
    
    namespace Visuals
    {
        namespace Trajectory
        {
            struct { bool Value = false; } inline Override;
            struct { bool Value = false; } inline Pipes;
            struct { float Value = 1100.f; } inline Speed;
            struct { float Value = 0.f; } inline Gravity;
            struct { float Value = 60.f; } inline LifeTime;
            struct { float Value = 0.f; } inline Hull;
            struct { float Value = 23.5f; } inline OffsetX;
            struct { float Value = 12.f; } inline OffsetY;
            struct { float Value = -3.f; } inline OffsetZ;
            struct { float Value = 0.f; } inline Drag;
            struct { float Value = 0.f; } inline DragX;
            struct { float Value = 0.f; } inline DragY;
            struct { float Value = 0.f; } inline DragZ;
            struct { float Value = 0.f; } inline AngularDragX;
            struct { float Value = 0.f; } inline AngularDragY;
            struct { float Value = 0.f; } inline AngularDragZ;
            struct { float Value = 0.f; } inline UpVelocity;
            struct { float Value = 0.f; } inline AngularVelocityX;
            struct { float Value = 0.f; } inline AngularVelocityY;
            struct { float Value = 0.f; } inline AngularVelocityZ;
            struct { float Value = 0.f; } inline MaxVelocity;
            struct { float Value = 0.f; } inline MaxAngularVelocity;
        }
        
        namespace Simulation
        {
            struct { bool Value = false; } inline Timed;
            struct { float Value = 2.f; } inline DrawDuration;
            struct PlayerPathWrapper { int get() const { return CFG::Visuals_Simulation_Movement_Style; } __declspec(property(get=get)) int Value; } inline PlayerPath;
            struct ProjectilePathWrapper { int get() const { return CFG::Visuals_Simulation_Projectile_Style; } __declspec(property(get=get)) int Value; } inline ProjectilePath;
            struct { int Value = 0; } inline RealPath;
        }
        
        namespace Hitbox
        {
            namespace BoundsEnabledEnum { enum { Off = 0, OnShot = 1 << 0, AimPoint = 1 << 1 }; }
            struct { int Value = 0; } inline BoundsEnabled;
            struct { float Value = 2.f; } inline DrawDuration;
        }
    }
    
    namespace Colors
    {
        struct { Color_t Value = { 255, 255, 255, 255 }; } inline BoundHitboxEdge;
        struct { Color_t Value = { 255, 255, 255, 0 }; } inline BoundHitboxEdgeIgnoreZ;
        struct { Color_t Value = { 255, 255, 255, 0 }; } inline BoundHitboxFace;
        struct { Color_t Value = { 255, 255, 255, 0 }; } inline BoundHitboxFaceIgnoreZ;
        struct PlayerPathColorWrapper { Color_t get() const { return CFG::Color_Simulation_Movement; } __declspec(property(get=get)) Color_t Value; } inline PlayerPath;
        struct PlayerPathIgnoreZColorWrapper { Color_t get() const { return CFG::Color_Simulation_Movement; } __declspec(property(get=get)) Color_t Value; } inline PlayerPathIgnoreZ;
        struct ProjectilePathColorWrapper { Color_t get() const { return CFG::Color_Simulation_Projectile; } __declspec(property(get=get)) Color_t Value; } inline ProjectilePath;
        struct ProjectilePathIgnoreZColorWrapper { Color_t get() const { return CFG::Color_Simulation_Projectile; } __declspec(property(get=get)) Color_t Value; } inline ProjectilePathIgnoreZ;
        struct { Color_t Value = { 255, 255, 255, 0 }; } inline RealPath;
        struct { Color_t Value = { 255, 255, 255, 255 }; } inline RealPathIgnoreZ;
        struct { Color_t Value = { 255, 255, 255, 0 }; } inline Local;
    }
    
    namespace Debug
    {
        struct { bool Value = false; } inline Logging;
        struct { bool Value = false; } inline Info;
    }
    
    namespace Menu
    {
        namespace IndicatorsEnum { enum { Ticks = 1 << 0 }; }
        struct { int Value = 0; } inline Indicators;
        struct { int x = 100; int y = 100; } inline TicksDisplay;
        namespace Theme
        {
            struct { Color_t Value = { 175, 150, 255, 255 }; } inline Accent;
            struct { Color_t Value = { 0, 0, 0, 250 }; } inline Background;
            struct { Color_t Value = { 255, 255, 255, 255 }; } inline Active;
        }
    }
    
    namespace Speedhack
    {
        struct { bool Value = false; } inline Enabled;
        struct { int Value = 1; } inline Amount;
    }
}

// ============================================
// F::Backtrack Stub
// ============================================

namespace F {
    class CBacktrackStub
    {
    public:
        // Get real latency for projectile prediction
        float GetReal() { return SDKUtils::GetRealLatency(); }
        
        // Get anticipated choke ticks
        int GetAnticipatedChoke() { return 0; }
        
        // Get bones for a target entity (returns nullptr - no backtrack)
        matrix3x4_t* GetBones(C_BaseEntity* pEntity)
        {
            if (!pEntity || !IsPlayer(pEntity))
                return nullptr;
            
            // Return current bones instead of backtracked bones
            static matrix3x4_t bones[128];
            if (pEntity->As<C_TFPlayer>()->SetupBones(bones, 128, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
                return bones;
            
            return nullptr;
        }
    };
    
    inline CBacktrackStub Backtrack;
}

// ============================================
// DrawBox_t and DrawPath_t Types
// ============================================

using DrawBox_t = G::DrawBox_t;
using DrawPath_t = G::DrawPath_t;

// ============================================
// Player Resource Helper
// ============================================

// Helper to get user ID from player resource
inline int GetUserIDFromPlayerResource(int playerindex)
{
    auto pResource = GetTFPlayerResource();
    if (!pResource)
        return -1;
    
    static int nOffset = NetVars::GetNetVar("CPlayerResource", "m_iUserID");
    if (nOffset == 0)
        return -1;
    
    return *reinterpret_cast<int*>((reinterpret_cast<std::uintptr_t>(pResource) + nOffset) + (playerindex * sizeof(int)));
}

// ============================================
// F::AmalgamAimbot - Aimbot wrapper with Store functionality
// Named differently to avoid conflict with SEOwnedDE's F::Aimbot
// ============================================

namespace F {
    class CAmalgamAimbotWrapper
    {
    private:
        size_t m_iSize = 0;
        int m_iPlayer = 0;
        
    public:
        bool m_bRan = false;
        bool m_bRunningSecondary = false;
        DrawPath_t m_tPath = {};
        
        // Store target entity for real path visualization
        void Store(C_BaseEntity* pEntity, size_t iSize)
        {
            if (!Vars::Visuals::Simulation::RealPath.Value)
                return;
            
            if (!pEntity || !IsPlayer(pEntity))
                return;
            
            // Get user ID from player resource
            int iUserID = GetUserIDFromPlayerResource(pEntity->entindex());
            if (iUserID < 0)
                return;
            
            m_tPath = { { pEntity->m_vecOrigin() }, I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Color_t(), Vars::Visuals::Simulation::RealPath.Value };
            m_iSize = iSize;
            m_iPlayer = iUserID;
        }
        
        // Update stored path (called each frame)
        void Store(bool bFrameStageNotify = true)
        {
            if (!Vars::Visuals::Simulation::RealPath.Value)
                return;
            
            int iLag = 1;
            if (bFrameStageNotify)
            {
                static int iStaticTickcount = I::GlobalVars->tickcount;
                iLag = I::GlobalVars->tickcount - iStaticTickcount;
                iStaticTickcount = I::GlobalVars->tickcount;
            }
            
            if (!m_tPath.m_flTime)
                return;
            else if (m_tPath.m_vPath.size() >= m_iSize || m_tPath.m_flTime < I::GlobalVars->curtime)
            {
                // Path complete - add to storage
                if (m_tPath.m_tColor = Vars::Colors::RealPath.Value, m_tPath.m_bZBuffer = true; m_tPath.m_tColor.a)
                    G::PathStorage.push_back({ m_tPath.m_vPath, m_tPath.m_flTime, m_tPath.m_tColor, m_tPath.m_iStyle, m_tPath.m_bZBuffer });
                if (m_tPath.m_tColor = Vars::Colors::RealPathIgnoreZ.Value, m_tPath.m_bZBuffer = false; m_tPath.m_tColor.a)
                    G::PathStorage.push_back({ m_tPath.m_vPath, m_tPath.m_flTime, m_tPath.m_tColor, m_tPath.m_iStyle, m_tPath.m_bZBuffer });
                m_tPath = {};
                return;
            }
            
            // Find player by user ID
            int iIndex = 0;
            for (int i = 1; i <= I::GlobalVars->maxClients; i++)
            {
                if (GetUserIDFromPlayerResource(i) == m_iPlayer)
                {
                    iIndex = i;
                    break;
                }
            }
            
            if (iIndex == 0)
                return;
            
            if (bFrameStageNotify ? iIndex == I::EngineClient->GetLocalPlayer() : iIndex != I::EngineClient->GetLocalPlayer())
                return;
            
            auto pPlayer = I::ClientEntityList->GetClientEntity(iIndex);
            if (!pPlayer)
                return;
            
            auto pTFPlayer = pPlayer->As<C_TFPlayer>();
            if (!pTFPlayer)
                return;
            
            for (int i = 0; i < iLag; i++)
                m_tPath.m_vPath.push_back(pTFPlayer->m_vecOrigin());
        }
    };
    
    inline CAmalgamAimbotWrapper AmalgamAimbot;
}
