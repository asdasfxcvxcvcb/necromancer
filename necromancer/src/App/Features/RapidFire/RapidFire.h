#pragma once

#include "../../../SDK/SDK.h"

class CRapidFire
{
	CUserCmd m_ShiftCmd = {};
	bool m_bShiftSilentAngles = false;
	bool m_bSetCommand = false;

	Vec3 m_vShiftStart = {};
	bool m_bStartedShiftOnGround = false;
	
	// For angle recalculation during DT
	int m_nSavedTargetIndex = -1;
	float m_flSavedSimTime = 0.0f;

	bool ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);

public:
	void Run(CUserCmd* pCmd, bool* pSendPacket);
	bool ShouldExitCreateMove(CUserCmd* pCmd);
	bool IsWeaponSupported(C_TFWeaponBase* pWeapon);

	bool GetShiftSilentAngles() { return m_bShiftSilentAngles; }

	// Returns available DT ticks if ready, 0 otherwise (from Amalgam)
	int GetTicks(C_TFWeaponBase* pWeapon = nullptr);
};

MAKE_SINGLETON_SCOPED(CRapidFire, RapidFire, F);
