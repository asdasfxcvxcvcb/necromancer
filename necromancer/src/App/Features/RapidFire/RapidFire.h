#pragma once

#include "../../../SDK/SDK.h"

class CRapidFire
{
	CUserCmd m_ShiftCmd = {};
	bool m_bShiftSilentAngles = false;
	bool m_bSetCommand = false;
	bool m_bIsProjectileDT = false;
	bool m_bIsStickyDT = false;

	Vec3 m_vShiftStart = {};
	bool m_bStartedShiftOnGround = false;
	
	bool m_bStickyCharging = false;

	bool ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	bool ShouldStartFastSticky(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	int GetFastStickyMaxRecharge();
	bool IsFastStickyUsable();

public:
	void Run(CUserCmd* pCmd, bool* pSendPacket);
	void RunFastSticky(CUserCmd* pCmd, bool* pSendPacket);
	bool ShouldExitCreateMove(CUserCmd* pCmd);
	bool IsWeaponSupported(C_TFWeaponBase* pWeapon);
	bool IsProjectileWeapon(C_TFWeaponBase* pWeapon);
	bool IsStickyWeapon(C_TFWeaponBase* pWeapon);

	bool GetShiftSilentAngles() { return m_bShiftSilentAngles; }
	int GetTicks(C_TFWeaponBase* pWeapon = nullptr);
	
	bool IsStickyCharging() const { return m_bStickyCharging; }
};

MAKE_SINGLETON_SCOPED(CRapidFire, RapidFire, F);
