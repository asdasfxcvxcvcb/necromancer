#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

MAKE_SIGNATURE(CBaseEntity_AddVar, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 57 41 56 41 57 48 83 EC ? 33 DB 48 89 74 24", 0x0);

MAKE_HOOK(CBaseEntity_AddVar, Signatures::CBaseEntity_AddVar.Get(), void, __fastcall,
	C_BaseEntity* ecx, void* data, IInterpolatedVar* watcher, int type, bool bSetup)
{
	// Safety check - skip if entity is invalid or during level transitions
	if (!ecx || G::bLevelTransition)
	{
		CALL_ORIGINAL(ecx, data, watcher, type, bSetup);
		return;
	}

	// Block interp vars if Disable Interp is on - choppy but accurate for aimbot
	if (CFG::Misc_Accuracy_Improvements && CFG::Visuals_Disable_Interp && watcher)
	{
		const auto hash = HASH_RT(watcher->GetDebugName());

		static constexpr auto m_iv_vecVelocity = HASH_CT("C_BaseEntity::m_iv_vecVelocity");
		static constexpr auto m_iv_angEyeAngles = HASH_CT("C_TFPlayer::m_iv_angEyeAngles");
		static constexpr auto m_iv_flPoseParameter = HASH_CT("C_BaseAnimating::m_iv_flPoseParameter");
		static constexpr auto m_iv_flCycle = HASH_CT("C_BaseAnimating::m_iv_flCycle");
		static constexpr auto m_iv_flMaxGroundSpeed = HASH_CT("CMultiPlayerAnimState::m_iv_flMaxGroundSpeed");

		if (hash == m_iv_vecVelocity
			|| hash == m_iv_flPoseParameter
			|| hash == m_iv_flCycle
			|| hash == m_iv_flMaxGroundSpeed)
			return;

		// Only check local player if we have a valid local player
		auto pLocal = H::Entities->GetLocal();
		if (pLocal && ecx != pLocal)
		{
			if (hash == m_iv_angEyeAngles)
				return;
		}
	}

	CALL_ORIGINAL(ecx, data, watcher, type, bSetup);
}

MAKE_HOOK(CBaseEntity_EstimateAbsVelocity, Signatures::CBaseEntity_EstimateAbsVelocity.Get(), void, __fastcall,
	C_BaseEntity* ecx, Vector& vel)
{
	// Safety check - skip if entity is invalid or during level transitions
	if (!ecx || G::bLevelTransition)
	{
		CALL_ORIGINAL(ecx, vel);
		return;
	}

	// Override velocity estimation if Disable Interp is on
	if (CFG::Misc_Accuracy_Improvements && CFG::Visuals_Disable_Interp)
	{
		// Safety: validate entity has a valid client class before calling GetClassId
		if (auto pNetworkable = ecx->GetClientNetworkable())
		{
			if (auto pClientClass = pNetworkable->GetClientClass())
			{
				if (pClientClass->m_ClassID == static_cast<int>(ETFClassIds::CTFPlayer))
				{
					if (const auto pPlayer = ecx->As<C_TFPlayer>())
					{
						vel = pPlayer->m_vecVelocity();
						return;
					}
				}
			}
		}
	}

	CALL_ORIGINAL(ecx, vel);
}
