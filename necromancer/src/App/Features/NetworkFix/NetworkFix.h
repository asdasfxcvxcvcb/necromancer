#pragma once

#include "../../../SDK/SDK.h"

class CReadPacketState
{
	float m_flFrameTimeClientState = 0.0f;
	float m_flFrameTime = 0.0f;
	float m_flCurTime = 0.0f;
	int m_nTickCount = 0;

public:
	void Store();
	void Restore();
};

class CNetworkFix
{
	CReadPacketState m_State = {};

public:
	float m_flLastCmdRate = -1.0f;  // Track cmdrate for ping reducer
	float m_flLastUpdateRate = -1.0f;  // Track updaterate for ping reducer
	int m_nLastWeaponType = -1;  // Track weapon type for auto interp
	
	void FixInputDelay(bool bFinalTick);
	bool ShouldReadPackets();
	void ApplyPingReducer();
	void ApplyAutoInterp();
	void ResetRates();  // Reset on map change
};

MAKE_SINGLETON_SCOPED(CNetworkFix, NetworkFix, F);
