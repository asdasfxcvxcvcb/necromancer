#pragma once
#include "../AimbotCommon/AimbotCommon.h"
#include "../../LagRecords/LagRecords.h"

class CAimbotMelee
{
	struct MeleeTarget_t : AimTarget_t
	{
		float SimulationTime = -1.0f;
		const LagRecord_t* LagRecord = nullptr;
		bool MeleeTraceHit = false;
		bool bIsFriendlyBuilding = false;
	};

	std::vector<MeleeTarget_t> m_vecTargets = {};

	int GetSwingTime(C_TFWeaponBase* pWeapon);
	bool CanHit(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, MeleeTarget_t& target);
	bool GetTarget(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, MeleeTarget_t& outTarget);
	bool ShouldAim(const CUserCmd* pCmd, C_TFWeaponBase* pWeapon);
	void Aim(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const Vec3& vAngles);
	bool ShouldFire(const MeleeTarget_t& target);
	void HandleFire(CUserCmd* pCmd, C_TFWeaponBase* pWeapon);
	
	// Friendly building support
	bool ShouldTargetFriendlyBuilding(C_BaseObject* pBuilding, C_TFWeaponBase* pWeapon);

public:
	bool IsFiring(const CUserCmd* pCmd, C_TFWeaponBase* pWeapon);
	void Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	void CrouchWhileAirborne(CUserCmd* pCmd, C_TFPlayer* pLocal);
};

MAKE_SINGLETON_SCOPED(CAimbotMelee, AimbotMelee, F);
