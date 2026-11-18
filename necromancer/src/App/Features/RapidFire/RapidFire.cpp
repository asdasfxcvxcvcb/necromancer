#include "RapidFire.h"

#include "../CFG.h"
#include "../Crits/Crits.h"
#include "../FakeAngle/FakeAngle.h"
#include "../TickbaseManip/TickbaseManip.h"
#include "../Aimbot/AimbotProjectileSim/AimbotProjectileSim.h"

bool IsRapidFireWeapon(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PISTOL:
	case TF_WEAPON_PISTOL_SCOUT:
	case TF_WEAPON_SMG: return true;
	default: return false;
	}
}

static bool CanInterruptReload(C_TFWeaponBase* pWeapon)
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

bool CRapidFire::ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (G::nTargetIndex <= 1 || !G::bFiring)
		return false;

	if (Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return false;

	if (m_iWait)
		return false;

	if (!CanDoubleTapNow(pLocal, pWeapon))
		return false;

	if (!H::Input->IsDown(CFG::Exploits_RapidFire_Key))
		return false;

	if (!F::CritHack->ShouldAllowFire(pLocal, pWeapon, G::CurrentUserCmd))
		return false;

	return true;
}

void CRapidFire::AntiWarp(C_TFPlayer* pLocal, CUserCmd* pCmd, int nTicks)
{
	m_nAntiWarpMaxTicks = std::max(nTicks + 1, m_nAntiWarpMaxTicks);

	Vec3 vAngles = {};
	Math::VectorAngles(m_vAntiWarpVelocity, vAngles);
	vAngles.y = pCmd->viewangles.y - vAngles.y;

	Vec3 vForward = {};
	Math::AngleVectors(vAngles, &vForward);
	vForward *= m_vAntiWarpVelocity.Length2D();

	if (nTicks > std::max(m_nAntiWarpMaxTicks - 8, 3))
	{
		pCmd->forwardmove = -vForward.x;
		pCmd->sidemove = -vForward.y;
	}
	else if (nTicks > 3)
	{
		pCmd->forwardmove = 0.0f;
		pCmd->sidemove = 0.0f;
	}
	else
	{
		pCmd->forwardmove = vForward.x;
		pCmd->sidemove = vForward.y;
	}
}

void CRapidFire::MaintainWait()
{
	if (G::Attacking == 1 || (!G::bCanPrimaryAttack && !G::bReloading))
		m_iWait = CFG::Exploits_RapidFire_Ticks;

	if (m_iWait > 0)
		m_iWait--;
}

void CRapidFire::Run(CUserCmd* pCmd, bool* pSendPacket)
{
	Shifting::bRapidFireWantShift = false;
	if (!pSendPacket)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon)
		return;

	if (ShouldStart(pLocal, pWeapon))
	{
		Shifting::bRapidFireWantShift = true;

		m_ShiftCmd = *pCmd;
		m_ShiftCmd.buttons |= IN_ATTACK;
		m_bShiftSilentAngles = G::bSilentAngles || G::bPSilentAngles;
		m_bSetCommand = false;

		// Amalgam structure: the fire stays on the aimbot's validated command
		// (same command number, same crit seed, same predicted shootpos) and
		// that command is choke-held. The shift batch released after it hands
		// the server the whole burst in one packet, so the server's clock
		// correction (player.cpp AdjustPlayerTimeBase) backdates the fire and
		// compresses the cooldown - without ever moving the fire off the
		// validated solution
		*pSendPacket = false;

		m_vShiftStart = pLocal->m_vecOrigin();
		m_vAntiWarpVelocity = pLocal->m_vecVelocity();
		m_bStartedShiftOnGround = pLocal->m_fFlags() & FL_ONGROUND;
		m_nAntiWarpMaxTicks = 0;
	}
}

bool CRapidFire::ShouldExitCreateMove(CUserCmd* pCmd)
{
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return false;

	if (!(Shifting::bShifting && !Shifting::bShiftingWarp))
		return false;

	const auto pWeapon = H::Entities->GetWeapon();
	const bool bChargeWeapon = IsStickyWeapon(pWeapon);

	if (!bChargeWeapon)
		return false;

	m_ShiftCmd.command_number = pCmd->command_number;

	if (!m_bSetCommand)
	{
		*pCmd = m_ShiftCmd;
		m_bSetCommand = true;
	}

	if (CFG::Exploits_RapidFire_Antiwarp && m_bStartedShiftOnGround)
	{
		*pCmd = m_ShiftCmd;
		const int nRemainingTicks = std::max(Shifting::nTotalShiftTicks - Shifting::nCurrentShiftTick, 0);
		AntiWarp(pLocal, pCmd, nRemainingTicks);
	}

	return true;
}

void CRapidFire::RunBatchCommand(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	if (Shifting::bShiftingWarp || IsStickyWeapon(H::Entities->GetWeapon()))
		return;

	if (CFG::Exploits_RapidFire_Antiwarp && m_bStartedShiftOnGround)
	{
		const int nRemainingTicks = std::max(Shifting::nTotalShiftTicks - Shifting::nCurrentShiftTick, 0);
		AntiWarp(pLocal, pCmd, nRemainingTicks);
	}
}

bool CRapidFire::IsWeaponSupported(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	const auto nWeaponType = H::AimUtils->GetWeaponType(pWeapon);

	if (nWeaponType == EWeaponType::MELEE || nWeaponType == EWeaponType::OTHER)
		return false;

	const auto nWeaponID = pWeapon->GetWeaponID();

	if (nWeaponID == TF_WEAPON_CROSSBOW
		|| nWeaponID == TF_WEAPON_COMPOUND_BOW
		|| nWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC
		|| nWeaponID == TF_WEAPON_FLAREGUN
		|| nWeaponID == TF_WEAPON_FLAREGUN_REVENGE
		|| nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER
		|| nWeaponID == TF_WEAPON_FLAMETHROWER
		|| nWeaponID == TF_WEAPON_CANNON
		|| pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
		return false;

	return true;
}

bool CRapidFire::IsStickyWeapon(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	const auto nWeaponID = pWeapon->GetWeaponID();
	return nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || nWeaponID == TF_WEAPON_CANNON;
}

bool CRapidFire::IsScottishResistance(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	return pWeapon->m_iItemDefinitionIndex() == Demoman_s_TheScottishResistance;
}

bool CRapidFire::IsQuickiebomb(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	return pWeapon->m_iItemDefinitionIndex() == Demoman_s_TheQuickiebombLauncher;
}

int CRapidFire::GetStickyPreferredTicks(C_TFWeaponBase* pWeapon)
{
	const int nMaxRecharge = std::max(CFG::Exploits_Shifting_Recharge_Limit, 8);

	if (CFG::Exploits_FastSticky_Ticks > 0)
		return std::min(CFG::Exploits_FastSticky_Ticks, nMaxRecharge);

	const int nPreferred = (IsScottishResistance(pWeapon) ? 15 : 20) + 2;
	return std::min(nPreferred, nMaxRecharge);
}

int CRapidFire::GetShotsWithinPacket(C_TFWeaponBase* pWeapon, int nTicks)
{
	if (!pWeapon || nTicks <= 0)
		return 0;

	int nDelay = 1;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		nDelay = 2;
		break;
	default:
		break;
	}

	const int nFireRateTicks = (std::max)(static_cast<int>(std::ceil(pWeapon->GetFireRate() / TICK_INTERVAL)), 1);
	if (nTicks < nDelay)
		return 1;

	return 1 + (nTicks - nDelay) / nFireRateTicks;
}

bool CRapidFire::CanDoubleTapNow(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (!pLocal)
		pLocal = H::Entities->GetLocal();
	if (!pWeapon)
		pWeapon = H::Entities->GetWeapon();

	if (!pLocal || !pWeapon || !IsWeaponSupported(pWeapon))
		return false;

	if (Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return false;

	if (m_iWait)
		return false;

	const int nBankTicks = std::clamp(Shifting::nAvailableTicks, 0, Shifting::nMaxUsrCmdProcessTicks);
	const int nPacketTicks = std::min(nBankTicks + 1, Shifting::nMaxUsrCmdProcessTicks);
	const int nRateShots = GetShotsWithinPacket(pWeapon, nPacketTicks);

	// Amalgam's comparison: their iTicks = shiftedTicks + 1 (the banked ticks
	// plus the fire command) must reach TickLimit before the DT arms - the
	// burst's clock span (the fire command + the fillers) has to cover the
	// weapon's fire rate for the second shell to land on the last batch
	// command. The slider counts the whole burst, so the bank needs
	// slider - 1 stored ticks
	if (!(nPacketTicks >= CFG::Exploits_RapidFire_Ticks || nRateShots > 1))
		return false;

	const int nClip = pWeapon->m_iClip1();
	if (nClip >= 0 && nClip < 2)
		return false;

	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
	{
		const int nState = pWeapon->As<C_TFMinigun>()->m_iWeaponState();
		if (nState == AC_STATE_IDLE || nState == AC_STATE_STARTFIRING || nState == AC_STATE_DRYFIRE)
			return false;
	}

	const int nSavedTickBase = pLocal->m_nTickBase();
	pLocal->m_nTickBase() = nSavedTickBase + 1;
	const bool bCanFireNextTick = pWeapon->CanPrimaryAttack(pLocal) && pWeapon->HasPrimaryAmmoForShot();
	pLocal->m_nTickBase() = nSavedTickBase;

	// Amalgam's Doubletap has no reload-specific gate: it arms whenever the
	// weapon can fire now, next tick, or the reload is interruptible, and the
	// engine's own reload-interrupt timing decides when the batch's shots land
	return G::bCanPrimaryAttack || bCanFireNextTick || CanInterruptReload(pWeapon);
}

int CRapidFire::GetReleaseTicks()
{
	// the slider counts the whole burst (the fire command included, Amalgam's
	// TickLimit semantic) - the fillers are one less
	return (std::min)({ CFG::Exploits_RapidFire_Ticks - 1, std::max(Shifting::nAvailableTicks, 0), MAX_NEW_COMMANDS + MAX_BACKUP_COMMANDS - 1 });
}

int CRapidFire::GetTicks(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		pWeapon = H::Entities->GetWeapon();

	if (!CanDoubleTapNow(nullptr, pWeapon))
		return 0;

	return GetReleaseTicks();
}

int CRapidFire::GetFastStickyMaxRecharge()
{
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
		return 8;

	return F::Ticks->GetOptimalRechargeLimit();
}

bool CRapidFire::IsFastStickyUsable()
{
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
		return false;

	if (CFG::Exploits_Shifting_Recharge_Limit < 8)
		return false;

	return true;
}

void CRapidFire::RunFastSticky(CUserCmd* pCmd, bool* pSendPacket)
{
	static int s_nLastTapClip = -1;
	static unsigned long long s_ullTapTime = 0;
	static int s_nPlateauBank = -1;
	static unsigned long long s_ullPlateauStart = 0;

	Shifting::bStickyDTWantShift = false;
	Shifting::nStickyRechargeTarget = 0;

	if (!pSendPacket)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
	{
		m_bStickyCharging = false;
		return;
	}

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon || !IsStickyWeapon(pWeapon))
	{
		m_bStickyCharging = false;
		return;
	}

	if (I::MatSystemSurface->IsCursorVisible() || I::EngineVGui->IsGameUIVisible())
	{
		m_bStickyCharging = false;
		return;
	}

	if (Shifting::bShifting || Shifting::bShiftingWarp)
		return;

	if (!H::Input->IsDown(CFG::Exploits_FastSticky_Key))
	{
		m_bStickyCharging = false;
		s_nLastTapClip = -1;
		s_nPlateauBank = -1;
		s_ullPlateauStart = 0;
		return;
	}

	if (!IsFastStickyUsable())
	{
		m_bStickyCharging = false;
		return;
	}

	m_bStickyCharging = true;

	const int nMaxRecharge = std::max(CFG::Exploits_Shifting_Recharge_Limit, 8);
	const int nAaReserve = F::FakeAngle->YawOn() ? F::FakeAngle->AntiAimTicks() : 0;
	const int nEffectiveCap = std::max(std::min(nMaxRecharge, Shifting::nMaxUsrCmdProcessTicks) - nAaReserve, 1);
	int nTargetTicks = std::min(GetStickyPreferredTicks(pWeapon), nEffectiveCap);

	const float flNextAttack = pWeapon->m_flNextPrimaryAttack();
	const float flNow = static_cast<float>(pLocal->m_nTickBase()) * I::GlobalVars->interval_per_tick;
	const bool bReloadPacing = flNextAttack - flNow > 0.35f;
	const bool bTapStalled = pWeapon->m_iClip1() == s_nLastTapClip && GetTickCount64() - s_ullTapTime < 800;
	const bool bReloadBusy = bReloadPacing || pWeapon->m_iClip1() == 0 || bTapStalled;

	if (bReloadBusy || Shifting::nAvailableTicks != s_nPlateauBank)
	{
		s_nPlateauBank = Shifting::nAvailableTicks;
		s_ullPlateauStart = GetTickCount64();
	}
	else if (Shifting::nAvailableTicks < nTargetTicks && GetTickCount64() - s_ullPlateauStart >= 400)
		nTargetTicks = std::max(Shifting::nAvailableTicks, 1);

	if (Shifting::nAvailableTicks < nTargetTicks)
	{
		Shifting::nStickyRechargeTarget = nTargetTicks;
		if (bReloadBusy)
			Shifting::bRecharging = false;
		else
			Shifting::bRecharging = true;
	}
	else if (bReloadBusy)
	{
		Shifting::nStickyRechargeTarget = nTargetTicks;
		Shifting::bRecharging = false;
	}
	else
	{
		s_nLastTapClip = pWeapon->m_iClip1();
		s_ullTapTime = GetTickCount64();

		pCmd->buttons |= IN_ATTACK;
		Shifting::bRecharging = false;
		Shifting::bStickyDTWantShift = true;
		Shifting::nStickyDTTicksToUse = nTargetTicks;

		m_ShiftCmd = *pCmd;
		m_ShiftCmd.buttons |= IN_ATTACK;
		m_bShiftSilentAngles = G::bSilentAngles;
		m_bSetCommand = false;

		m_vShiftStart = pLocal->m_vecOrigin();
		m_vAntiWarpVelocity = pLocal->m_vecVelocity();
		m_bStartedShiftOnGround = false;
		m_nAntiWarpMaxTicks = 0;
		*pSendPacket = true;
	}
}

void CRapidFire::RunFastRev(CUserCmd* pCmd, bool* pSendPacket)
{
	Shifting::bFastRevWantShift = false;
	if (!CFG::Exploits_FastRev || !pSendPacket)
	{
		m_bFastRevDraining = false;
		return;
	}

	const auto pLocal = H::Entities->GetLocal();
	const auto pWeapon = H::Entities->GetWeapon();
	if (!pLocal || pLocal->deadflag() || !pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_MINIGUN)
	{
		m_bFastRevDraining = false;
		return;
	}

	if (Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return;

	const int nState = pWeapon->As<C_TFMinigun>()->m_iWeaponState();
	const bool bUserSpin = (G::OriginalCmd.buttons & (IN_ATTACK | IN_ATTACK2)) != 0;
	const bool bAimbotSpin = (pCmd->buttons & (IN_ATTACK | IN_ATTACK2)) != 0;

	if (m_bFastRevDraining)
	{
		// keep pouring the bank into the spin-up, one full packet per frame:
		// stop when the bank is dry, the wind-up finished (the client clock
		// passed the spin timer) or aborted (idle again), or nothing is
		// driving the rev anymore
		if (Shifting::nAvailableTicks <= 0 || nState != AC_STATE_STARTFIRING || (!bUserSpin && !bAimbotSpin))
		{
			m_bFastRevDraining = false;
			return;
		}
	}
	else
	{
		// a fresh spin-up only: the state machine must be at idle when the
		// wind-up runs (respin states have their own shorter timers)
		if (nState != AC_STATE_IDLE)
			return;

		// two ways the wind-up starts, both anchored by this exact command's
		// prediction (it runs WindUp and sets the spin-up timer,
		// m_flNextPrimaryAttack = curtime + TF_MINIGUN_SPINUP_TIME = 0.75s = 50
		// ticks; the command has to be choke-held INTO the batch so the timer's
		// origin lands on the batch's backdated clock):
		// - the user's raw input rising edge (a manual rev). The edge reads
		//   OriginalCmd, never the final command: the aimbot writes the attack
		//   bits on solution ticks while fighting, which would mask the edge
		// - the aimbot's rev write on the spun-down minigun: HandleMinigun
		//   writes IN_ATTACK2 (not IN_ATTACK - that only comes later once it
		//   can actually fire, by which point the state is past idle) while
		//   targeting, and the engine winds up from that input
		const bool bSpinWasPressed = m_bLastUserSpinInput;
		m_bLastUserSpinInput = bUserSpin;

		const bool bUserEdge = bUserSpin && !bSpinWasPressed;
		const bool bAimbotEdge = !bUserSpin && bAimbotSpin;
		if (!bUserEdge && !bAimbotEdge)
			return;

		m_bFastRevDraining = true;
	}

	// no full-bank gate: the saving equals whatever bank is stored, and the
	// spin-up happens mid-fight where the bank is rarely full
	Shifting::bFastRevWantShift = true;
	*pSendPacket = false;

	m_vShiftStart = pLocal->m_vecOrigin();
	m_vAntiWarpVelocity = pLocal->m_vecVelocity();
	m_bStartedShiftOnGround = pLocal->m_fFlags() & FL_ONGROUND;
	m_nAntiWarpMaxTicks = 0;
}
