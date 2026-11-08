#pragma once

#include "../../../../SDK/SDK.h"

class CAutoBackstab
{
public:
	void Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
	float m_flLastStab = 0.f;
};

MAKE_SINGLETON_SCOPED(CAutoBackstab, AutoBackstab, F);
