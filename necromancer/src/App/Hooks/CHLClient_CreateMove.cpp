#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

#include "../Features/Aimbot/Aimbot.h"
#include "../Features/Aimbot/AimbotProjectile/AimbotProjectile.h"
#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/Misc/Misc.h"
#include "../Features/MiscVisuals/MiscVisuals.h"
#include "../Features/RapidFire/RapidFire.h"
#include "../Features/Triggerbot/Triggerbot.h"
#include "../Features/Triggerbot/AutoVaccinator/AutoVaccinator.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/Crits/Crits.h"
#include "../Features/FakeLag/FakeLag.h"
#include "../Features/FakeAngle/FakeAngle.h"
#include "../Features/ProjectileDodge/ProjectileDodge.h"
#include "../Features/Misc/AntiCheatCompat/AntiCheatCompat.h"
#include "../Features/amalgam_port/AmalgamCompat.h"
#include "../Features/amalgam_port/Ticks/Ticks.h"

// Taunt delay processing - defined in IVEngineClient013_ClientCmd.cpp
extern void ProcessTauntDelay();

MAKE_SIGNATURE(ValidateUserCmd_, "client.dll", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F9 41 8B D8", 0, 0);
MAKE_HOOK(ValidateUserCmd, Signatures::ValidateUserCmd_.Get(), void, __fastcall, void* rcx, CUserCmd* cmd,
	int sequence_number) {
	return;
}

// Local animations - Amalgam style
// This updates the local player's animation state based on the cmd angles
// The fake model uses separate bones set up in SetupFakeModel
static inline void LocalAnimations(C_TFPlayer* pLocal, CUserCmd* pCmd, bool bSendPacket)
{
	static std::vector<Vec3> vAngles = {};
	
	// Push the current cmd angles (after anti-aim modified them)
	// On choked ticks: these are REAL angles
	// On send tick: these are FAKE angles
	vAngles.push_back(pCmd->viewangles);

	auto pAnimState = pLocal->GetAnimState();
	if (bSendPacket && pAnimState)
	{
		float flOldFrametime = I::GlobalVars->frametime;
		float flOldCurtime = I::GlobalVars->curtime;
		I::GlobalVars->frametime = TICK_INTERVAL;
		I::GlobalVars->curtime = TICKS_TO_TIME(pLocal->m_nTickBase());

		for (auto& vAngle : vAngles)
		{
			if (pLocal->InCond(TF_COND_TAUNTING) && pLocal->m_bAllowMoveDuringTaunt())
				pLocal->m_flTauntYaw() = vAngle.y;
			pAnimState->m_flEyeYaw = vAngle.y;
			pAnimState->Update(vAngle.y, vAngle.x);
			pLocal->FrameAdvance(TICK_INTERVAL);
		}

		I::GlobalVars->frametime = flOldFrametime;
		I::GlobalVars->curtime = flOldCurtime;
		vAngles.clear();

		// Setup fake model bones AFTER animation update (like Amalgam)
		F::FakeAngle->SetupFakeModel(pLocal);
	}
}

// Anti-aim packet check (like Amalgam's AntiAimCheck in PacketManip)
// Only choke for anti-aim if we're not attacking
static inline bool AntiAimCheck(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	// Regular anti-aim check
	bool bAntiAim = F::FakeAngle->YawOn()
		&& F::FakeAngle->ShouldRun(pLocal, pWeapon, pCmd)
		&& !Shifting::bRecharging
		&& I::ClientState->chokedcommands < F::FakeAngle->AntiAimTicks();
	
	return bAntiAim;
}

MAKE_HOOK(CHLClient_Createmove, Memory::GetVFunc(I::ClientModeShared, 21), bool, __fastcall,
	CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd)
{
	// Reset per-frame state
	G::bSilentAngles = false;
	G::bPSilentAngles = false;
	G::bFiring = false;
	G::Attacking = 0;
	G::Throwing = false;
	G::LastUserCmd = G::CurrentUserCmd ? G::CurrentUserCmd : pCmd;
	G::CurrentUserCmd = pCmd;
	G::OriginalCmd = *pCmd;
	
	// Store TRUE original angles before ANY modification
	// This is used for view restoration and should NEVER be modified by anti-cheat
	G::vTrueOriginalAngles = pCmd->viewangles;
	G::bHasTrueOriginalAngles = true;

	if (!pCmd || !pCmd->command_number)
		return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);

	// Block player movement when freecam is active
	if (F::MiscVisuals->IsFreecamActive())
	{
		pCmd->forwardmove = 0.0f;
		pCmd->sidemove = 0.0f;
		pCmd->upmove = 0.0f;
		pCmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_JUMP | IN_DUCK);
	}

	// ============================================
	// TAUNT DELAY HANDLING - Process pending taunt with tick delay
	// ============================================
	ProcessTauntDelay();

	CUserCmd* pBufferCmd = I::Input->GetUserCmd(pCmd->command_number);
	if (!pBufferCmd)
		pBufferCmd = pCmd;

	I::Prediction->Update(
		I::ClientState->m_nDeltaTick,
		I::ClientState->m_nDeltaTick > 0,
		I::ClientState->last_command_ack,
		I::ClientState->lastoutgoingcommand + I::ClientState->chokedcommands
	);

	// Update tick tracking for doubletap high ping compensation
	{
		float flLatency = 0.0f;
		if (const auto pNetChannel = I::EngineClient->GetNetChannelInfo())
		{
			if (!pNetChannel->IsLoopback())
				flLatency = pNetChannel->GetAvgLatency(FLOW_OUTGOING);
		}
		Shifting::UpdateTickTracking(
			I::ClientState->m_nDeltaTick,
			I::ClientState->last_command_ack,
			flLatency
		);
	}

	F::AutoVaccinator->PreventReload(pCmd);

	// Run AutoVaccinator early if Always On
	if (CFG::Triggerbot_AutoVaccinator_Always_On)
	{
		auto pLocalVacc = H::Entities->GetLocal();
		auto pWeaponVacc = H::Entities->GetWeapon();
		if (pLocalVacc && !pLocalVacc->deadflag() && pWeaponVacc)
			F::AutoVaccinator->Run(pLocalVacc, pWeaponVacc, pCmd);
	}

	// RapidFire early exit
	if (F::RapidFire->ShouldExitCreateMove(pCmd))
	{
		auto pLocal = H::Entities->GetLocal();
		auto pWeapon = H::Entities->GetWeapon();
		if (pLocal && pWeapon && !pLocal->deadflag())
		{
			F::CritHack->Run(pLocal, pWeapon, pCmd);
		}

		return F::RapidFire->GetShiftSilentAngles() ? false : CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);
	}

	if (Shifting::bRecharging)
	{
		if (pCmd->buttons & IN_JUMP)
			pCmd->buttons &= ~IN_JUMP;
		return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);
	}

	bool* pSendPacket = reinterpret_cast<bool*>(uintptr_t(_AddressOfReturnAddress()) + 0x128);

	// Cache original angles/movement for pSilent restoration
	const Vec3 vOldAngles = pCmd->viewangles;
	const float flOldSide = pCmd->sidemove;
	const float flOldForward = pCmd->forwardmove;

	auto pLocal = H::Entities->GetLocal();
	auto pWeapon = H::Entities->GetWeapon();

	// Early check: Temporarily disable Legit AA when engineer tries to pick up a building
	// This must run BEFORE anti-aim so the pickup works on the first try
	{
		static bool bDisabledForBuildingPickup = false;

		if (pLocal && pLocal->m_iClass() == TF_CLASS_ENGINEER)
		{
			// Check if we're currently carrying a building
			const bool bCarryingBuilding = pLocal->m_bCarryingObject();

			// Re-enable Legit AA once we've picked up the building
			if (bDisabledForBuildingPickup && bCarryingBuilding)
			{
				CFG::Exploits_LegitAA_Enabled = true;
				bDisabledForBuildingPickup = false;
			}

			// Disable Legit AA when trying to pick up a building (attack2 while looking at own building)
			if (CFG::Exploits_LegitAA_Enabled && !bCarryingBuilding && (pCmd->buttons & IN_ATTACK2))
			{
				// Trace to see if we're looking at our own building
				Vec3 vStart = pLocal->GetShootPos();
				Vec3 vForward;
				Math::AngleVectors(pCmd->viewangles, &vForward);
				Vec3 vEnd = vStart + vForward * 150.0f; // Building pickup range is ~150 units

				CGameTrace trace;
				CTraceFilterHitscan filter;
				filter.m_pIgnore = pLocal;
				SDK::Trace(vStart, vEnd, MASK_SOLID, &filter, &trace);

				if (trace.m_pEnt)
				{
					const auto nClassId = trace.m_pEnt->GetClassId();
					// Check if it's a building (sentry, dispenser, teleporter)
					if (nClassId == ETFClassIds::CObjectSentrygun ||
						nClassId == ETFClassIds::CObjectDispenser ||
						nClassId == ETFClassIds::CObjectTeleporter)
					{
						auto pBuilding = trace.m_pEnt->As<C_BaseObject>();
						// Check if it's our building
						if (pBuilding && pBuilding->m_hBuilder() == pLocal)
						{
							CFG::Exploits_LegitAA_Enabled = false;
							bDisabledForBuildingPickup = true;
						}
					}
				}
			}
		}
		else
		{
			// Not engineer anymore, reset state
			bDisabledForBuildingPickup = false;
		}
	}

	// Reset state
	G::bFiring = false;
	G::bCanPrimaryAttack = false;
	G::bCanSecondaryAttack = false;
	G::bReloading = false;

	if (pLocal && pWeapon)
	{
		G::bCanHeadshot = pWeapon->CanHeadShot(pLocal);

		// Amalgam's CheckReload trick
		if (pWeapon->GetMaxClip1() != -1 && !pWeapon->m_bReloadsSingly())
		{
			float flOldCurtime = I::GlobalVars->curtime;
			I::GlobalVars->curtime = TICKS_TO_TIME(pLocal->m_nTickBase());
			pWeapon->CheckReload();
			I::GlobalVars->curtime = flOldCurtime;
		}

		G::bCanPrimaryAttack = pWeapon->CanPrimaryAttack(pLocal);
		G::bCanSecondaryAttack = pWeapon->CanSecondaryAttack(pLocal);

		// Minigun special handling - only can attack when spun up
		if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
		{
			int iState = pWeapon->As<C_TFMinigun>()->m_iWeaponState();
			// Only allow attack when in FIRING or SPINNING state with ammo
			if (iState == AC_STATE_IDLE || iState == AC_STATE_STARTFIRING || !pWeapon->HasPrimaryAmmoForShot())
				G::bCanPrimaryAttack = false;
		}

		// Non-melee weapon reload state
		if (pWeapon->GetSlot() != WEAPON_SLOT_MELEE)
		{
			bool bAmmo = pWeapon->HasPrimaryAmmoForShot();
			bool bReload = pWeapon->IsInReload();

			if (!bAmmo)
			{
				G::bCanPrimaryAttack = false;
				G::bCanSecondaryAttack = false;
			}

			if (bReload && bAmmo && !G::bCanPrimaryAttack)
				G::bReloading = true;
		}

		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, false);
	}
	else
	{
		G::bCanHeadshot = false;
	}

	// Track ticks since can fire
	// For weapons that reload singly OR can interrupt reload (like SMG), we track ticks even during reload if we have ammo
	// because pressing attack will interrupt the reload and allow firing
	{
		static bool bOldCanFire = G::bCanPrimaryAttack;
		
		// Check if we can fire during reload
		bool bCanFireDuringReload = false;
		if (pWeapon && G::bReloading && pWeapon->m_iClip1() > 0)
		{
			// Single-reload weapons (shotguns, etc.)
			if (pWeapon->m_bReloadsSingly())
			{
				bCanFireDuringReload = true;
			}
			// SMG and other weapons that can interrupt reload
			else
			{
				const int nWeaponID = pWeapon->GetWeaponID();
				if (nWeaponID == TF_WEAPON_SMG || nWeaponID == TF_WEAPON_CHARGED_SMG ||
					nWeaponID == TF_WEAPON_PISTOL || nWeaponID == TF_WEAPON_PISTOL_SCOUT ||
					nWeaponID == TF_WEAPON_MINIGUN)
				{
					bCanFireDuringReload = true;
				}
			}
		}
		
		// Effective "can fire" state includes reload interrupt capability
		bool bEffectiveCanFire = G::bCanPrimaryAttack || bCanFireDuringReload;
		
		if (bEffectiveCanFire != bOldCanFire)
		{
			G::nTicksSinceCanFire = 0;
			bOldCanFire = bEffectiveCanFire;
		}
		else
		{
			if (bEffectiveCanFire)
				G::nTicksSinceCanFire++;
			else
				G::nTicksSinceCanFire = 0;
		}
	}

	if (!pLocal)
		return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);

	// ============================================
	// AMALGAM ORDER: Misc features first
	// ============================================
	F::Misc->Bunnyhop(pCmd);
	F::Misc->AutoStrafer(pCmd);
	F::Misc->FastStop(pCmd);
	F::Misc->FastAccelerate(pCmd);
	F::Misc->NoiseMakerSpam();
	F::Misc->AutoRocketJump(pCmd);
	F::Misc->AutoFaN(pCmd);
	F::Misc->AutoUber(pCmd);
	F::Misc->AutoDisguise(pCmd);
	F::Misc->MovementLock(pCmd);
	F::Misc->MvmInstaRespawn();
	F::Misc->AntiAFK(pCmd);

	// Projectile Dodge
	F::ProjectileDodge->Run(pLocal, pCmd);

	// ============================================
	// AMALGAM ORDER: Engine Prediction Start
	// ============================================
	F::EnginePrediction->Start(pLocal, pCmd);
	{
		// Choke on bhop
		if (CFG::Misc_Choke_On_Bhop && CFG::Misc_Bunnyhop)
		{
			if ((pLocal->m_fFlags() & FL_ONGROUND) && !(F::EnginePrediction->flags & FL_ONGROUND))
				*pSendPacket = false;
		}

		F::Misc->CrouchWhileAirborne(pCmd);
		
		// IMPORTANT: Save shoot position AFTER prediction starts but BEFORE aimbot
		// This ensures projectile aimbot uses the correct predicted eye position
		F::Ticks.SaveShootPos(pLocal);
		
		F::Misc->AutoMedigun(pCmd);
		F::Aimbot->Run(pCmd);

		// IMPORTANT: Update G::Attacking AFTER aimbot runs
		// Aimbot may have added IN_ATTACK, so we need to re-check
		// This is how Amalgam does it - G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true)
		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);

		// CritHack after aimbot - pass pCmd directly so it sees the aimbot's changes
		F::CritHack->Run(pLocal, pWeapon, pCmd);

		F::Triggerbot->Run(pCmd);
	}
	// NOTE: EnginePrediction.End is called AFTER anti-aim (like Amalgam)

	F::Misc->AutoCallMedic();
	F::SeedPred->AdjustAngles(pCmd);

	// Track target same ticks
	{
		static int nOldTargetIndex = G::nTargetIndexEarly;
		if (G::nTargetIndexEarly != nOldTargetIndex)
		{
			G::nTicksTargetSame = 0;
			nOldTargetIndex = G::nTargetIndexEarly;
		}
		else
		{
			G::nTicksTargetSame++;
		}
		if (G::nTargetIndexEarly <= 1)
			G::nTicksTargetSame = 0;
	}

	// Taunt Slide
	if (CFG::Misc_Taunt_Slide && pLocal)
	{
		if (pLocal->InCond(TF_COND_TAUNTING) && pLocal->m_bAllowMoveDuringTaunt())
		{
			static float flYaw = pCmd->viewangles.y;

			if (H::Input->IsDown(CFG::Misc_Taunt_Spin_Key) && fabsf(CFG::Misc_Taunt_Spin_Speed))
			{
				float yaw = CFG::Misc_Taunt_Spin_Speed;
				if (CFG::Misc_Taunt_Spin_Sine)
					yaw = sinf(I::GlobalVars->curtime) * CFG::Misc_Taunt_Spin_Speed;

				flYaw -= yaw;
				flYaw = Math::NormalizeAngle(flYaw);
				pCmd->viewangles.y = flYaw;
			}
			else
			{
				flYaw = pCmd->viewangles.y;
			}

			if (CFG::Misc_Taunt_Slide_Control)
				pCmd->viewangles.x = (pCmd->buttons & IN_BACK) ? 91.0f : (pCmd->buttons & IN_FORWARD) ? 0.0f : 90.0f;

			G::bSilentAngles = true;
		}
	}

	// Warp exploit
	if (CFG::Exploits_Warp_Exploit && CFG::Exploits_Warp_Mode == 1 && Shifting::bShiftingWarp && pLocal)
	{
		if (CFG::Exploits_Warp_Exploit == 1)
		{
			if (Shifting::nAvailableTicks <= (MAX_COMMANDS - 1))
			{
				Vec3 vAngle = {};
				Math::VectorAngles(pLocal->m_vecVelocity(), vAngle);
				pCmd->viewangles.x = 90.0f;
				pCmd->viewangles.y = vAngle.y;
				G::bSilentAngles = true;
				pCmd->sidemove = pCmd->forwardmove = 0;
			}
		}

		if (CFG::Exploits_Warp_Exploit == 2)
		{
			if (Shifting::nAvailableTicks <= 1)
			{
				Vec3 vAngle = {};
				Math::VectorAngles(pLocal->m_vecVelocity(), vAngle);
				pCmd->viewangles.x = 90.0f;
				pCmd->viewangles.y = vAngle.y;
				G::bSilentAngles = true;
				pCmd->sidemove = pCmd->forwardmove = 0;
			}
		}
	}

	// ============================================
	// AMALGAM ORDER: PacketManip (FakeLag + AntiAim packet check)
	// ============================================
	*pSendPacket = true;
	F::FakeLag->Run(pLocal, pWeapon, pCmd, pSendPacket);
	F::FakeLag->UpdateDrawChams(); // Update fake model visibility based on actual fakelag state

	// Anti-aim choking - ShouldRun already checks G::Attacking == 1
	if (AntiAimCheck(pLocal, pWeapon, pCmd))
		*pSendPacket = false;

	// Prevent overchoking
	if (I::ClientState->chokedcommands > 21)
		*pSendPacket = true;

	// pSilent handling - MUST run BEFORE RapidFire (like reference project)
	// This ensures RapidFire saves the command with aimbot angles, not restored angles
	{
		static bool bWasSet = false;
		if (G::bPSilentAngles)
		{
			*pSendPacket = false;
			bWasSet = true;
		}
		else
		{
			if (bWasSet && !G::bSilentAngles)
			{
				*pSendPacket = true;
				pCmd->viewangles = vOldAngles;
				pCmd->sidemove = flOldSide;
				pCmd->forwardmove = flOldForward;
				bWasSet = false;
			}
			else if (bWasSet && G::bSilentAngles)
			{
				bWasSet = false;
			}
		}
	}

	// ============================================
	// RapidFire/Ticks management - AFTER pSilent handling (like reference)
	// ============================================
	F::RapidFire->RunFastSticky(pCmd, pSendPacket);  // Fast sticky shooting key (runs first)
	F::RapidFire->Run(pCmd, pSendPacket);

	// ============================================
	// AMALGAM ORDER: AntiAim.Run
	// ============================================
	F::FakeAngle->Run(pCmd, pLocal, pWeapon, *pSendPacket, vOldAngles);

	// ============================================
	// AMALGAM ORDER: EnginePrediction.End (AFTER anti-aim)
	// ============================================
	F::EnginePrediction->End(pLocal, pCmd);

	// ============================================
	// AMALGAM ORDER: AntiCheatCompatibility
	// ============================================
	F::AntiCheatCompat->ProcessCommand(pCmd, pSendPacket);

	// If anti-cheat modified angles, we need to restore view to original
	// The modified angles are sent to server, but player should see original view
	if (F::AntiCheatCompat->DidModifyAngles())
	{
		G::bSilentAngles = true;  // Force silent angle restoration
	}

	// Store bones when packet is sent (for fakelag visualization)
	if (*pSendPacket)
		F::FakeAngle->StoreSentBones(pLocal);

	// ============================================
	// AMALGAM ORDER: LocalAnimations (at the very end)
	// ============================================
	LocalAnimations(pLocal, pCmd, *pSendPacket);

	G::bChoking = !*pSendPacket;
	G::nOldButtons = pCmd->buttons;
	G::vUserCmdAngles = pCmd->viewangles;

	// Silent aim handling
	if (G::bSilentAngles || G::bPSilentAngles)
	{
		// Use TRUE original angles for view restoration
		// This ensures anti-cheat lerping doesn't affect what the player sees
		Vec3 vRestoreAngles;
		if (G::bUseSmoothAimAngles)
		{
			// Smooth aimbot is active - use smooth angles
			vRestoreAngles = G::vSmoothAimAngles;
		}
		else if (G::bHasTrueOriginalAngles)
		{
			// Use the TRUE original angles (before any modification)
			vRestoreAngles = G::vTrueOriginalAngles;
		}
		else
		{
			// Fallback to vOldAngles (shouldn't happen)
			vRestoreAngles = vOldAngles;
		}
		
		I::EngineClient->SetViewAngles(vRestoreAngles);
		I::Prediction->SetLocalViewAngles(vRestoreAngles);
		
		// Reset flags for next tick
		G::bUseSmoothAimAngles = false;
		G::bHasTrueOriginalAngles = false;
		return false;
	}
	
	// Reset flags for next tick
	G::bUseSmoothAimAngles = false;
	G::bHasTrueOriginalAngles = false;

	return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);
}
