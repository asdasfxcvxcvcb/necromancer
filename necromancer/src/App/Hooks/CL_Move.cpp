#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/NetworkFix/NetworkFix.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/ProjectileDodge/ProjectileDodge.h"
#include "../Features/Misc/AntiCheatCompat/AntiCheatCompat.h"
#include "../Features/AutoQueue/AutoQueue.h"
#include "../Features/FakeAngle/FakeAngle.h"

MAKE_SIGNATURE(CL_Move, "engine.dll", "40 55 53 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 83 3D", 0x0);

MAKE_HOOK(CL_Move, Signatures::CL_Move.Get(), void, __fastcall,
	float accumulated_extra_samples, bool bFinalTick)
{
	// Apply ping reducer BEFORE anything else
	F::NetworkFix->ApplyPingReducer();
	F::NetworkFix->ApplyAutoInterp();
	
	// Auto-queue
	F::AutoQueue->Run();

	// Ping reducer fix
	if (CFG::Misc_Ping_Reducer)
		F::NetworkFix->FixInputDelay(bFinalTick);

	// Seed prediction
	F::SeedPred->AskForPlayerPerf();

	// Calculate max ticks based on anti-cheat and anti-aim state
	// Anti-cheat: max 8 ticks
	// Anti-aim ON: max 22 ticks (need 2 for fakelag)
	// Anti-aim OFF: max 24 ticks
	int nMaxTicks;
	if (CFG::Misc_AntiCheat_Enabled)
	{
		nMaxTicks = 8;
	}
	else if (F::FakeAngle->AntiAimOn())
	{
		nMaxTicks = 22; // Reserve 2 ticks for anti-aim fakelag
	}
	else
	{
		nMaxTicks = 24; // Full 24 ticks when no anti-aim
	}

	// Handle recharging BEFORE the callOriginal lambda (like reference)
	if (Shifting::nAvailableTicks < nMaxTicks)
	{
		if (!Shifting::bRecharging && !Shifting::bShifting && !Shifting::bShiftingWarp && H::Entities->GetWeapon())
		{
			if (H::Input->IsDown(CFG::Exploits_Shifting_Recharge_Key))
			{
				if (!I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible())
					Shifting::bRecharging = true;
			}
		}

		if (Shifting::bRecharging)
		{
			Shifting::nAvailableTicks++;
			return;
		}
	}
	else
	{
		Shifting::bRecharging = false;
	}

	// Simple callOriginal - just calls the original function (like reference)
	auto callOriginal = [&](bool bFinal)
	{
		G::UpdatePathStorage();
		CALL_ORIGINAL(accumulated_extra_samples, bFinal);
	};

	// RapidFire/DoubleTap shifting
	if (Shifting::bRapidFireWantShift)
	{
		Shifting::bRapidFireWantShift = false;
		Shifting::bShifting = true;

		// Use configured ticks, capped by available
		const int nTicks = std::min(CFG::Exploits_RapidFire_Ticks, Shifting::nAvailableTicks);

		for (int n = 0; n < nTicks && Shifting::nAvailableTicks > 0; n++)
		{
			callOriginal(n == nTicks - 1);
			Shifting::nAvailableTicks--;
		}

		Shifting::bShifting = false;
		return;
	}

	const auto pLocal = H::Entities->GetLocal();
	if (pLocal && !pLocal->deadflag() && !Shifting::bRecharging && !Shifting::bShifting && !Shifting::bShiftingWarp)
	{
		// ProjectileDodge warp
		if (F::ProjectileDodge->bWantWarp && Shifting::nAvailableTicks > 0)
		{
			F::ProjectileDodge->bWantWarp = false;
			
			Shifting::bShifting = true;
			Shifting::bShiftingWarp = true;

			const int nTicks = Shifting::nAvailableTicks;
			for (int n = 0; n < nTicks; n++)
			{
				callOriginal(n == nTicks - 1);
				Shifting::nAvailableTicks--;
			}

			Shifting::bShifting = false;
			Shifting::bShiftingWarp = false;
			return;
		}
		
		// Manual warp
		if (H::Input->IsDown(CFG::Exploits_Warp_Key) && Shifting::nAvailableTicks > 0)
		{
			if (!I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible())
			{
				Shifting::bShifting = true;
				Shifting::bShiftingWarp = true;

				if (CFG::Exploits_Warp_Mode == 0)
				{
					// Slow warp - 2 ticks at a time
					for (int n = 0; n < 2 && Shifting::nAvailableTicks > 0; n++)
					{
						callOriginal(n == 1);
						Shifting::nAvailableTicks--;
					}
				}
				else
				{
					// Full warp - all ticks
					const int nTicks = Shifting::nAvailableTicks;
					for (int n = 0; n < nTicks; n++)
					{
						callOriginal(n == nTicks - 1);
						Shifting::nAvailableTicks--;
					}
				}

				Shifting::bShifting = false;
				Shifting::bShiftingWarp = false;
				return;
			}
		}
	}

	callOriginal(bFinalTick);
}
