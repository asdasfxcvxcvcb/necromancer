#include "RapidFire.h"

#include "../CFG.h"
#include "../Crits/Crits.h"
#include "../Hitchance/Hitchance.h"

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
	if (G::nTicksSinceCanFire < 24 || G::nTargetIndex <= 1 || !G::bFiring || Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return false;

	// When anti-cheat is enabled, limit to 8 ticks max
	const int nEffectiveTicks = CFG::Misc_AntiCheat_Enabled 
		? std::min(CFG::Exploits_RapidFire_Ticks, 8) 
		: CFG::Exploits_RapidFire_Ticks;

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

	// Hitchance check - don't start rapid fire if hitchance is too low
	if (CFG::Aimbot_Hitscan_Hitchance > 0)
	{
		// Get target entity
		auto pTarget = I::ClientEntityList->GetClientEntity(G::nTargetIndex);
		if (pTarget)
		{
			// Determine hitbox radius based on target type
			float flHitboxRadius = 12.0f;
			if (pTarget->GetClassId() == ETFClassIds::CTFPlayer)
				flHitboxRadius = 10.0f; // Head size for players
			else if (pTarget->GetClassId() == ETFClassIds::CObjectSentrygun ||
					 pTarget->GetClassId() == ETFClassIds::CObjectDispenser ||
					 pTarget->GetClassId() == ETFClassIds::CObjectTeleporter)
				flHitboxRadius = 24.0f; // Buildings

			// Get target position (use center of entity)
			Vec3 vTargetPos = pTarget->GetAbsOrigin();
			if (pTarget->GetClassId() == ETFClassIds::CTFPlayer)
			{
				// Use eye position for players
				const auto pPlayer = pTarget->As<C_TFPlayer>();
				if (pPlayer)
					vTargetPos = pPlayer->GetAbsOrigin() + Vec3(0, 0, 40); // Approximate body center
			}

			const Vec3 vShootPos = pLocal->GetShootPos();
			const float flHitchance = F::Hitchance->Calculate(pLocal, pWeapon, vShootPos, vTargetPos, flHitboxRadius, true, 2);

			if (flHitchance < static_cast<float>(CFG::Aimbot_Hitscan_Hitchance))
				return false;
		}
	}

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

		// Save shift start position and ground state (from Amalgam)
		m_vShiftStart = pLocal->m_vecOrigin();
		m_bStartedShiftOnGround = pLocal->m_fFlags() & FL_ONGROUND;

		// Save to Shifting namespace for cross-system access (from Amalgam)
		Shifting::vShiftStartPos = m_vShiftStart;
		Shifting::bStartedOnGround = m_bStartedShiftOnGround;
		Shifting::SavedCmd = *pCmd;
		Shifting::bSavedAngles = G::bSilentAngles || G::bPSilentAngles;
		Shifting::bHasSavedCmd = true;
		
		// Save target info for aimbot during shifted ticks
		Shifting::nRapidFireTargetIndex = G::nTargetIndex;

		/*if (CFG::Exploits_RapidFire_Antiwarp)
		{
			Vec3 vAngle = {};
			Math::VectorAngles(pLocal->m_vecVelocity(), vAngle);

			pCmd->viewangles.x = 90.0f;
			pCmd->viewangles.y = vAngle.y;
			pCmd->forwardmove = pCmd->sidemove = 0.0f;

			G::bSilentAngles = true;
		}*/

		// Remove attack button on the real tick so both shots fire during shifted ticks
		pCmd->buttons &= ~IN_ATTACK;

		*pSendPacket = true;
	}
	else
	{
		// Clear saved command state when DT is not active and not shifting (from Amalgam)
		if (!Shifting::bShifting && !Shifting::bShiftingWarp)
		{
			Shifting::bHasSavedCmd = false;
			Shifting::bSavedAngles = false;
		}
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
		// During rapid fire shifting, we want to:
		// 1. Restore movement from saved command (anti-warp, etc.)
		// 2. Let aimbot run to recalculate angles for each tick
		// 3. Set attack button for each shifted tick
		
		if (Shifting::bHasSavedCmd)
		{
			// Restore movement but NOT viewangles - aimbot will set those
			pCmd->forwardmove = Shifting::SavedCmd.forwardmove;
			pCmd->sidemove = Shifting::SavedCmd.sidemove;
			pCmd->upmove = Shifting::SavedCmd.upmove;
			pCmd->buttons = Shifting::SavedCmd.buttons;
			pCmd->impulse = Shifting::SavedCmd.impulse;
			pCmd->weaponselect = Shifting::SavedCmd.weaponselect;
			pCmd->weaponsubtype = Shifting::SavedCmd.weaponsubtype;
			
			// Apply anti-warp if enabled and started on ground
			if (CFG::Exploits_RapidFire_Antiwarp && Shifting::bStartedOnGround)
			{
				const float flTicks = std::max(14.f, std::min(22.f, static_cast<float>(CFG::Exploits_RapidFire_Ticks)));
				const float flScale = Math::RemapValClamped(flTicks, 14.f, 22.f, 0.605f, 1.f);
				SDKUtils::WalkTo(pCmd, pLocal->m_vecOrigin(), Shifting::vShiftStartPos, flScale);
			}
			
			// Set attack button for this shifted tick
			pCmd->buttons |= IN_ATTACK;
			
			// Mark that we're firing during rapid fire shift
			G::bFiring = true;
			
			// Restore target index so aimbot knows what to aim at
			G::nTargetIndex = Shifting::nRapidFireTargetIndex;
		}
		else
		{
			// Fallback to member variable approach (legacy behavior)
			m_ShiftCmd.command_number = pCmd->command_number;

			if (!m_bSetCommand)
			{
				// Restore movement but NOT viewangles
				pCmd->forwardmove = m_ShiftCmd.forwardmove;
				pCmd->sidemove = m_ShiftCmd.sidemove;
				pCmd->upmove = m_ShiftCmd.upmove;
				pCmd->buttons = m_ShiftCmd.buttons;
				pCmd->impulse = m_ShiftCmd.impulse;
				pCmd->weaponselect = m_ShiftCmd.weaponselect;
				pCmd->weaponsubtype = m_ShiftCmd.weaponsubtype;
				m_bSetCommand = true;
			}

			if (CFG::Exploits_RapidFire_Antiwarp && m_bStartedShiftOnGround)
			{
				const float moveScale = Math::RemapValClamped(static_cast<float>(CFG::Exploits_RapidFire_Ticks), 14.0f, 22.0f, 0.605f, 1.0f);
				SDKUtils::WalkTo(pCmd, pLocal->m_vecOrigin(), m_vShiftStart, moveScale);
			}
			
			// Set attack button for this shifted tick
			pCmd->buttons |= IN_ATTACK;
			
			// Mark that we're firing during rapid fire shift
			G::bFiring = true;
		}

		// Return false to let aimbot run and recalculate angles for this tick
		// The aimbot will update viewangles based on current target position
		return false;
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

	// Not enough ticks since can fire (threshold 24)
	if (G::nTicksSinceCanFire < 24)
		return 0;

	// Currently shifting or recharging
	if (Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return 0;

	// Return available ticks capped by config
	return std::min(CFG::Exploits_RapidFire_Ticks, Shifting::nAvailableTicks);
}
