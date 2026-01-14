#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/LagRecords/LagRecords.h"

MAKE_SIGNATURE(CBaseAnimating_SetupBones, "client.dll", "48 8B C4 44 89 40 ? 48 89 50 ? 55 53", 0x0);

//this here belongs to boss
//bless boss
//blizzman here, fuck you boss, this shit made the cheat crash, ive spent 8 hours trying to fix the crash and it was just you asshole cant vaildiate if we are optimizing a vaild player

C_BaseEntity* GetRootMoveParent(C_BaseEntity* baseEnt)
{
	if (!baseEnt)
		return nullptr;

	auto pEntity = baseEnt;
	auto pParent = baseEnt->GetMoveParent();

	int its = 0;

	while (pParent)
	{
		if (its++ > 32)
			break;

		// Validate parent is a valid entity before using it
		if (!pParent->GetClientNetworkable())
			break;

		pEntity = pParent;
		pParent = pEntity->GetMoveParent();
	}

	return pEntity;
}

MAKE_HOOK(CBaseAnimating_SetupBones, Signatures::CBaseAnimating_SetupBones.Get(), bool, __fastcall,
	C_BaseAnimating* ecx, matrix3x4_t* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime)
{
	// Safety check - skip custom logic during level transitions or invalid entity
	if (G::bLevelTransition || !ecx)
		return CALL_ORIGINAL(ecx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	// Use cached bones if Disable Interp is on - choppy but accurate for aimbot
	if (CFG::Misc_SetupBones_Optimization && CFG::Visuals_Disable_Interp && !F::LagRecords->IsSettingUpBones())
	{
		// ecx is C_BaseAnimating*, we need to get C_BaseEntity* by adjusting for vtable
		const auto baseEnt = reinterpret_cast<C_BaseEntity*>(reinterpret_cast<uintptr_t>(ecx) - sizeof(uintptr_t));

		if (baseEnt)
		{
			// Validate entity before any operations
			auto pNetworkable = baseEnt->GetClientNetworkable();
			if (!pNetworkable)
				return CALL_ORIGINAL(ecx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

			auto pClientClass = pNetworkable->GetClientClass();
			if (!pClientClass)
				return CALL_ORIGINAL(ecx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

			// Only optimize for CTFPlayer entities
			if (pClientClass->m_ClassID != static_cast<int>(ETFClassIds::CTFPlayer))
				return CALL_ORIGINAL(ecx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

			// Get root parent if entity is attached to something
			const auto owner = GetRootMoveParent(baseEnt);
			const auto ent = owner ? owner : baseEnt;

			// Re-validate after getting root parent
			pNetworkable = ent->GetClientNetworkable();
			if (!pNetworkable)
				return CALL_ORIGINAL(ecx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

			pClientClass = pNetworkable->GetClientClass();
			if (!pClientClass || pClientClass->m_ClassID != static_cast<int>(ETFClassIds::CTFPlayer))
				return CALL_ORIGINAL(ecx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

			auto pLocal = H::Entities->GetLocal();
			if (pLocal && ent != pLocal)
			{
				// Check if entity has a valid model before accessing bones
				if (!ent->GetModel())
					return CALL_ORIGINAL(ecx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

				if (pBoneToWorldOut && nMaxBones > 0)
				{
					const auto pAnimating = ent->As<C_BaseAnimating>();
					if (!pAnimating)
						return CALL_ORIGINAL(ecx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

					const auto bones = pAnimating->GetCachedBoneData();
					
					// Validate bone cache is valid and has data
					if (bones && bones->Count() > 0 && bones->Base())
					{
						const int copyCount = std::min(nMaxBones, bones->Count());
						if (copyCount > 0)
						{
							std::memcpy(pBoneToWorldOut, bones->Base(), sizeof(matrix3x4_t) * copyCount);
							return true;
						}
					}
				}
				
				// If we couldn't use cached bones, fall through to original
			}
		}
	}

	return CALL_ORIGINAL(ecx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);
}
