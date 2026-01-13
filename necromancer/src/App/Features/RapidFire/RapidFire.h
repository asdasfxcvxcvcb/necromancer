#pragma once

#include "../../../SDK/SDK.h"

// Weapon fire rate data for optimal doubletap timing
struct WeaponFireData_t
{
	int nWeaponID;
	float flFireDelay;      // Time between shots in seconds
	int nTicksBetweenShots; // Ticks needed between shots (at 66 tick)
	bool bCanFireWhileReloading; // Can interrupt reload to fire
};

class CRapidFire
{
	CUserCmd m_ShiftCmd = {};
	bool m_bShiftSilentAngles = false;
	bool m_bSetCommand = false;
	bool m_bIsProjectileDT = false;  // True if this DT is for projectile weapon (post-fire cooldown skip)
	bool m_bIsStickyDT = false;      // True if this DT is for sticky launcher (fires on release)

	Vec3 m_vShiftStart = {};
	Vec3 m_vShiftVelocity = {};
	bool m_bStartedShiftOnGround = false;
	
	// For smart multi-shot timing
	int m_nShotsToFire = 0;
	std::vector<int> m_vecFireTicks = {};
	
	// Track if this DT was started during reload
	bool m_bWasReloadInterrupt = false;

	// Sticky DT dedicated key state
	bool m_bStickyCharging = false;        // Currently in gaza bombing mode

	bool ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	bool ShouldStartFastSticky(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	int GetFastStickyMaxRecharge();  // Calculate max ticks we can recharge to for fast sticky
	bool IsFastStickyUsable();       // Check if fast sticky is usable with current settings
	
	const WeaponFireData_t* GetWeaponFireData(C_TFWeaponBase* pWeapon);
	void CalculateFireTicks(C_TFWeaponBase* pWeapon, int nTotalTicks);
	void ApplyAirAntiWarp(CUserCmd* pCmd, C_TFPlayer* pLocal, int nCurrentTick, int nTotalTicks);

public:
	void Run(CUserCmd* pCmd, bool* pSendPacket);
	void RunFastSticky(CUserCmd* pCmd, bool* pSendPacket);  // Fast sticky shooting handler
	bool ShouldExitCreateMove(CUserCmd* pCmd);
	bool IsWeaponSupported(C_TFWeaponBase* pWeapon);
	bool IsProjectileWeapon(C_TFWeaponBase* pWeapon);
	bool IsStickyWeapon(C_TFWeaponBase* pWeapon);  // Sticky launcher / Loose Cannon
	bool CanFireDuringReload(C_TFWeaponBase* pWeapon);

	bool GetShiftSilentAngles() { return m_bShiftSilentAngles; }
	int GetTicks(C_TFWeaponBase* pWeapon = nullptr);
	const std::vector<int>& GetFireTicks() const { return m_vecFireTicks; }
	
	// Sticky DT state
	bool IsStickyCharging() const { return m_bStickyCharging; }
};

MAKE_SINGLETON_SCOPED(CRapidFire, RapidFire, F);
