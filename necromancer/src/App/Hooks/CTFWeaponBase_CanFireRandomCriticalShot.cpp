#include "../../SDK/SDK.h"
#include "../Features/Crits/Crits.h"

MAKE_SIGNATURE(CTFWeaponBase_CanFireRandomCriticalShot, "client.dll", "F3 0F 58 0D ? ? ? ? 0F 2F 89", 0x0);

MAKE_HOOK(
	CTFWeaponBase_CanFireRandomCriticalShot, Signatures::CTFWeaponBase_CanFireRandomCriticalShot.Get(), bool, __fastcall,
	void* rcx, float flCritChance)
{
	int nRandomRangedCritDamage = static_cast<int>(F::CritHack->GetCritDamage());
	int nTotalDamage = static_cast<int>(F::CritHack->GetRangedDamage());
	if (!nTotalDamage)
		return true;

	auto pWeapon = reinterpret_cast<C_TFWeaponBase*>(rcx);
	constexpr float flCritMultiplier = 3.0f;
	float flNormalizedDamage = nRandomRangedCritDamage / flCritMultiplier;
	pWeapon->m_flObservedCritChance() = flNormalizedDamage / (flNormalizedDamage + (nTotalDamage - nRandomRangedCritDamage));

	return CALL_ORIGINAL(rcx, flCritChance);
}
