#include "RapidFire.h"

#include "../CFG.h"
#include "../Crits/Crits.h"

bool IsRapidFireWeapon(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	// NOTE: Minigun is NOT included here because it should respect the tick cooldown
	// like other weapons. Minigun doubletap should wait for ticks to recharge.
	case TF_WEAPON_PISTOL:
	case TF_WEAPON_PISTOL_SCOUT:
	case TF_WEAPON_SMG: return true;
	default: return false;
	}
}

bool CRapidFire::ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (G::nTargetIndex <= 1 || !G::bFiring || Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return false;

	// Rapid-fire weapons (minigun, pistol, SMG) don't need the 24-tick wait
	// They fire continuously so the wait doesn't make sense
	if (!IsRapidFireWeapon(pWeapon))
	{
		if (G::nTicksSinceCanFire < 24)
			return false;
	}

	// Calculate effective ticks needed
	// If slider is at 23 (MAX), use all available ticks but require minimum 2
	int nEffectiveTicks;
	if (CFG::Misc_AntiCheat_Enabled)
	{
		nEffectiveTicks = std::min(CFG::Exploits_RapidFire_Ticks, 8);
	}
	else if (CFG::Exploits_RapidFire_Ticks >= 23)
	{
		// MAX mode - require at least 2 ticks to do anything useful
		if (Shifting::nAvailableTicks < 2)
			return false;
		nEffectiveTicks = Shifting::nAvailableTicks;
	}
	else
	{
		nEffectiveTicks = CFG::Exploits_RapidFire_Ticks;
	}

	if (Shifting::nAvailableTicks < nEffectiveTicks)
		return false;

	if (!(H::Input->IsDown(CFG::Exploits_RapidFire_Key)))
		return false;

	if (!IsWeaponSupported(pWeapon))
		return false;

	// Disable double tap while airborne (save ticks for projectile dodge)
	if (CFG::Misc_Projectile_Dodge_Enabled && CFG::Misc_Projectile_Dodge_Disable_DT_Airborne)
	{
		if (!(pLocal->m_fFlags() & FL_ONGROUND))
			return false; // Airborne - don't use DT
	}

	// Don't start doubletap if safe mode is waiting for a crit
	// This prevents wasting DT ticks while waiting for a crit command
	if (!F::CritHack->ShouldAllowFire(pLocal, pWeapon, G::CurrentUserCmd))
		return false;

	// NOTE: We don't check hitchance here anymore because G::bFiring already implies
	// that ShouldFire() passed, which includes hitchance and smart shotgun checks.
	// This ensures RapidFire only starts when the aimbot actually decided to fire.

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
		//hacky
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

		// Save shift start position and ground state
		m_vShiftStart = pLocal->m_vecOrigin();
		m_bStartedShiftOnGround = pLocal->m_fFlags() & FL_ONGROUND;

		// Remove attack button on the real tick so shots fire during shifted ticks
		pCmd->buttons &= ~IN_ATTACK;
		
		// Minigun special handling - keep spin up (attack2) so it stays revved
		if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
		{
			pCmd->buttons |= IN_ATTACK2;
		}

		*pSendPacket = true;
	}
	else
	{
		// Nothing to do when not starting DT
	}
}

bool CRapidFire::ShouldExitCreateMove(CUserCmd* pCmd)
{
	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return false;

	// Only handle rapid fire shifting, not warp shifting
	if (Shifting::bShiftingRapidFire)
	{
		// Recalculate angle on every tick
		bool bRecalculateAngle = true;
		
		if (bRecalculateAngle)
		{
			// Don't exit early - let CreateMove and aimbot run to recalculate angle
			// But still set up the command basics
			pCmd->buttons |= IN_ATTACK;
			G::bFiring = true;
			G::bSilentAngles = m_bShiftSilentAngles;
			
			// Apply anti-warp if enabled and started on ground
			if (CFG::Exploits_RapidFire_Antiwarp && m_bStartedShiftOnGround)
			{
				const float flTicks = std::max(14.f, std::min(24.f, static_cast<float>(CFG::Exploits_RapidFire_Ticks)));
				const float flScale = Math::RemapValClamped(flTicks, 14.f, 24.f, 0.605f, 1.f);
				SDKUtils::WalkTo(pCmd, pLocal->m_vecOrigin(), m_vShiftStart, flScale);
			}
			
			return false; // Let aimbot recalculate
		}
		
		// Replay the saved command exactly - don't recalculate angles
		// The aimbot already calculated the correct angles on the first tick
		m_ShiftCmd.command_number = pCmd->command_number;
		
		// Copy the entire saved command
		*pCmd = m_ShiftCmd;
		
		// Ensure attack button is set
		pCmd->buttons |= IN_ATTACK;
		
		// Apply anti-warp if enabled and started on ground
		if (CFG::Exploits_RapidFire_Antiwarp && m_bStartedShiftOnGround)
		{
			const float flTicks = std::max(14.f, std::min(24.f, static_cast<float>(CFG::Exploits_RapidFire_Ticks)));
			const float flScale = Math::RemapValClamped(flTicks, 14.f, 24.f, 0.605f, 1.f);
			SDKUtils::WalkTo(pCmd, pLocal->m_vecOrigin(), m_vShiftStart, flScale);
		}
		
		// Mark that we're firing during rapid fire shift
		G::bFiring = true;
		G::bSilentAngles = m_bShiftSilentAngles;
		
		// Return true to exit CreateMove early - we don't need aimbot to recalculate
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

int CRapidFire::GetTicks(C_TFWeaponBase* pWeapon)
{
	// Get weapon if not provided
	if (!pWeapon)
		pWeapon = H::Entities->GetWeapon();

	// No weapon or unsupported weapon
	if (!pWeapon || !IsWeaponSupported(pWeapon))
		return 0;

	// Not enough ticks since can fire (threshold 24) - skip for rapid-fire weapons
	if (!IsRapidFireWeapon(pWeapon) && G::nTicksSinceCanFire < 24)
		return 0;

	// Currently shifting or recharging
	if (Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return 0;

	// Return available ticks capped by config
	// If slider is at 23 (MAX), return all available ticks
	if (CFG::Exploits_RapidFire_Ticks >= 23)
		return Shifting::nAvailableTicks;
	
	return std::min(CFG::Exploits_RapidFire_Ticks, Shifting::nAvailableTicks);
}
