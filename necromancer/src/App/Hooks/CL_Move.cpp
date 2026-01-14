#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/NetworkFix/NetworkFix.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/ProjectileDodge/ProjectileDodge.h"
#include "../Features/Misc/AntiCheatCompat/AntiCheatCompat.h"
#include "../Features/AutoQueue/AutoQueue.h"
#include "../Features/FakeAngle/FakeAngle.h"

MAKE_SIGNATURE(CL_Move, "engine.dll", "40 55 53 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 83 3D", 0x0);
MAKE_SIGNATURE(net_time, "engine.dll", "F2 0F 10 0D ? ? ? ? F2 0F 5C 0D", 0x4);

MAKE_HOOK(CL_Move, Signatures::CL_Move.Get(), void, __fastcall,
	float accumulated_extra_samples, bool bFinalTick)
{
	// Safety check - during level transitions, just call original
	if (G::bLevelTransition || !I::EngineClient)
	{
		CALL_ORIGINAL(accumulated_extra_samples, bFinalTick);
		return;
	}

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
	// Anti-cheat: max 8 ticks (unless Ignore Tick Limit is enabled)
	// Anti-aim ON: max 22 ticks (need 2 for fakelag)
	// Anti-aim OFF: max 24 ticks
	// User can also set a custom limit via Exploits_Shifting_Recharge_Limit (2-24)
	int nMaxTicks;
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
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
	
	// Apply user's recharge limit (clamp to 2-24 range)
	const int nUserLimit = std::clamp(CFG::Exploits_Shifting_Recharge_Limit, 2, 24);
	nMaxTicks = std::min(nMaxTicks, nUserLimit);

	// ============================================
	// Deficit Compensation (from Amalgam)
	// ============================================
	// If the server rejected commands (deficit > 0), reduce available ticks
	// This prevents getting stuck in a bad state after failed shifts
	if (CFG::Exploits_RapidFire_Deficit_Tracking && Shifting::nDeficit > 0)
	{
		// Reduce both deficit and available ticks
		Shifting::nDeficit--;
		if (Shifting::nAvailableTicks > 0)
			Shifting::nAvailableTicks--;
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
		Shifting::bShiftingRapidFire = true;

		// Use configured ticks, capped by available
		const int nTicks = std::min(CFG::Exploits_RapidFire_Ticks, Shifting::nAvailableTicks);
		
		// Set shift tracking for prediction fix
		Shifting::nTotalShiftTicks = nTicks;
		Shifting::nCurrentShiftTick = 0;

		for (int n = 0; n < nTicks && Shifting::nAvailableTicks > 0; n++)
		{
			Shifting::nCurrentShiftTick = n;
			callOriginal(n == nTicks - 1);
			Shifting::nAvailableTicks--;
		}

		Shifting::bShifting = false;
		Shifting::bShiftingRapidFire = false;
		Shifting::nCurrentShiftTick = 0;
		Shifting::nTotalShiftTicks = 0;
		
		// Clear deficit after successful shift (like Amalgam does for warp)
		Shifting::nDeficit = 0;
		
		return;
	}

	// Sticky DT shifting - uses calculated ticks (21 if available, otherwise what we have)
	if (Shifting::bStickyDTWantShift)
	{
		Shifting::bStickyDTWantShift = false;
		Shifting::bShifting = true;
		Shifting::bShiftingRapidFire = true;

		// Use the ticks calculated by RapidFire (21 if available, otherwise what we have)
		const int nTicks = std::min(Shifting::nStickyDTTicksToUse, Shifting::nAvailableTicks);
		Shifting::nStickyDTTicksToUse = 0; // Reset for next use
		
		// Set shift tracking for prediction fix
		Shifting::nTotalShiftTicks = nTicks;
		Shifting::nCurrentShiftTick = 0;

		for (int n = 0; n < nTicks && Shifting::nAvailableTicks > 0; n++)
		{
			Shifting::nCurrentShiftTick = n;
			callOriginal(n == nTicks - 1);
			Shifting::nAvailableTicks--;
		}

		Shifting::bShifting = false;
		Shifting::bShiftingRapidFire = false;
		Shifting::nCurrentShiftTick = 0;
		Shifting::nTotalShiftTicks = 0;
		
		// Clear deficit after successful shift
		Shifting::nDeficit = 0;
		
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

	// Optimal command rate - fixes doubletap on high ping servers
	// This limits how fast commands are sent to the server, preventing rejection
	// Check if we're connected and in game (m_nSignonState >= SIGNONSTATE_FULL which is 6)
	if (I::EngineClient->IsInGame() && I::ClientState->m_nSignonState >= 6)
	{
		// Get net_time from the signature (it's a pointer to a double)
		const double flNetTime = *reinterpret_cast<double*>(Signatures::net_time.Get());
		
		// Use optimal tick rate for command timing
		// This ensures commands are spaced properly even on high ping
		I::ClientState->m_flNextCmdTime = flNetTime + I::GlobalVars->interval_per_tick;
	}
	else
	{
		const double flNetTime = *reinterpret_cast<double*>(Signatures::net_time.Get());
		I::ClientState->m_flNextCmdTime = flNetTime + 0.2;
	}
}
