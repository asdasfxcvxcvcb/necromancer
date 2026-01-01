#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

// External crit state from FireBullet hook
namespace CritTracerState
{
	extern bool g_bCurrentBulletIsCrit;
}

MAKE_SIGNATURE(CTFWeaponBase_GetTracerType, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 4C 8D 0D", 0x0);

// Helper function to get tracer name by index
static const char* GetTracerNameByIndex(int nIndex, C_TFPlayer* pLocal)
{
	switch (nIndex)
	{
		case 1: return pLocal->m_iTeamNum() == 2 ? "bullet_tracer_raygun_red" : "bullet_tracer_raygun_blue";
		case 2: return "dxhr_sniper_rail";
		case 3: return pLocal->m_iTeamNum() == 2 ? "dxhr_sniper_rail_red" : "dxhr_sniper_rail_blue";
		case 4: return pLocal->m_iTeamNum() == 2 ? "bullet_bignasty_tracer01_red" : "bullet_bignasty_tracer01_blue";
		case 5: return pLocal->m_iTeamNum() == 2 ? "dxhr_lightningball_hit_zap_red" : "dxhr_lightningball_hit_zap_blue";
		case 6: return "merasmus_zap";
		default: return nullptr;
	}
}

MAKE_HOOK(CTFWeaponBase_GetTracerType, Signatures::CTFWeaponBase_GetTracerType.Get(), const char*, __fastcall,
	C_TFWeaponBase* ecx)
{
	if (const auto pLocal = H::Entities->GetLocal())
	{
		if (const auto pWeapon = ecx)
		{
			if (pWeapon->m_hOwner().Get() == pLocal)
			{
				const bool bIsTF2DevsHorribleCode = pWeapon->m_iItemDefinitionIndex() == Sniper_m_TheMachina
					|| pWeapon->m_iItemDefinitionIndex() == Sniper_m_ShootingStar;

				// Check for crit tracers first (only applies to crit shots)
				if (CFG::Visuals_Crit_Tracer_Type > 0 && CritTracerState::g_bCurrentBulletIsCrit)
				{
					int nType = CFG::Visuals_Crit_Tracer_Type;
					if (nType > 6)
					{
						nType = SDKUtils::RandomInt(bIsTF2DevsHorribleCode ? 1 : 0, (nType == 8) ? 5 : 6);
					}
					
					if (const char* tracerName = GetTracerNameByIndex(nType, pLocal))
						return tracerName;
				}

				// Regular tracer effect
				if (const auto nType = CFG::Visuals_Tracer_Type)
				{
					int nFinalType = nType;
					if (nType > 6)
					{
						nFinalType = SDKUtils::RandomInt(bIsTF2DevsHorribleCode ? 1 : 0, (nType == 8) ? 5 : 6);
					}
					
					if (const char* tracerName = GetTracerNameByIndex(nFinalType, pLocal))
						return tracerName;
				}
			}
		}
	}

	return CALL_ORIGINAL(ecx);
}
