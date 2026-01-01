#pragma once

#include "../../../SDK/SDK.h"

#define WEAPON_RANDOM_RANGE 10000
#define TF_DAMAGE_CRIT_MULTIPLIER 3.0f
#define TF_DAMAGE_CRIT_CHANCE 0.02f
#define TF_DAMAGE_CRIT_CHANCE_RAPID 0.02f
#define TF_DAMAGE_CRIT_CHANCE_MELEE 0.15f
#define TF_DAMAGE_CRIT_DURATION_RAPID 2.0f

#define SEED_ATTEMPTS 4096
#define BUCKET_ATTEMPTS 1000

enum ECritRequest
{
	CritRequest_Any,
	CritRequest_Crit,
	CritRequest_Skip
};

struct HealthHistory_t
{
	int m_iNewHealth = 0;
	int m_iOldHealth = 0;

	struct HealthStorage_t
	{
		int m_iOldHealth = 0;
		float m_flTime = 0.f;
	};
	std::unordered_map<int, HealthStorage_t> m_mHistory = {};
};

class CCritHack
{
private:
	// Core crit calculation
	int GetCritCommand(C_TFWeaponBase* pWeapon, int iCommandNumber, bool bCrit = true, bool bSafe = true);
	bool IsCritCommand(int iCommandNumber, C_TFWeaponBase* pWeapon, bool bCrit = true, bool bSafe = true);
	bool IsCritSeed(int iSeed, C_TFWeaponBase* pWeapon, bool bCrit = true, bool bSafe = true);
	int CommandToSeed(int iCommandNumber);

	// Info updates
	void UpdateWeaponInfo(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	void UpdateInfo(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	int GetCritRequest(CUserCmd* pCmd, C_TFWeaponBase* pWeapon);

	void Reset();
	void StoreHealthHistory(int iIndex, int iHealth, bool bDamage = false);

	// Damage tracking
	int m_iCritDamage = 0;
	int m_iRangedDamage = 0;
	int m_iMeleeDamage = 0;
	int m_iResourceDamage = 0;
	int m_iDesyncDamage = 0;
	std::unordered_map<int, HealthHistory_t> m_mHealthHistory = {};

	// Crit ban status
	bool m_bCritBanned = false;
	float m_flDamageTilFlip = 0;

	// Weapon info
	float m_flDamage = 0.f;
	float m_flCost = 0.f;
	int m_iAvailableCrits = 0;
	int m_iPotentialCrits = 0;
	int m_iNextCrit = 0;

	int m_iEntIndex = 0;
	bool m_bMelee = false;
	float m_flCritChance = 0.f;
	float m_flMultCritChance = 1.f;

	// UI dragging
	void Drag();
	
	// Smooth bar animation
	float m_flDisplayedFillRatio = 0.f;
	float m_flDisplayedNextCritRatio = 0.f;  // Ghost fill for next crit progress
	int m_iLastNextCrit = 0;  // Track previous m_iNextCrit to detect shot progress
	int m_iShotProgress = 0;  // Shots fired toward next crit

public:
	void Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Event(IGameEvent* pEvent, uint32_t uHash);
	void Store();
	void Draw();

	bool WeaponCanCrit(C_TFWeaponBase* pWeapon, bool bWeaponOnly = false);
	int PredictCmdNum(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
	
	// Safe mode check - returns true if aimbot should fire, false if it should wait
	// Call this BEFORE adding IN_ATTACK to check if safe mode would block the shot
	bool ShouldAllowFire(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);

	// Getters for external use
	float GetCritDamage() { return static_cast<float>(m_iCritDamage); }
	float GetRangedDamage() { return static_cast<float>(m_iRangedDamage); }
	bool IsCritBanned() { return m_bCritBanned; }
	int GetAvailableCrits() { return m_iAvailableCrits; }

	// Forced crit state - set by Run(), used by CalcIsAttackCritical hook
	int m_iForcedCommandNumber = 0;  // The command number we forced (0 = none)
	int m_iForcedSeed = 0;           // The seed corresponding to forced command
	bool m_bForcingCrit = false;     // True if we're forcing a crit this tick
	bool m_bForcingSkip = false;     // True if we're forcing a non-crit (skip)
};

MAKE_SINGLETON_SCOPED(CCritHack, CritHack, F);
