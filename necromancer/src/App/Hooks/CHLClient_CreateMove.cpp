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
#include "../Features/FakeLagFix/FakeLagFix.h"
#include "../Features/FakeAngle/FakeAngle.h"
#include "../Features/Networking/Networking.h"
#include "../Features/ProjectileDodge/ProjectileDodge.h"
#include "../Features/NavBot/NavBot.h"
#include "../Features/NavBot/NavEngine/NavEngine.h"
#include "../Features/Misc/AntiCheatCompat/AntiCheatCompat.h"
#include "../Features/amalgam_port/AmalgamCompat.h"
#include "../Features/amalgam_port/Ticks/Ticks.h"
#include "../Features/AutoUberPop/AutoUberPop.h"
#include "../Features/AntiBackstab/AntiBackstab.h"
#include "../Features/Players/Players.h"
#include "../Features/MoveSimDebug/MoveSimDebug.h"
#include "../../Utils/CrashHandler/CrashHandler.h"

extern void ProcessTauntDelay();

namespace TickbaseFix
{
	void Add(int nStartCommandNumber, int nTickbaseShift);
}

MAKE_SIGNATURE(ValidateUserCmd_, "client.dll", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F9 41 8B D8", 0, 0);
MAKE_HOOK(ValidateUserCmd, Signatures::ValidateUserCmd_.Get(), void, __fastcall, void* rcx, CUserCmd* cmd,
	int sequence_number) {
	return;
}

static inline void LocalAnimations(C_TFPlayer* pLocal, CUserCmd* pCmd, bool bSendPacket)
{
	static std::vector<Vec3> vAngles = {};

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

		F::FakeAngle->SetupFakeModel(pLocal);
	}
}

static inline bool AntiAimCheck(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	bool bAntiAim = F::FakeAngle->YawOn()
		&& F::FakeAngle->ShouldRun(pLocal, pWeapon, pCmd)
		&& !Shifting::bRecharging
		&& I::ClientState->chokedcommands < F::FakeAngle->AntiAimTicks();

	return bAntiAim;
}

static inline bool CanInterruptReload(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon || pWeapon->m_iClip1() <= 0 || !pWeapon->IsInReload())
		return false;

	if (pWeapon->m_bReloadsSingly())
		return true;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_SHOTGUN_PRIMARY:
	case TF_WEAPON_SHOTGUN_SOLDIER:
	case TF_WEAPON_SHOTGUN_HWG:
	case TF_WEAPON_SHOTGUN_PYRO:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
	case TF_WEAPON_SCATTERGUN:
	case TF_WEAPON_SODA_POPPER:
	case TF_WEAPON_PEP_BRAWLER_BLASTER:
	case TF_WEAPON_SMG:
	case TF_WEAPON_CHARGED_SMG:
	case TF_WEAPON_PISTOL:
	case TF_WEAPON_PISTOL_SCOUT:
	case TF_WEAPON_REVOLVER:
	case TF_WEAPON_HANDGUN_SCOUT_PRIMARY:
	case TF_WEAPON_HANDGUN_SCOUT_SECONDARY:
	case TF_WEAPON_MINIGUN:
		return true;
	default:
		break;
	}

	return H::AimUtils->GetWeaponType(pWeapon) == EWeaponType::HITSCAN && pWeapon->GetSlot() != WEAPON_SLOT_MELEE;
}

enum class CMFlow
{
	CONTINUE,
	CALL_ORIGINAL_EXIT,
	RETURN_FALSE_EXIT,
	SKIP_TO_S10
};

struct CMContext
{
	bool& bSendPacket;
	C_TFPlayer* pLocal;
	C_TFWeaponBase* pWeapon;
	Vec3 vOldAngles;
	float flOldSide;
	float flOldForward;
};

static CMFlow SectionEntryGuard(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd)
{
	if (G::bLevelTransition)
		return CMFlow::CALL_ORIGINAL_EXIT;

	return CMFlow::CONTINUE;
}

static CMFlow SectionPerTickReset(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd)
{
	F::CrashHandler->s_Context.m_pszLastHook = "CreateMove";
	F::CrashHandler->s_Context.m_pszLastFeature = "none";
	F::CrashHandler->s_Context.m_pszLastSubFeature = "none";
	F::CrashHandler->s_Context.m_nEntityIndex = -1;
	F::CrashHandler->s_Context.m_bLevelTransition = G::bLevelTransition;
	if (pCmd)
		F::CrashHandler->s_Context.m_nCommandNumber = pCmd->command_number;

	G::bSilentAngles = false;
	G::bPSilentAngles = false;
	G::bFiring = false;
	G::bAutoScopeWaitActive = false;
	G::Attacking = 0;
	G::Throwing = false;
	G::LastUserCmd = G::CurrentUserCmd ? G::CurrentUserCmd : pCmd;
	G::CurrentUserCmd = pCmd;
	G::OriginalCmd = *pCmd;

	F::Players->CheckDeferredSave();

	G::vTrueOriginalAngles = pCmd->viewangles;
	G::bHasTrueOriginalAngles = true;

	if (!pCmd || !pCmd->command_number)
		return CMFlow::CALL_ORIGINAL_EXIT;

	return CMFlow::CONTINUE;
}

static CMFlow SectionCacheAndInput(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd, bool& bRapidFireExit)
{
	H::Entities->ValidateGroups();

	if (F::MiscVisuals->IsFreecamActive())
	{
		pCmd->forwardmove = 0.0f;
		pCmd->sidemove = 0.0f;
		pCmd->upmove = 0.0f;
		pCmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_JUMP | IN_DUCK);
	}

	ProcessTauntDelay();

	bRapidFireExit = false;
	if (Shifting::bShifting && !Shifting::bShiftingWarp)
	{
		bRapidFireExit = F::RapidFire->ShouldExitCreateMove(pCmd);
		pCmd->random_seed = MD5_PseudoRandom(pCmd->command_number) & 0x7fffffff;

		if (bRapidFireExit && !Shifting::bTickbaseFixRecorded && Shifting::nCurrentShiftTick == 0 && Shifting::nTotalShiftTicks > 0)
		{
			TickbaseFix::Add(pCmd->command_number, Shifting::nTotalShiftTicks);
			Shifting::bTickbaseFixRecorded = true;
		}
	}

	I::Prediction->Update(
		I::ClientState->m_nDeltaTick,
		I::ClientState->m_nDeltaTick > 0,
		I::ClientState->last_command_ack,
		bRapidFireExit ? pCmd->command_number : I::ClientState->lastoutgoingcommand + I::ClientState->chokedcommands
	);

	F::AutoVaccinator->PreventReload(pCmd);

	if (CFG::Triggerbot_AutoVaccinator_Always_On)
	{
		auto pLocalVacc = H::Entities->GetLocal();
		auto pWeaponVacc = H::Entities->GetWeapon();
		if (pLocalVacc && !pLocalVacc->deadflag() && pWeaponVacc)
			F::AutoVaccinator->Run(pLocalVacc, pWeaponVacc, pCmd);
	}

	return CMFlow::CONTINUE;
}

static CMFlow SectionShiftShortCircuit(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd, bool bRapidFireExit)
{
	if (bRapidFireExit || F::RapidFire->ShouldExitCreateMove(pCmd))
	{
		auto pLocal = H::Entities->GetLocal();
		auto pWeapon = H::Entities->GetWeapon();
		if (pLocal && pWeapon && !pLocal->deadflag())
		{
			F::CrashHandler->s_Context.m_pszLastFeature = "CritHack";
			F::CritHack->Run(pLocal, pWeapon, pCmd);
		}

		return F::RapidFire->GetShiftSilentAngles() ? CMFlow::RETURN_FALSE_EXIT : CMFlow::CALL_ORIGINAL_EXIT;
	}

	if (Shifting::bRecharging)
	{
		if (pCmd->buttons & IN_JUMP)
			pCmd->buttons &= ~IN_JUMP;
		if (pCmd->buttons & IN_ATTACK)
			pCmd->buttons &= ~IN_ATTACK;
		if (pCmd->buttons & IN_ATTACK2)
			pCmd->buttons &= ~IN_ATTACK2;
		return CMFlow::CALL_ORIGINAL_EXIT;
	}

	return CMFlow::CONTINUE;
}

static CMFlow SectionWeaponSnapshot(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd, CMContext& ctx)
{
	ctx.vOldAngles = pCmd->viewangles;
	ctx.flOldSide = pCmd->sidemove;
	ctx.flOldForward = pCmd->forwardmove;

	ctx.bSendPacket = true;

	ctx.pLocal = H::Entities->GetLocal();
	ctx.pWeapon = H::Entities->GetWeapon();

	{
		static bool bDisabledForBuildingPickup = false;

		if (ctx.pLocal && ctx.pLocal->m_iClass() == TF_CLASS_ENGINEER)
		{
			const bool bCarryingBuilding = ctx.pLocal->m_bCarryingObject();

			if (bDisabledForBuildingPickup && bCarryingBuilding)
			{
				CFG::Exploits_LegitAA_Enabled = true;
				bDisabledForBuildingPickup = false;
			}

			if (CFG::Exploits_LegitAA_Enabled && !bCarryingBuilding && (pCmd->buttons & IN_ATTACK2))
			{
				Vec3 vStart = ctx.pLocal->GetShootPos();
				Vec3 vForward;
				Math::AngleVectors(pCmd->viewangles, &vForward);
				Vec3 vEnd = vStart + vForward * 150.0f;

				CGameTrace trace;
				CTraceFilterHitscan filter;
				filter.m_pIgnore = ctx.pLocal;
				SDK::Trace(vStart, vEnd, MASK_SOLID, &filter, &trace);

				if (trace.m_pEnt)
				{
					const auto nClassId = trace.m_pEnt->GetClassId();
					if (nClassId == ETFClassIds::CObjectSentrygun ||
						nClassId == ETFClassIds::CObjectDispenser ||
						nClassId == ETFClassIds::CObjectTeleporter)
					{
						auto pBuilding = trace.m_pEnt->As<C_BaseObject>();
						if (pBuilding && pBuilding->m_hBuilder() == ctx.pLocal)
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
			bDisabledForBuildingPickup = false;
		}
	}

	G::bCanPrimaryAttack = false;
	G::bCanSecondaryAttack = false;
	G::bReloading = false;

	if (ctx.pLocal && ctx.pWeapon && H::Entities->IsEntityValid(ctx.pWeapon))
	{
		G::bCanHeadshot = ctx.pWeapon->CanHeadShot(ctx.pLocal);

		G::bCanPrimaryAttack = ctx.pWeapon->CanPrimaryAttack(ctx.pLocal);
		G::bCanSecondaryAttack = ctx.pWeapon->CanSecondaryAttack(ctx.pLocal);

		if (ctx.pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
		{
			int iState = ctx.pWeapon->As<C_TFMinigun>()->m_iWeaponState();
			if (iState == AC_STATE_IDLE || iState == AC_STATE_STARTFIRING || !ctx.pWeapon->HasPrimaryAmmoForShot())
				G::bCanPrimaryAttack = false;
		}

		if (ctx.pWeapon->GetSlot() != WEAPON_SLOT_MELEE)
		{
			bool bAmmo = ctx.pWeapon->HasPrimaryAmmoForShot();
			bool bReload = ctx.pWeapon->IsInReload();

			if (!bAmmo)
			{
				G::bCanPrimaryAttack = false;
				G::bCanSecondaryAttack = false;
			}

			if (bReload && bAmmo)
				G::bReloading = true;

			if (CanInterruptReload(ctx.pWeapon))
				G::bCanPrimaryAttack = true;
		}

		G::Attacking = SDK::IsAttacking(ctx.pLocal, ctx.pWeapon, pCmd, false);
	}
	else
	{
		G::bCanHeadshot = false;
	}

	{
		static bool bOldCanFire = G::bCanPrimaryAttack;

		bool bCanFireDuringReload = false;
		if (ctx.pWeapon && G::bReloading && ctx.pWeapon->m_iClip1() > 0)
		{
			if (ctx.pWeapon->m_bReloadsSingly())
			{
				bCanFireDuringReload = true;
			}
			else
			{
				const int nWeaponID = ctx.pWeapon->GetWeaponID();
				if (nWeaponID == TF_WEAPON_SMG || nWeaponID == TF_WEAPON_CHARGED_SMG ||
					nWeaponID == TF_WEAPON_PISTOL || nWeaponID == TF_WEAPON_PISTOL_SCOUT ||
					nWeaponID == TF_WEAPON_MINIGUN)
				{
					bCanFireDuringReload = true;
				}
			}
		}

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

	if (!ctx.pLocal)
		return CMFlow::CALL_ORIGINAL_EXIT;

	return CMFlow::CONTINUE;
}

static CMFlow SectionMovementFeatures(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd, CMContext& ctx)
{
	F::Misc->Bunnyhop(pCmd);
	F::Misc->AutoJumpbug(ctx.pLocal, pCmd);
	F::Misc->AutoStrafer(pCmd);
	F::Misc->FastStop(pCmd);
	F::Misc->FastAccelerate(pCmd);
	F::Misc->NoiseMakerSpam();
	F::Misc->VoiceCommandSpam();
	F::Misc->AutoRocketJump(pCmd);
	F::Misc->AutoFaN(pCmd);
	F::AutoUberPop->Run(pCmd);
	F::Misc->AutoDisguise(pCmd);
	F::Misc->MovementLock(pCmd);
	F::Misc->MvmInstaRespawn();
	F::Misc->AntiAFK(pCmd);
	if (CFG::Misc_Auto_FastClassSwitch)
		F::Misc->FastClassSwitch();

	if (CFG::NavBot_Enabled)
	{
		if (!G_NavEngine.IsReady())
			G_NavEngine.Reset();
		g_NavBot.Run(ctx.pLocal, pCmd);

		ctx.pLocal = H::Entities->GetLocal();
		ctx.pWeapon = H::Entities->GetWeapon();
	}
	else if (g_NavBot.IsActive())
	{
		g_NavBot.Stop();
	}

	if (ctx.pLocal)
		F::ProjectileDodge->Run(ctx.pLocal, pCmd);

	return CMFlow::CONTINUE;
}

static CMFlow SectionPredictionEntry(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd, CMContext& ctx)
{
	if (!ctx.pLocal)
	{
		F::EnginePrediction->Start(nullptr, pCmd);
		F::EnginePrediction->End(nullptr, pCmd);
		return CMFlow::SKIP_TO_S10;
	}

	F::EnginePrediction->Start(ctx.pLocal, pCmd);

	return CMFlow::CONTINUE;
}

static CMFlow SectionCombatInPrediction(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd, CMContext& ctx)
{
	if (CFG::Misc_Choke_On_Bhop && CFG::Misc_Bunnyhop)
	{
		if ((ctx.pLocal->m_fFlags() & FL_ONGROUND) && !(F::EnginePrediction->flags & FL_ONGROUND))
			ctx.bSendPacket = false;
	}

	F::Misc->CrouchWhileAirborne(pCmd);

	F::AmalgamTicks->SaveShootPos(ctx.pLocal);

	{
		F::Misc->AutoMedigun(pCmd);
	}
	F::FakeLagFix->Update();

	F::CrashHandler->s_Context.m_pszLastFeature = "Aimbot";
	{
		F::Aimbot->Run(pCmd);
	}

	if (!ctx.pLocal || !ctx.pWeapon
		|| !H::Entities->SafeIsEntityValid(ctx.pLocal, I::EngineClient->GetLocalPlayer())
		|| !H::Entities->IsEntityValid(ctx.pWeapon))
	{
		F::EnginePrediction->End(nullptr, pCmd);
		return CMFlow::SKIP_TO_S10;
	}

	G::Attacking = SDK::IsAttacking(ctx.pLocal, ctx.pWeapon, pCmd, true);

	F::Misc->PDAExploit(pCmd);

	{
		F::CritHack->Run(ctx.pLocal, ctx.pWeapon, pCmd);
	}

	{
		F::CrashHandler->s_Context.m_pszLastFeature = "CritEffects";
		F::CritHack->RunEffects(ctx.pLocal);
	}

	F::CrashHandler->s_Context.m_pszLastFeature = "Triggerbot";
	{
		F::Triggerbot->Run(pCmd);
	}

	return CMFlow::CONTINUE;
}

static CMFlow SectionPostAimCorrections(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd, CMContext& ctx)
{
	{
		F::Misc->AutoCallMedic();
	}

	{
		F::SeedPred->AdjustAngles(pCmd);
	}

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

	if (CFG::Misc_Taunt_Slide && ctx.pLocal)
	{
		if (ctx.pLocal->InCond(TF_COND_TAUNTING) && ctx.pLocal->m_bAllowMoveDuringTaunt())
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

	if (CFG::Exploits_Warp_Exploit && CFG::Exploits_Warp_Mode == 1 && Shifting::bShiftingWarp && ctx.pLocal)
	{
		if (CFG::Exploits_Warp_Exploit == 1)
		{
			if (Shifting::nAvailableTicks <= (MAX_COMMANDS - 1))
			{
				Vec3 vAngle = {};
				Math::VectorAngles(ctx.pLocal->m_vecVelocity(), vAngle);
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
				Math::VectorAngles(ctx.pLocal->m_vecVelocity(), vAngle);
				pCmd->viewangles.x = 90.0f;
				pCmd->viewangles.y = vAngle.y;
				G::bSilentAngles = true;
				pCmd->sidemove = pCmd->forwardmove = 0;
			}
		}
	}

	return CMFlow::CONTINUE;
}

static CMFlow SectionPacketAndAntiAim(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd, CMContext& ctx)
{
	ctx.bSendPacket = true;
	{
		F::FakeLag->Run(ctx.pLocal, ctx.pWeapon, pCmd, &ctx.bSendPacket);
		F::FakeLag->UpdateDrawChams();
	}

	{
		if (AntiAimCheck(ctx.pLocal, ctx.pWeapon, pCmd))
			ctx.bSendPacket = false;
	}

	{
		F::FakeAngle->Run(pCmd, ctx.pLocal, ctx.pWeapon, ctx.bSendPacket, ctx.vOldAngles);
	}

	F::EnginePrediction->End(ctx.pLocal, pCmd);

	return CMFlow::CONTINUE;
}

static CMFlow SectionMaskingAndExploits(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd, CMContext& ctx)
{
	{
		F::AntiCheatCompat->ProcessCommand(pCmd, &ctx.bSendPacket);

		if (F::AntiCheatCompat->DidModifyAngles())
		{
			G::bSilentAngles = true;
		}
	}

	{
		static bool bWasSet = false;

		if (G::bPSilentAngles)
		{
			ctx.bSendPacket = false;
			bWasSet = true;
		}
		else
		{
			if (bWasSet)
			{
				ctx.bSendPacket = true;
				if (!G::bSilentAngles)
				{
					pCmd->viewangles = ctx.vOldAngles;
					pCmd->sidemove = ctx.flOldSide;
					pCmd->forwardmove = ctx.flOldForward;
				}
				bWasSet = false;
			}
		}
	}

		{
			if (I::ClientState->chokedcommands > 22 || I::ClientState->chokedcommands > 0 && (Shifting::bRapidFireWantShift || Shifting::bStickyDTWantShift))
				ctx.bSendPacket = true;

		F::RapidFire->RunFastSticky(pCmd, &ctx.bSendPacket);
		F::RapidFire->Run(pCmd, &ctx.bSendPacket);
	}

	return CMFlow::CONTINUE;
}

static CMFlow SectionFinalize(CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd, CMContext& ctx)
{
	{
		if (ctx.bSendPacket)
			F::FakeAngle->StoreSentBones(ctx.pLocal);
	}

	{
		LocalAnimations(ctx.pLocal, pCmd, ctx.bSendPacket);
	}

	G::nOldButtons = pCmd->buttons;
	G::vUserCmdAngles = pCmd->viewangles;

	if (G::bSilentAngles || G::bPSilentAngles)
	{
		Vec3 vRestoreAngles;
		if (G::bUseSmoothAimAngles)
		{
			vRestoreAngles = G::vSmoothAimAngles;
		}
		else if (G::bHasTrueOriginalAngles)
		{
			vRestoreAngles = G::vTrueOriginalAngles;
		}
		else
		{
			vRestoreAngles = ctx.vOldAngles;
		}

		I::EngineClient->SetViewAngles(vRestoreAngles);
		I::Prediction->SetLocalViewAngles(vRestoreAngles);

		G::bUseSmoothAimAngles = false;
		G::bHasTrueOriginalAngles = false;
		return CMFlow::RETURN_FALSE_EXIT;
	}

	G::bUseSmoothAimAngles = false;
	G::bHasTrueOriginalAngles = false;

	return CMFlow::CALL_ORIGINAL_EXIT;
}

MAKE_HOOK(CHLClient_Createmove, Memory::GetVFunc(I::ClientModeShared, 21), bool, __fastcall,
	CClientModeShared* ecx, float flInputSampleTime, CUserCmd* pCmd)
{
	if (SectionEntryGuard(ecx, flInputSampleTime, pCmd) != CMFlow::CONTINUE)
		return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);

	if (SectionPerTickReset(ecx, flInputSampleTime, pCmd) != CMFlow::CONTINUE)
		return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);

	bool bRapidFireExit = false;
	if (SectionCacheAndInput(ecx, flInputSampleTime, pCmd, bRapidFireExit) != CMFlow::CONTINUE)
		return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);

	const CMFlow eShift = SectionShiftShortCircuit(ecx, flInputSampleTime, pCmd, bRapidFireExit);
	if (eShift == CMFlow::RETURN_FALSE_EXIT)
		return false;
	if (eShift != CMFlow::CONTINUE)
		return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);

	CMContext ctx { F::Networking->bSendPacket, nullptr, nullptr, {}, 0.0f, 0.0f };

	if (SectionWeaponSnapshot(ecx, flInputSampleTime, pCmd, ctx) != CMFlow::CONTINUE)
		return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);

	if (SectionMovementFeatures(ecx, flInputSampleTime, pCmd, ctx) != CMFlow::CONTINUE)
		return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);

	CMFlow eFlow = SectionPredictionEntry(ecx, flInputSampleTime, pCmd, ctx);
	if (eFlow == CMFlow::CONTINUE)
		eFlow = SectionCombatInPrediction(ecx, flInputSampleTime, pCmd, ctx);
	if (eFlow == CMFlow::CONTINUE)
		eFlow = SectionPostAimCorrections(ecx, flInputSampleTime, pCmd, ctx);
	if (eFlow == CMFlow::CONTINUE)
		eFlow = SectionPacketAndAntiAim(ecx, flInputSampleTime, pCmd, ctx);

	if (eFlow == CMFlow::CALL_ORIGINAL_EXIT)
		return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);
	if (eFlow == CMFlow::RETURN_FALSE_EXIT)
		return false;

	{
		F::CrashHandler->s_Context.m_pszLastFeature = "MoveSimDebug";
		F::MoveSimDebug->Run(pCmd);
	}

	SectionMaskingAndExploits(ecx, flInputSampleTime, pCmd, ctx);

	if (SectionFinalize(ecx, flInputSampleTime, pCmd, ctx) == CMFlow::RETURN_FALSE_EXIT)
		return false;

	return CALL_ORIGINAL(ecx, flInputSampleTime, pCmd);
}
