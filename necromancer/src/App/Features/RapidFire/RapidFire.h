#pragma once

#include "../../../SDK/SDK.h"

class CRapidFire
{
	CUserCmd m_ShiftCmd = {};
	bool m_bShiftSilentAngles = false;
	bool m_bSetCommand = false;
	int m_iWait = 0;

	Vec3 m_vShiftStart = {};
	Vec3 m_vAntiWarpVelocity = {};
	bool m_bStartedShiftOnGround = false;
	int m_nAntiWarpMaxTicks = 0;

	bool m_bStickyCharging = false;

	bool ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	void AntiWarp(C_TFPlayer* pLocal, CUserCmd* pCmd, int nTicks);
	int GetFastStickyMaxRecharge();
	bool IsFastStickyUsable();
	bool IsScottishResistance(C_TFWeaponBase* pWeapon);
	bool IsQuickiebomb(C_TFWeaponBase* pWeapon);
	int GetStickyPreferredTicks(C_TFWeaponBase* pWeapon);

public:
	void Run(CUserCmd* pCmd, bool* pSendPacket);
	void MaintainWait();
	void RunFastSticky(CUserCmd* pCmd, bool* pSendPacket);
	bool ShouldExitCreateMove(CUserCmd* pCmd);
	bool IsWeaponSupported(C_TFWeaponBase* pWeapon);
	bool IsStickyWeapon(C_TFWeaponBase* pWeapon);

	bool GetShiftSilentAngles() { return m_bShiftSilentAngles; }
	int GetTicks(C_TFWeaponBase* pWeapon = nullptr);
	int GetShotsWithinPacket(C_TFWeaponBase* pWeapon, int nTicks);
	bool CanDoubleTapNow(C_TFPlayer* pLocal = nullptr, C_TFWeaponBase* pWeapon = nullptr);
	int GetReleaseTicks();

	bool IsStickyCharging() const { return m_bStickyCharging; }
};

MAKE_SINGLETON_SCOPED(CRapidFire, RapidFire, F);
