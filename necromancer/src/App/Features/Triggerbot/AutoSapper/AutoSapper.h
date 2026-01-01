#pragma once
#include "../../../../SDK/SDK.h"

class CAutoSapper
{
	// SDK sapper range: tf_obj_max_attach_dist = 160
	static constexpr float SAPPER_RANGE = 160.f;

	struct SapperTarget_t
	{
		C_BaseObject* Building = nullptr;
		Vec3 Position = {};
		Vec3 AngleTo = {};
		float FOVTo = 0.f;
		float DistanceTo = 0.f;
		bool bInRange = false;
		bool bVisible = false;
	};

	std::vector<SapperTarget_t> m_vecTargets = {};

	bool IsHoldingSapper(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	bool CanSapBuilding(C_BaseObject* pBuilding);
	bool IsVisible(C_TFPlayer* pLocal, C_BaseObject* pBuilding, const Vec3& vTargetPos);
	bool FindTargets(C_TFPlayer* pLocal);
	void Aim(CUserCmd* pCmd, C_TFPlayer* pLocal, const Vec3& vAngle);

public:
	void Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
	void DrawESP();
};

MAKE_SINGLETON_SCOPED(CAutoSapper, AutoSapper, F);
