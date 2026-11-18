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
#include "../Features/RapidFire/RapidFire.h"
#include "../Features/Models/Models.h"

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
		F::Models->ConsumePendingFlush();
		CALL_ORIGINAL(accumulated_extra_samples, bFinalTick);
		return;

	}

	static auto sv_maxusrcmdprocessticks = I::CVar->FindVar("sv_maxusrcmdprocessticks");
	const int nBudgetCap = sv_maxusrcmdprocessticks ? std::max(sv_maxusrcmdprocessticks->GetInt(), 1) : 24;
	Shifting::nServerBudgetEstimate = std::min(Shifting::nServerBudgetEstimate + 1, nBudgetCap);

	F::RapidFire->MaintainWait();

	// Apply ping reducer BEFORE anything else
	// ping reducer good, maybe later i will incress the slider max
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
	// tried lerp angle more after silent aimbot fov 15, the idea works, just need a better AI and more fucks to give
	int nMaxTicks;
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
	{
		nMaxTicks = 8;
	}
	else
	{
		nMaxTicks = nBudgetCap;
	}

	if (F::FakeAngle->YawOn())
		nMaxTicks -= F::FakeAngle->AntiAimTicks();
	nMaxTicks = std::max(nMaxTicks, 1);

	// Apply user's recharge limit or auto settings. The slider may store past
	// the server budget - fast rev drains the bank across packets and every
	// spend path clamps itself; the anticheat profile keeps the budget ceiling
	const int nUserLimit = F::Ticks->GetOptimalRechargeLimit();
	const bool bAntiCheat = CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit;
	if (bAntiCheat)
		nMaxTicks = std::min(nMaxTicks, nUserLimit);
	const int nBankCap = bAntiCheat ? nMaxTicks : nUserLimit;

	auto callOriginal = [&](bool bFinal)
	{
		G::UpdatePathStorage();
		F::Networking->CL_Move(accumulated_extra_samples, bFinal);
	};

	// Handle recharging
	// When fast sticky sets a recharge target, stop at that count instead of the global max
	const int nRechargeCap = (Shifting::nStickyRechargeTarget > 0) ? std::min(nBankCap, Shifting::nStickyRechargeTarget) : nBankCap;

	if (Shifting::nAvailableTicks > nBankCap)
		Shifting::nAvailableTicks = nBankCap;

	if (Shifting::bRecharging && !H::Entities->GetWeapon())
	{
		Shifting::bRecharging = false;
	}

	{
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
			if (I::ClientState->chokedcommands)
			{
				callOriginal(bFinalTick);
				return;
			}
			if (Shifting::nAvailableTicks < nRechargeCap)
				Shifting::nAvailableTicks++;
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
	}

	// Sticky DT shifting
	{
		if (Shifting::bStickyDTWantShift)
		{
			if (I::ClientState->chokedcommands)
			{
				callOriginal(bFinalTick);
				return;
			}

			const auto pLocal = H::Entities->GetLocal();
			if (CFG::Misc_Projectile_Dodge_Enabled && CFG::Misc_Projectile_Dodge_Disable_DT_Airborne && pLocal && !(pLocal->m_fFlags() & FL_ONGROUND))
			{
				Shifting::bStickyDTWantShift = false;
				Shifting::nStickyDTTicksToUse = 0;
			}
			else
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
					Shifting::nShiftCooldownTicks = nTicks;

					for (int n = 0; n < nTicks && Shifting::nAvailableTicks > 0; n++)
					{
						Shifting::nCurrentShiftTick = n;
						Shifting::nAvailableTicks--;
						callOriginal(n == nTicks - 1);
					}

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
		}
	
		const auto pLocal = H::Entities->GetLocal();
		if (pLocal && !pLocal->deadflag() && !Shifting::bRecharging && !Shifting::bShifting && !Shifting::bShiftingWarp)
		{
			// ProjectileDodge warp
			if (F::ProjectileDodge->bWantWarp && Shifting::nAvailableTicks > 0)
			{
				if (I::ClientState->chokedcommands)
				{
					callOriginal(bFinalTick);
					return;
				}

				F::ProjectileDodge->bWantWarp = false;

				Shifting::bShifting = true;
				Shifting::bShiftingWarp = true;
				Shifting::flLastWarp = I::EngineClient->Time();

				const int nTicks = std::min(Shifting::nAvailableTicks, Shifting::nServerBudgetEstimate);
				Shifting::nShiftCooldownTicks = nTicks;
				for (int n = 0; n < nTicks; n++)
				{
					Shifting::nAvailableTicks--;
					callOriginal(n == nTicks - 1);
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
					if (I::ClientState->chokedcommands)
					{
						callOriginal(bFinalTick);
						return;
					}

					Shifting::bShifting = true;
					Shifting::bShiftingWarp = true;
					Shifting::flLastWarp = I::EngineClient->Time();

					if (CFG::Exploits_Warp_Mode == 0)
					{
						int nTicksSent = 0;
						const int nTicksToSend = std::min(std::min(2, Shifting::nAvailableTicks), Shifting::nServerBudgetEstimate);
						Shifting::nShiftCooldownTicks = nTicksToSend;
						for (int n = 0; n < nTicksToSend && Shifting::nAvailableTicks > 0; n++)
						{
							Shifting::nAvailableTicks--;
							callOriginal(n == nTicksToSend - 1);
							nTicksSent++;
						}
					}
					else
					{
						const int nTicks = std::min(Shifting::nAvailableTicks, Shifting::nServerBudgetEstimate);
						Shifting::nShiftCooldownTicks = nTicks;
						for (int n = 0; n < nTicks; n++)
						{
							Shifting::nAvailableTicks--;
							callOriginal(n == nTicks - 1);
						}
					}
	
					Shifting::bShifting = false;
					Shifting::bShiftingWarp = false;
					return;
				}
			}
		}
	}

	{
		callOriginal(bFinalTick);
	}

	// RapidFire/DoubleTap shifting - Amalgam's structure. The fire stays on
	// the aimbot's validated command, which RapidFire choke-held in this
	// frame's CreateMove (inside the call above). The batch commands below are
	// fresh CreateMove passes (the aimbot re-solves on each one, so whatever
	// command the server actually fires carries a solution validated for that
	// exact moment) and every one except the last is choked: the final call
	// releases the fire command plus the whole batch as ONE packet, whose
	// server-side clock correction backdates the fire and compresses the
	// cooldown. The aimbot runs during the batch like Amalgam's does.
	// Fast Rev anchors the minigun's wind-up command the same way: its
	// prediction sets the spin-up timer, and anchoring it into the backdated
	// batch completes the spin-up ~a bank's worth of ticks sooner.
	if ((Shifting::bRapidFireWantShift || Shifting::bFastRevWantShift) && !Shifting::bRecharging && !Shifting::bShifting && !Shifting::bShiftingWarp)
	{
		const bool bFastRevBatch = Shifting::bFastRevWantShift;
		Shifting::bRapidFireWantShift = false;
		Shifting::bFastRevWantShift = false;

		Shifting::bShifting = true;

		// the batch length must not shrink below the fire rate's clock span or
		// the second shell misses the batch - Amalgam caps theirs by
		// sv_maxusrcmdprocessticks-derived m_iMaxShift, not a running estimate,
		// and the burst (fire + fillers <= 15 new + 7 backup) fits the budget.
		// The estimate clamp here shaved the span by ticks depending on sag,
		// another "sometimes it double taps" source.
		// Fast rev is the exception: it spends the whole bank one full packet
		// per frame until the drain in RunFastRev stops it
		int nTicks = bFastRevBatch
			? std::min(Shifting::nAvailableTicks, std::max(MAX_NEW_COMMANDS + MAX_BACKUP_COMMANDS - I::ClientState->chokedcommands, 1))
			: std::min(F::RapidFire->GetReleaseTicks(), std::max(MAX_NEW_COMMANDS + MAX_BACKUP_COMMANDS - I::ClientState->chokedcommands, 1));

		if (nTicks > 0)
		{
			Shifting::nTotalShiftTicks = nTicks;
			Shifting::nCurrentShiftTick = 0;
			Shifting::bTickbaseFixRecorded = false;
			Shifting::nShiftCooldownTicks = nTicks;

			const int nBacklog = I::ClientState->chokedcommands;

			for (int n = 0; n < nTicks && Shifting::nAvailableTicks > 0; n++)
			{
				Shifting::nCurrentShiftTick = n;
				Shifting::nAvailableTicks--;
				callOriginal(n == nTicks - 1);
			}

			// the bank carries the whole burst's server-clock debt: the fillers
			// spent above plus the choked backlog (the fire command, silent
			// angle holds) riding the same packet - the bar then drops by
			// exactly what the DT Ticks slider counts
			Shifting::nAvailableTicks = std::max(Shifting::nAvailableTicks - nBacklog, 0);

			Shifting::bShifting = false;
			Shifting::nCurrentShiftTick = 0;
			Shifting::nTotalShiftTicks = 0;
			Shifting::bTickbaseFixRecorded = false;
		}
		else
			Shifting::bShifting = false;
	}
}
