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

	// For hitscan weapons: original logic - need target, firing, and 24 ticks since can fire
	if (G::nTicksSinceCanFire < 24 || G::nTargetIndex <= 1 || !G::bFiring)
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

	if (ShouldStart(pLocal, pWeapon))
	{
		const bool bIsProjectile = IsProjectileWeapon(pWeapon);

		// For hitscan: check target same ticks
		if (!bIsProjectile && G::nTicksTargetSame < CFG::Exploits_RapidFire_Min_Ticks_Target_Same)
		{
			pCmd->buttons &= ~IN_ATTACK;
			G::bFiring = false;
			return;
		}

		Shifting::bRapidFireWantShift = true;

		// Save the command WITH IN_ATTACK set - this ensures all shifted ticks will attack
		m_ShiftCmd = *pCmd;
		m_ShiftCmd.buttons |= IN_ATTACK;  // Force attack in saved command
		
		m_bShiftSilentAngles = G::bSilentAngles || G::bPSilentAngles;
		m_bSetCommand = false;
		m_bIsProjectileDT = bIsProjectile;

		m_vShiftStart = pLocal->m_vecOrigin();
		m_bStartedShiftOnGround = pLocal->m_fFlags() & FL_ONGROUND;

		// Remove attack on this tick - it will be added on first tick of shift
		// This is the same for both hitscan and projectile
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

	if (Shifting::bShifting && !Shifting::bShiftingWarp)
	{
		m_ShiftCmd.command_number = pCmd->command_number;

		// Copy the saved command first
		*pCmd = m_ShiftCmd;

		if (m_bIsProjectileDT)
		{
			// Projectile DT: fire on FIRST tick only, then skip cooldown
			if (!m_bSetCommand)
			{
				pCmd->buttons |= IN_ATTACK;  // Fire on first tick
				m_bSetCommand = true;
			}
			else
			{
				pCmd->buttons &= ~IN_ATTACK;  // Don't attack on remaining ticks (skip cooldown)
			}
		}
		else
		{
			// Hitscan DT: attack on all shifted ticks to fire multiple shots
			pCmd->buttons |= IN_ATTACK;
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
		|| nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER   // Charge weapon
		|| nWeaponID == TF_WEAPON_FLAMETHROWER
		|| nWeaponID == TF_WEAPON_CANNON             // Charge weapon
		|| pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
		return false;

	return true;
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
