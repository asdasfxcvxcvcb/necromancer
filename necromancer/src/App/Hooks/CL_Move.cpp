#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/NetworkFix/NetworkFix.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/ProjectileDodge/ProjectileDodge.h"
#include "../Features/Misc/AntiCheatCompat/AntiCheatCompat.h"
#include "../Features/AutoQueue/AutoQueue.h"
#include "../Features/FakeAngle/FakeAngle.h"

MAKE_SIGNATURE(CL_Move, "engine.dll", "40 55 53 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 83 3D", 0x0);

// Max ticks for the shifting system
constexpr int MAX_SHIFT_TICKS = 24;
constexpr int ANTIAIM_TICKS = 2;

// Track if anti-aim was active last frame to detect when it's enabled
static bool s_bWasAntiAimActive = false;

MAKE_HOOK(CL_Move, Signatures::CL_Move.Get(), void, __fastcall,
	float accumulated_extra_samples, bool bFinalTick)
{
	// Apply ping reducer BEFORE the lambda (like Amalgam)
	F::NetworkFix->ApplyPingReducer();
	F::NetworkFix->ApplyAutoInterp();
	
	// Auto-queue BEFORE tick processing
	F::AutoQueue->Run();
	
	// Calculate max ticks (like Amalgam's Ticks.Move)
	static auto sv_maxusrcmdprocessticks = I::CVar->FindVar("sv_maxusrcmdprocessticks");
	int nServerMaxTicks = sv_maxusrcmdprocessticks ? sv_maxusrcmdprocessticks->GetInt() : MAX_COMMANDS;
	
	// Check if anti-aim is active
	bool bAntiAimActive = F::FakeAngle->AntiAimOn();
	
	// If anti-aim was just enabled and we have more ticks than allowed, consume the excess
	if (bAntiAimActive && !s_bWasAntiAimActive)
	{
		int nMaxWithAntiAim = MAX_SHIFT_TICKS - ANTIAIM_TICKS;
		
		// If we have more ticks than allowed with anti-aim, consume the excess
		if (Shifting::nAvailableTicks > nMaxWithAntiAim)
		{
			Shifting::nAvailableTicks = nMaxWithAntiAim;
		}
	}
	s_bWasAntiAimActive = bAntiAimActive;
	
	// Calculate effective max ticks
	// When anti-aim is OFF: can store up to 24 ticks
	// When anti-aim is ON: can store up to 22 ticks (24 - 2 for anti-aim)
	int nMaxTicks = std::min(nServerMaxTicks, MAX_SHIFT_TICKS);
	
	// Reserve ticks for anti-aim when it's enabled
	int nMaxRechargeTicks = bAntiAimActive ? (nMaxTicks - ANTIAIM_TICKS) : nMaxTicks;
	nMaxRechargeTicks = std::max(nMaxRechargeTicks, 0); // Safety clamp
	
	auto callOriginal = [&](bool bFinal)
	{
		// Update path storage (like Amalgam)
		G::UpdatePathStorage();

		if (CFG::Misc_Ping_Reducer)
			F::NetworkFix->FixInputDelay(bFinal);

		F::SeedPred->AskForPlayerPerf();

		// Recharging is limited by nMaxRechargeTicks (reserves ticks for anti-aim)
		if (Shifting::nAvailableTicks < nMaxRechargeTicks)
		{
			if (H::Entities->GetWeapon())
			{
				if (!Shifting::bRecharging && !Shifting::bShifting && !Shifting::bShiftingWarp)
				{
					if (H::Input->IsDown(CFG::Exploits_Shifting_Recharge_Key))
					{
						Shifting::bRecharging = !I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible();
					}
				}
			}

			if (Shifting::bRecharging)
			{
				if (Shifting::nAvailableTicks >= nMaxRechargeTicks)
				{
					Shifting::bRecharging = false;
				}
				else
				{
					Shifting::nAvailableTicks++;
					return;
				}
			}
		}
		else
		{
			Shifting::bRecharging = false;
		}

		CALL_ORIGINAL(accumulated_extra_samples, bFinal);
	};

	// RapidFire shifting
	if (Shifting::bRapidFireWantShift)
	{
		Shifting::bRapidFireWantShift = false;
		Shifting::bShifting = true;
		Shifting::bShiftingRapidFire = true;

		// Calculate ticks to shift
		// If slider is at 23 (MAX), use all available ticks
		int nTicks;
		if (CFG::Exploits_RapidFire_Ticks >= 23)
		{
			nTicks = std::min(Shifting::nAvailableTicks, nMaxRechargeTicks);
		}
		else
		{
			nTicks = std::min(CFG::Exploits_RapidFire_Ticks, nMaxRechargeTicks);
			nTicks = std::min(nTicks, Shifting::nAvailableTicks);
		}
		
		for (int n = 0; n < nTicks; n++)
		{
			callOriginal(n == nTicks - 1);
			Shifting::nAvailableTicks--;
		}

		Shifting::bShifting = false;
		Shifting::bShiftingRapidFire = false;
		return;
	}

	if (const auto pLocal = H::Entities->GetLocal())
	{
		if (!pLocal->deadflag() && !Shifting::bRecharging && !Shifting::bShifting && !Shifting::bShiftingWarp && !Shifting::bRapidFireWantShift)
		{
			// ProjectileDodge warp
			if (F::ProjectileDodge->bWantWarp && Shifting::nAvailableTicks)
			{
				F::ProjectileDodge->bWantWarp = false;
				
				Shifting::bShifting = true;
				Shifting::bShiftingWarp = true;

				int nTicks = std::min(Shifting::nAvailableTicks, nMaxRechargeTicks);

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
			if (!I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible() && H::Input->IsDown(CFG::Exploits_Warp_Key))
			{
				if (Shifting::nAvailableTicks)
				{
					Shifting::bShifting = true;
					Shifting::bShiftingWarp = true;

					if (CFG::Exploits_Warp_Mode == 0)
					{
						for (int n = 0; n < 2; n++)
							callOriginal(n == 1);
						Shifting::nAvailableTicks--;
					}

					if (CFG::Exploits_Warp_Mode == 1)
					{
						int nTicks = std::min(Shifting::nAvailableTicks, nMaxRechargeTicks);

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
	}

	callOriginal(bFinalTick);
}
