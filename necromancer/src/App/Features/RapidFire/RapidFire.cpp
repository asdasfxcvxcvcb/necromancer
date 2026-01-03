#include "RapidFire.h"

#include "../CFG.h"
#include "../Crits/Crits.h"

// ============================================
// Weapon Fire Rate Table
// Based on TF2 weapon data - fire delays in seconds
// At 66 tick, 1 tick = 0.015151... seconds
// ============================================
static const WeaponFireData_t g_WeaponFireData[] = {
	// Hitscan weapons
	{ TF_WEAPON_SCATTERGUN,           0.625f,  41, true  },
	{ TF_WEAPON_SODA_POPPER,          0.625f,  41, true  },
	{ TF_WEAPON_PEP_BRAWLER_BLASTER,  0.625f,  41, true  },
	{ TF_WEAPON_SHOTGUN_PRIMARY,      0.625f,  41, true  },
	{ TF_WEAPON_SHOTGUN_SOLDIER,      0.625f,  41, true  },
	{ TF_WEAPON_SHOTGUN_HWG,          0.625f,  41, true  },
	{ TF_WEAPON_SHOTGUN_PYRO,         0.625f,  41, true  },
	{ TF_WEAPON_SHOTGUN_BUILDING_RESCUE, 0.625f, 41, true },
	{ TF_WEAPON_SENTRY_REVENGE,       0.625f,  41, true  },
	{ TF_WEAPON_PISTOL,               0.15f,   10, false },
	{ TF_WEAPON_PISTOL_SCOUT,         0.15f,   10, false },
	{ TF_WEAPON_SMG,                  0.1f,    7,  false },
	{ TF_WEAPON_MINIGUN,              0.1f,    7,  false },
	{ TF_WEAPON_REVOLVER,             0.5f,    33, false },
	{ TF_WEAPON_SNIPERRIFLE,          1.5f,    99, false },
	{ TF_WEAPON_SNIPERRIFLE_CLASSIC,  1.5f,    99, false },
	{ TF_WEAPON_SNIPERRIFLE_DECAP,    1.5f,    99, false },
	{ TF_WEAPON_ROCKETLAUNCHER,       0.8f,    53, true  },
	{ TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT, 0.8f, 53, true },
	{ TF_WEAPON_GRENADELAUNCHER,      0.6f,    40, true  },
	{ TF_WEAPON_PIPEBOMBLAUNCHER,     0.6f,    40, false },
	{ TF_WEAPON_NONE,                 0.5f,    33, true  },
};

static const int g_nWeaponFireDataCount = sizeof(g_WeaponFireData) / sizeof(g_WeaponFireData[0]);

bool IsRapidFireWeapon(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PISTOL:
	case TF_WEAPON_PISTOL_SCOUT:
	case TF_WEAPON_SMG: return true;
	default: return false;
	}
}

const WeaponFireData_t* CRapidFire::GetWeaponFireData(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return &g_WeaponFireData[g_nWeaponFireDataCount - 1];
	
	const int nWeaponID = pWeapon->GetWeaponID();
	
	for (int i = 0; i < g_nWeaponFireDataCount - 1; i++)
	{
		if (g_WeaponFireData[i].nWeaponID == nWeaponID)
			return &g_WeaponFireData[i];
	}
	
	return &g_WeaponFireData[g_nWeaponFireDataCount - 1];
}

void CRapidFire::CalculateFireTicks(C_TFWeaponBase* pWeapon, int nTotalTicks)
{
	m_vecFireTicks.clear();
	
	if (nTotalTicks <= 0)
		return;
	
	const WeaponFireData_t* pData = GetWeaponFireData(pWeapon);
	
	m_vecFireTicks.push_back(0);
	
	if (pData->nTicksBetweenShots > 0 && pData->nTicksBetweenShots < nTotalTicks)
	{
		int nNextFireTick = pData->nTicksBetweenShots;
		while (nNextFireTick < nTotalTicks)
		{
			m_vecFireTicks.push_back(nNextFireTick);
			nNextFireTick += pData->nTicksBetweenShots;
		}
	}
	
	m_nShotsToFire = static_cast<int>(m_vecFireTicks.size());
}

bool CRapidFire::CanFireDuringReload(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;
	
	// Must have ammo in clip to fire
	if (pWeapon->m_iClip1() <= 0)
		return false;
	
	// Weapons that reload singly (shotguns, rocket launchers, grenade launchers)
	// can interrupt their reload by pressing attack if they have ammo in clip
	// This is how TF2 works - see tf_weaponbase.cpp CheckReload()
	if (pWeapon->m_bReloadsSingly())
		return true;
	
	// Check weapon fire data for other weapons that support reload interruption
	const WeaponFireData_t* pData = GetWeaponFireData(pWeapon);
	if (pData->bCanFireWhileReloading)
		return true;
	
	return false;
}

Vec3 CRapidFire::PredictTargetPosition(C_TFPlayer* pTarget, int nTickOffset)
{
	if (!pTarget)
		return m_vSavedTargetPos;
	
	const float flTime = nTickOffset * TICK_INTERVAL;
	return m_vSavedTargetPos + (m_vSavedTargetVelocity * flTime);
}


// Check if weapon needs air antiwarp (rapid fire weapons that benefit from it)
static bool NeedsAirAntiWarp(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;
	
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_SCATTERGUN:        // Force-A-Nature uses scattergun ID
	case TF_WEAPON_SODA_POPPER:
	case TF_WEAPON_PISTOL:
	case TF_WEAPON_PISTOL_SCOUT:
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_SMG:
		return true;
	default:
		return false;
	}
}

void CRapidFire::ApplyAirAntiWarp(CUserCmd* pCmd, C_TFPlayer* pLocal, int nCurrentTick, int nTotalTicks)
{
	if (!pLocal || !pCmd)
		return;
	
	if (m_bStartedShiftOnGround)
	{
		const float flTicks = std::max(14.f, std::min(24.f, static_cast<float>(nTotalTicks)));
		const float flScale = Math::RemapValClamped(flTicks, 14.f, 24.f, 0.605f, 1.f);
		SDKUtils::WalkTo(pCmd, pLocal->m_vecOrigin(), m_vShiftStart, flScale);
	}
	else
	{
		// Air anti-warp only for specific rapid fire weapons
		auto pWeapon = H::Entities->GetWeapon();
		if (!NeedsAirAntiWarp(pWeapon))
			return;
		
		// Air anti-warp - try to maintain horizontal position
		Vec3 vCurrentPos = pLocal->m_vecOrigin();
		Vec3 vDelta = m_vShiftStart - vCurrentPos;
		
		if (vDelta.Length2D() > 1.0f)
		{
			Vec3 vDir = {};
			Math::VectorAngles(Vec3(vDelta.x, vDelta.y, 0.0f), vDir);
			
			const float flYaw = DEG2RAD(vDir.y - pCmd->viewangles.y);
			pCmd->forwardmove = std::cosf(flYaw) * 450.0f;
			pCmd->sidemove = -std::sinf(flYaw) * 450.0f;
		}
		else
		{
			pCmd->forwardmove = 0.0f;
			pCmd->sidemove = 0.0f;
		}
	}
}

bool CRapidFire::ShouldStart(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (G::nTargetIndex <= 1 || Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return false;

	// Minigun special case - check if spun up and has ammo
	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
	{
		int iState = pWeapon->As<C_TFMinigun>()->m_iWeaponState();
		if (iState != AC_STATE_FIRING && iState != AC_STATE_SPINNING)
			return false;
		if (!pWeapon->HasPrimaryAmmoForShot())
			return false;
	}
	else
	{
		// Need at least 2 in clip to doubletap (fire 2 shots)
		if (pWeapon->m_iClip1() < 2)
			return false;
	}

	// Amalgam-style check: allow DT if G::bCanPrimaryAttack OR G::bReloading
	// G::Attacking is set by SDK::IsAttacking which returns:
	// - 1 if can attack and pressing attack
	// - 2 if reloading and pressing attack (queued attack that will interrupt reload)
	// - 0 otherwise
	bool bAttacking = G::Attacking != 0;
	
	// Must be able to attack OR be reloading (with ammo to interrupt)
	if (!G::bCanPrimaryAttack && !G::bReloading)
		return false;
	
	// Must be attacking (or already in DT)
	if (!bAttacking)
		return false;

	// Skip the 24-tick wait for reload-interrupt DT
	// The weapon fire rate check doesn't apply when interrupting reload
	bool bIsReloadInterrupt = G::bReloading && !G::bCanPrimaryAttack;
	
	if (!IsRapidFireWeapon(pWeapon) && !bIsReloadInterrupt)
	{
		if (G::nTicksSinceCanFire < 24)
			return false;
	}

	int nEffectiveTicks;
	if (CFG::Misc_AntiCheat_Enabled)
	{
		nEffectiveTicks = std::min(CFG::Exploits_RapidFire_Ticks, 8);
	}
	else if (CFG::Exploits_RapidFire_Ticks >= 23)
	{
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

	if (CFG::Misc_Projectile_Dodge_Enabled && CFG::Misc_Projectile_Dodge_Disable_DT_Airborne)
	{
		if (!(pLocal->m_fFlags() & FL_ONGROUND))
			return false;
	}

	if (!F::CritHack->ShouldAllowFire(pLocal, pWeapon, G::CurrentUserCmd))
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
		
		// Track if this is a reload-interrupt DT
		m_bWasReloadInterrupt = G::bReloading && !G::bCanPrimaryAttack;
		
		m_nSavedTargetIndex = G::nTargetIndex;
		m_flSavedSimTime = 0.0f;
		m_vSavedTargetPos = {};
		m_vSavedTargetVelocity = {};
		m_nSavedTargetHitbox = HITBOX_PELVIS;
		
		if (m_nSavedTargetIndex > 0)
		{
			if (auto pTarget = I::ClientEntityList->GetClientEntity(m_nSavedTargetIndex))
			{
				if (auto pPlayer = pTarget->As<C_TFPlayer>())
				{
					m_flSavedSimTime = pPlayer->m_flSimulationTime();
					m_vSavedTargetVelocity = pPlayer->m_vecVelocity();
					
					if (H::AimUtils->IsWeaponCapableOfHeadshot(pWeapon))
						m_nSavedTargetHitbox = HITBOX_HEAD;
					else
						m_nSavedTargetHitbox = HITBOX_PELVIS;
					
					m_vSavedTargetPos = pPlayer->GetHitboxPos(m_nSavedTargetHitbox);
				}
			}
		}

		m_vShiftStart = pLocal->m_vecOrigin();
		m_vShiftVelocity = pLocal->m_vecVelocity();
		m_bStartedShiftOnGround = pLocal->m_fFlags() & FL_ONGROUND;
		
		int nTotalTicks;
		if (CFG::Exploits_RapidFire_Ticks >= 23)
			nTotalTicks = Shifting::nAvailableTicks;
		else
			nTotalTicks = std::min(CFG::Exploits_RapidFire_Ticks, Shifting::nAvailableTicks);
		
		CalculateFireTicks(pWeapon, nTotalTicks);

		pCmd->buttons &= ~IN_ATTACK;
		
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

	if (Shifting::bShiftingRapidFire)
	{
		m_ShiftCmd.command_number = pCmd->command_number;
		*pCmd = m_ShiftCmd;
		
		const int nCurrentTick = Shifting::nCurrentShiftTick;
		const int nTotalTicks = Shifting::nTotalShiftTicks;
		
		// Check if this was a reload-interrupt DT
		// During reload interrupt, we need to keep IN_ATTACK held to abort reload
		// The first tick with IN_ATTACK will abort reload, subsequent ticks will fire
		bool bIsReloadInterruptDT = m_bWasReloadInterrupt;
		
		// For reload-interrupt DT, we ALWAYS fire on tick 0 to abort the reload
		// Then subsequent ticks follow the normal fire schedule
		bool bShouldFireThisTick = false;
		
		// Always fire on first tick (tick 0) - this aborts reload if active
		if (nCurrentTick == 0)
		{
			bShouldFireThisTick = true;
		}
		else
		{
			// Check fire schedule for subsequent ticks
			for (int nFireTick : m_vecFireTicks)
			{
				if (nFireTick == nCurrentTick)
				{
					bShouldFireThisTick = true;
					break;
				}
			}
		}
		
		// During reload-interrupt DT, keep IN_ATTACK held on ALL ticks
		// This ensures the reload is properly aborted and we fire ASAP
		if (bIsReloadInterruptDT)
		{
			pCmd->buttons |= IN_ATTACK;
			bShouldFireThisTick = true; // Mark as firing for aimbot
		}
		else
		{
			if (bShouldFireThisTick)
				pCmd->buttons |= IN_ATTACK;
			else
				pCmd->buttons &= ~IN_ATTACK;
		}
		
		if (m_nSavedTargetIndex > 0 && bShouldFireThisTick)
		{
			if (auto pTarget = I::ClientEntityList->GetClientEntity(m_nSavedTargetIndex))
			{
				if (auto pPlayer = pTarget->As<C_TFPlayer>())
				{
					if (!pPlayer->deadflag())
					{
						Vec3 vTargetPos = PredictTargetPosition(pPlayer, nCurrentTick);
						
						Vec3 vCurrentHitbox = pPlayer->GetHitboxPos(m_nSavedTargetHitbox);
						if (vTargetPos.DistTo(vCurrentHitbox) > 100.0f)
							vTargetPos = vCurrentHitbox;
						
						Vec3 vLocalPos = pLocal->GetShootPos();
						Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vTargetPos);
						vAngleTo -= pLocal->m_vecPunchAngle();
						Math::ClampAngles(vAngleTo);
						
						if (m_bShiftSilentAngles)
						{
							H::AimUtils->FixMovement(pCmd, vAngleTo);
						}
						pCmd->viewangles = vAngleTo;
					}
				}
			}
		}
		
		if (CFG::Exploits_RapidFire_Antiwarp)
		{
			ApplyAirAntiWarp(pCmd, pLocal, nCurrentTick, nTotalTicks);
		}
		
		G::bFiring = bShouldFireThisTick;
		G::bSilentAngles = m_bShiftSilentAngles;
		
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
	if (!pWeapon)
		pWeapon = H::Entities->GetWeapon();

	if (!pWeapon || !IsWeaponSupported(pWeapon))
		return 0;

	// Minigun special case - check if spun up and has ammo
	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
	{
		int iState = pWeapon->As<C_TFMinigun>()->m_iWeaponState();
		if (iState != AC_STATE_FIRING && iState != AC_STATE_SPINNING)
			return 0;
		if (!pWeapon->HasPrimaryAmmoForShot())
			return 0;
	}
	else
	{
		// Need at least 2 in clip to doubletap (for non-minigun weapons)
		if (pWeapon->m_iClip1() < 2)
			return 0;
	}

	// Skip the 24-tick wait for reload-interrupt DT (like Amalgam)
	bool bIsReloadInterrupt = G::bReloading && !G::bCanPrimaryAttack;
	if (!IsRapidFireWeapon(pWeapon) && !bIsReloadInterrupt && G::nTicksSinceCanFire < 24)
		return 0;

	if (Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
		return 0;

	if (CFG::Exploits_RapidFire_Ticks >= 23)
		return Shifting::nAvailableTicks;
	
	return std::min(CFG::Exploits_RapidFire_Ticks, Shifting::nAvailableTicks);
}
