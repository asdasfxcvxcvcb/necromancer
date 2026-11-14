#pragma once

#include "../../../../SDK/SDK.h"

class CAutoVaccinator
{
	int m_GoalResType = MEDIGUN_NUM_RESISTS;
	bool m_IsChangingRes = false;
	bool m_ShouldPop = false;
	bool m_bKeyPopQueued = false;
	int m_SwitchDelayTicks = 0;
	float m_flChangeStart = 0.f;
	int m_nPredictedResist = -1;
	float m_flPredictTime = 0.f;
	float m_flLastPopTime[MEDIGUN_NUM_RESISTS] = {};
	float m_flRealtimeLastPop = 0.f;

	bool CanPop(C_WeaponMedigun* medigun, int type) const;

public:
	void Reset();
	void Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* cmd);
	void ProcessPlayerHurt(IGameEvent* event);
	void PreventReload(CUserCmd* cmd);
	int GetGoalResType() const { return m_GoalResType; }
	bool IsChangingRes() const { return m_IsChangingRes; }
	bool IsKeyPopQueued() const { return m_bKeyPopQueued; }
	float GetLastPopTime() const;
};

MAKE_SINGLETON_SCOPED(CAutoVaccinator, AutoVaccinator, F);
