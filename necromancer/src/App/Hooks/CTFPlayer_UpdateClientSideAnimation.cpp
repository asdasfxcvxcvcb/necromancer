#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/FakeAngle/FakeAngle.h"

MAKE_HOOK(CTFPlayer_UpdateClientSideAnimation, Signatures::CTFPlayer_UpdateClientSideAnimation.Get(), void, __fastcall,
	C_TFPlayer* ecx)
{
	if (CFG::Misc_Accuracy_Improvements)
	{
		if (const auto pLocal = H::Entities->GetLocal())
		{
			if (ecx == pLocal)
			{
				if (!pLocal->InCond(TF_COND_HALLOWEEN_KART))
				{
					if (const auto pWeapon = H::Entities->GetWeapon())
					{
						pWeapon->UpdateAllViewmodelAddons(); //credits: KGB
					}

					// Block normal animation updates for local player
					// Animation is handled in CreateMove's LocalAnimations section
					return;
				}
				CALL_ORIGINAL(ecx);
			}
		}

		// If Disable Interp is on, block normal animation updates for other players
		if (CFG::Visuals_Disable_Interp && !G::bUpdatingAnims)
		{
			return;
		}
	}

	CALL_ORIGINAL(ecx);
}
