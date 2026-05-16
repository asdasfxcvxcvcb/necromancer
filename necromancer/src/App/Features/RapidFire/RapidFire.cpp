#include "RapidFire.h"

#include "../CFG.h"
#include "../Crits/Crits.h"
#include "../TickbaseManip/TickbaseManip.h"

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

// SEOwnedDE-style ShouldStart — simple conditions, no per-weapon branching
bool CRapidFire::ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	// Need target and firing intent
	if (G::nTicksSinceCanFire < 24 || G::nTargetIndex <= 1 || !G::bFiring)
		return false;

	// Don't start if already shifting or recharging
	if (Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return false;

	if (!CanDoubleTapNow(pLocal, pWeapon))
		return false;

	// Need DT key held
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

// SEOwnedDE-style Run — save command, set shift flag, strip attack from real tick
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
		// Delay ticks check (hacky but works — same as SEOwnedDE)
		if (G::nTicksTargetSame < CFG::Exploits_RapidFire_Min_Ticks_Target_Same)
		{
			pCmd->buttons &= ~IN_ATTACK;
			G::bFiring = false;
			return;
		}

		Shifting::bRapidFireWantShift = true;

		m_ShiftCmd = *pCmd;
		m_bShiftSilentAngles = G::bSilentAngles || G::bPSilentAngles;
		m_bSetCommand = false;

		pCmd->buttons &= ~IN_ATTACK;  // Don't fire on the "real" tick
		*pSendPacket = true;           // Send this tick immediately

		m_vShiftStart = pLocal->m_vecOrigin();
		m_vAntiWarpVelocity = pLocal->m_vecVelocity();
		m_bStartedShiftOnGround = pLocal->m_fFlags() & FL_ONGROUND;
		m_nAntiWarpMaxTicks = 0;
	}
}

// SEOwnedDE-style ShouldExitCreateMove — WalkTo antiwarp, no per-weapon attack logic
bool CRapidFire::ShouldExitCreateMove(CUserCmd* pCmd)
{
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return false;

	if (Shifting::bShifting && !Shifting::bShiftingWarp)
	{
		m_ShiftCmd.command_number = pCmd->command_number;

		// First shift tick: apply the saved command (has IN_ATTACK)
		if (!m_bSetCommand)
		{
			*pCmd = m_ShiftCmd;
			m_bSetCommand = true;
		}

		// Antiwarp: Amalgam-style velocity phase correction
		if (CFG::Exploits_RapidFire_Antiwarp && m_bStartedShiftOnGround)
		{
			*pCmd = m_ShiftCmd;
			const int nRemainingTicks = std::max(Shifting::nTotalShiftTicks - Shifting::nCurrentShiftTick, 0);
			AntiWarp(pLocal, pCmd, nRemainingTicks);
		}
		else
		{
			// Rapid fire weapons (minigun/pistol/SMG) always use shift cmd
			const auto pWeapon = H::Entities->GetWeapon();
			if (IsRapidFireWeapon(pWeapon))
				*pCmd = m_ShiftCmd;
		}

		return true;
	}

	return false;
}

// SEOwnedDE-style blocklist — same weapons they block
bool CRapidFire::IsWeaponSupported(C_TFWeaponBase* pWeapon)
{
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

int CRapidFire::GetStickyPreferredTicks(C_TFWeaponBase* pWeapon)
{
	// Scottish Resistance: 15 preferred ticks (lower fire rate)
	// Normal sticky / Iron Bomber: 20 preferred ticks
	const int nPreferred = IsScottishResistance(pWeapon) ? 15 : 20;

	// Can't use more ticks than the recharge limit allows
	const int nMaxRecharge = GetFastStickyMaxRecharge();
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

	if (G::nTicksSinceCanFire < 24)
		return false;

	const int nProcessableTicks = F::Ticks->GetProcessableTicks();
	const int nPacketTicks = std::min(nProcessableTicks + 1, Shifting::nMaxUsrCmdProcessTicks);
	if (nProcessableTicks < CFG::Exploits_RapidFire_Ticks && GetShotsWithinPacket(pWeapon, nPacketTicks) <= 1)
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

	return G::bCanPrimaryAttack || bCanFireNextTick || CanInterruptReload(pWeapon);
}

int CRapidFire::GetTicks(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		pWeapon = H::Entities->GetWeapon();

	if (!CanDoubleTapNow(nullptr, pWeapon))
		return 0;

	const int nProcessableTicks = F::Ticks->GetProcessableTicks();
	return std::min(CFG::Exploits_RapidFire_Ticks, nProcessableTicks);
}

// ============================================
// FAST STICKY SHOOTING
// ============================================

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

	// Need at least 8 recharge limit for fast sticky to be useful
	if (CFG::Exploits_Shifting_Recharge_Limit < 8)
		return false;

	return true;
}

bool CRapidFire::ShouldStartFastSticky(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (Shifting::bShifting || Shifting::bShiftingWarp)
		return false;

	if (F::Ticks->GetProcessableTicks() < 1)
		return false;

	return true;
}

void CRapidFire::RunFastSticky(CUserCmd* pCmd, bool* pSendPacket)
{
	if (!pSendPacket)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
	{
		m_bStickyCharging = false;
		Shifting::nStickyRechargeTarget = 0;
		return;
	}

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon || !IsStickyWeapon(pWeapon))
	{
		m_bStickyCharging = false;
		Shifting::nStickyRechargeTarget = 0;
		return;
	}

	if (I::MatSystemSurface->IsCursorVisible() || I::EngineVGui->IsGameUIVisible())
	{
		m_bStickyCharging = false;
		Shifting::nStickyRechargeTarget = 0;
		return;
	}

	if (Shifting::bShifting || Shifting::bShiftingWarp)
		return;

	const bool bKeyDown = H::Input->IsDown(CFG::Exploits_FastSticky_Key);

	if (!bKeyDown)
	{
		m_bStickyCharging = false;
		Shifting::nStickyRechargeTarget = 0;
		return;
	}

	if (!IsFastStickyUsable())
	{
		m_bStickyCharging = false;
		Shifting::nStickyRechargeTarget = 0;
		return;
	}

	m_bStickyCharging = true;

	const bool bCanFire = G::bCanSecondaryAttack && pWeapon->HasPrimaryAmmoForShot();
	const int nMaxRecharge = GetFastStickyMaxRecharge();
	const int nTargetTicks = GetStickyPreferredTicks(pWeapon);
	const int nRechargeTarget = std::min(nTargetTicks, nMaxRecharge);

	// Tell CL_Move to stop recharging once we have enough ticks for the next shot
	Shifting::nStickyRechargeTarget = nRechargeTarget;

	if (!bCanFire)
	{
		if (Shifting::nAvailableTicks >= nRechargeTarget)
			Shifting::bRecharging = false;
		else
			Shifting::bRecharging = true;
		return;
	}

	int nTicksToUse = 0;

	const int nProcessableTicks = F::Ticks->GetProcessableTicks();
	if (nProcessableTicks >= nTargetTicks)
		nTicksToUse = nTargetTicks;
	else if (nProcessableTicks >= 1)
		nTicksToUse = nProcessableTicks;

	if (nTicksToUse >= 1)
	{
		Shifting::bRecharging = false;
		pCmd->buttons |= IN_ATTACK;

		Shifting::bStickyDTWantShift = true;
		Shifting::nStickyDTTicksToUse = nTicksToUse;

		m_ShiftCmd = *pCmd;
		m_ShiftCmd.buttons |= IN_ATTACK;

		m_bShiftSilentAngles = G::bSilentAngles;
		m_bSetCommand = false;

		m_vShiftStart = pLocal->m_vecOrigin();
		m_vAntiWarpVelocity = pLocal->m_vecVelocity();
		m_bStartedShiftOnGround = pLocal->m_fFlags() & FL_ONGROUND;
		m_nAntiWarpMaxTicks = 0;
	}
	else
	{
		const int nRechargeTarget = std::min(nTargetTicks, nMaxRecharge);
		if (Shifting::nAvailableTicks >= nRechargeTarget)
			Shifting::bRecharging = false;
		else
			Shifting::bRecharging = true;
	}
}
