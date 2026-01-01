#include "AimbotMelee.h"

#include "../../CFG.h"
#include "../../Crits/Crits.h"

int CAimbotMelee::GetSwingTime(C_TFWeaponBase* pWeapon)
{
	// Knife has instant backstab, no swing delay
	if (pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
		return 0;
	
	// Get the actual smack delay from weapon data and convert to ticks
	int iSmackTicks = static_cast<int>(ceilf(pWeapon->GetSmackDelay() / I::GlobalVars->interval_per_tick));
	return iSmackTicks;
}

bool CAimbotMelee::ShouldTargetFriendlyBuilding(C_BaseObject* pBuilding, C_TFWeaponBase* pWeapon)
{
	const bool bIsWrench = pWeapon->GetWeaponID() == TF_WEAPON_WRENCH;
	const bool bCanRemoveSapper = SDKUtils::AttribHookValue(0.f, "set_dmg_apply_to_sapper", pWeapon) > 0.f;
	
	// Building has sapper - both wrench and homewrecker can remove
	if (pBuilding->m_bHasSapper())
		return bIsWrench || bCanRemoveSapper;
	
	// Only wrench can repair/upgrade
	if (!bIsWrench)
		return false;
	
	// Building needs repair
	if (pBuilding->m_iHealth() < pBuilding->m_iMaxHealth())
		return true;
	
	// Building can be upgraded (not mini and not max level)
	if (!pBuilding->m_bMiniBuilding() && pBuilding->m_iUpgradeLevel() < 3)
		return true;
	
	// Check sentry ammo
	if (pBuilding->GetClassId() == ETFClassIds::CObjectSentrygun)
	{
		const auto pSentry = pBuilding->As<C_ObjectSentrygun>();
		
		// Level 1 sentry: 150 max shells
		// Level 2 sentry: 200 max shells
		// Level 3 sentry: 200 max shells, 20 max rockets
		int iMaxShells = pSentry->m_iUpgradeLevel() == 1 ? 150 : 200;
		int iMaxRockets = pSentry->m_iUpgradeLevel() == 3 ? 20 : 0;
		
		if (pSentry->m_iAmmoShells() < iMaxShells)
			return true;
		
		if (iMaxRockets > 0 && pSentry->m_iAmmoRockets() < iMaxRockets)
			return true;
	}
	
	return false;
}

bool CAimbotMelee::CanHit(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, MeleeTarget_t& target)
{
	// Get weapon-specific range and hull size
	float flRange = SDKUtils::AttribHookValue(pWeapon->GetSwingRange(), "melee_range_multiplier", pWeapon);
	float flHull = SDKUtils::AttribHookValue(18.0f, "melee_bounds_multiplier", pWeapon);
	
	// Account for player model scale
	if (pLocal->m_flModelScale() > 1.0f)
	{
		flRange *= pLocal->m_flModelScale();
		flHull *= pLocal->m_flModelScale();
	}
	
	// Wrench has special range for friendly buildings
	if (pWeapon->GetWeaponID() == TF_WEAPON_WRENCH && target.Entity->m_iTeamNum() == pLocal->m_iTeamNum())
	{
		flRange = 70.0f;
		flHull = 18.0f;
	}

	Vec3 vSwingMins = { -flHull, -flHull, -flHull };
	Vec3 vSwingMaxs = { flHull, flHull, flHull };

	const Vec3 vEyePos = pLocal->GetShootPos();
	const bool bIsPlayer = target.Entity->GetClassId() == ETFClassIds::CTFPlayer;

	// For players with lag records, we need to test against the record's position
	if (bIsPlayer && target.LagRecord)
	{
		const auto pRecord = target.LagRecord;
		
		// Store original entity state
		Vec3 vRestoreOrigin = target.Entity->GetAbsOrigin();
		Vec3 vRestoreMins = target.Entity->m_vecMins();
		Vec3 vRestoreMaxs = target.Entity->m_vecMaxs();

		// Set entity to record's position and bounds (account for origin compression like Amalgam)
		target.Entity->SetAbsOrigin(pRecord->AbsOrigin);
		target.Entity->m_vecMins() = target.Entity->m_vecMins() + 0.125f;
		target.Entity->m_vecMaxs() = target.Entity->m_vecMaxs() - 0.125f;

		// Calculate optimal aim position - aim at Z height that matches local player when possible
		Vec3 vDiff = { 0, 0, std::clamp(vEyePos.z - pRecord->AbsOrigin.z, target.Entity->m_vecMins().z, target.Entity->m_vecMaxs().z) };
		target.Position = pRecord->AbsOrigin + vDiff;
		target.AngleTo = Math::CalcAngle(vEyePos, target.Position);

		Vec3 vForward;
		Math::AngleVectors(target.AngleTo, &vForward);
		Vec3 vTraceEnd = vEyePos + (vForward * flRange);

		// Set up bones for the trace
		F::LagRecordMatrixHelper->Set(pRecord);

		// First try: simple trace
		trace_t trace = {};
		CTraceFilterHitscan filter = {};
		filter.m_pIgnore = pLocal;
		
		H::AimUtils->TraceHull(vEyePos, vTraceEnd, {}, {}, MASK_SOLID, &filter, &trace);
		bool bHit = trace.m_pEnt && trace.m_pEnt == target.Entity;
		
		// Second try: hull trace
		if (!bHit)
		{
			H::AimUtils->TraceHull(vEyePos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
			bHit = trace.m_pEnt && trace.m_pEnt == target.Entity;
		}

		F::LagRecordMatrixHelper->Restore();

		// Restore entity state
		target.Entity->SetAbsOrigin(vRestoreOrigin);
		target.Entity->m_vecMins() = vRestoreMins;
		target.Entity->m_vecMaxs() = vRestoreMaxs;

		if (bHit)
		{
			target.MeleeTraceHit = true;
			return true;
		}

		// For smooth/assistive aim, check if current view angles can hit
		if (CFG::Aimbot_Melee_Aim_Type == 2 || CFG::Aimbot_Melee_Aim_Type == 3)
		{
			Vec3 vCurrentForward;
			Math::AngleVectors(I::EngineClient->GetViewAngles(), &vCurrentForward);
			Vec3 vCurrentTraceEnd = vEyePos + (vCurrentForward * flRange);
			
			target.MeleeTraceHit = H::AimUtils->TraceEntityMelee(target.Entity, vEyePos, vCurrentTraceEnd);
			return target.MeleeTraceHit;
		}

		return false;
	}

	// For non-backtrack targets (buildings, current position fallback)
	target.AngleTo = Math::CalcAngle(vEyePos, target.Position);
	
	Vec3 vForward;
	Math::AngleVectors(target.AngleTo, &vForward);
	Vec3 vTraceEnd = vEyePos + (vForward * flRange);

	// First try: simple trace
	bool bHit = H::AimUtils->TraceEntityMelee(target.Entity, vEyePos, vTraceEnd);
	
	// Second try: hull trace
	if (!bHit)
	{
		trace_t trace = {};
		CTraceFilterHitscan filter = {};
		filter.m_pIgnore = pLocal;
		
		H::AimUtils->TraceHull(vEyePos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
		bHit = trace.m_pEnt && trace.m_pEnt == target.Entity;
	}

	if (bHit)
	{
		target.MeleeTraceHit = true;
		return true;
	}

	// For smooth/assistive aim, check if current view angles can hit
	if (CFG::Aimbot_Melee_Aim_Type == 2 || CFG::Aimbot_Melee_Aim_Type == 3)
	{
		Vec3 vCurrentForward;
		Math::AngleVectors(I::EngineClient->GetViewAngles(), &vCurrentForward);
		Vec3 vCurrentTraceEnd = vEyePos + (vCurrentForward * flRange);
		
		target.MeleeTraceHit = H::AimUtils->TraceEntityMelee(target.Entity, vEyePos, vCurrentTraceEnd);
		return target.MeleeTraceHit;
	}

	return false;
}

bool CAimbotMelee::GetTarget(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, MeleeTarget_t& outTarget)
{
	const Vec3 vLocalPos = pLocal->GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	m_vecTargets.clear();
	
	// Separate vector for friendly buildings - these bypass the target max limit
	std::vector<MeleeTarget_t> vecFriendlyBuildings;

	// Find player targets
	if (CFG::Aimbot_Target_Players)
	{
		auto group{ pWeapon->m_iItemDefinitionIndex() == Soldier_t_TheDisciplinaryAction ? EEntGroup::PLAYERS_ALL : EEntGroup::PLAYERS_ENEMIES };

		if (!CFG::Aimbot_Melee_Whip_Teammates)
		{
			group = EEntGroup::PLAYERS_ENEMIES;
		}

		for (const auto pEntity : H::Entities->GetGroup(group))
		{
			if (!pEntity)
				continue;

			const auto pPlayer = pEntity->As<C_TFPlayer>();
			if (pPlayer->deadflag() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
				continue;

			if (pPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
			{
				if (CFG::Aimbot_Ignore_Friends && pPlayer->IsPlayerOnSteamFriendsList())
					continue;

				if (CFG::Aimbot_Ignore_Invisible && pPlayer->IsInvisible())
					continue;

				if (pWeapon->m_iItemDefinitionIndex() != Heavy_t_TheHolidayPunch && CFG::Aimbot_Ignore_Invulnerable && pPlayer->IsInvulnerable())
					continue;

				if (CFG::Aimbot_Ignore_Taunting && pPlayer->InCond(TF_COND_TAUNTING))
					continue;
			}

			if (pPlayer->m_iTeamNum() != pLocal->m_iTeamNum() && CFG::Aimbot_Melee_Target_LagRecords)
			{
				int nRecords = 0;
				bool bHasValidLagRecords = false;
				const bool bFakeLatencyActive = F::LagRecords->GetFakeLatency() > 0.0f;
				
				// Get the smack delay in ticks - we need records that will still be valid after this delay
				const int nSmackTicks = GetSwingTime(pWeapon);
				const float flSmackDelay = nSmackTicks * I::GlobalVars->interval_per_tick;
				
				// Get weapon-specific range (each melee has different range)
				float flSwingRange = SDKUtils::AttribHookValue(pWeapon->GetSwingRange(), "melee_range_multiplier", pWeapon);
				if (pLocal->m_flModelScale() > 1.0f)
					flSwingRange *= pLocal->m_flModelScale();
				
				// Calculate max distance we could possibly hit from, accounting for movement during smack delay
				// Local player max speed + some buffer for the target potentially moving towards us
				const float flMaxSpeed = pLocal->TeamFortress_CalculateMaxSpeed(false);
				const float flMovementDuringSmack = flMaxSpeed * flSmackDelay * 2.0f; // *2 for both players potentially moving
				const float flMaxTargetDist = flSwingRange + flMovementDuringSmack + 50.0f; // 50 unit buffer

				if (F::LagRecords->HasRecords(pPlayer, &nRecords))
				{
					// When fake latency is active: use the last 5 records (not including the very last one)
					// Prefer middle record (3rd from end), then 2nd and 4th, then 1st and 5th
					if (bFakeLatencyActive)
					{
						// Priority order for the 5 records before the last one
						const int priorityOffsets[] = { 3, 4, 2, 5, 1 };
						
						for (int offset : priorityOffsets)
						{
							const int n = nRecords - offset;
							if (n < 1) // Don't use record 0
								continue;
							
							const auto pRecord = F::LagRecords->GetRecord(pPlayer, n, true);
							if (!pRecord || !F::LagRecords->DiffersFromCurrent(pRecord))
								continue;
							
							// Check if this record will still be valid after the smack delay
							const float flRecordAge = I::GlobalVars->curtime - pRecord->SimulationTime;
							const float flAgeAtSmack = flRecordAge + flSmackDelay;
							if (flAgeAtSmack > F::LagRecords->GetMaxUnlag() - 0.1f)
								continue;

							Vec3 vPos = SDKUtils::GetHitboxPosFromMatrix(pPlayer, HITBOX_BODY, const_cast<matrix3x4_t*>(pRecord->BoneMatrix));
							const float flDistTo = vLocalPos.DistTo(vPos);
							
							// Skip if too far away to possibly hit even with movement
							if (flDistTo > flMaxTargetDist)
								continue;

							bHasValidLagRecords = true;
							Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
							const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;

							if (CFG::Aimbot_Melee_Sort == 0 && flFOVTo > CFG::Aimbot_Melee_FOV)
								continue;

							m_vecTargets.emplace_back(MeleeTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo, pRecord->SimulationTime, pRecord });
						}
					}
					else
					{
						// Normal behavior when fake latency is 0: use ALL lag records
						// but skip records that will be too old or too far by the time the smack happens
						for (int n = 0; n <= nRecords; n++)
						{
							const auto pRecord = F::LagRecords->GetRecord(pPlayer, n, true);
							if (!pRecord || !F::LagRecords->DiffersFromCurrent(pRecord))
								continue;
							
							// Check if this record will still be valid after the smack delay
							const float flRecordAge = I::GlobalVars->curtime - pRecord->SimulationTime;
							const float flAgeAtSmack = flRecordAge + flSmackDelay;
							if (flAgeAtSmack > F::LagRecords->GetMaxUnlag() - 0.1f)
								continue;

							Vec3 vPos = SDKUtils::GetHitboxPosFromMatrix(pPlayer, HITBOX_BODY, const_cast<matrix3x4_t*>(pRecord->BoneMatrix));
							const float flDistTo = vLocalPos.DistTo(vPos);
							
							// Skip if too far away to possibly hit even with movement
							if (flDistTo > flMaxTargetDist)
								continue;

							bHasValidLagRecords = true;
							Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
							const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;

							if (CFG::Aimbot_Melee_Sort == 0 && flFOVTo > CFG::Aimbot_Melee_FOV)
								continue;

							m_vecTargets.emplace_back(MeleeTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo, pRecord->SimulationTime, pRecord });
						}
					}
				}

				// Fallback: if no valid lag records exist that differ from current (enemy standing still), target the real model position
				// Skip this when fake latency is active - only use lag records
				if (!bHasValidLagRecords && !bFakeLatencyActive)
				{
					Vec3 vPos = pPlayer->GetHitboxPos(HITBOX_BODY);
					Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
					const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
					const float flDistTo = vLocalPos.DistTo(vPos);

					if (CFG::Aimbot_Melee_Sort == 0 && flFOVTo <= CFG::Aimbot_Melee_FOV)
					{
						m_vecTargets.emplace_back(MeleeTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo, pPlayer->m_flSimulationTime() });
					}
				}
			}
			else
			{
				// Not using lag records, just target current position
				Vec3 vPos = pPlayer->GetHitboxPos(HITBOX_BODY);
				Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
				const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
				const float flDistTo = vLocalPos.DistTo(vPos);

				if (CFG::Aimbot_Melee_Sort == 0 && flFOVTo > CFG::Aimbot_Melee_FOV)
					continue;

				m_vecTargets.emplace_back(MeleeTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo, pPlayer->m_flSimulationTime() });
			}
		}
	}

	// Find enemy building targets
	if (CFG::Aimbot_Target_Buildings)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
		{
			if (!pEntity)
				continue;

			const auto pBuilding = pEntity->As<C_BaseObject>();

			if (pBuilding->m_bPlacing())
				continue;

			Vec3 vPos = pBuilding->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
			const float flDistTo = vLocalPos.DistTo(vPos);

			if (CFG::Aimbot_Melee_Sort == 0 && flFOVTo > CFG::Aimbot_Melee_FOV)
				continue;

			m_vecTargets.emplace_back(MeleeTarget_t{ pBuilding, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	// Find friendly building targets (auto repair/upgrade) - stored separately to bypass target max
	if (CFG::Aimbot_Melee_Auto_Repair)
	{
		const bool bIsWrench = pWeapon->GetWeaponID() == TF_WEAPON_WRENCH;
		const bool bCanRemoveSapper = SDKUtils::AttribHookValue(0.f, "set_dmg_apply_to_sapper", pWeapon) > 0.f;
		
		if (bIsWrench || bCanRemoveSapper)
		{
			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_TEAMMATES))
			{
				if (!pEntity)
					continue;

				const auto pBuilding = pEntity->As<C_BaseObject>();

				if (pBuilding->m_bPlacing() || pBuilding->m_bCarried())
					continue;

				// Check if this building needs attention
				if (!ShouldTargetFriendlyBuilding(pBuilding, pWeapon))
					continue;

				Vec3 vPos = pBuilding->GetCenter();
				Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
				const float flDistTo = vLocalPos.DistTo(vPos);

				// No FOV check for friendly buildings - auto repair should work regardless of where you're looking
				MeleeTarget_t target = { pBuilding, vPos, vAngleTo, 0.0f, flDistTo };
				target.bIsFriendlyBuilding = true;
				vecFriendlyBuildings.emplace_back(target);
			}
		}
	}

	if (m_vecTargets.empty() && vecFriendlyBuildings.empty())
		return false;

	// Sort main targets by priority
	if (!m_vecTargets.empty())
		F::AimbotCommon->Sort(m_vecTargets, CFG::Aimbot_Melee_Sort);
	
	// Sort friendly buildings by distance (closest first for repair priority)
	if (!vecFriendlyBuildings.empty())
	{
		std::sort(vecFriendlyBuildings.begin(), vecFriendlyBuildings.end(), [](const MeleeTarget_t& a, const MeleeTarget_t& b) {
			return a.DistanceTo < b.DistanceTo;
		});
	}

	const int itEnd = std::min(4, static_cast<int>(m_vecTargets.size()));

	// Find and return the first valid target from main targets
	for (int n = 0; n < itEnd; n++)
	{
		auto& target = m_vecTargets[n];

		if (!CanHit(pLocal, pWeapon, target))
			continue;

		outTarget = target;
		return true;
	}
	
	// If no main target found, check friendly buildings (bypasses target max limit)
	for (auto& target : vecFriendlyBuildings)
	{
		if (!CanHit(pLocal, pWeapon, target))
			continue;

		outTarget = target;
		return true;
	}

	return false;
}

bool CAimbotMelee::ShouldAim(const CUserCmd* pCmd, C_TFWeaponBase* pWeapon)
{
	return CFG::Aimbot_Melee_Aim_Type != 1 || IsFiring(pCmd, pWeapon);
}

void CAimbotMelee::Aim(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const Vec3& vAngles)
{
	Vec3 vAngleTo = vAngles - pLocal->m_vecPunchAngle();
	Math::ClampAngles(vAngleTo);

	switch (CFG::Aimbot_Melee_Aim_Type)
	{
		// Plaint
		case 0:
		{
			pCmd->viewangles = vAngleTo;
			break;
		}

		// Silent
		case 1:
		{
			if (IsFiring(pCmd, pWeapon))
			{
				H::AimUtils->FixMovement(pCmd, vAngleTo);
				pCmd->viewangles = vAngleTo;

				if (Shifting::bShifting && Shifting::bShiftingWarp)
					G::bSilentAngles = true;

				else G::bPSilentAngles = true;
			}

			break;
		}

		// Smooth
		case 2:
		{
			Vec3 vDelta = vAngleTo - pCmd->viewangles;
			Math::ClampAngles(vDelta);

			if (vDelta.Length() > 0.0f && CFG::Aimbot_Melee_Smoothing)
			{
				pCmd->viewangles += vDelta / CFG::Aimbot_Melee_Smoothing;
			}

			break;
		}

		default: break;
	}
}

bool CAimbotMelee::ShouldFire(const MeleeTarget_t& target)
{
	return !CFG::Aimbot_AutoShoot ? false : target.MeleeTraceHit;
}

void CAimbotMelee::HandleFire(CUserCmd* pCmd, C_TFWeaponBase* pWeapon)
{
	pCmd->buttons |= IN_ATTACK;
}

bool CAimbotMelee::IsFiring(const CUserCmd* pCmd, C_TFWeaponBase* pWeapon)
{
	if (Shifting::bShifting && Shifting::bShiftingWarp)
	{
		return true;
	}

	if (pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
	{
		return (pCmd->buttons & IN_ATTACK) && G::bCanPrimaryAttack;
	}

	return fabsf(pWeapon->m_flSmackTime() - I::GlobalVars->curtime) < I::GlobalVars->interval_per_tick * 2.0f;
}

void CAimbotMelee::CrouchWhileAirborne(CUserCmd* pCmd, C_TFPlayer* pLocal)
{
	if (!(pLocal->m_fFlags() & FL_ONGROUND))
	{
		// Trace down to find distance to ground
		// Use world-only filter so we ignore all players/buildings - we want to land on enemies crouched
		Vec3 vStart = pLocal->m_vecOrigin();
		Vec3 vEnd = vStart - Vec3(0, 0, 500.f); // Trace 500 units down
		
		trace_t trace;
		CTraceFilterWorldOnly filter;
		Ray_t ray;
		ray.Init(vStart, vEnd, pLocal->m_vecMins(), pLocal->m_vecMaxs());
		I::EngineTrace->TraceRay(ray, MASK_PLAYERSOLID, &filter, &trace);
		
		float flDistToGround = (vStart.z - trace.endpos.z);
		
		// Calculate time to land based on current velocity and gravity
		float flZVel = pLocal->m_vecVelocity().z;
		float flGravity = SDKUtils::GetGravity();
		
		// Solve: dist = vel*t + 0.5*g*t^2 for t
		// Using quadratic formula, approximate ticks to land
		float flTimeToLand = 0.f;
		if (flGravity > 0.f)
		{
			// Quadratic: 0.5*g*t^2 + vel*t - dist = 0
			float a = 0.5f * flGravity;
			float b = -flZVel; // negative because vel is negative when falling
			float c = -flDistToGround;
			float discriminant = b * b - 4 * a * c;
			if (discriminant >= 0.f)
				flTimeToLand = (-b + sqrtf(discriminant)) / (2 * a);
		}
		
		int iTicksToLand = static_cast<int>(flTimeToLand / I::GlobalVars->interval_per_tick);
		
		// Uncrouch 2 ticks before landing to land standing, otherwise crouch
		if (iTicksToLand > 2)
			pCmd->buttons |= IN_DUCK;
		else
			pCmd->buttons &= ~IN_DUCK;
	}
}

void CAimbotMelee::Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (!CFG::Aimbot_Melee_Active)
		return;

	if (CFG::Aimbot_Melee_Sort == 0)
		G::flAimbotFOV = CFG::Aimbot_Melee_FOV;

	// Allow aimbot to run during rapid fire shifting to recalculate angles for each tick
	// Skip only during warp shifting (not rapid fire)
	if (Shifting::bShifting && !Shifting::bShiftingRapidFire)
		return;

	// Skip sapper - handled by AutoSapper triggerbot
	if (pWeapon->GetWeaponID() == TF_WEAPON_BUILDER)
		return;

	// NOTE: Crouch while airborne is handled globally, not in melee aimbot
	// This prevents melee from interfering with the global crouch behavior

	const bool isFiring = IsFiring(pCmd, pWeapon);

	MeleeTarget_t target = {};
	if (GetTarget(pLocal, pWeapon, target) && target.Entity)
	{
		const auto aimKeyDown = H::Input->IsDown(CFG::Aimbot_Key) || CFG::Aimbot_Melee_Always_Active;
		if (aimKeyDown || isFiring)
		{
			G::nTargetIndex = target.Entity->entindex();

			// Auto shoot
			if (aimKeyDown)
			{
				if (ShouldFire(target))
				{
					HandleFire(pCmd, pWeapon);
				}
			}

			const bool bIsFiring = IsFiring(pCmd, pWeapon);
			G::bFiring = bIsFiring;

			// Are we ready to aim?
			if (ShouldAim(pCmd, pWeapon) || bIsFiring)
			{
				if (aimKeyDown)
				{
					Aim(pCmd, pLocal, pWeapon, target.AngleTo);
				}

				// Set tick_count for backtrack when the melee smack happens (bIsFiring)
				// Melee has a swing delay - the hit detection happens AFTER you press attack
				// The server checks the hit at the smack time, so we set tick_count then
				if (bIsFiring && target.Entity->GetClassId() == ETFClassIds::CTFPlayer && target.LagRecord)
				{
					// Use the same tick_count calculation as Amalgam's melee aimbot
					// SimulationTime + fake interp (or regular lerp if no fake interp)
					pCmd->tick_count = TIME_TO_TICKS(target.SimulationTime) + TIME_TO_TICKS(SDKUtils::GetLerp());
				}
			}

			// Walk to target
			if (CFG::Aimbot_Melee_Walk_To_Target && (pLocal->m_fFlags() & FL_ONGROUND))
			{
				SDKUtils::WalkTo(pCmd, pLocal->m_vecOrigin(), target.Position, 1.f);
			}
		}
	}
}
