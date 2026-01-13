#include "RapidFire.h"

#include "../CFG.h"
#include "../Crits/Crits.h"

// Check if weapon needs antiwarp in air (rapid fire weapons)
bool NeedsAirAntiwarp(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	const int nWeaponID = pWeapon->GetWeaponID();
	const int nDefIndex = pWeapon->m_iItemDefinitionIndex();

	switch (nWeaponID)
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PISTOL:
	case TF_WEAPON_PISTOL_SCOUT:
	case TF_WEAPON_SMG:
		return true;
	case TF_WEAPON_SCATTERGUN:
		// Force-A-Nature and Soda Popper
		if (nDefIndex == Scout_m_ForceANature || nDefIndex == Scout_m_FestiveForceANature || nDefIndex == Scout_m_TheSodaPopper)
			return true;
		break;
	default:
		break;
	}

	return false;
}

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

bool CRapidFire::ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	// Don't start if already shifting
	if (Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return false;

	// Need enough ticks
	if (Shifting::nAvailableTicks < CFG::Exploits_RapidFire_Ticks)
		return false;

	// Need DT key held
	if (!(H::Input->IsDown(CFG::Exploits_RapidFire_Key)))
		return false;

	// Weapon must be supported
	if (!IsWeaponSupported(pWeapon))
		return false;

	// Tick tracking - delay shift if we're too far ahead of server
	if (Shifting::ShouldDelayShift(CFG::Exploits_RapidFire_Tick_Tracking))
		return false;

	// Projectile dodge airborne check
	if (CFG::Misc_Projectile_Dodge_Enabled && CFG::Misc_Projectile_Dodge_Disable_DT_Airborne)
	{
		if (!(pLocal->m_fFlags() & FL_ONGROUND))
			return false;
	}

	// Crit hack check
	if (!F::CritHack->ShouldAllowFire(pLocal, pWeapon, G::CurrentUserCmd))
		return false;

	// ============================================
	// STICKY LAUNCHER / LOOSE CANNON HANDLING
	// ============================================
	// These weapons fire on RELEASE of attack button
	// We check CanSecondaryAttack (which tracks m_flNextSecondaryAttack)
	// When you release a sticky, it sets m_flNextSecondaryAttack
	// We can DT to skip that cooldown
	if (IsStickyWeapon(pWeapon))
	{
		// Need target and firing intent
		if (G::nTargetIndex <= 1 || !G::bFiring)
			return false;

		// For sticky: check CanSecondaryAttack (like Amalgam)
		// This is set when m_flNextSecondaryAttack <= curtime
		if (!G::bCanSecondaryAttack)
			return false;

		// Need ammo
		if (!pWeapon->HasPrimaryAmmoForShot())
			return false;

		return true;
	}

	// For projectile weapons: trigger when aimbot wants to fire (G::bFiring)
	// Same as hitscan - we fire on first tick of shift, then skip cooldown
	if (IsProjectileWeapon(pWeapon))
	{
		// Need target and firing intent
		if (G::nTargetIndex <= 1 || !G::bFiring)
			return false;
		
		// For projectile: only trigger when weapon is ready to fire
		// Projectile weapons can't interrupt reload like some hitscan can
		if (!G::bCanPrimaryAttack)
			return false;

		// Only DT with full clip and not reloading
		if (pWeapon->IsInReload())
			return false;
		
		const int nMaxClip = pWeapon->GetMaxClip1();
		if (nMaxClip > 0 && pWeapon->m_iClip1() < nMaxClip)
			return false;

		return true;
	}

	// For hitscan weapons: need target and firing intent
	if (G::nTargetIndex <= 1 || !G::bFiring)
		return false;

	// Minigun special case: when fully revved up (FIRING or SPINNING state), allow DT immediately
	// No 24 tick delay needed - the minigun is already ready to fire
	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
	{
		const int nState = pWeapon->As<C_TFMinigun>()->m_iWeaponState();
		if (nState == AC_STATE_FIRING || nState == AC_STATE_SPINNING)
		{
			// Fully revved - can DT immediately, just need ammo
			if (!pWeapon->HasPrimaryAmmoForShot())
				return false;
			
			// Still respect target same ticks for accuracy
			if (G::nTicksTargetSame < CFG::Exploits_RapidFire_Min_Ticks_Target_Same)
				return false;
			
			return true;
		}
		// Not fully revved - don't allow DT
		return false;
	}

	// For other hitscan weapons: need 24 ticks since can fire
	if (G::nTicksSinceCanFire < 24)
		return false;

	if (G::nTicksTargetSame < CFG::Exploits_RapidFire_Min_Ticks_Target_Same)
		return false;

	return true;
}

void CRapidFire::Run(CUserCmd* pCmd, bool* pSendPacket)
{
	Shifting::bRapidFireWantShift = false;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon)
		return;

	// Only interfere with firing if we're about to start a DT shift
	// Don't block normal firing just because DT key is held - let aimbot work normally
	// RapidFire should only take over when ShouldStart returns true

	if (ShouldStart(pLocal, pWeapon))
	{
		const bool bIsProjectile = IsProjectileWeapon(pWeapon);
		const bool bIsSticky = IsStickyWeapon(pWeapon);

		Shifting::bRapidFireWantShift = true;

		// Save the command - we'll modify buttons during shift
		m_ShiftCmd = *pCmd;
		
		// For sticky weapons: save command WITHOUT attack (we fire by releasing)
		// For other weapons: save command WITH attack
		if (bIsSticky)
		{
			m_ShiftCmd.buttons &= ~IN_ATTACK;  // Sticky fires on release
		}
		else
		{
			m_ShiftCmd.buttons |= IN_ATTACK;  // Other weapons fire on press
		}
		
		m_bShiftSilentAngles = G::bSilentAngles || G::bPSilentAngles;
		m_bSetCommand = false;
		m_bIsProjectileDT = bIsProjectile;
		m_bIsStickyDT = bIsSticky;

		m_vShiftStart = pLocal->m_vecOrigin();
		m_bStartedShiftOnGround = pLocal->m_fFlags() & FL_ONGROUND;

		// Remove attack on this tick - it will be handled in the shift
		pCmd->buttons &= ~IN_ATTACK;

		// Keep minigun spinning during DT
		if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
		{
			pCmd->buttons |= IN_ATTACK2;
		}

		*pSendPacket = true;
	}
}

bool CRapidFire::ShouldExitCreateMove(CUserCmd* pCmd)
{
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return false;

	// Only handle RapidFire shifts, not Warp
	if (Shifting::bShifting && !Shifting::bShiftingWarp)
	{
		m_ShiftCmd.command_number = pCmd->command_number;

		// Copy the saved command first
		*pCmd = m_ShiftCmd;

		if (m_bIsStickyDT)
		{
			// Sticky DT: Press attack on tick 0, release on tick 1 to fire instantly
			// No charging - just press/release
			if (!m_bSetCommand)
			{
				// First tick: press attack to start
				pCmd->buttons |= IN_ATTACK;
				m_bSetCommand = true;
			}
			else
			{
				// Second tick onwards: release attack to fire, then no attack
				pCmd->buttons &= ~IN_ATTACK;
			}
		}
		else if (m_bIsProjectileDT)
		{
			// Projectile DT: fire on FIRST tick only (saved cmd has attack)
			// Then no attack on remaining ticks (skip cooldown)
			if (!m_bSetCommand)
			{
				// First tick: saved cmd has attack, projectile fires
				m_bSetCommand = true;
			}
			else
			{
				// Remaining ticks: no attack (skip cooldown)
				pCmd->buttons &= ~IN_ATTACK;
			}
		}
		else
		{
			// Hitscan DT: attack on all shifted ticks to fire multiple shots
			// Saved cmd already has attack set
			m_bSetCommand = true;
		}

		// Antiwarp - adjust movement to stay in place
		if (CFG::Exploits_RapidFire_Antiwarp)
		{
			const auto pWeapon = H::Entities->GetWeapon();
			
			// On ground: always antiwarp
			// In air: only antiwarp for specific weapons (FaN, Soda Popper, pistols, minigun, SMG)
			if (m_bStartedShiftOnGround || NeedsAirAntiwarp(pWeapon))
			{
				const float flScale = Math::RemapValClamped(static_cast<float>(CFG::Exploits_RapidFire_Ticks), 14.0f, 22.0f, 0.605f, 1.0f);
				SDKUtils::WalkTo(pCmd, pLocal->m_vecOrigin(), m_vShiftStart, flScale);
			}
		}

		return true;
	}

	return false;
}

bool CRapidFire::IsWeaponSupported(C_TFWeaponBase* pWeapon)
{
	const auto nWeaponType = H::AimUtils->GetWeaponType(pWeapon);

	if (nWeaponType == EWeaponType::MELEE || nWeaponType == EWeaponType::OTHER)
		return false;

	const auto nWeaponID = pWeapon->GetWeaponID();

	// These weapons don't work well with DT
	if (nWeaponID == TF_WEAPON_COMPOUND_BOW          // Charge weapon
		|| nWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC
		|| nWeaponID == TF_WEAPON_FLAMETHROWER
		|| pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
		return false;

	// Sticky launcher and Loose Cannon ARE supported - they use special handling
	// (check CanSecondaryAttack instead of CanPrimaryAttack)

	return true;
}

// Check if weapon is a sticky-type weapon (fires on release, uses secondary attack timing)
bool CRapidFire::IsStickyWeapon(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	const auto nWeaponID = pWeapon->GetWeaponID();
	return nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || nWeaponID == TF_WEAPON_CANNON;
}

// Check if weapon is a projectile weapon that benefits from post-fire DT
bool CRapidFire::IsProjectileWeapon(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	const auto nWeaponID = pWeapon->GetWeaponID();

	switch (nWeaponID)
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
	case TF_WEAPON_GRENADELAUNCHER:
	case TF_WEAPON_FLAREGUN:
	case TF_WEAPON_FLAREGUN_REVENGE:
	case TF_WEAPON_CROSSBOW:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
	case TF_WEAPON_SYRINGEGUN_MEDIC:
	case TF_WEAPON_RAYGUN:
	case TF_WEAPON_DRG_POMSON:
		return true;
	default:
		return false;
	}
}

int CRapidFire::GetTicks(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		pWeapon = H::Entities->GetWeapon();

	if (!pWeapon || !IsWeaponSupported(pWeapon))
		return 0;

	if (Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return 0;

	// Need enough ticks to actually DT (user configured amount)
	if (Shifting::nAvailableTicks < CFG::Exploits_RapidFire_Ticks)
		return 0;

	// For sticky weapons: check CanSecondaryAttack
	if (IsStickyWeapon(pWeapon))
	{
		if (!G::bCanSecondaryAttack)
			return 0;
		if (!pWeapon->HasPrimaryAmmoForShot())
			return 0;
		return std::min(CFG::Exploits_RapidFire_Ticks, Shifting::nAvailableTicks);
	}

	// For projectile weapons: need full clip and not reloading
	if (IsProjectileWeapon(pWeapon))
	{
		if (!G::bCanPrimaryAttack)
			return 0;
		if (pWeapon->IsInReload())
			return 0;
		const int nMaxClip = pWeapon->GetMaxClip1();
		if (nMaxClip > 0 && pWeapon->m_iClip1() < nMaxClip)
			return 0;
		return std::min(CFG::Exploits_RapidFire_Ticks, Shifting::nAvailableTicks);
	}

	// For hitscan weapons: need 24 ticks since can fire
	if (G::nTicksSinceCanFire < 24)
		return 0;

	return std::min(CFG::Exploits_RapidFire_Ticks, Shifting::nAvailableTicks);
}

// ============================================
// FAST STICKY SHOOTING
// ============================================
// Auto-fires stickies as fast as possible using DT
// NO CHARGING - instant fire, fires at your current viewangles
// Flow: shoot → use ticks → recharge → shoot → use ticks → recharge
//
// Smart tick usage:
// - Tries to use 21 ticks first (optimal for fast sticky)
// - Falls back to available ticks if less than 21
// - Recharge respects anticheat settings and user limits
// - If user recharge limit is 12 or below, fast sticky is useless

// Calculate max ticks we can recharge to for fast sticky
int CRapidFire::GetFastStickyMaxRecharge()
{
	// If anticheat is ON and ignore tick limit is OFF, we're limited to 8 ticks
	// That's too low for effective fast sticky
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
		return 8; // Anticheat hard limit

	// User has ignore tick limit ON or anticheat OFF
	// Use user's recharge limit, capped at 24
	const int nUserLimit = std::clamp(CFG::Exploits_Shifting_Recharge_Limit, 2, 24);

	// Return the user limit (we want 21 ideally, but respect user settings)
	return nUserLimit;
}

// Check if fast sticky is even worth using with current settings
bool CRapidFire::IsFastStickyUsable()
{
	// If anticheat is ON and ignore tick limit is OFF, max is 8 - not enough
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
		return false;

	// If user's recharge limit is 12 or below, it's useless
	if (CFG::Exploits_Shifting_Recharge_Limit <= 12)
		return false;

	return true;
}

bool CRapidFire::ShouldStartFastSticky(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (Shifting::bShifting || Shifting::bShiftingWarp)
		return false;

	if (Shifting::nAvailableTicks < 1)
		return false;

	return true;
}

void CRapidFire::RunFastSticky(CUserCmd* pCmd, bool* pSendPacket)
{
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

	const bool bKeyDown = H::Input->IsDown(CFG::Exploits_FastSticky_Key);

	if (!bKeyDown)
	{
		m_bStickyCharging = false;
		return;
	}

	// Check if fast sticky is even usable with current settings
	if (!IsFastStickyUsable())
	{
		m_bStickyCharging = false;
		return;
	}

	m_bStickyCharging = true;

	// Check if weapon can fire
	const bool bCanFire = G::bCanSecondaryAttack && pWeapon->HasPrimaryAmmoForShot();

	// Calculate our target and max ticks
	const int nMaxRecharge = GetFastStickyMaxRecharge();
	const int nTargetTicks = 21; // Optimal for fast sticky

	// If we CAN'T fire yet, just recharge ticks (no attack button)
	if (!bCanFire)
	{
		// Recharge up to our target (21) or max allowed
		const int nRechargeTarget = std::min(nTargetTicks, nMaxRecharge);
		if (Shifting::nAvailableTicks < nRechargeTarget)
			Shifting::bRecharging = true;
		return;
	}

	// We CAN fire! Determine how many ticks to use
	// Priority: use 21 if available, otherwise use what we have
	int nTicksToUse = 0;

	if (Shifting::nAvailableTicks >= nTargetTicks)
	{
		// We have 21+ ticks, use exactly 21
		nTicksToUse = nTargetTicks;
	}
	else if (Shifting::nAvailableTicks >= 1)
	{
		// We have less than 21, use what we have
		nTicksToUse = Shifting::nAvailableTicks;
	}

	if (nTicksToUse >= 1)
	{
		// Stop recharging
		Shifting::bRecharging = false;

		// INSTANT FIRE - press attack to fire sticky at current viewangles
		pCmd->buttons |= IN_ATTACK;

		// Trigger DT with calculated ticks
		Shifting::bStickyDTWantShift = true;
		Shifting::nStickyDTTicksToUse = nTicksToUse;

		// Save command WITH attack (first tick fires)
		m_ShiftCmd = *pCmd;
		m_ShiftCmd.buttons |= IN_ATTACK;

		m_bShiftSilentAngles = G::bSilentAngles || G::bPSilentAngles;
		m_bSetCommand = false;
		m_bIsProjectileDT = false;
		m_bIsStickyDT = true;

		m_vShiftStart = pLocal->m_vecOrigin();
		m_bStartedShiftOnGround = pLocal->m_fFlags() & FL_ONGROUND;

		*pSendPacket = true;
	}
	else
	{
		// No ticks, recharge up to target
		const int nRechargeTarget = std::min(nTargetTicks, nMaxRecharge);
		if (Shifting::nAvailableTicks < nRechargeTarget)
			Shifting::bRecharging = true;
	}
}
