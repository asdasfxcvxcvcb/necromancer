#include "AimbotMelee.h"

#include "../../CFG.h"
#include "../../Crits/Crits.h"
#include "../../Players/Players.h"
#include "../../amalgam_port/AmalgamCompat.h"

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
	// Validate target entity is still alive — may have been destroyed since GetTarget
	if (!target.Entity || !H::Entities->IsEntityValid(target.Entity))
		return false;

	// Get weapon-specific range and hull size - matching server's DoSwingTraceInternal
	float flRange = SDKUtils::AttribHookValue(pWeapon->GetSwingRange(), "melee_range_multiplier", pWeapon);
	float flHull = SDKUtils::AttribHookValue(18.0f, "melee_bounds_multiplier", pWeapon);

	// Account for player model scale (server does this too)
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

	const Vec3 vEyePos = SDK::GetPredictedShootPos(pLocal);
	const bool bIsPlayer = target.Entity && H::Entities->IsEntityValid(target.Entity) && target.Entity->GetClassId() == ETFClassIds::CTFPlayer;

	// For players with lag records, we need to test against the record's position
	if (bIsPlayer && target.LagRecord)
	{
		const auto pRecord = target.LagRecord;

		// Set up bones and collision bounds for the trace (matching server's BacktrackPlayer)
		F::LagRecordMatrixHelper->Set(pRecord);

		if (!F::LagRecordMatrixHelper->WasSuccessful())
			return false;

		// Calculate optimal aim position - aim at Z height that matches local player when possible
		Vec3 vDiff = { 0, 0, std::clamp(vEyePos.z - pRecord->AbsOrigin.z, pRecord->VecMinsPreScaled.z, pRecord->VecMaxsPreScaled.z) };
		target.Position = pRecord->AbsOrigin + vDiff;
		target.AngleTo = Math::CalcAngle(vEyePos, target.Position);

		Vec3 vForward;
		Math::AngleVectors(target.AngleTo, &vForward);
		Vec3 vTraceEnd = vEyePos + (vForward * flRange);

		// Use ClipRayToEntity to bypass stale spatial partition
		bool bHit = H::AimUtils->TraceEntityMeleeDirect(target.Entity, vEyePos, vTraceEnd, vSwingMins, vSwingMaxs);

		F::LagRecordMatrixHelper->Restore();

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

			target.MeleeTraceHit = H::AimUtils->TraceEntityMeleeDirect(target.Entity, vEyePos, vCurrentTraceEnd, vSwingMins, vSwingMaxs);
			return target.MeleeTraceHit;
		}

		return false;
	}

	// For non-backtrack targets (buildings, current position fallback)
	target.AngleTo = Math::CalcAngle(vEyePos, target.Position);

	Vec3 vForward;
	Math::AngleVectors(target.AngleTo, &vForward);
	Vec3 vTraceEnd = vEyePos + (vForward * flRange);

	// Use ClipRayToEntity to bypass stale spatial partition
	bool bHit = H::AimUtils->TraceEntityMeleeDirect(target.Entity, vEyePos, vTraceEnd, vSwingMins, vSwingMaxs);

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

		target.MeleeTraceHit = H::AimUtils->TraceEntityMeleeDirect(target.Entity, vEyePos, vCurrentTraceEnd, vSwingMins, vSwingMaxs);
		return target.MeleeTraceHit;
	}

	return false;
}

bool CAimbotMelee::GetTarget(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, MeleeTarget_t& outTarget)
{
	const Vec3 vLocalPos = SDK::GetPredictedShootPos(pLocal);
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

				// Note: Vaccinator ignore is NOT applied to melee — melee damage (DMG_CLUB)
				// bypasses all Vaccinator uber types (bullet/blast/fire resist)

				// Ignore untagged players when key is held
				if (CFG::Aimbot_Ignore_Untagged_Key != 0 && H::Input->IsDown(CFG::Aimbot_Ignore_Untagged_Key))
				{
					PlayerPriority playerPriority = {};
					if (!F::Players->GetInfo(pPlayer->entindex(), playerPriority) ||
						(!playerPriority.Cheater && !playerPriority.Targeted && !playerPriority.Nigger && !playerPriority.RetardLegit && !playerPriority.Streamer))
						continue;
				}
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
				const float flMaxSpeed = pLocal->TeamFortress_CalculateMaxSpeed(false);
				const float flMovementDuringSmack = flMaxSpeed * flSmackDelay * 2.0f;
				const float flMaxTargetDist = flSwingRange + flMovementDuringSmack + 50.0f;

				const float flIdealAge = F::LagRecords->GetServerAcceptedAge();
				const float flAgeTolerance = F::LagRecords->GetServerAcceptedAgeRange();
				constexpr float TELEPORT_DIST_SQR = 64.0f * 64.0f;

				if (F::LagRecords->HasRecords(pPlayer, &nRecords))
				{
					// ========================================================
					// STEP 1: Current model position (primary target when fake latency = 0)
					// With fake latency = 0, the server uses our cmd->tick_count as-is,
					// so targeting the current model is accurate. This is the primary target.
					// With fake latency > 0, the server backtracks — skip current model entirely
					// and only use backtrack records (matching AutoBackstab pattern).
					// ========================================================
					if (!bFakeLatencyActive)
					{
						const float flCurrentAge = I::GlobalVars->curtime - pPlayer->m_flSimulationTime();
						if (F::LagRecords->IsRecordAgeValidForServer(flCurrentAge))
						{
							Vec3 vPos = pPlayer->GetHitboxPos(HITBOX_BODY);
							Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
							const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
							const float flDistTo = vLocalPos.DistTo(vPos);

							if (CFG::Aimbot_Melee_Sort != 0 || flFOVTo <= CFG::Aimbot_Melee_FOV)
							{
								if (flDistTo <= flMaxTargetDist)
								{
									bHasValidLagRecords = true;
									m_vecTargets.emplace_back(MeleeTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo, pPlayer->m_flSimulationTime() });
								}
							}
						}
					}

					// ========================================================
					// STEP 2: Interpolated record at ideal target simtime
					// Matches server's BacktrackPlayer exactly — the server
					// interpolates between two records to get the precise position.
					// ========================================================
					{
						const float flIdealTargetSimTime = I::GlobalVars->curtime - flIdealAge;
						bool bIsInterpolated = false;
						LagRecord_t interpRecord = F::LagRecords->GetInterpolatedRecord(pPlayer, flIdealTargetSimTime, &bIsInterpolated);

						if (interpRecord.bIsAlive && interpRecord.Player == pPlayer)
						{
							const float flInterpAge = I::GlobalVars->curtime - interpRecord.SimulationTime;
							if (F::LagRecords->IsRecordAgeValidForServer(flInterpAge) && flInterpAge <= F::LagRecords->GetMaxUnlag())
							{
								// Store interpolated record as a static so CanHit can use it
								static LagRecord_t s_interpRecord = {};
								s_interpRecord = interpRecord;

								Vec3 vPos = SDKUtils::GetHitboxPosFromMatrix(pPlayer, HITBOX_BODY, const_cast<matrix3x4_t*>(s_interpRecord.BoneMatrix));
								const float flDistTo = vLocalPos.DistTo(vPos);

								if (flDistTo <= flMaxTargetDist)
								{
									Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
									const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;

									if (CFG::Aimbot_Melee_Sort != 0 || flFOVTo <= CFG::Aimbot_Melee_FOV)
									{
										bHasValidLagRecords = true;
										m_vecTargets.emplace_back(MeleeTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo, s_interpRecord.SimulationTime, &s_interpRecord });
									}
								}
							}
						}
					}

					// ========================================================
					// STEP 3: Individual lag records as fallback
					// When fake latency > 0, only target the last 5 backtrack records
					// (matching AutoBackstab: nStartRecord = max(1, numRecords - 5))
					// When fake latency = 0, target all records as additional fallback
					// ========================================================
					struct ValidRecord_t {
						const LagRecord_t* pRecord;
						float flAgeDelta;
						Vec3 vPos;
						Vec3 vAngleTo;
						float flFOVTo;
						float flDistTo;
					};
					std::vector<ValidRecord_t> validRecords;

					const int nStart = bFakeLatencyActive ? std::max(1, nRecords - 5) : 1;
					const int nEnd = nRecords;

					for (int n = nStart; n <= nEnd; n++)
					{
						const auto pRecord = F::LagRecords->GetRecord(pPlayer, n, true);
						if (!pRecord)
							continue;

						if (!pRecord->bIsAlive)
							continue;

						if (!bFakeLatencyActive && !F::LagRecords->DiffersFromCurrent(pRecord))
							continue;

						const float flRecordAge = I::GlobalVars->curtime - pRecord->SimulationTime;
						if (flRecordAge > F::LagRecords->GetMaxUnlag())
							continue;

						if (n > 0)
						{
							const auto pPrevRecord = F::LagRecords->GetRecord(pPlayer, n - 1, true);
							if (pPrevRecord)
							{
								Vec3 vDelta = pRecord->AbsOrigin - pPrevRecord->AbsOrigin;
								if (vDelta.Length2DSqr() > TELEPORT_DIST_SQR)
									continue;
							}
						}

						Vec3 vPos = SDKUtils::GetHitboxPosFromMatrix(pPlayer, HITBOX_BODY, const_cast<matrix3x4_t*>(pRecord->BoneMatrix));
						const float flDistTo = vLocalPos.DistTo(vPos);
						
						if (flDistTo > flMaxTargetDist)
							continue;

						Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
						const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;

						if (CFG::Aimbot_Melee_Sort == 0 && flFOVTo > CFG::Aimbot_Melee_FOV)
							continue;

						const float flAgeDelta = fabsf(flRecordAge - flIdealAge);
						validRecords.push_back({ pRecord, flAgeDelta, vPos, vAngleTo, flFOVTo, flDistTo });
					}

					std::sort(validRecords.begin(), validRecords.end(),
						[](const ValidRecord_t& a, const ValidRecord_t& b) {
							return a.flAgeDelta < b.flAgeDelta;
						});

					for (const auto& vr : validRecords)
					{
						bHasValidLagRecords = true;
						m_vecTargets.emplace_back(MeleeTarget_t{ pPlayer, vr.vPos, vr.vAngleTo, vr.flFOVTo, vr.flDistTo, vr.pRecord->SimulationTime, vr.pRecord });
					}
				}

				// Fallback: if no valid targets were added, try the real model position.
				// Even with fake latency, the real model is still worth targeting — CanHit
				// will verify if the trace actually hits, and the server may accept it.
				if (!bHasValidLagRecords)
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
					G::bSilentAngles = true;  // Warp: choke handled by warp system

				else G::bPSilentAngles = true;  // Fire tick: pSilent choke
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
	if (!CFG::Aimbot_AutoShoot)
		return false;
	if (G::bAutoScopeWaitActive)
		return false;
	return target.MeleeTraceHit;
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

	// Skip during doubletap shifting (but not warp)
	if (Shifting::bShifting && !Shifting::bShiftingWarp)
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
		// Re-validate entity — it may have been destroyed between GetTarget and here
		if (!H::Entities->IsEntityValid(target.Entity))
			return;

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

				// Anti-cheat compatibility: skip tick count manipulation
				if (CFG::Misc_AntiCheat_Enabled)
					return;

				// Set tick_count for lag compensation (all player targets, not just lag records)
				// Server: targettick = cmd->tick_count - TIME_TO_TICKS(lerpTime)
				// We want targettick = TIME_TO_TICKS(SimulationTime), so tick_count = TIME_TO_TICKS(SimulationTime + lerp)
				// This applies to current position targets too — without it, the server backtracks
				// to a slightly different tick than where we're aiming, causing misses on fast enemies.
				if (bIsFiring && target.Entity && H::Entities->IsEntityValid(target.Entity) && target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
				{
					pCmd->tick_count = TIME_TO_TICKS(target.SimulationTime + SDKUtils::GetLerp());
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
