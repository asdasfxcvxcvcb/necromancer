#pragma once
#include "c_baseprojectile.h"
#include "../../Utils/SignatureManager/SignatureManager.h"

MAKE_SIGNATURE(CBaseCombatWeapon_HasAmmo, "client.dll", "40 53 48 83 EC ? 83 B9 ? ? ? ? ? 48 8B D9 75 ? 83 B9 ? ? ? ? ? 74 ? 48 8B 01", 0x0);

class C_BaseCombatWeapon : public C_BaseAnimating
{
public:
	NETVAR(m_iClip1, int, "CBaseCombatWeapon", "m_iClip1");
	NETVAR(m_iClip2, int, "CBaseCombatWeapon", "m_iClip2");
	NETVAR(m_iPrimaryAmmoType, int, "CBaseCombatWeapon", "m_iPrimaryAmmoType");
	NETVAR(m_iSecondaryAmmoType, int, "CBaseCombatWeapon", "m_iSecondaryAmmoType");
	NETVAR(m_nViewModelIndex, int, "CBaseCombatWeapon", "m_nViewModelIndex");
	NETVAR(m_bFlipViewModel, bool, "CBaseCombatWeapon", "m_bFlipViewModel");
	NETVAR(m_flNextPrimaryAttack, float, "CBaseCombatWeapon", "m_flNextPrimaryAttack");
	NETVAR(m_flNextSecondaryAttack, float, "CBaseCombatWeapon", "m_flNextSecondaryAttack");
	NETVAR(m_nNextThinkTick, int, "CBaseCombatWeapon", "m_nNextThinkTick");
	NETVAR(m_flTimeWeaponIdle, float, "CBaseCombatWeapon", "m_flTimeWeaponIdle");
	NETVAR(m_iViewModelIndex, int, "CBaseCombatWeapon", "m_iViewModelIndex");
	NETVAR(m_iWorldModelIndex, int, "CBaseCombatWeapon", "m_iWorldModelIndex");
	NETVAR(m_iState, int, "CBaseCombatWeapon", "m_iState");
	NETVAR(m_hOwner, EHANDLE, "CBaseCombatWeapon", "m_hOwner");

	// Amalgam-style offset netvars
	bool& m_bReloadsSingly() {
		static int nOffset = NetVars::GetNetVar("CBaseCombatWeapon", "m_iClip2") + 24;
		return *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(this) + nOffset);
	}

	bool HasAmmo() {
		return reinterpret_cast<bool(__fastcall *)(void *)>(Signatures::CBaseCombatWeapon_HasAmmo.Get())(this);
	}

	// Amalgam virtual functions for faster reload detection
	void CheckReload() {
		return reinterpret_cast<void(__fastcall*)(void*)>(Memory::GetVFunc(this, 278))(this);
	}

	int GetMaxClip1() {
		return reinterpret_cast<int(__fastcall*)(void*)>(Memory::GetVFunc(this, 322))(this);
	}
};