#include "AimbotWrangler.h"
#include "../../CFG.h"
#include "../../amalgam_port/AmalgamCompat.h"
#include "../../Players/Players.h"
#include <algorithm>
#include <cmath>

// Sentry rocket speed (units per second) - from source SDK tf_weaponbase_rocket.cpp
constexpr float SENTRY_ROCKET_SPEED = 1100.0f;
// Sentry rocket splash radius (same as soldier rockets) - TF_ROCKET_RADIUS
constexpr float SENTRY_ROCKET_SPLASH_RADIUS = 146.0f;
// Note: Source SDK rockets use vec3_origin (point trace) - UTIL_SetSize( this, -Vector(0,0,0), Vector(0,0,0) )

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


// Generate points on a sphere for splash prediction (Fibonacci sphere) - from Amalgam
// Also generates floor/wall points spread outward from target
// Now caches the sphere points and only regenerates if radius changes
std::vector<std::pair<Vec3, int>> CAimbotWrangler::GenerateSplashSphere(float flRadius, int nSamples)
{
	// Use cached points if radius hasn't changed
	if (!m_vecSplashSpherePoints.empty() && fabsf(m_flLastSplashRadius - flRadius) < 0.1f)
		return m_vecSplashSpherePoints;
	
	m_vecSplashSpherePoints.clear();
	m_vecSplashSpherePoints.reserve(nSamples * 2 + 50);  // Extra space for floor/wall points
	m_flLastSplashRadius = flRadius;
	
	const float fPhi = 3.14159265f * (3.0f - sqrtf(5.0f));  // Golden angle
	
	// Standard sphere points
	for (int i = 0; i < nSamples; i++)
	{
		float t = fPhi * i;
		float y = 1.0f - (i / float(nSamples - 1)) * 2.0f;
		float r = sqrtf(1.0f - y * y);
		float x = cosf(t) * r;
		float z = sinf(t) * r;
		
		Vec3 vPoint = Vec3(x, y, z) * flRadius;
		m_vecSplashSpherePoints.emplace_back(vPoint, 1);
	}
	
	// Add extra floor points spread outward (for corners and edges)
	// These are at different distances to catch more surfaces
	for (float flMult = 0.5f; flMult <= 1.5f; flMult += 0.25f)
	{
		float flDist = flRadius * flMult;
		// Floor points in 8 directions
		for (int i = 0; i < 8; i++)
		{
			float flAngle = (i / 8.0f) * 2.0f * 3.14159265f;
			Vec3 vPoint = Vec3(cosf(flAngle) * flDist, sinf(flAngle) * flDist, -flRadius);
			m_vecSplashSpherePoints.emplace_back(vPoint, 1);
		}
	}
	
	// Add direct floor point
	m_vecSplashSpherePoints.emplace_back(Vec3(0.0f, 0.0f, -flRadius * 1.5f), 1);
	
	// Add wall points at different heights
	for (float flHeight = -0.5f; flHeight <= 0.5f; flHeight += 0.25f)
	{
		for (int i = 0; i < 8; i++)
		{
			float flAngle = (i / 8.0f) * 2.0f * 3.14159265f;
			Vec3 vPoint = Vec3(cosf(flAngle) * flRadius * 1.2f, sinf(flAngle) * flRadius * 1.2f, flHeight * flRadius);
			m_vecSplashSpherePoints.emplace_back(vPoint, 1);
		}
	}
	
	return m_vecSplashSpherePoints;
}

// Simulate rocket path from start to end and check if it reaches the destination
// Returns true if rocket can reach the target point, false if blocked
bool CAimbotWrangler::SimulateRocketPath(const Vec3& vStart, const Vec3& vEnd, C_BaseEntity* pIgnore)
{
	// Sentry rockets travel in a straight line at 1100 u/s with no gravity
	// Trace the full path - rockets use point traces (vec3_origin hull)
	CTraceFilterWorldAndPropsOnly filter = {};
	
	CGameTrace trace = {};
	
	// Use Ray_t and IEngineTrace directly since H::AimUtils->Trace expects CTraceFilter*
	Ray_t ray = {};
	ray.Init(vStart, vEnd);
	I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &trace);
	
	// If we didn't hit anything, path is clear
	if (!trace.DidHit())
		return true;
	
	// If we hit something, check if we reached close to our destination
	// The trace endpoint should be very close to our target (the surface we want to hit)
	float flDistToTarget = trace.endpos.DistTo(vEnd);
	
	// Allow small tolerance for the surface we're trying to hit
	return flDistToTarget < 3.0f;
}

// Find a valid splash point near the target
// Requirements:
// 1. Player can see the splash point (for aiming)
// 2. Sentry can see the splash point (rocket path clear)
// 3. Splash damage can reach target from splash point
bool CAimbotWrangler::FindSplashPoint(C_TFPlayer* pLocal, C_ObjectSentrygun* pSentry, const Vec3& vTargetPos, C_BaseEntity* pTarget, Vec3& outSplashPoint)
{
	if (!pLocal || !pSentry || !pTarget)
		return false;
	
	const Vec3 vRocketPos = GetSentryRocketPos(pSentry);
	const Vec3 vLocalPos = pLocal->GetShootPos();
	
	// Use configured splash radius percentage from projectile aimbot settings
	float flSplashRadius = SENTRY_ROCKET_SPLASH_RADIUS * (CFG::Aimbot_Amalgam_Projectile_SplashRadius / 100.0f);
	
	// Generate sphere points around target (Amalgam style) with extra floor/wall points
	// Hardcoded to 80 points for better coverage
	constexpr int SPLASH_POINT_COUNT = 80;
	GenerateSplashSphere(flSplashRadius, SPLASH_POINT_COUNT);
	auto& vSpherePoints = m_vecSplashSpherePoints;
	
	struct SplashCandidate
	{
		Vec3 vPoint;
		Vec3 vNormal;
		float flDistToTarget;
		float flScore;  // Lower is better
	};
	std::vector<SplashCandidate> vCandidates;
	
	// Get target feet position (for floor splash) and center
	Vec3 vTargetFeet = vTargetPos;
	if (pTarget->GetClassId() == ETFClassIds::CTFPlayer)
	{
		vTargetFeet.z += pTarget->m_vecMins().z;
	}
	
	Vec3 vTargetCenter = vTargetPos;
	if (pTarget->GetClassId() == ETFClassIds::CTFPlayer)
		vTargetCenter.z += (pTarget->m_vecMaxs().z - pTarget->m_vecMins().z) * 0.5f;
	
	for (auto& spherePoint : vSpherePoints)
	{
		// Test point is relative to target feet for floor splash, center for walls
		Vec3 vTestPoint = vTargetFeet + spherePoint.first;
		
		// Trace from target to find surface
		CTraceFilterHitscan filterWorld = {};
		filterWorld.m_pIgnore = pTarget;
		
		CGameTrace traceToSurface = {};
		H::AimUtils->Trace(vTargetCenter, vTestPoint, MASK_SOLID, &filterWorld, &traceToSurface);
		
		// We need to hit a surface
		if (traceToSurface.fraction >= 1.0f)
			continue;
		
		// Check if surface is valid (not sky, not moving)
		if (traceToSurface.surface.flags & SURF_SKY)
			continue;
		
		// Skip moving surfaces
		if (traceToSurface.m_pEnt && !traceToSurface.m_pEnt->GetAbsVelocity().IsZero())
			continue;
		
		Vec3 vSurfacePoint = traceToSurface.endpos;
		Vec3 vSurfaceNormal = traceToSurface.plane.normal;
		
		// Check distance from surface point to target - must be within splash radius
		float flDistToTarget = vSurfacePoint.DistTo(vTargetFeet);
		if (flDistToTarget > flSplashRadius)
			continue;
		
		// === VALIDATION 1: Player can see the surface point (for aiming) ===
		CTraceFilterHitscan filterPlayer = {};
		filterPlayer.m_pIgnore = pLocal;
		
		CGameTrace tracePlayerToSurface = {};
		H::AimUtils->Trace(vLocalPos, vSurfacePoint, MASK_SOLID, &filterPlayer, &tracePlayerToSurface);
		
		// Player must be able to see the surface (trace should reach it)
		if (tracePlayerToSurface.fraction < 0.98f)
			continue;
		
		// === VALIDATION 2: Sentry rocket can reach the surface point (NO WALLS IN BETWEEN) ===
		if (!SimulateRocketPath(vRocketPos, vSurfacePoint, pSentry))
			continue;
		
		// Check rocket approach angle - surface must face the rocket
		Vec3 vRocketDir = (vSurfacePoint - vRocketPos).Normalized();
		float flDotProduct = vRocketDir.Dot(vSurfaceNormal);
		if (flDotProduct >= 0)  // Surface facing away from rocket
			continue;
		
		// === VALIDATION 3: Splash damage can reach target ===
		Vec3 vSplashOrigin = vSurfacePoint + vSurfaceNormal * 1.0f;
		CGameTrace traceSplashToTarget = {};
		H::AimUtils->Trace(vSplashOrigin, vTargetCenter, MASK_SHOT, &filterWorld, &traceSplashToTarget);
		
		// Splash must be able to reach target (or hit the target)
		if (traceSplashToTarget.fraction < 1.0f && traceSplashToTarget.m_pEnt != pTarget)
			continue;
		
		// Calculate score - prefer closer splash points and floor hits
		float flScore = flDistToTarget;
		
		// Bonus for floor hits (normal pointing up)
		if (vSurfaceNormal.z > 0.7f)
			flScore *= 0.8f;
		
		// Bonus for being closer to target feet
		float flFeetDist = vSurfacePoint.DistTo(vTargetFeet);
		flScore += flFeetDist * 0.1f;
		
		vCandidates.push_back({ vSurfacePoint, vSurfaceNormal, flDistToTarget, flScore });
	}
	
	if (vCandidates.empty())
		return false;
	
	// Sort by score (lower is better)
	std::sort(vCandidates.begin(), vCandidates.end(), [](const SplashCandidate& a, const SplashCandidate& b) {
		return a.flScore < b.flScore;
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
			
			// Ignore untagged players when key is held
			if (CFG::Aimbot_Ignore_Untagged_Key != 0 && H::Input->IsDown(CFG::Aimbot_Ignore_Untagged_Key))
			{
				PlayerPriority playerPriority = {};
				if (!F::Players->GetInfo(pPlayer->entindex(), playerPriority) ||
					(!playerPriority.Cheater && !playerPriority.Targeted && !playerPriority.Nigger && !playerPriority.RetardLegit && !playerPriority.Streamer))
					continue;
			}
			
			// Target body for sentry (more reliable)
			Vec3 vPos = pPlayer->GetHitboxPos(HITBOX_PELVIS);
			
			// Check if SENTRY can see the target (this is what matters for wrangler)
			CGameTrace sentryTrace = {};
			CTraceFilterHitscan sentryFilter = {};
			sentryFilter.m_pIgnore = pSentry;
			H::AimUtils->Trace(vSentryPos, vPos, MASK_SHOT, &sentryFilter, &sentryTrace);
			if (sentryTrace.m_pEnt != pPlayer && sentryTrace.fraction < 1.0f)
				continue;
			
			// Wrangler aims based on player's view - calculate angle from player's eye
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			const float flDistTo = vSentryPos.DistTo(vPos);
			
			if (flFOVTo > CFG::Aimbot_Hitscan_FOV)
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
			
			// Check if SENTRY can see the target
			CGameTrace sentryTrace = {};
			CTraceFilterHitscan sentryFilter = {};
			sentryFilter.m_pIgnore = pSentry;
			H::AimUtils->Trace(vSentryPos, vPos, MASK_SHOT, &sentryFilter, &sentryTrace);
			if (sentryTrace.m_pEnt != pBuilding && sentryTrace.fraction < 1.0f)
				continue;
			
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
	
	// Sort targets by FOV (closest to crosshair first) or distance
	int iSortHitscan = CFG::Aimbot_Hitscan_Sort;
	std::sort(m_vecTargets.begin(), m_vecTargets.end(), [iSortHitscan](const WranglerTarget_t& a, const WranglerTarget_t& b) {
		if (iSortHitscan == 1)
			return a.DistanceTo < b.DistanceTo;
		return a.FOVTo < b.FOVTo;
	});
	
	// Return the single best target (already filtered for sentry visibility)
	outTarget = m_vecTargets[0];
	return true;
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
			
			// Ignore untagged players when key is held
			if (CFG::Aimbot_Ignore_Untagged_Key != 0 && H::Input->IsDown(CFG::Aimbot_Ignore_Untagged_Key))
			{
				PlayerPriority playerPriority = {};
				if (!F::Players->GetInfo(pPlayer->entindex(), playerPriority) ||
					(!playerPriority.Cheater && !playerPriority.Targeted && !playerPriority.Nigger && !playerPriority.RetardLegit && !playerPriority.Streamer))
					continue;
			}
			
			// Initial position for FOV check (from player's view)
			Vec3 vCurrentPos = pPlayer->GetCenter();
			Vec3 vAngleToCheck = Math::CalcAngle(vLocalPos, vCurrentPos);
			const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleToCheck);
			
			if (flFOVTo > CFG::Aimbot_Hitscan_FOV)
				continue;
			
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
			
			// Check if sentry can see the building (point trace like source SDK)
			CGameTrace sentryTrace = {};
			CTraceFilterHitscan sentryFilter = {};
			sentryFilter.m_pIgnore = pSentry;
			H::AimUtils->Trace(vRocketPos, vPos, MASK_SOLID, &sentryFilter, &sentryTrace);
			if (sentryTrace.m_pEnt != pBuilding && sentryTrace.fraction < 1.0f)
				continue;
			
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
	
	// Sort targets by FOV (closest to crosshair first) or distance
	int iSortRocket = CFG::Aimbot_Hitscan_Sort;
	std::sort(m_vecTargets.begin(), m_vecTargets.end(), [iSortRocket](const WranglerTarget_t& a, const WranglerTarget_t& b) {
		if (iSortRocket == 1)
			return a.DistanceTo < b.DistanceTo;
		return a.FOVTo < b.FOVTo;
	});
	
	// Only process the single best target (closest to crosshair/nearest)
	// This avoids running expensive movement simulation on multiple targets
	auto& target = m_vecTargets[0];
	
	// For players, do movement prediction NOW (only for this single target)
	Vec3 vTargetPos = target.Position;
	float flTravelTime = 0.0f;
	bool bIsAirborne = false;
	
	if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
	{
		auto pPlayer = target.Entity->As<C_TFPlayer>();
		
		// Check if player is airborne (not on ground)
		bIsAirborne = !(pPlayer->m_fFlags() & FL_ONGROUND);
		
		// Single prediction pass - calculate travel time then predict position
		flTravelTime = GetRocketTravelTime(vRocketPos, target.Position);
		Vec3 vPredictedPos = PredictTargetPosition(pPlayer, flTravelTime);
		vPredictedPos.z += (pPlayer->m_vecMaxs().z - pPlayer->m_vecMins().z) * 0.5f;
		
		vTargetPos = vPredictedPos;
		target.Position = vPredictedPos;
		target.AngleTo = Math::CalcAngle(vLocalPos, vPredictedPos);
		target.PredictedTime = flTravelTime;
	}
	
	// Check if we can direct hit the target (rocket path must be clear to the target entity)
	CGameTrace trace = {};
	CTraceFilterHitscan filter = {};
	filter.m_pIgnore = pSentry;
	H::AimUtils->Trace(vRocketPos, vTargetPos, MASK_SOLID, &filter, &trace);
	
	bool bCanDirectHit = (trace.m_pEnt == target.Entity || trace.fraction >= 0.99f);
	
	// AIRBORNE TARGETS: Try direct hit first, then splash
	if (bIsAirborne)
	{
		if (bCanDirectHit)
		{
			outTarget = target;
			return true;
		}
		
		// Try splash as fallback for airborne
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
		// No valid aim solution for this airborne target
		return false;
	}
	
	// GROUNDED TARGETS: Try splash first, then direct hit
	// Try splash first for grounded targets
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
	
	// Fallback to direct hit if splash failed (only if path is clear)
	if (bCanDirectHit)
	{
		outTarget = target;
		return true;
	}
	
	// No valid aim solution for this target
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

void CAimbotWrangler::DrawVisualization(C_ObjectSentrygun* pSentry, const WranglerTarget_t& target, bool bIsRocket)
{
	if (!pSentry || !target.Entity)
		return;
	
	// Only draw if visualization is enabled
	if (CFG::Visuals_Draw_Predicted_Path_Style == 0)
		return;
	
	const Vec3 vRocketPos = GetSentryRocketPos(pSentry);
	constexpr float flDrawDuration = 0.1f;  // Short duration, refreshes each frame
	
	// Draw rocket trajectory using projectile path style and color
	if (CFG::Visuals_Draw_Predicted_Path_Style > 0 && bIsRocket)
	{
		const auto& col = CFG::Color_Simulation_Projectile;
		const int r = col.r, g = col.g, b = col.b;
		
		// Style 1: Line
		if (CFG::Visuals_Draw_Predicted_Path_Style == 1)
		{
			I::DebugOverlay->AddLineOverlay(vRocketPos, target.Position, r, g, b, false, flDrawDuration);
		}
		// Style 2: Dashed - draw segments
		else if (CFG::Visuals_Draw_Predicted_Path_Style == 2)
		{
			Vec3 vDir = (target.Position - vRocketPos).Normalized();
			float flDist = vRocketPos.DistTo(target.Position);
			float flSegmentLen = 20.0f;
			int nSegments = static_cast<int>(flDist / flSegmentLen);
			for (int i = 0; i < nSegments; i += 2)
			{
				Vec3 vStart = vRocketPos + vDir * (i * flSegmentLen);
				Vec3 vEnd = vRocketPos + vDir * std::min((i + 1) * flSegmentLen, flDist);
				I::DebugOverlay->AddLineOverlay(vStart, vEnd, r, g, b, false, flDrawDuration);
			}
		}
		// Style 3: With markers
		else if (CFG::Visuals_Draw_Predicted_Path_Style == 3)
		{
			I::DebugOverlay->AddLineOverlay(vRocketPos, target.Position, r, g, b, false, flDrawDuration);
			// Add cross markers along the path
			Vec3 vDir = (target.Position - vRocketPos).Normalized();
			float flDist = vRocketPos.DistTo(target.Position);
			Vec3 right{};
			Math::AngleVectors(Math::CalcAngle(vRocketPos, target.Position), nullptr, &right, nullptr);
			for (float f = 50.0f; f < flDist; f += 50.0f)
			{
				Vec3 vPoint = vRocketPos + vDir * f;
				I::DebugOverlay->AddLineOverlay(vPoint + right * 5.0f, vPoint - right * 5.0f, r, g, b, false, flDrawDuration);
			}
		}
		
		// Draw box at impact point
		I::DebugOverlay->AddBoxOverlay(target.Position, Vec3(-4, -4, -4), Vec3(4, 4, 4), Vec3(0, 0, 0), r, g, b, 100, flDrawDuration);
	}
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
	
	// Only run when aimbot key is pressed (or Always On is enabled)
	const bool bAimKeyDown = CFG::Aimbot_Always_On || (CFG::Aimbot_Key == 0) || H::Input->IsDown(CFG::Aimbot_Key);
	if (!bAimKeyDown)
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
	
	// Find targets only when key is pressed
	if (CanFireBullets(pSentry))
		bFoundBulletTarget = GetHitscanTarget(pLocal, pSentry, bulletTarget);
	
	if (bRocketReady)
		bFoundRocketTarget = GetRocketTarget(pLocal, pSentry, rocketTarget);
	
	// Determine which target to aim at
	WranglerTarget_t* pAimTarget = nullptr;
	
	if (bRocketReady && bFoundRocketTarget)
	{
		// Rocket is ready - prioritize rocket aim with prediction
		pAimTarget = &rocketTarget;
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
	
	// Draw visualization for rocket targets
	if (bRocketReady && bFoundRocketTarget)
		DrawVisualization(pSentry, rocketTarget, true);
	
	Aim(pCmd, pLocal, pAimTarget->AngleTo);
	
	// Auto-fire
	if (ShouldFire(pLocal, pSentry, *pAimTarget))
	{
		// Always fire bullets if we have ammo
		if (CanFireBullets(pSentry))
			pCmd->buttons |= IN_ATTACK;
		
		// Fire rocket if ready
		if (bRocketReady && bFoundRocketTarget)
			pCmd->buttons |= IN_ATTACK2;
	}
}
