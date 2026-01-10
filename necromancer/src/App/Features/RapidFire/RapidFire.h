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

	Vec3 m_vShiftStart = {};
	Vec3 m_vShiftVelocity = {};
	bool m_bStartedShiftOnGround = false;
	
	// For smart multi-shot timing
	int m_nShotsToFire = 0;
	std::vector<int> m_vecFireTicks = {};
	
	// Track if this DT was started during reload
	bool m_bWasReloadInterrupt = false;

	bool ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	
	const WeaponFireData_t* GetWeaponFireData(C_TFWeaponBase* pWeapon);
	void CalculateFireTicks(C_TFWeaponBase* pWeapon, int nTotalTicks);
	void ApplyAirAntiWarp(CUserCmd* pCmd, C_TFPlayer* pLocal, int nCurrentTick, int nTotalTicks);

public:
	void Run(CUserCmd* pCmd, bool* pSendPacket);
	bool ShouldExitCreateMove(CUserCmd* pCmd);
	bool IsWeaponSupported(C_TFWeaponBase* pWeapon);
	bool IsProjectileWeapon(C_TFWeaponBase* pWeapon);
	bool CanFireDuringReload(C_TFWeaponBase* pWeapon);

	bool GetShiftSilentAngles() { return m_bShiftSilentAngles; }
	int GetTicks(C_TFWeaponBase* pWeapon = nullptr);
	const std::vector<int>& GetFireTicks() const { return m_vecFireTicks; }
};

MAKE_SINGLETON_SCOPED(CRapidFire, RapidFire, F);
