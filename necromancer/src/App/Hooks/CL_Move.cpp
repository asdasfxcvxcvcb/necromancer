#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/NetworkFix/NetworkFix.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/ProjectileDodge/ProjectileDodge.h"
#include "../Features/Misc/AntiCheatCompat/AntiCheatCompat.h"
#include "../Features/AutoQueue/AutoQueue.h"
#include "../Features/FakeAngle/FakeAngle.h"
#include "../Features/TickbaseManip/TickbaseManip.h"
#include "../Features/Networking/Networking.h"

MAKE_SIGNATURE(CL_Move, "engine.dll", "40 55 53 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 83 3D", 0x0);

MAKE_HOOK(CL_Move, Signatures::CL_Move.Get(), void, __fastcall,
	float accumulated_extra_samples, bool bFinalTick)
{
	// During signon (not fully in game), call the original engine CL_Move.
	// The rebuilt CL_Move has subtle differences in signon handling that can
	// cause "Parsing game info" to hang indefinitely. Only use the rebuilt
	// version when fully in-game for tick manipulation (shifting/recharging).
	// blizzman here, only god and i know how this files work
	// blizzman here, only god knows how this file works (hours spent fixing issues: 20H)
	if (!I::ClientState->IsActive())
	{
		CALL_ORIGINAL(accumulated_extra_samples, bFinalTick);
		return;
	}

	// Apply ping reducer BEFORE anything else
	// ping reducer good, maybe later i will incress the slider max
	F::NetworkFix->ApplyPingReducer();
	F::NetworkFix->ApplyAutoInterp();
	
	// Auto-queue
	// not pasted from amalgam
	F::AutoQueue->Run();

	// Ping reducer fix
	if (CFG::Misc_Ping_Reducer)
		F::NetworkFix->FixInputDelay(bFinalTick);

	// Seed prediction
	F::SeedPred->AskForPlayerPerf();

	// Calculate max ticks based on anti-cheat and anti-aim state
	// tried lerp angle more after silent aimbot fov 15, the idea works, just need a better AI and more fucks to give
	int nMaxTicks;
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
	{
		nMaxTicks = 8;
	}
	else if (F::FakeAngle->AntiAimOn())
	{
		nMaxTicks = 22;
	}
	else
	{
		nMaxTicks = 24;
	}
	
	// Apply user's recharge limit or auto settings
	const int nUserLimit = F::Ticks->GetOptimalRechargeLimit();
	nMaxTicks = std::min(nMaxTicks, nUserLimit);

	// Deficit Compensation — reduce available ticks when server drops commands
	if (Shifting::nDeficit > 0)
	{
		Shifting::nDeficit--;
		if (Shifting::nAvailableTicks > 0)
			Shifting::nAvailableTicks--;
	}

	auto callOriginal = [&](bool bFinal)
	{
		G::UpdatePathStorage();
		F::Networking->CL_Move(accumulated_extra_samples, bFinal);
	};

	// Handle recharging
	// When fast sticky sets a recharge target, stop at that count instead of the global max
	const int nRechargeCap = (Shifting::nStickyRechargeTarget > 0) ? std::min(nMaxTicks, Shifting::nStickyRechargeTarget) : nMaxTicks;

	if (Shifting::nAvailableTicks < nRechargeCap)
	{
		if (!Shifting::bRecharging && !Shifting::bShifting && !Shifting::bShiftingWarp && H::Entities->GetWeapon())
		{
			if (H::Input->IsDown(CFG::Exploits_Shifting_Recharge_Key))
			{
				if (!I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible())
				{
					Shifting::bRecharging = true;
				}
			}
		}

		if (Shifting::bRecharging)
		{
			Shifting::nAvailableTicks++;
			if (I::ClientState->chokedcommands)
				callOriginal(bFinalTick);
			return;
		}
	}
	else
	{
		Shifting::bRecharging = false;
		// Clear sticky recharge target once we've reached it
		if (Shifting::nStickyRechargeTarget > 0 && Shifting::nAvailableTicks >= Shifting::nStickyRechargeTarget)
			Shifting::nStickyRechargeTarget = 0;
	}

	// RapidFire/DoubleTap shifting
	if (Shifting::bRapidFireWantShift)
	{
		Shifting::bRapidFireWantShift = false;
		Shifting::bShifting = true;

		// Amalgam-style: account for choked commands eating server budget
		const int nTicks = std::min(CFG::Exploits_RapidFire_Ticks, F::Ticks->GetProcessableTicks());

		if (nTicks > 0)
		{
			Shifting::nTotalShiftTicks = nTicks;
			Shifting::nCurrentShiftTick = 0;
			Shifting::bTickbaseFixRecorded = false;

			for (int n = 0; n < nTicks && Shifting::nAvailableTicks > 0; n++)
			{
				Shifting::nCurrentShiftTick = n;
				Shifting::nAvailableTicks--;
				callOriginal(n == nTicks - 1);
			}

			Shifting::OnCommandsSent(nTicks);

			Shifting::bShifting = false;
			Shifting::nCurrentShiftTick = 0;
			Shifting::nTotalShiftTicks = 0;
			Shifting::bTickbaseFixRecorded = false;
		}
		else
		{
			Shifting::bShifting = false;
		}

		return;
	}

	// Sticky DT shifting
	if (Shifting::bStickyDTWantShift)
	{
		Shifting::bStickyDTWantShift = false;
		Shifting::bShifting = true;

		const int nTicks = std::min(Shifting::nStickyDTTicksToUse, F::Ticks->GetProcessableTicks());
		Shifting::nStickyDTTicksToUse = 0;

		if (nTicks > 0)
		{
			Shifting::nTotalShiftTicks = nTicks;
			Shifting::nCurrentShiftTick = 0;
			Shifting::bTickbaseFixRecorded = false;

			for (int n = 0; n < nTicks && Shifting::nAvailableTicks > 0; n++)
			{
				Shifting::nCurrentShiftTick = n;
				Shifting::nAvailableTicks--;
				callOriginal(n == nTicks - 1);
			}

			Shifting::OnCommandsSent(nTicks);

			Shifting::bShifting = false;
			Shifting::nCurrentShiftTick = 0;
			Shifting::nTotalShiftTicks = 0;
			Shifting::bTickbaseFixRecorded = false;
		}
		else
		{
			Shifting::bShifting = false;
		}

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
				Shifting::nAvailableTicks--;
				callOriginal(n == nTicks - 1);
			}

			Shifting::OnCommandsSent(nTicks);

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
					int nTicksSent = 0;
					const int nTicksToSend = std::min(2, Shifting::nAvailableTicks);
					for (int n = 0; n < nTicksToSend && Shifting::nAvailableTicks > 0; n++)
					{
						Shifting::nAvailableTicks--;
						callOriginal(n == nTicksToSend - 1);
						nTicksSent++;
					}
					Shifting::OnCommandsSent(nTicksSent);
				}
				else
				{
					const int nTicks = Shifting::nAvailableTicks;
					for (int n = 0; n < nTicks; n++)
					{
						Shifting::nAvailableTicks--;
						callOriginal(n == nTicks - 1);
					}
					Shifting::OnCommandsSent(nTicks);
				}

				Shifting::bShifting = false;
				Shifting::bShiftingWarp = false;
				return;
			}
		}
	}

	callOriginal(bFinalTick);
}
