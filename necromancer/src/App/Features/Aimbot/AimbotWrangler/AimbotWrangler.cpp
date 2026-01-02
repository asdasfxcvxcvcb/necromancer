#include "AimbotWrangler.h"
#include "../../CFG.h"
#include "../../amalgam_port/AmalgamCompat.h"
#include <algorithm>
#include <cmath>

// Sentry rocket speed (units per second)
constexpr float SENTRY_ROCKET_SPEED = 1100.0f;
// Sentry rocket splash radius (same as soldier rockets)
constexpr float SENTRY_ROCKET_SPLASH_RADIUS = 146.0f;

bool CAimbotWrangler::IsWrangler(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;
	
	return pWeapon->GetWeaponID() == TF_WEAPON_LASER_POINTER;
}

C_ObjectSentrygun* CAimbotWrangler::GetLocalSentry(C_TFPlayer* pLocal)
{
	if (!pLocal)
		return nullptr;
	
	// Find sentry owned by local player (check teammate buildings since local buildings are teammates)
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_TEAMMATES))
	{
		if (!pEntity || pEntity->GetClassId() != ETFClassIds::CObjectSentrygun)
			continue;
		
		auto pSentry = pEntity->As<C_ObjectSentrygun>();
		if (!pSentry || pSentry->m_bBuilding() || pSentry->m_bPlacing() || pSentry->m_bCarried())
			continue;
		
		// Check if this sentry belongs to local player
		if (pSentry->m_hBuilder().GetEntryIndex() != pLocal->entindex())
			continue;
		
		return pSentry;
	}
	
	return nullptr;
}

Vec3 CAimbotWrangler::GetSentryShootPos(C_ObjectSentrygun* pSentry)
{
	if (!pSentry)
		return Vec3();
	
	// Try to get actual muzzle attachment position
	// Attachment names: "muzzle" for level 1, "muzzle" and "muzzle_l" for level 2+
	int iAttachment = pSentry->LookupAttachment("muzzle");
	if (iAttachment > 0)
	{
		Vec3 vPos;
		QAngle vAng;
		if (pSentry->GetAttachment(iAttachment, vPos, vAng))
			return vPos;
	}
	
	// Fallback to eye offset if attachment not found
	Vec3 vOrigin = pSentry->GetAbsOrigin();
	int nLevel = pSentry->m_iUpgradeLevel();
	
	switch (nLevel)
	{
		case 1: return vOrigin + Vec3(0, 0, 32);
		case 2: return vOrigin + Vec3(0, 0, 40);
		case 3: return vOrigin + Vec3(0, 0, 46);
		default: return vOrigin + Vec3(0, 0, 32);
	}
}

Vec3 CAimbotWrangler::GetSentryRocketPos(C_ObjectSentrygun* pSentry)
{
	if (!pSentry)
		return Vec3();
	
	// Try to get actual rocket attachment position
	int iAttachment = pSentry->LookupAttachment("rocket");
	if (iAttachment > 0)
	{
		Vec3 vPos;
		QAngle vAng;
		if (pSentry->GetAttachment(iAttachment, vPos, vAng))
			return vPos;
	}
	
	// Fallback - use muzzle position offset to the right
	Vec3 vShootPos = GetSentryShootPos(pSentry);
	Vec3 vAngles = pSentry->GetAbsAngles();
	Vec3 vForward, vRight, vUp;
	Math::AngleVectors(vAngles, &vForward, &vRight, &vUp);
	
	return vShootPos + vRight * 10.0f;
}

bool CAimbotWrangler::CanFireRockets(C_ObjectSentrygun* pSentry)
{
	if (!pSentry)
		return false;
	
	// Only level 3 sentries have rockets
	if (pSentry->m_iUpgradeLevel() < 3)
		return false;
	
	// Check rocket ammo
	return pSentry->m_iAmmoRockets() > 0;
}

bool CAimbotWrangler::CanFireBullets(C_ObjectSentrygun* pSentry)
{
	if (!pSentry)
		return false;
	
	return pSentry->m_iAmmoShells() > 0;
}

float CAimbotWrangler::GetRocketTravelTime(const Vec3& vFrom, const Vec3& vTo)
{
	float flDistance = vFrom.DistTo(vTo);
	return flDistance / SENTRY_ROCKET_SPEED;
}

Vec3 CAimbotWrangler::PredictTargetPosition(C_TFPlayer* pTarget, float flTime)
{
	if (!pTarget || flTime <= 0.0f)
		return pTarget ? pTarget->GetAbsOrigin() : Vec3();
	
	// Clamp to max simulation time
	flTime = std::min(flTime, CFG::Aimbot_Projectile_Max_Simulation_Time);
	
	// Use movement simulation to predict position
	if (!F::MovementSimulation->Initialize(pTarget))
		return pTarget->GetAbsOrigin();
	
	int nTicks = TIME_TO_TICKS(flTime);
	for (int i = 0; i < nTicks; i++)
	{
		F::MovementSimulation->RunTick();
	}
	
	Vec3 vPredicted = F::MovementSimulation->GetOrigin();
	F::MovementSimulation->Restore();
	
	return vPredicted;
}

// Generate points on a sphere for splash prediction (Fibonacci sphere)
std::vector<Vec3> CAimbotWrangler::GenerateSplashSphere(float flRadius, int nSamples)
{
	std::vector<Vec3> vPoints;
	vPoints.reserve(nSamples);
	
	const float fPhi = 3.14159265f * (3.0f - sqrtf(5.0f));  // Golden angle
	
	for (int i = 0; i < nSamples; i++)
	{
		float y = 1.0f - (i / float(nSamples - 1)) * 2.0f;  // y goes from 1 to -1
		float radiusAtY = sqrtf(1.0f - y * y);
		float theta = fPhi * i;
		
		float x = cosf(theta) * radiusAtY;
		float z = sinf(theta) * radiusAtY;
		
		vPoints.push_back(Vec3(x, z, y) * flRadius);  // Note: swapped y and z for TF2 coordinate system
	}
	
	return vPoints;
}

// Find a valid splash point near the target
bool CAimbotWrangler::FindSplashPoint(C_TFPlayer* pLocal, C_ObjectSentrygun* pSentry, const Vec3& vTargetPos, C_BaseEntity* pTarget, Vec3& outSplashPoint)
{
	if (!pLocal || !pSentry || !pTarget)
		return false;
	
	const Vec3 vRocketPos = GetSentryRocketPos(pSentry);
	const Vec3 vLocalPos = pLocal->GetShootPos();
	
	// Use configured splash radius percentage from projectile aimbot settings
	float flSplashRadius = SENTRY_ROCKET_SPLASH_RADIUS * (CFG::Aimbot_Amalgam_Projectile_SplashRadius / 100.0f);
	
	// Generate sphere points around target
	auto vSpherePoints = GenerateSplashSphere(flSplashRadius, CFG::Aimbot_Amalgam_Projectile_SplashPoints);
	
	struct SplashCandidate
	{
		Vec3 vPoint;
		float flDistToTarget;
	};
	std::vector<SplashCandidate> vCandidates;
	
	CTraceFilterHitscan filter = {};
	filter.m_pIgnore = pSentry;
	
	for (const auto& vOffset : vSpherePoints)
	{
		Vec3 vTestPoint = vTargetPos + vOffset;
		
		// Trace from target center to splash point to find surface
		CGameTrace traceToSurface = {};
		H::AimUtils->Trace(vTargetPos, vTestPoint, MASK_SOLID, &filter, &traceToSurface);
		
		// We need to hit a surface (not the target itself)
		if (traceToSurface.fraction >= 1.0f || traceToSurface.m_pEnt == pTarget)
			continue;
		
		// Check if surface is valid (not sky, not moving)
		if (traceToSurface.surface.flags & SURF_SKY)
			continue;
		
		// Skip surfaces that aren't mostly horizontal or vertical (walls/floors)
		if (!traceToSurface.m_pEnt->GetAbsVelocity().IsZero())
			continue;
		
		Vec3 vSurfacePoint = traceToSurface.endpos;
		
		// Check if rocket can reach this surface point from sentry
		CGameTrace traceFromSentry = {};
		Vec3 vRocketMins(-2, -2, -2);
		Vec3 vRocketMaxs(2, 2, 2);
		
		H::AimUtils->TraceHull(vRocketPos, vSurfacePoint, vRocketMins, vRocketMaxs, MASK_SOLID, &filter, &traceFromSentry);
		
		// Check if we can reach close to the surface point
		if (traceFromSentry.fraction < 0.9f && traceFromSentry.endpos.DistTo(vSurfacePoint) > 20.0f)
			continue;
		
		// Check distance from splash point to target
		float flDistToTarget = vSurfacePoint.DistTo(vTargetPos);
		if (flDistToTarget > flSplashRadius)
			continue;
		
		vCandidates.push_back({ vSurfacePoint, flDistToTarget });
	}
	
	if (vCandidates.empty())
		return false;
	
	// Sort by distance to target (closer = more damage)
	std::sort(vCandidates.begin(), vCandidates.end(), [](const SplashCandidate& a, const SplashCandidate& b) {
		return a.flDistToTarget < b.flDistToTarget;
	});
	
	outSplashPoint = vCandidates[0].vPoint;
	return true;
}


bool CAimbotWrangler::GetHitscanTarget(C_TFPlayer* pLocal, C_ObjectSentrygun* pSentry, WranglerTarget_t& outTarget)
{
	if (!pLocal || !pSentry)
		return false;
	
	const Vec3 vSentryPos = GetSentryShootPos(pSentry);
	const Vec3 vLocalPos = pLocal->GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
	
	m_vecTargets.clear();
	
	// Find player targets
	if (CFG::Aimbot_Target_Players)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
		{
			if (!pEntity)
				continue;
			
			const auto pPlayer = pEntity->As<C_TFPlayer>();
			if (pPlayer->deadflag() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
				continue;
			
			if (CFG::Aimbot_Ignore_Friends && pPlayer->IsPlayerOnSteamFriendsList())
				continue;
			
			if (CFG::Aimbot_Ignore_Invisible && pPlayer->IsInvisible())
				continue;
			
			if (CFG::Aimbot_Ignore_Invulnerable && pPlayer->IsInvulnerable())
				continue;
			
			if (CFG::Aimbot_Ignore_Taunting && pPlayer->InCond(TF_COND_TAUNTING))
				continue;
			
			// Target body for sentry (more reliable)
			Vec3 vPos = pPlayer->GetHitboxPos(HITBOX_PELVIS);
			// Wrangler aims based on player's view - calculate angle from player's eye
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			const float flDistTo = vSentryPos.DistTo(vPos);
			
			if (flFOVTo > CFG::Aimbot_Hitscan_FOV)
				continue;
			
			// Check if player can see the enemy (visibility from player's view)
			CGameTrace visTrace = {};
			CTraceFilterHitscan visFilter = {};
			visFilter.m_pIgnore = pLocal;
			H::AimUtils->Trace(vLocalPos, vPos, MASK_SHOT, &visFilter, &visTrace);
			if (visTrace.m_pEnt != pPlayer && visTrace.fraction < 1.0f)
				continue;
			
			m_vecTargets.emplace_back(WranglerTarget_t{
				AimTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo },
				HITBOX_PELVIS, pPlayer->m_flSimulationTime(), nullptr, false, 0.0f
			});
		}
	}
	
	// Find building targets
	if (CFG::Aimbot_Target_Buildings)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
		{
			if (!pEntity)
				continue;
			
			const auto pBuilding = pEntity->As<C_BaseObject>();
			if (pBuilding->m_bPlacing() || pBuilding->m_bBuilding())
				continue;
			
			Vec3 vPos = pBuilding->GetCenter();
			// Wrangler aims based on player's view
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			const float flDistTo = vSentryPos.DistTo(vPos);
			
			if (flFOVTo > CFG::Aimbot_Hitscan_FOV)
				continue;
			
			m_vecTargets.emplace_back(WranglerTarget_t{
				AimTarget_t{ pBuilding, vPos, vAngleTo, flFOVTo, flDistTo },
				-1, 0.0f, nullptr, false, 0.0f
			});
		}
	}
	
	if (m_vecTargets.empty())
		return false;
	
	// Sort targets using proj aimbot's target selection method (hitscan)
	int iSortHitscan = Vars::Aimbot::General::TargetSelection.Value;
	std::sort(m_vecTargets.begin(), m_vecTargets.end(), [iSortHitscan](const WranglerTarget_t& a, const WranglerTarget_t& b) {
		if (iSortHitscan == 1)
			return a.DistanceTo < b.DistanceTo;
		return a.FOVTo < b.FOVTo;
	});
	
	// Limit to max targets
	int nMaxTargetsHitscan = Vars::Aimbot::General::MaxTargets.Value;
	if (nMaxTargetsHitscan > 0 && m_vecTargets.size() > static_cast<size_t>(nMaxTargetsHitscan))
		m_vecTargets.resize(nMaxTargetsHitscan);
	
	// Find first visible target (trace from sentry, but aim from player view)
	for (auto& target : m_vecTargets)
	{
		// Trace from sentry to target to check visibility
		CGameTrace trace = {};
		CTraceFilterHitscan filter = {};
		filter.m_pIgnore = pSentry;
		
		H::AimUtils->Trace(vSentryPos, target.Position, MASK_SHOT, &filter, &trace);
		
		if (trace.m_pEnt != target.Entity && trace.fraction < 1.0f)
			continue;
		
		outTarget = target;
		return true;
	}
	
	return false;
}

bool CAimbotWrangler::GetRocketTarget(C_TFPlayer* pLocal, C_ObjectSentrygun* pSentry, WranglerTarget_t& outTarget)
{
	if (!pLocal || !pSentry || !CanFireRockets(pSentry))
		return false;
	
	const Vec3 vRocketPos = GetSentryRocketPos(pSentry);
	const Vec3 vLocalPos = pLocal->GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
	
	m_vecTargets.clear();
	
	// Find player targets (collect without simulation first)
	if (CFG::Aimbot_Target_Players)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
		{
			if (!pEntity)
				continue;
			
			const auto pPlayer = pEntity->As<C_TFPlayer>();
			if (pPlayer->deadflag() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
				continue;
			
			if (CFG::Aimbot_Ignore_Friends && pPlayer->IsPlayerOnSteamFriendsList())
				continue;
			
			if (CFG::Aimbot_Ignore_Invisible && pPlayer->IsInvisible())
				continue;
			
			if (CFG::Aimbot_Ignore_Invulnerable && pPlayer->IsInvulnerable())
				continue;
			
			if (CFG::Aimbot_Ignore_Taunting && pPlayer->InCond(TF_COND_TAUNTING))
				continue;
			
			// Initial position for FOV check (from player's view)
			Vec3 vCurrentPos = pPlayer->GetCenter();
			Vec3 vAngleToCheck = Math::CalcAngle(vLocalPos, vCurrentPos);
			const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleToCheck);
			
			if (flFOVTo > CFG::Aimbot_Hitscan_FOV)
				continue;
			
			// NOTE: For rockets, we DON'T check current visibility here
			// The enemy might be behind cover now but will be at the predicted position
			// when the rocket arrives. Visibility is checked against the predicted position later.
			
			// Store current position - prediction will be done only for selected target
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vCurrentPos);
			const float flDistTo = vRocketPos.DistTo(vCurrentPos);
			
			m_vecTargets.emplace_back(WranglerTarget_t{
				AimTarget_t{ pPlayer, vCurrentPos, vAngleTo, flFOVTo, flDistTo },
				-1, pPlayer->m_flSimulationTime(), nullptr, true, 0.0f
			});
		}
	}
	
	// Find building targets (no prediction needed - they don't move)
	if (CFG::Aimbot_Target_Buildings)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
		{
			if (!pEntity)
				continue;
			
			const auto pBuilding = pEntity->As<C_BaseObject>();
			if (pBuilding->m_bPlacing() || pBuilding->m_bBuilding())
				continue;
			
			Vec3 vPos = pBuilding->GetCenter();
			// Wrangler aims based on player's view
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			const float flDistTo = vRocketPos.DistTo(vPos);
			
			if (flFOVTo > CFG::Aimbot_Hitscan_FOV)
				continue;
			
			float flTravelTime = GetRocketTravelTime(vRocketPos, vPos);
			
			m_vecTargets.emplace_back(WranglerTarget_t{
				AimTarget_t{ pBuilding, vPos, vAngleTo, flFOVTo, flDistTo },
				-1, 0.0f, nullptr, true, flTravelTime
			});
		}
	}
	
	if (m_vecTargets.empty())
		return false;
	
	// Sort targets using proj aimbot's target selection method (rockets)
	int iSortRocket = Vars::Aimbot::General::TargetSelection.Value;
	std::sort(m_vecTargets.begin(), m_vecTargets.end(), [iSortRocket](const WranglerTarget_t& a, const WranglerTarget_t& b) {
		if (iSortRocket == 1)
			return a.DistanceTo < b.DistanceTo;
		return a.FOVTo < b.FOVTo;
	});
	
	// Limit to max targets
	int nMaxTargetsRocket = Vars::Aimbot::General::MaxTargets.Value;
	if (nMaxTargetsRocket > 0 && m_vecTargets.size() > static_cast<size_t>(nMaxTargetsRocket))
		m_vecTargets.resize(nMaxTargetsRocket);
	
	// Find first target with clear rocket path (trace from sentry)
	for (auto& target : m_vecTargets)
	{
		// For players, do movement prediction NOW (only for this candidate)
		Vec3 vTargetPos = target.Position;
		float flTravelTime = 0.0f;
		
		if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
		{
			auto pPlayer = target.Entity->As<C_TFPlayer>();
			Vec3 vPredictedPos = target.Position;
			
			// Iterative prediction: predict position, calculate travel time, repeat
			for (int i = 0; i < 3; i++)
			{
				flTravelTime = GetRocketTravelTime(vRocketPos, vPredictedPos);
				vPredictedPos = PredictTargetPosition(pPlayer, flTravelTime);
				vPredictedPos.z += (pPlayer->m_vecMaxs().z - pPlayer->m_vecMins().z) * 0.5f;
			}
			
			vTargetPos = vPredictedPos;
			target.Position = vPredictedPos;
			target.AngleTo = Math::CalcAngle(vLocalPos, vPredictedPos);
			target.PredictedTime = flTravelTime;
		}
		
		// Trace rocket path (use hull trace for rocket size)
		CGameTrace trace = {};
		CTraceFilterHitscan filter = {};
		filter.m_pIgnore = pSentry;
		
		Vec3 vMins(-2, -2, -2);
		Vec3 vMaxs(2, 2, 2);
		
		H::AimUtils->TraceHull(vRocketPos, vTargetPos, vMins, vMaxs, MASK_SOLID, &filter, &trace);
		
		// Check if we hit the target or got close enough (direct hit)
		if (trace.m_pEnt == target.Entity || trace.fraction >= 0.95f)
		{
			outTarget = target;
			return true;
		}
		
		// Try splash prediction if enabled and direct hit failed
		if (CFG::Aimbot_Amalgam_Projectile_Splash > 0)
		{
			Vec3 vSplashPoint;
			if (FindSplashPoint(pLocal, pSentry, vTargetPos, target.Entity, vSplashPoint))
			{
				target.Position = vSplashPoint;
				target.AngleTo = Math::CalcAngle(vLocalPos, vSplashPoint);
				outTarget = target;
				return true;
			}
		}
		
		// Fallback: check if rocket would land near them (within splash radius)
		if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
		{
			float flDistToTarget = trace.endpos.DistTo(vTargetPos);
			if (flDistToTarget < SENTRY_ROCKET_SPLASH_RADIUS)
			{
				outTarget = target;
				return true;
			}
		}
	}
	
	return false;
}

void CAimbotWrangler::Aim(CUserCmd* pCmd, C_TFPlayer* pLocal, const Vec3& vAngles)
{
	Vec3 vAngleTo = vAngles;
	Math::ClampAngles(vAngleTo);
	
	// Use hitscan aim type setting
	switch (CFG::Aimbot_Hitscan_Aim_Type)
	{
		// Plain
		case 0:
		{
			pCmd->viewangles = vAngleTo;
			break;
		}
		
		// Silent
		case 1:
		{
			H::AimUtils->FixMovement(pCmd, vAngleTo);
			pCmd->viewangles = vAngleTo;
			G::bSilentAngles = true;
			break;
		}
		
		// Smooth
		case 2:
		{
			Vec3 vDelta = vAngleTo - pCmd->viewangles;
			Math::ClampAngles(vDelta);
			
			if (vDelta.Length() > 0.0f && CFG::Aimbot_Hitscan_Smoothing > 0.f)
				pCmd->viewangles += vDelta / CFG::Aimbot_Hitscan_Smoothing;
			break;
		}
		
		// Triggerbot - same as smooth with smoothing=0 (no angle adjustment)
		case 3:
		{
			break;
		}
		
		default: break;
	}
}

bool CAimbotWrangler::ShouldFire(C_TFPlayer* pLocal, C_ObjectSentrygun* pSentry, const WranglerTarget_t& target)
{
	if (!CFG::Aimbot_AutoShoot)
		return false;
	
	if (!pSentry || pSentry->IsDisabled())
		return false;
	
	return true;
}

void CAimbotWrangler::Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	// Wrangler uses hitscan active setting
	if (!CFG::Aimbot_Hitscan_Active || !IsWrangler(pWeapon))
		return;
	
	// Get local player's sentry
	C_ObjectSentrygun* pSentry = GetLocalSentry(pLocal);
	if (!pSentry || pSentry->IsDisabled())
		return;
	
	G::flAimbotFOV = CFG::Aimbot_Hitscan_FOV;
	
	const bool bAimKeyDown = CFG::Aimbot_Key == 0 || H::Input->IsDown(CFG::Aimbot_Key);
	if (!bAimKeyDown && !CFG::Aimbot_AutoShoot)
		return;
	
	// Track rocket cooldown by watching ammo count
	int nCurrentRocketAmmo = pSentry->m_iAmmoRockets();
	float flCurTime = I::GlobalVars->curtime;
	
	// Detect when a rocket was fired (ammo decreased)
	if (m_nLastRocketAmmo > 0 && nCurrentRocketAmmo < m_nLastRocketAmmo)
		m_flLastRocketFireTime = flCurTime;
	m_nLastRocketAmmo = nCurrentRocketAmmo;
	
	// Check if rocket is ready to fire (3 second cooldown after each shot)
	bool bRocketReady = CanFireRockets(pSentry) && 
		(flCurTime - m_flLastRocketFireTime) >= ROCKET_COOLDOWN;
	
	WranglerTarget_t bulletTarget = {};
	WranglerTarget_t rocketTarget = {};
	bool bFoundBulletTarget = false;
	bool bFoundRocketTarget = false;
	
	// Always try to find a bullet target for continuous fire
	if (CanFireBullets(pSentry))
		bFoundBulletTarget = GetHitscanTarget(pLocal, pSentry, bulletTarget);
	
	// Only calculate rocket prediction when rocket is ready to fire
	if (bRocketReady)
		bFoundRocketTarget = GetRocketTarget(pLocal, pSentry, rocketTarget);
	
	// Determine which target to aim at
	WranglerTarget_t* pAimTarget = nullptr;
	
	if (bRocketReady && bFoundRocketTarget)
	{
		// Rocket is ready - prioritize rocket aim with prediction
		pAimTarget = &rocketTarget;
		
		// Draw movement prediction path for rocket target (only when aim key is pressed, not autoshoot)
		if (bAimKeyDown && rocketTarget.Entity && rocketTarget.Entity->GetClassId() == ETFClassIds::CTFPlayer)
		{
			auto pPlayer = rocketTarget.Entity->As<C_TFPlayer>();
			if (pPlayer && rocketTarget.PredictedTime > 0.0f && Vars::Visuals::Simulation::PlayerPath.Value)
			{
				// Generate prediction path for visualization
				std::vector<Vec3> vPath;
				vPath.push_back(pPlayer->GetAbsOrigin());
				
				if (F::MovementSimulation->Initialize(pPlayer))
				{
					int nTicks = TIME_TO_TICKS(rocketTarget.PredictedTime);
					for (int i = 0; i < nTicks; i++)
					{
						F::MovementSimulation->RunTick();
						vPath.push_back(F::MovementSimulation->GetOrigin());
					}
					F::MovementSimulation->Restore();
				}
				
				// Add to path storage for rendering using user's settings
				if (vPath.size() > 1)
				{
					if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
						G::PathStorage.emplace_back(vPath, flCurTime + 0.1f, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Simulation::PlayerPath.Value);
					if (Vars::Colors::PlayerPath.Value.a)
						G::PathStorage.emplace_back(vPath, flCurTime + 0.1f, Vars::Colors::PlayerPath.Value, Vars::Visuals::Simulation::PlayerPath.Value, true);
				}
			}
		}
	}
	else if (bFoundBulletTarget)
	{
		// No rocket ready or no rocket target - use bullet aim
		pAimTarget = &bulletTarget;
	}
	
	if (!pAimTarget || !pAimTarget->Entity)
		return;
	
	G::nTargetIndex = pAimTarget->Entity->entindex();
	G::nTargetIndexEarly = pAimTarget->Entity->entindex();
	
	if (bAimKeyDown)
	{
		Aim(pCmd, pLocal, pAimTarget->AngleTo);
		
		// Auto-fire
		if (ShouldFire(pLocal, pSentry, *pAimTarget))
		{
			// Always fire bullets if we have ammo
			if (CanFireBullets(pSentry))
				pCmd->buttons |= IN_ATTACK;
			
			// Fire rocket if ready (unstick delay handled by early aiming)
			if (bRocketReady && bFoundRocketTarget)
				pCmd->buttons |= IN_ATTACK2;
		}
	}
}
