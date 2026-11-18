#pragma once

#include "tf_weaponbase.h"
#include "../../Utils/SignatureManager/SignatureManager.h"

enum EFlareGunType
{
	FLAREGUN_NORMAL = 0,
	FLAREGUN_DETONATE,
	FLAREGUN_GRORDBORT,
	FLAREGUN_SCORCHSHOT
};

class C_TFFlareGun : public C_TFWeaponBase
{
public:
	int GetFlareGunType()
	{
		return static_cast<int>(reinterpret_cast<float(__fastcall*)(float, const char*, void*, void*, bool)>(Signatures::AttribHookValue.Get())(0.f, "set_weapon_mode", this, nullptr, true));
	}
};
