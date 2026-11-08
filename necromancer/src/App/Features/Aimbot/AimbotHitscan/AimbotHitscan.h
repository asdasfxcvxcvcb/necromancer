#pragma once

#include "../AimbotCommon/AimbotCommon.h"

#include "../../LagRecords/LagRecords.h"



class CAimbotHitscan

{

	struct HitscanTarget_t : AimTarget_t

	{

		int AimedHitbox = -1;

		float SimulationTime = -1.0f;

		const LagRecord_t* LagRecord = nullptr;

		bool WasMultiPointed = false;

	};



	std::vector<HitscanTarget_t> m_vecTargets = {};

	bool m_bMinigunBurst = false;

	Vec3 m_vLastSilentAngles = {};
	int m_nLastSilentCmd = 0;
	bool m_bOwnedLastTick = false;
	bool m_bReturnActive = false;



	int GetAimHitbox(C_TFWeaponBase* pWeapon);


	bool ScanHead(C_TFPlayer* pLocal, HitscanTarget_t& target);


	bool ScanBody(C_TFPlayer* pLocal, HitscanTarget_t& target);


	bool ScanBuilding(C_TFPlayer* pLocal, HitscanTarget_t& target);


	bool GetTarget(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, HitscanTarget_t& outTarget);


	bool ScanHead(C_TFPlayer* pLocal, const CUserCmd* pCmd, HitscanTarget_t& target);


	bool ScanBody(C_TFPlayer* pLocal, const CUserCmd* pCmd, HitscanTarget_t& target);


	bool ScanBuilding(C_TFPlayer* pLocal, const CUserCmd* pCmd, HitscanTarget_t& target);


	bool GetTarget(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const CUserCmd* pCmd, HitscanTarget_t& outTarget);

	int GetActiveAimType(C_TFPlayer* pLocal) const;

	bool ShouldAim(const CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);

	void Aim(CUserCmd* pCmd, C_TFPlayer* pLocal, const Vec3& vAngles);

	bool ShouldFire(const CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const HitscanTarget_t& target);

	void HandleFire(CUserCmd* pCmd, C_TFWeaponBase* pWeapon);

	bool HandleMinigun(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const HitscanTarget_t& target, bool bCanFireNow, bool bUserAttack2, bool bAimKeyDown);

	void UpdateSilentReturn(CUserCmd* pCmd, C_TFPlayer* pLocal, bool bOwned);

	void DragIndicator();



public:

	bool IsFiring(const CUserCmd* pCmd, C_TFWeaponBase* pWeapon);

	void Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);

	void DrawSwitchIndicator();

	void HandleSwitchKey();

	bool IsHeadshotCapableWeapon(C_TFWeaponBase* pWeapon);

};



MAKE_SINGLETON_SCOPED(CAimbotHitscan, AimbotHitscan, F);