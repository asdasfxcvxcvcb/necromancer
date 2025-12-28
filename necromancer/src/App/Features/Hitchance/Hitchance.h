#pragma once

#include "../../../SDK/SDK.h"
#include "../../../Utils/Singleton/Singleton.h"

class CHitchance
{
private:
	bool m_bInitialized = false;
	Vec2 m_SpreadLUT[256] = {};

	void InitSpreadLUT();

public:
	// Spread calculation
	Vec2 GetSpreadForSeed(int nSeed);
	Vec2 GetFixedSpreadForPellet(int nPellet, int nTotalPellets);

	// Weapon-specific spread multipliers
	float GetMinigunSpreadMult(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal);
	float GetAmbassadorSpreadMult(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal);
	float GetWeaponSpreadMult(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal);

	// First shot accuracy
	bool HasFirstShotAccuracy(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal);
	float GetFirstShotVariance(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal);

	// Spread mode detection
	bool IsFixedSpreadEnabled(C_TFWeaponBase* pWeapon);

	// Weapon info
	int GetBulletsPerShot(C_TFWeaponBase* pWeapon);
	int GetConsecutiveShots(C_TFWeaponBase* pWeapon);

	// Main hitchance calculation
	float Calculate(
		C_TFPlayer* pLocal,
		C_TFWeaponBase* pWeapon,
		const Vec3& vShootPos,
		const Vec3& vTargetPos,
		float flHitboxRadius,
		bool bIsRapidFire = false,
		int nRapidFireShots = 1
	);
};

MAKE_SINGLETON_SCOPED(CHitchance, Hitchance, F);
