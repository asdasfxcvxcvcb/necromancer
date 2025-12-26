#pragma once

#include "../AimbotCommon/AimbotCommon.h"
#include "../../LagRecords/LagRecords.h"
#include <vector>

// Arc weapon aimbot - uses the old proven code for grenade launchers, stickies, etc.
// Rockets use the Amalgam projectile aimbot instead (no gravity, different prediction)

class CAimbotProjectileArc
{
public:
	void Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	bool IsFiring(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	float GetSplashRadius(C_TFWeaponBase* pWeapon, C_TFPlayer* pPlayer);

	// Check if a weapon is an arc weapon (has gravity)
	static bool IsArcWeapon(C_TFWeaponBase* pWeapon);

	// Path storage for visualization
	std::vector<Vec3> m_TargetPath;
	std::vector<Vec3> m_ProjectilePath;

	// Sticky/Huntsman target locking - prevents re-targeting while charging
	struct LockedTarget_t
	{
		int m_nTargetIndex = 0;
		Vec3 m_vLockedAngle = {};
		float m_flLockTime = 0.f;
		float m_flLockedVelocity = 0.f;
		bool m_bValid = false;
		
		void Reset()
		{
			m_nTargetIndex = 0;
			m_vLockedAngle = {};
			m_flLockTime = 0.f;
			m_flLockedVelocity = 0.f;
			m_bValid = false;
		}
	};
	LockedTarget_t m_LockedTarget;

	// Loose Cannon specific tracking
	struct LooseCannonState_t
	{
		bool m_bAimbotInitiatedCharge = false;  // True if aimbot started the charge (not user)
		bool m_bWasAimbotKeyDown = false;       // Track aimbot key state for release detection
		bool m_bNeedsQuickSwitch = false;       // True if we need to quick-switch to reset fuse
		int m_nQuickSwitchTick = 0;             // Tick when quick-switch was initiated
		int m_nQuickSwitchCooldownTick = 0;     // Cooldown after quick-switch to prevent immediate re-charge
		int m_nQuickSwitchPhase = 0;            // 0 = not switching, 1 = switched away, waiting to switch back
		
		void Reset()
		{
			m_bAimbotInitiatedCharge = false;
			m_bWasAimbotKeyDown = false;
			m_bNeedsQuickSwitch = false;
			m_nQuickSwitchTick = 0;
			m_nQuickSwitchPhase = 0;
			// Don't reset cooldown here
		}
	};
	LooseCannonState_t m_LooseCannonState;
};

MAKE_SINGLETON_SCOPED(CAimbotProjectileArc, AimbotProjectileArc, F);
