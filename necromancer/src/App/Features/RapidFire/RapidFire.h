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

	Vec3 m_vShiftStart = {};
	Vec3 m_vShiftVelocity = {};  // Saved velocity for air anti-warp
	bool m_bStartedShiftOnGround = false;
	
	// For angle recalculation during DT
	int m_nSavedTargetIndex = -1;
	float m_flSavedSimTime = 0.0f;
	Vec3 m_vSavedTargetPos = {};
	Vec3 m_vSavedTargetVelocity = {};
	int m_nSavedTargetHitbox = HITBOX_PELVIS;
	
	// For smart multi-shot timing
	int m_nShotsToFire = 0;
	std::vector<int> m_vecFireTicks = {}; // Which ticks to fire on
	
	// Track if this DT was started during reload
	bool m_bWasReloadInterrupt = false;

	bool ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	
	// Get weapon fire rate data
	const WeaponFireData_t* GetWeaponFireData(C_TFWeaponBase* pWeapon);
	
	// Calculate optimal fire ticks based on weapon fire rate
	void CalculateFireTicks(C_TFWeaponBase* pWeapon, int nTotalTicks);
	
	// Predict target position for a given tick offset
	Vec3 PredictTargetPosition(C_TFPlayer* pTarget, int nTickOffset);
	
	// Air anti-warp - predict position to stay in place while airborne
	void ApplyAirAntiWarp(CUserCmd* pCmd, C_TFPlayer* pLocal, int nCurrentTick, int nTotalTicks);

public:
	void Run(CUserCmd* pCmd, bool* pSendPacket);
	bool ShouldExitCreateMove(CUserCmd* pCmd);
	bool IsWeaponSupported(C_TFWeaponBase* pWeapon);
	
	// Check if we can fire during reload (weapon-specific)
	bool CanFireDuringReload(C_TFWeaponBase* pWeapon);

	bool GetShiftSilentAngles() { return m_bShiftSilentAngles; }

	// Returns available DT ticks if ready, 0 otherwise (from Amalgam)
	int GetTicks(C_TFWeaponBase* pWeapon = nullptr);
	
	// Get the fire ticks for current shift
	const std::vector<int>& GetFireTicks() const { return m_vecFireTicks; }
};

MAKE_SINGLETON_SCOPED(CRapidFire, RapidFire, F);
