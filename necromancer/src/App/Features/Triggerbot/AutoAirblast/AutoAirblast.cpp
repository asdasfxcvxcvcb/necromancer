#include "AutoAirblast.h"

#include "../../CFG.h"
#include "../../MovementSimulation/MovementSimulation.h"
#include "../../ProjectileSim/ProjectileSim.h"
#include "../../amalgam_port/ProjectileSimulation/ProjectileSimulation.h"
#include "../../amalgam_port/Ticks/Ticks.h"

#include <unordered_map>

// ============================================================================
// CONSTANTS - Projectile speeds and properties from Source SDK
// ============================================================================

// Reflected projectile speeds (from tf_weaponbase_rocket.cpp and tf_weapon_flamethrower.cpp)
constexpr float REFLECTED_ROCKET_SPEED = 1100.0f;        // Standard rockets
constexpr float REFLECTED_SENTRY_ROCKET_SPEED = 1100.0f; // Sentry rockets
constexpr float REFLECTED_ARROW_SPEED = 2600.0f;         // Huntsman arrows (max charge)
constexpr float REFLECTED_FLARE_SPEED = 2000.0f;         // Flares
constexpr float REFLECTED_PIPE_SPEED = 1200.0f;          // Pipes/stickies

// Splash radii (from tf_shareddefs.h)
constexpr float ROCKET_SPLASH_RADIUS = 146.0f;
constexpr float SENTRY_ROCKET_SPLASH_RADIUS = 146.0f;

// Gravity multipliers for arc projectiles
constexpr float ARROW_GRAVITY = 0.2f;
constexpr float PIPE_GRAVITY = 1.0f;
constexpr float FLARE_GRAVITY = 0.3f;

// Airblast cone angle (from tf_weapon_flamethrower.cpp)
constexpr float AIRBLAST_CONE_ANGLE = 35.0f;

// ============================================================================
// HELPER STRUCTURES
// ============================================================================

struct TargetProjectile
{
	C_BaseProjectile* Projectile = nullptr;
	Vec3 Position = {};
};

struct AirblastAimbotTarget
{
	C_BaseEntity* Entity = nullptr;
	Vec3 Position = {};
	Vec3 PredictedPosition = {};
	Vec3 AimAngles = {};
	float FOV = 0.0f;
	float Distance = 0.0f;
	float TravelTime = 0.0f;
	bool IsAirborne = false;
	bool UseSplash = false;
	Vec3 SplashPoint = {};
};


// ============================================================================
// PROJECTILE TYPE DETECTION
// ============================================================================

EReflectProjectileType CAutoAirblast::GetProjectileType(C_BaseProjectile* pProjectile)
{
	if (!pProjectile)
		return EReflectProjectileType::Unknown;

	switch (pProjectile->GetClassId())
	{
	// Straight line projectiles (no gravity)
	case ETFClassIds::CTFProjectile_Rocket:
	case ETFClassIds::CTFProjectile_SentryRocket:
	case ETFClassIds::CTFProjectile_EnergyBall:
	case ETFClassIds::CTFProjectile_EnergyRing:
		return EReflectProjectileType::Rocket;

	// Arc projectiles (gravity affected)
	case ETFClassIds::CTFProjectile_Arrow:
	case ETFClassIds::CTFProjectile_HealingBolt:
	case ETFClassIds::CTFGrenadePipebombProjectile:
	case ETFClassIds::CTFProjectile_Flare:
	case ETFClassIds::CTFProjectile_Jar:
	case ETFClassIds::CTFProjectile_JarMilk:
	case ETFClassIds::CTFProjectile_JarGas:
	case ETFClassIds::CTFProjectile_Cleaver:
		return EReflectProjectileType::Arc;

	default:
		return EReflectProjectileType::Unknown;
	}
}

bool CAutoAirblast::SupportsAimbot(C_BaseProjectile* pProjectile)
{
	if (!pProjectile)
		return false;

	EReflectProjectileType type = GetProjectileType(pProjectile);
	return type == EReflectProjectileType::Rocket || type == EReflectProjectileType::Arc;
}

float CAutoAirblast::GetReflectedProjectileSpeed(C_BaseProjectile* pProjectile)
{
	if (!pProjectile)
		return REFLECTED_ROCKET_SPEED;

	switch (pProjectile->GetClassId())
	{
	case ETFClassIds::CTFProjectile_Rocket:
		return REFLECTED_ROCKET_SPEED;
	case ETFClassIds::CTFProjectile_SentryRocket:
		return REFLECTED_SENTRY_ROCKET_SPEED;
	case ETFClassIds::CTFProjectile_Arrow:
	case ETFClassIds::CTFProjectile_HealingBolt:
		return REFLECTED_ARROW_SPEED;
	case ETFClassIds::CTFProjectile_Flare:
		return REFLECTED_FLARE_SPEED;
	case ETFClassIds::CTFGrenadePipebombProjectile:
		return REFLECTED_PIPE_SPEED;
	case ETFClassIds::CTFProjectile_EnergyBall:
	case ETFClassIds::CTFProjectile_EnergyRing:
		return 1100.0f;
	default:
		return REFLECTED_ROCKET_SPEED;
	}
}

float CAutoAirblast::GetReflectedRocketSpeed(C_BaseProjectile* pProjectile)
{
	return GetReflectedProjectileSpeed(pProjectile);
}

float CAutoAirblast::GetReflectedSplashRadius(C_BaseProjectile* pProjectile)
{
	if (!pProjectile)
		return 0.0f;

	// Only rockets and sentry rockets have splash damage
	// Everything else (arrows, flares, pipes, etc.) should use direct hits
	switch (pProjectile->GetClassId())
	{
	case ETFClassIds::CTFProjectile_Rocket:
		return ROCKET_SPLASH_RADIUS;
	case ETFClassIds::CTFProjectile_SentryRocket:
		return SENTRY_ROCKET_SPLASH_RADIUS;
	default:
		return 0.0f; // No splash for arrows, flares, pipes, energy balls, etc.
	}
}


// ============================================================================
// SPLASH POINT GENERATION (Fibonacci sphere like AimbotWrangler)
// Cache the sphere points since they're always the same for a given radius
// ============================================================================

static const std::vector<std::pair<Vec3, int>>& GetCachedSplashSphere(float flRadius, int nSamples)
{
	// Cache for common radius values to avoid regeneration
	static std::unordered_map<int, std::vector<std::pair<Vec3, int>>> s_mapCache;
	
	// Use integer key (radius * 10) to handle floating point comparison
	const int nKey = static_cast<int>(flRadius * 10.0f);
	
	auto it = s_mapCache.find(nKey);
	if (it != s_mapCache.end())
		return it->second;
	
	// Generate new sphere points
	std::vector<std::pair<Vec3, int>> vPoints;
	vPoints.reserve(nSamples * 2 + 50);

	const float fPhi = 3.14159265f * (3.0f - sqrtf(5.0f)); // Golden angle

	// Standard sphere points
	for (int i = 0; i < nSamples; i++)
	{
		const float t = fPhi * i;
		const float y = 1.0f - (i / float(nSamples - 1)) * 2.0f;
		const float r = sqrtf(1.0f - y * y);
		const float x = cosf(t) * r;
		const float z = sinf(t) * r;

		Vec3 vPoint = Vec3(x, y, z) * flRadius;
		vPoints.emplace_back(vPoint, 1);
	}

	// Add extra floor points spread outward
	for (float flMult = 0.5f; flMult <= 1.5f; flMult += 0.25f)
	{
		const float flDist = flRadius * flMult;
		for (int i = 0; i < 8; i++)
		{
			const float flAngle = (i / 8.0f) * 2.0f * 3.14159265f;
			Vec3 vPoint = Vec3(cosf(flAngle) * flDist, sinf(flAngle) * flDist, -flRadius);
			vPoints.emplace_back(vPoint, 1);
		}
	}

	// Direct floor point
	vPoints.emplace_back(Vec3(0.0f, 0.0f, -flRadius * 1.5f), 1);

	// Wall points at different heights
	for (float flHeight = -0.5f; flHeight <= 0.5f; flHeight += 0.25f)
	{
		for (int i = 0; i < 8; i++)
		{
			const float flAngle = (i / 8.0f) * 2.0f * 3.14159265f;
			Vec3 vPoint = Vec3(cosf(flAngle) * flRadius * 1.2f, sinf(flAngle) * flRadius * 1.2f, flHeight * flRadius);
			vPoints.emplace_back(vPoint, 1);
		}
	}

	s_mapCache[nKey] = std::move(vPoints);
	return s_mapCache[nKey];
}

// Legacy wrapper for compatibility
static std::vector<std::pair<Vec3, int>> GenerateSplashSphere(float flRadius, int nSamples)
{
	return GetCachedSplashSphere(flRadius, nSamples);
}

// ============================================================================
// SPLASH POINT FINDING - Finds the CLOSEST valid splash point to the enemy
// ============================================================================

static bool FindSplashPoint(C_TFPlayer* pLocal, const Vec3& vShootPos, const Vec3& vTargetPos, 
	C_BaseEntity* pTarget, float flSplashRadius, Vec3& outSplashPoint)
{
	if (!pLocal || !pTarget || flSplashRadius <= 0.0f)
		return false;

	auto vSpherePoints = GenerateSplashSphere(flSplashRadius, 60);

	struct SplashCandidate
	{
		Vec3 vPoint;
		Vec3 vNormal;
		float flDistToTarget;  // Distance from splash point to target - PRIMARY sort key
	};
	std::vector<SplashCandidate> vCandidates;

	// Get target feet position (for ground splash)
	Vec3 vTargetFeet = vTargetPos;
	if (pTarget->GetClassId() == ETFClassIds::CTFPlayer)
	{
		auto pPlayer = pTarget->As<C_TFPlayer>();
		vTargetFeet = pPlayer->GetAbsOrigin();  // Use actual origin, not center
	}

	Vec3 vTargetCenter = vTargetPos;
	if (pTarget->GetClassId() == ETFClassIds::CTFPlayer)
		vTargetCenter.z += (pTarget->m_vecMaxs().z - pTarget->m_vecMins().z) * 0.5f;

	for (auto& spherePoint : vSpherePoints)
	{
		Vec3 vTestPoint = vTargetFeet + spherePoint.first;

		// Trace from target to find surface
		CTraceFilterHitscan filterWorld = {};
		filterWorld.m_pIgnore = pTarget;

		CGameTrace traceToSurface = {};
		H::AimUtils->Trace(vTargetCenter, vTestPoint, MASK_SOLID, &filterWorld, &traceToSurface);

		if (traceToSurface.fraction >= 1.0f)
			continue;

		if (traceToSurface.surface.flags & SURF_SKY)
			continue;

		// Skip moving surfaces
		if (traceToSurface.m_pEnt && !traceToSurface.m_pEnt->GetAbsVelocity().IsZero())
			continue;

		Vec3 vSurfacePoint = traceToSurface.endpos;
		Vec3 vSurfaceNormal = traceToSurface.plane.normal;

		// Calculate distance from splash point to target feet
		float flDistToTarget = vSurfacePoint.DistTo(vTargetFeet);
		
		// Must be within splash radius
		if (flDistToTarget > flSplashRadius)
			continue;

		// Check if we can see the surface point (for aiming)
		CTraceFilterHitscan filterPlayer = {};
		filterPlayer.m_pIgnore = pLocal;

		CGameTrace tracePlayerToSurface = {};
		H::AimUtils->Trace(vShootPos, vSurfacePoint, MASK_SOLID, &filterPlayer, &tracePlayerToSurface);

		if (tracePlayerToSurface.fraction < 0.98f)
			continue;

		// Check rocket approach angle - rocket must hit the surface from the correct side
		Vec3 vRocketDir = (vSurfacePoint - vShootPos).Normalized();
		float flDotProduct = vRocketDir.Dot(vSurfaceNormal);
		if (flDotProduct >= 0)  // Rocket would hit from behind the surface
			continue;

		// Check splash can reach target (no walls between splash and target)
		Vec3 vSplashOrigin = vSurfacePoint + vSurfaceNormal * 1.0f;
		CGameTrace traceSplashToTarget = {};
		H::AimUtils->Trace(vSplashOrigin, vTargetCenter, MASK_SHOT, &filterWorld, &traceSplashToTarget);

		if (traceSplashToTarget.fraction < 1.0f && traceSplashToTarget.m_pEnt != pTarget)
			continue;

		vCandidates.push_back({ vSurfacePoint, vSurfaceNormal, flDistToTarget });
	}

	if (vCandidates.empty())
		return false;

	// Sort by distance to target - CLOSEST FIRST
	// This ensures we pick the splash point that will deal the most damage
	std::sort(vCandidates.begin(), vCandidates.end(), [](const SplashCandidate& a, const SplashCandidate& b) {
		return a.flDistToTarget < b.flDistToTarget;
	});

	outSplashPoint = vCandidates[0].vPoint;
	return true;
}


// ============================================================================
// PROJECTILE PATH CALCULATION
// For rockets (no gravity): straight line from projectile pos to aim point
// For arc projectiles: simulate with gravity/drag
//
// The correct way to visualize:
// 1. Trace from LOCAL PLAYER EYE in aim direction → get END POINT
// 2. Draw line from PROJECTILE POSITION to that END POINT
// ============================================================================

// Get the end point where we're aiming (trace from eye in aim direction)
static Vec3 GetAimEndPoint(C_TFPlayer* pLocal, const Vec3& vAimAngles, float flMaxDist = 8192.0f)
{
	Vec3 vEyePos = pLocal->GetShootPos();
	Vec3 vForward;
	Math::AngleVectors(vAimAngles, &vForward, nullptr, nullptr);
	Vec3 vEndPos = vEyePos + vForward * flMaxDist;
	
	CTraceFilterWorldAndPropsOnly filter = {};
	CGameTrace trace = {};
	Ray_t ray = {};
	ray.Init(vEyePos, vEndPos);
	I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &trace);
	
	return trace.endpos;
}

// Build the projectile path from projectile position to aim end point
// For rockets: straight line
// For arc projectiles: simulate with physics
static bool BuildProjectilePath(const Vec3& vProjectilePos, const Vec3& vAimEndPoint, 
	float flSpeed, float flGravity, int nProjectileType, std::vector<Vec3>& outPath, bool& bHitsWall)
{
	outPath.clear();
	outPath.push_back(vProjectilePos);
	bHitsWall = false;

	// For rockets (no gravity), just draw a straight line
	if (flGravity <= 0.001f)
	{
		// Trace from projectile to aim point
		CTraceFilterWorldAndPropsOnly filter = {};
		CGameTrace trace = {};
		Ray_t ray = {};
		ray.Init(vProjectilePos, vAimEndPoint);
		I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &trace);
		
		outPath.push_back(trace.endpos);
		bHitsWall = trace.DidHit();
		return true;
	}

	// For arc projectiles, we need to simulate with physics
	const float flMaxTime = std::max(CFG::Aimbot_Projectile_Max_Simulation_Time, 2.0f);
	const int nMaxTicks = TIME_TO_TICKS(flMaxTime);

	// Calculate direction from projectile to aim point
	Vec3 vDir = (vAimEndPoint - vProjectilePos).Normalized();
	Vec3 vAimAngles = Math::CalcAngle(Vec3(0, 0, 0), vDir);

	// For pipes/stickies/cannonballs, use the proper ProjectileSim with VPhysics
	bool bIsPipe = (nProjectileType == TF_PROJECTILE_PIPEBOMB || 
	                nProjectileType == TF_PROJECTILE_PIPEBOMB_REMOTE ||
	                nProjectileType == TF_PROJECTILE_CANNONBALL);

	if (bIsPipe)
	{
		// Use ProjectileSim for accurate pipe physics
		ProjSimInfo info = {};
		info.m_type = static_cast<ProjectileType_t>(nProjectileType);
		info.m_pos = vProjectilePos;
		info.m_ang = vAimAngles;
		info.m_speed = flSpeed;
		info.m_gravity_mod = flGravity;
		info.no_spin = false;

		if (!F::ProjectileSim->Init(info, true))
			goto simple_sim;

		CTraceFilterWorldAndPropsOnly filter = {};

		for (int i = 0; i < nMaxTicks; i++)
		{
			Vec3 vOldPos = F::ProjectileSim->GetOrigin();
			F::ProjectileSim->RunTick();
			Vec3 vNewPos = F::ProjectileSim->GetOrigin();

			outPath.push_back(vNewPos);

			CGameTrace trace = {};
			Ray_t ray = {};
			ray.Init(vOldPos, vNewPos);
			I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &trace);

			if (trace.DidHit())
			{
				outPath.back() = trace.endpos;
				bHitsWall = true;
				return true;
			}
		}
		return false;
	}

simple_sim:
	// Simple physics for arrows, flares (gravity but no drag)
	Vec3 vForward;
	Math::AngleVectors(vAimAngles, &vForward, nullptr, nullptr);
	Vec3 vVelocity = vForward * flSpeed;

	Vec3 vPos = vProjectilePos;
	const float flDt = TICK_INTERVAL;

	CTraceFilterWorldAndPropsOnly filter = {};

	for (int i = 0; i < nMaxTicks; i++)
	{
		Vec3 vOldPos = vPos;
		vVelocity.z -= 800.0f * flGravity * flDt;
		vPos = vPos + vVelocity * flDt;
		outPath.push_back(vPos);

		CGameTrace trace = {};
		Ray_t ray = {};
		ray.Init(vOldPos, vPos);
		I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &trace);

		if (trace.DidHit())
		{
			outPath.back() = trace.endpos;
			bHitsWall = true;
			return true;
		}
	}
	return false;
}

// Legacy function for compatibility - now just wraps BuildProjectilePath
static bool SimulateReflectedProjectilePath(const Vec3& vProjectilePos, const Vec3& vAimAngles, 
	float flSpeed, float flGravity, int nProjectileType, std::vector<Vec3>& outPath, bool& bHitsWall)
{
	// Get end point from aim angles (far away in that direction)
	Vec3 vForward;
	Math::AngleVectors(vAimAngles, &vForward, nullptr, nullptr);
	Vec3 vAimEndPoint = vProjectilePos + vForward * 8192.0f;
	
	return BuildProjectilePath(vProjectilePos, vAimEndPoint, flSpeed, flGravity, nProjectileType, outPath, bHitsWall);
}

// Overload without projectile type (defaults to simple physics)
static bool SimulateReflectedProjectilePath(const Vec3& vProjectilePos, const Vec3& vAimAngles, 
	float flSpeed, float flGravity, std::vector<Vec3>& outPath, bool& bHitsWall)
{
	return SimulateReflectedProjectilePath(vProjectilePos, vAimAngles, flSpeed, flGravity, 0, outPath, bHitsWall);
}

// ============================================================================
// AIRBORNE DETECTION - Distinguish truly airborne players from bunnyhopping
// ============================================================================

// Check if a player is truly airborne (not just bunnyhopping)
// Truly airborne = high off ground, significant upward/downward velocity, or blast jumping
static bool IsTrulyAirborne(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return false;
	
	// If on ground, definitely not airborne
	if (pPlayer->m_fFlags() & FL_ONGROUND)
		return false;
	
	// Check vertical velocity - bunnyhopping has low vertical velocity
	Vec3 vVel = pPlayer->m_vecVelocity();
	float flVerticalSpeed = fabsf(vVel.z);
	
	// High vertical speed indicates blast jump, fall, or being launched
	// Bunnyhopping typically has vertical speed < 300
	if (flVerticalSpeed > 300.0f)
		return true;
	
	// Check height above ground
	Vec3 vOrigin = pPlayer->GetAbsOrigin();
	CTraceFilterHitscan filter = {};
	filter.m_pIgnore = pPlayer;
	CGameTrace trace = {};
	H::AimUtils->Trace(vOrigin, vOrigin - Vec3(0, 0, 200), MASK_SOLID, &filter, &trace);
	
	float flHeightAboveGround = 200.0f * (1.0f - trace.fraction);
	
	// If more than ~80 units off ground, they're truly airborne
	// Bunnyhopping rarely gets you more than 50-60 units off ground
	if (flHeightAboveGround > 80.0f)
		return true;
	
	// Check for blast jump conditions (rocket/sticky jumping)
	if (pPlayer->InCond(TF_COND_BLASTJUMPING))
		return true;
	
	// Not truly airborne - probably bunnyhopping or just jumped
	return false;
}

// Check if there's a clear path from projectile to target (no obstacles)
static bool HasClearPath(const Vec3& vProjPos, const Vec3& vTargetPos, C_BaseEntity* pIgnore = nullptr)
{
	CTraceFilterHitscan filter = {};
	filter.m_pIgnore = pIgnore;
	CGameTrace trace = {};
	H::AimUtils->Trace(vProjPos, vTargetPos, MASK_SOLID, &filter, &trace);
	
	// Clear only if we reach all the way without hitting anything
	return trace.fraction >= 1.0f;
}

// ============================================================================
// INTERCEPT CALCULATION - Solve for aim angle that syncs projectile with target
// 
// KEY INSIGHT: The reflected projectile travels in the direction we're LOOKING.
// So if we look at angle A, the projectile goes from its position in direction A.
// 
// To hit a target at position T from projectile position P:
// - We need to look in the direction (T - P).Normalized()
// - The aim angle is calculated from the PROJECTILE position to the TARGET
// - NOT from our eye to the target!
// ============================================================================

// Calculate aim angle for DIRECT HIT on moving/airborne targets
// The reflected projectile starts at vProjPos and travels in the direction we're LOOKING
// So we need to find the view angle that makes (vProjPos + viewDir * t) pass through vTargetPos
// This means: viewDir = (vTargetPos - vProjPos).Normalized()
static Vec3 CalcAimAngleForReflect(const Vec3& vProjPos, const Vec3& vTargetPos)
{
	Vec3 vDir = vTargetPos - vProjPos;
	
	if (vDir.Length() < 1.0f)
		return Vec3(0, 0, 0);
	
	// The direction from projectile to target IS the direction we need to look
	return Math::CalcAngle(vProjPos, vTargetPos);
}

// Calculate aim angle for SPLASH POINTS (ground, walls, geometry)
// For fixed world positions, we aim from our eye to the splash point
// This is simpler because the splash point doesn't move
static Vec3 CalcAimAngleForSplash(const Vec3& vEyePos, const Vec3& vSplashPoint)
{
	Vec3 vDir = vSplashPoint - vEyePos;
	
	if (vDir.Length() < 1.0f)
		return Vec3(0, 0, 0);
	
	return Math::CalcAngle(vEyePos, vSplashPoint);
}

// Iteratively solve for intercept - find aim angle where projectile arrives at target position
// at the same time the target arrives there
static bool SolveIntercept(C_TFPlayer* pLocal, C_TFPlayer* pTarget, const Vec3& vProjPos, 
	float flProjSpeed, float flSplashRadius, bool bUseSplash,
	Vec3& outAimAngles, Vec3& outInterceptPos, float& outTravelTime, std::vector<Vec3>& outPlayerPath)
{
	const Vec3 vEyePos = pLocal->GetShootPos();
	const float flLatency = SDKUtils::GetLatency();
	const int nMaxIterations = 6;
	const float flConvergenceThreshold = 2.0f;
	
	// Start with current target position
	Vec3 vInterceptPos = pTarget->GetCenter();
	float flTravelTime = 0.0f;
	float flLastDelta = FLT_MAX;
	
	// Get target velocity for fallback
	Vec3 vTargetVel = pTarget->m_vecVelocity();
	bool bIsMoving = vTargetVel.Length2D() > 10.0f;
	
	// Initialize movement simulation
	outPlayerPath.clear();
	
	// For stationary or slow-moving targets, just use current position
	if (!bIsMoving)
	{
		outInterceptPos = pTarget->GetCenter();
		outTravelTime = vProjPos.DistTo(outInterceptPos) / flProjSpeed + flLatency;
		outAimAngles = CalcAimAngleForReflect(vProjPos, outInterceptPos);
		outPlayerPath.push_back(pTarget->GetAbsOrigin());
		return true;
	}
	
	if (!F::MovementSimulation->Initialize(pTarget))
	{
		// Fallback to simple velocity extrapolation
		flTravelTime = vProjPos.DistTo(vInterceptPos) / flProjSpeed + flLatency;
		outInterceptPos = pTarget->GetAbsOrigin() + vTargetVel * flTravelTime;
		outInterceptPos.z += (pTarget->m_vecMaxs().z - pTarget->m_vecMins().z) * 0.5f;
		outAimAngles = CalcAimAngleForReflect(vProjPos, outInterceptPos);
		outPlayerPath.push_back(pTarget->GetAbsOrigin());
		return true;
	}
	
	// First pass: estimate travel time to get simulation length
	float flInitialDist = vProjPos.DistTo(vInterceptPos);
	float flEstimatedTime = flInitialDist / flProjSpeed + flLatency;
	
	// Simulate for the estimated travel time + buffer
	// Keep it short to avoid over-prediction
	int nSimTicks = TIME_TO_TICKS(flEstimatedTime + 0.3f);
	nSimTicks = std::clamp(nSimTicks, 5, TIME_TO_TICKS(1.5f));  // Max 1.5 seconds
	
	// Build player path by running simulation
	outPlayerPath.push_back(pTarget->GetAbsOrigin());
	for (int t = 0; t < nSimTicks; t++)
	{
		F::MovementSimulation->RunTick();
		outPlayerPath.push_back(F::MovementSimulation->GetOrigin());
	}
	F::MovementSimulation->Restore();
	
	// Iteratively solve for intercept
	for (int iter = 0; iter < nMaxIterations; iter++)
	{
		// Calculate travel time from projectile to current intercept position
		float flDist = vProjPos.DistTo(vInterceptPos);
		flTravelTime = flDist / flProjSpeed + flLatency;
		
		// Get player position at that travel time
		int nTick = TIME_TO_TICKS(flTravelTime);
		nTick = std::clamp(nTick, 0, static_cast<int>(outPlayerPath.size()) - 1);
		
		Vec3 vNewInterceptPos = outPlayerPath[nTick];
		
		// Add height offset for body center
		vNewInterceptPos.z += (pTarget->m_vecMaxs().z - pTarget->m_vecMins().z) * 0.5f;
		
		// Check convergence
		float flDelta = vNewInterceptPos.DistTo(vInterceptPos);
		if (flDelta < flConvergenceThreshold || flDelta >= flLastDelta)
			break;
		
		flLastDelta = flDelta;
		vInterceptPos = vNewInterceptPos;
	}
	
	outInterceptPos = vInterceptPos;
	outTravelTime = flTravelTime;
	outAimAngles = CalcAimAngleForReflect(vProjPos, vInterceptPos);
	
	return true;
}

// Same as above but for splash damage - find a surface point near the target
static bool SolveInterceptSplash(C_TFPlayer* pLocal, C_TFPlayer* pTarget, const Vec3& vProjPos,
	float flProjSpeed, float flSplashRadius,
	Vec3& outAimAngles, Vec3& outSplashPoint, float& outTravelTime, std::vector<Vec3>& outPlayerPath)
{
	const Vec3 vEyePos = pLocal->GetShootPos();
	const float flLatency = SDKUtils::GetLatency();
	const int nMaxIterations = 5;
	const float flConvergenceThreshold = 3.0f;
	
	// First, solve for where the target will be
	Vec3 vInterceptPos;
	float flTravelTime;
	if (!SolveIntercept(pLocal, pTarget, vProjPos, flProjSpeed, flSplashRadius, false,
		outAimAngles, vInterceptPos, flTravelTime, outPlayerPath))
		return false;
	
	// Now find a splash point near that predicted position
	Vec3 vSplashPoint;
	if (!FindSplashPoint(pLocal, vEyePos, vInterceptPos, pTarget, flSplashRadius, vSplashPoint))
		return false;
	
	// Iterate to refine - the splash point changes the travel time
	for (int iter = 0; iter < nMaxIterations; iter++)
	{
		float flDist = vProjPos.DistTo(vSplashPoint);
		flTravelTime = flDist / flProjSpeed + flLatency;
		
		// Get player position at that travel time
		int nTick = TIME_TO_TICKS(flTravelTime);
		nTick = std::clamp(nTick, 0, static_cast<int>(outPlayerPath.size()) - 1);
		
		Vec3 vNewTargetPos = outPlayerPath[nTick];
		vNewTargetPos.z += (pTarget->m_vecMaxs().z - pTarget->m_vecMins().z) * 0.5f;
		
		// Find new splash point for this position
		Vec3 vNewSplashPoint;
		if (!FindSplashPoint(pLocal, vEyePos, vNewTargetPos, pTarget, flSplashRadius, vNewSplashPoint))
			break;
		
		float flDelta = vNewSplashPoint.DistTo(vSplashPoint);
		if (flDelta < flConvergenceThreshold)
			break;
		
		vSplashPoint = vNewSplashPoint;
	}
	
	outSplashPoint = vSplashPoint;
	outTravelTime = flTravelTime;
	
	// Splash points are fixed world geometry - aim from eye to splash point
	outAimAngles = CalcAimAngleForSplash(vEyePos, vSplashPoint);
	
	return true;
}

// ============================================================================
// AIMBOT TARGET FINDING - STRAIGHT LINE PROJECTILES (Rockets)
// ============================================================================

bool CAutoAirblast::FindAimbotTarget(C_TFPlayer* pLocal, C_BaseProjectile* pProjectile, const Vec3& vProjectilePos,
	Vec3& outAngles, std::vector<Vec3>& outPlayerPath, std::vector<Vec3>& outRocketPath)
{
	if (!pLocal || !pProjectile)
		return false;

	const Vec3 vLocalPos = pLocal->GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
	const float flMaxFOV = CFG::Aimbot_Projectile_FOV;
	const float flRocketSpeed = GetReflectedProjectileSpeed(pProjectile);
	const float flSplashRadius = GetReflectedSplashRadius(pProjectile) * (CFG::Aimbot_Amalgam_Projectile_SplashRadius / 100.0f);

	std::vector<AirblastAimbotTarget> vTargets;

	// Find enemy players
	if (CFG::Aimbot_Target_Players)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
		{
			if (!pEntity)
				continue;

			auto pPlayer = pEntity->As<C_TFPlayer>();
			if (!pPlayer || pPlayer->deadflag())
				continue;

			if (CFG::Aimbot_Ignore_Friends && pPlayer->IsPlayerOnSteamFriendsList())
				continue;
			if (CFG::Aimbot_Ignore_Invisible && pPlayer->IsInvisible())
				continue;
			if (CFG::Aimbot_Ignore_Invulnerable && pPlayer->IsInvulnerable())
				continue;
			if (CFG::Aimbot_Ignore_Taunting && pPlayer->InCond(TF_COND_TAUNTING))
				continue;
			if (pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
				continue;

			Vec3 vPos = pPlayer->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOV = Math::CalcFov(vLocalAngles, vAngleTo);

			if (flFOV > flMaxFOV)
				continue;

			float flDist = vLocalPos.DistTo(vPos);
			bool bAirborne = !(pPlayer->m_fFlags() & FL_ONGROUND);

			AirblastAimbotTarget target = {};
			target.Entity = pPlayer;
			target.Position = vPos;
			target.FOV = flFOV;
			target.Distance = flDist;
			target.IsAirborne = bAirborne;

			vTargets.push_back(target);
		}
	}

	// Find enemy buildings
	if (CFG::Aimbot_Target_Buildings)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
		{
			if (!pEntity)
				continue;

			auto pBuilding = pEntity->As<C_BaseObject>();
			if (!pBuilding || pBuilding->m_bPlacing() || pBuilding->m_bBuilding())
				continue;

			Vec3 vPos = pBuilding->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOV = Math::CalcFov(vLocalAngles, vAngleTo);

			if (flFOV > flMaxFOV)
				continue;

			float flDist = vLocalPos.DistTo(vPos);

			AirblastAimbotTarget target = {};
			target.Entity = pBuilding;
			target.Position = vPos;
			target.FOV = flFOV;
			target.Distance = flDist;
			target.IsAirborne = false;

			vTargets.push_back(target);
		}
	}

	if (vTargets.empty())
		return false;

	// Sort by FOV or distance
	int iSort = CFG::Aimbot_Projectile_Sort;
	std::sort(vTargets.begin(), vTargets.end(), [iSort](const AirblastAimbotTarget& a, const AirblastAimbotTarget& b) {
		if (iSort == 1)
			return a.Distance < b.Distance;
		return a.FOV < b.FOV;
	});

	// Limit targets
	const size_t nMaxTargets = std::min(static_cast<size_t>(CFG::Aimbot_Projectile_Max_Processing_Targets), vTargets.size());
	vTargets.resize(nMaxTargets);

	// Process each target with intercept calculation
	// PRIORITY ORDER: 1) Feet  2) Splash points  3) Direct hit anywhere
	for (auto& target : vTargets)
	{
		std::vector<Vec3> vPlayerPath;
		Vec3 vAimAngles;
		Vec3 vInterceptPos;
		float flTravelTime = 0.0f;

		// For players, use intercept solving
		if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
		{
			auto pPlayer = target.Entity->As<C_TFPlayer>();
			const bool bOnGround = pPlayer->m_fFlags() & FL_ONGROUND;
			const bool bTrulyAirborne = IsTrulyAirborne(pPlayer);

			// ================================================================
			// FOR TRULY AIRBORNE TARGETS: Skip to direct hit
			// Bunnyhopping/just jumped = try splash/feet first
			// Blast jumping/high in air = direct hit only
			// ================================================================
			if (bTrulyAirborne)
			{
				// Direct hit on airborne target
				if (SolveIntercept(pLocal, pPlayer, vProjectilePos, flRocketSpeed, flSplashRadius, false,
					vAimAngles, vInterceptPos, flTravelTime, vPlayerPath))
				{
					// Check if there's a clear path to the target
					if (!HasClearPath(vProjectilePos, vInterceptPos, pPlayer))
						continue; // Obstacle in the way, try next target

					target.UseSplash = false;
					target.PredictedPosition = vInterceptPos;
					target.AimAngles = vAimAngles;
					target.TravelTime = flTravelTime;

					// Build projectile path for visualization
					Vec3 vAimEndPoint = GetAimEndPoint(pLocal, vAimAngles);
					bool bHitsWall = false;
					BuildProjectilePath(vProjectilePos, vAimEndPoint, flRocketSpeed, 0.0f, 0, outRocketPath, bHitsWall);

					outAngles = vAimAngles;
					outPlayerPath = vPlayerPath;
					return true;
				}
				continue; // Try next target if this one failed
			}

			// ================================================================
			// PRIORITY 1: SPLASH POINTS (nearby surfaces)
			// Find a surface near the enemy to splash damage
			// ================================================================
			if (flSplashRadius > 0.0f && CFG::Aimbot_Amalgam_Projectile_Splash > 0)
			{
				Vec3 vSplashPoint;
				if (SolveInterceptSplash(pLocal, pPlayer, vProjectilePos, flRocketSpeed, flSplashRadius,
					vAimAngles, vSplashPoint, flTravelTime, vPlayerPath))
				{
					target.UseSplash = true;
					target.SplashPoint = vSplashPoint;
					target.AimAngles = vAimAngles;
					target.TravelTime = flTravelTime;

					// Build projectile path for visualization
					Vec3 vAimEndPoint = GetAimEndPoint(pLocal, vAimAngles);
					bool bHitsWall = false;
					BuildProjectilePath(vProjectilePos, vAimEndPoint, flRocketSpeed, 0.0f, 0, outRocketPath, bHitsWall);

					outAngles = vAimAngles;
					outPlayerPath = vPlayerPath;
					return true;
				}
			}

			// ================================================================
			// PRIORITY 2: AIM AT FEET (ground under predicted position)
			// Try to hit the ground directly at their feet
			// Only for grounded or bunnyhopping players
			// ================================================================
			if (flSplashRadius > 0.0f)
			{
				// Get predicted position first
				if (SolveIntercept(pLocal, pPlayer, vProjectilePos, flRocketSpeed, flSplashRadius, false,
					vAimAngles, vInterceptPos, flTravelTime, vPlayerPath))
				{
					// Calculate feet position (ground level at predicted position)
					Vec3 vFeetPos = vInterceptPos;
					vFeetPos.z = vPlayerPath.empty() ? pPlayer->GetAbsOrigin().z : vPlayerPath.back().z;
					
					// Trace down to find ground
					CTraceFilterHitscan filterGround = {};
					filterGround.m_pIgnore = pPlayer;
					CGameTrace traceGround = {};
					H::AimUtils->Trace(vFeetPos + Vec3(0, 0, 10), vFeetPos - Vec3(0, 0, 50), MASK_SOLID, &filterGround, &traceGround);
					
					if (traceGround.DidHit() && !(traceGround.surface.flags & SURF_SKY))
					{
						Vec3 vGroundPoint = traceGround.endpos;
						
						// Ground is fixed geometry - aim from eye to ground point
						Vec3 vFeetAimAngles = CalcAimAngleForSplash(vLocalPos, vGroundPoint);
						Vec3 vAimEndPoint = GetAimEndPoint(pLocal, vFeetAimAngles);
						
						bool bHitsWall = false;
						std::vector<Vec3> vTestPath;
						BuildProjectilePath(vProjectilePos, vAimEndPoint, flRocketSpeed, 0.0f, 0, vTestPath, bHitsWall);
						
						// Check if projectile reaches close to the ground point
						if (!vTestPath.empty())
						{
							float flDistToFeet = vTestPath.back().DistTo(vGroundPoint);
							if (flDistToFeet < 50.0f)  // Within 50 units of feet
							{
								target.UseSplash = true;
								target.SplashPoint = vGroundPoint;
								target.AimAngles = vFeetAimAngles;
								target.TravelTime = flTravelTime;
								
								outAngles = vFeetAimAngles;
								outPlayerPath = vPlayerPath;
								outRocketPath = vTestPath;
								return true;
							}
						}
					}
				}
			}

			// ================================================================
			// PRIORITY 3: DIRECT HIT (body center)
			// Last resort - try to hit them directly
			// ================================================================
			if (SolveIntercept(pLocal, pPlayer, vProjectilePos, flRocketSpeed, flSplashRadius, false,
				vAimAngles, vInterceptPos, flTravelTime, vPlayerPath))
			{
				// Check if there's a clear path to the target
				if (!HasClearPath(vProjectilePos, vInterceptPos, pPlayer))
					continue; // Obstacle in the way, try next target

				target.UseSplash = false;
				target.PredictedPosition = vInterceptPos;
				target.AimAngles = vAimAngles;
				target.TravelTime = flTravelTime;

				// Build projectile path for visualization
				Vec3 vAimEndPoint = GetAimEndPoint(pLocal, vAimAngles);
				bool bHitsWall = false;
				BuildProjectilePath(vProjectilePos, vAimEndPoint, flRocketSpeed, 0.0f, 0, outRocketPath, bHitsWall);

				outAngles = vAimAngles;
				outPlayerPath = vPlayerPath;
				return true;
			}

			// Absolute last resort - just aim at current position (only if clear path)
			if (HasClearPath(vProjectilePos, target.Position, pPlayer))
			{
				target.UseSplash = false;
				target.PredictedPosition = target.Position;
				target.AimAngles = CalcAimAngleForReflect(vProjectilePos, target.Position);
				
				Vec3 vAimEndPoint = GetAimEndPoint(pLocal, target.AimAngles);
				bool bHitsWall = false;
				BuildProjectilePath(vProjectilePos, vAimEndPoint, flRocketSpeed, 0.0f, 0, outRocketPath, bHitsWall);

				outAngles = target.AimAngles;
				return true;
			}
		}
		else
		{
			// Buildings don't move - they're fixed like splash points, aim from eye
			// Check if there's a clear path to the building
			if (!HasClearPath(vProjectilePos, target.Position, target.Entity))
				continue; // Obstacle in the way, try next target

			target.PredictedPosition = target.Position;
			target.TravelTime = vProjectilePos.DistTo(target.Position) / flRocketSpeed;
			target.AimAngles = CalcAimAngleForSplash(vLocalPos, target.Position);

			Vec3 vAimEndPoint = GetAimEndPoint(pLocal, target.AimAngles);
			bool bHitsWall = false;
			BuildProjectilePath(vProjectilePos, vAimEndPoint, flRocketSpeed, 0.0f, 0, outRocketPath, bHitsWall);

			outAngles = target.AimAngles;
			return true;
		}
	}

	return false;
}


// ============================================================================
// AIMBOT TARGET FINDING - ARC PROJECTILES (Arrows, Pipes, Flares)
// Uses iterative intercept solving with arc physics
// ============================================================================

// Solve intercept for arc projectiles - accounts for gravity
// The aim angle is calculated from projectile position (reflected projectile starts there)
static bool SolveInterceptArc(C_TFPlayer* pLocal, C_TFPlayer* pTarget, const Vec3& vProjPos,
	float flProjSpeed, float flGravity, int nProjType,
	Vec3& outAimAngles, Vec3& outInterceptPos, float& outTravelTime, std::vector<Vec3>& outPlayerPath)
{
	const float flLatency = SDKUtils::GetLatency();
	const bool bDucked = pTarget->m_fFlags() & FL_DUCKING;
	const int nMaxIterations = 6;
	const float flConvergenceThreshold = 3.0f;
	
	// Initialize movement simulation
	outPlayerPath.clear();
	if (!F::MovementSimulation->Initialize(pTarget))
	{
		// Fallback
		Vec3 vVel = pTarget->m_vecVelocity();
		Vec3 vDelta = pTarget->GetCenter() - vProjPos;
		float flDist2D = vDelta.Length2D();
		outTravelTime = flDist2D / flProjSpeed + flLatency;
		outInterceptPos = pTarget->GetAbsOrigin() + vVel * outTravelTime;
		outInterceptPos.z += (pTarget->m_vecMaxs().z - pTarget->m_vecMins().z) * 0.5f;
		
		// Calculate arc angle from projectile position
		Vec3 vDeltaFinal = outInterceptPos - vProjPos;
		float flYaw = atan2f(vDeltaFinal.y, vDeltaFinal.x);
		outAimAngles.x = 0.0f;
		outAimAngles.y = RAD2DEG(flYaw);
		outAimAngles.z = 0.0f;
		return true;
	}
	
	// First pass: estimate travel time
	Vec3 vInitialTarget = pTarget->GetCenter();
	float flInitialDist = (vInitialTarget - vProjPos).Length2D();
	float flEstimatedTime = flInitialDist / flProjSpeed + flLatency;
	
	// Only simulate as long as needed
	int nSimTicks = TIME_TO_TICKS(flEstimatedTime * 1.5f + 0.3f);
	nSimTicks = std::clamp(nSimTicks, 10, TIME_TO_TICKS(2.0f));
	
	outPlayerPath.push_back(pTarget->GetAbsOrigin());
	for (int t = 0; t < nSimTicks; t++)
	{
		F::MovementSimulation->RunTick();
		outPlayerPath.push_back(F::MovementSimulation->GetOrigin());
	}
	F::MovementSimulation->Restore();
	
	// Start with current position
	Vec3 vInterceptPos = pTarget->GetCenter();
	float flTravelTime = 0.0f;
	float flLastDelta = FLT_MAX;
	
	for (int iter = 0; iter < nMaxIterations; iter++)
	{
		// Calculate arc physics to get travel time (from projectile position)
		Vec3 vDelta = vInterceptPos - vProjPos;
		float flDist2D = vDelta.Length2D();
		float flHeight = vDelta.z;
		float flGrav = 800.0f * flGravity;
		
		if (flDist2D < 1.0f)
			break;
		
		// Check if target is reachable with arc
		float flV2 = flProjSpeed * flProjSpeed;
		float flV4 = flV2 * flV2;
		float flDiscriminant = flV4 - flGrav * (flGrav * flDist2D * flDist2D + 2.0f * flHeight * flV2);
		
		if (flDiscriminant < 0.0f)
		{
			// Can't reach - use simple estimate
			flTravelTime = flDist2D / flProjSpeed + flLatency;
		}
		else
		{
			// Calculate actual arc travel time
			float flPitch = atanf((flV2 - sqrtf(flDiscriminant)) / (flGrav * flDist2D));
			float flVelHoriz = flProjSpeed * cosf(flPitch);
			flTravelTime = flDist2D / flVelHoriz + flLatency;
		}
		
		// Get player position at that time
		int nTick = TIME_TO_TICKS(flTravelTime);
		nTick = std::clamp(nTick, 0, static_cast<int>(outPlayerPath.size()) - 1);
		
		Vec3 vNewInterceptPos = outPlayerPath[nTick];
		
		// Add appropriate height offset based on projectile type
		bool bIsArrow = (nProjType == TF_PROJECTILE_ARROW);
		if (bIsArrow)
		{
			// Head aim for arrows
			const float flMaxZ = (bDucked ? 62.0f : 82.0f) * pTarget->m_flModelScale();
			vNewInterceptPos.z += flMaxZ * 0.92f;
		}
		else
		{
			// Body aim for pipes, flares
			vNewInterceptPos.z += (pTarget->m_vecMaxs().z - pTarget->m_vecMins().z) * 0.5f;
		}
		
		float flDeltaPos = vNewInterceptPos.DistTo(vInterceptPos);
		if (flDeltaPos < flConvergenceThreshold || flDeltaPos >= flLastDelta)
			break;
		
		flLastDelta = flDeltaPos;
		vInterceptPos = vNewInterceptPos;
	}
	
	outInterceptPos = vInterceptPos;
	outTravelTime = flTravelTime;
	
	// Calculate arc aim angle from projectile position
	Vec3 vDelta = vInterceptPos - vProjPos;
	float flDist2D = vDelta.Length2D();
	float flHeight = vDelta.z;
	float flGrav = 800.0f * flGravity;
	
	if (flDist2D < 1.0f)
	{
		outAimAngles = CalcAimAngleForReflect(vProjPos, vInterceptPos);
		return true;
	}
	
	float flV2 = flProjSpeed * flProjSpeed;
	float flV4 = flV2 * flV2;
	float flDiscriminant = flV4 - flGrav * (flGrav * flDist2D * flDist2D + 2.0f * flHeight * flV2);
	
	if (flDiscriminant < 0.0f)
		return false;
	
	float flPitch = atanf((flV2 - sqrtf(flDiscriminant)) / (flGrav * flDist2D));
	float flYaw = atan2f(vDelta.y, vDelta.x);
	
	outAimAngles.x = -RAD2DEG(flPitch);
	outAimAngles.y = RAD2DEG(flYaw);
	outAimAngles.z = 0.0f;
	
	return true;
}

bool CAutoAirblast::FindAimbotTargetArc(C_TFPlayer* pLocal, C_BaseProjectile* pProjectile, const Vec3& vProjectilePos,
	Vec3& outAngles, std::vector<Vec3>& outPlayerPath, std::vector<Vec3>& outProjectilePath)
{
	if (!pLocal || !pProjectile)
		return false;

	const Vec3 vLocalPos = pLocal->GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
	const float flMaxFOV = CFG::Aimbot_Projectile_FOV;
	const float flProjSpeed = GetReflectedProjectileSpeed(pProjectile);

	// Get gravity multiplier based on projectile type
	float flGravity = 0.0f;
	int nProjType = 0;
	switch (pProjectile->GetClassId())
	{
	case ETFClassIds::CTFProjectile_Arrow:
	case ETFClassIds::CTFProjectile_HealingBolt:
		flGravity = ARROW_GRAVITY;
		nProjType = TF_PROJECTILE_ARROW;
		break;
	case ETFClassIds::CTFGrenadePipebombProjectile:
		flGravity = PIPE_GRAVITY;
		nProjType = TF_PROJECTILE_PIPEBOMB;
		break;
	case ETFClassIds::CTFProjectile_Flare:
		flGravity = FLARE_GRAVITY;
		nProjType = TF_PROJECTILE_FLARE;
		break;
	default:
		flGravity = 0.3f;
		nProjType = 0;
		break;
	}

	std::vector<AirblastAimbotTarget> vTargets;

	// Find enemy players
	if (CFG::Aimbot_Target_Players)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
		{
			if (!pEntity)
				continue;

			auto pPlayer = pEntity->As<C_TFPlayer>();
			if (!pPlayer || pPlayer->deadflag())
				continue;

			if (CFG::Aimbot_Ignore_Friends && pPlayer->IsPlayerOnSteamFriendsList())
				continue;
			if (CFG::Aimbot_Ignore_Invisible && pPlayer->IsInvisible())
				continue;
			if (CFG::Aimbot_Ignore_Invulnerable && pPlayer->IsInvulnerable())
				continue;
			if (CFG::Aimbot_Ignore_Taunting && pPlayer->InCond(TF_COND_TAUNTING))
				continue;
			if (pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
				continue;

			Vec3 vPos = pPlayer->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOV = Math::CalcFov(vLocalAngles, vAngleTo);

			if (flFOV > flMaxFOV)
				continue;

			float flDist = vLocalPos.DistTo(vPos);
			bool bAirborne = !(pPlayer->m_fFlags() & FL_ONGROUND);

			AirblastAimbotTarget target = {};
			target.Entity = pPlayer;
			target.Position = vPos;
			target.FOV = flFOV;
			target.Distance = flDist;
			target.IsAirborne = bAirborne;

			vTargets.push_back(target);
		}
	}

	if (vTargets.empty())
		return false;

	// Sort by FOV or distance
	int iSort = CFG::Aimbot_Projectile_Sort;
	std::sort(vTargets.begin(), vTargets.end(), [iSort](const AirblastAimbotTarget& a, const AirblastAimbotTarget& b) {
		if (iSort == 1)
			return a.Distance < b.Distance;
		return a.FOV < b.FOV;
	});

	// Process best target with intercept calculation
	for (auto& target : vTargets)
	{
		if (target.Entity->GetClassId() != ETFClassIds::CTFPlayer)
			continue;

		auto pPlayer = target.Entity->As<C_TFPlayer>();
		
		Vec3 vAimAngles;
		Vec3 vInterceptPos;
		float flTravelTime;
		std::vector<Vec3> vPlayerPath;
		
		if (!SolveInterceptArc(pLocal, pPlayer, vProjectilePos, flProjSpeed, flGravity, nProjType,
			vAimAngles, vInterceptPos, flTravelTime, vPlayerPath))
			continue;
		
		// Check if there's a clear path to the target (for arc projectiles, check line of sight)
		if (!HasClearPath(vProjectilePos, vInterceptPos, pPlayer))
			continue; // Obstacle in the way, try next target

		target.PredictedPosition = vInterceptPos;
		target.AimAngles = vAimAngles;
		target.TravelTime = flTravelTime;

		// Simulate projectile path for visualization
		bool bHitsWall = false;
		SimulateReflectedProjectilePath(vProjectilePos, vAimAngles, flProjSpeed, flGravity, nProjType, outProjectilePath, bHitsWall);
		
		// If projectile would hit wall before target, try adjusting
		if (bHitsWall && !outProjectilePath.empty())
		{
			float flWallDist = outProjectilePath.back().DistTo(vProjectilePos);
			float flTargetDist = vInterceptPos.DistTo(vProjectilePos);
			
			if (flWallDist < flTargetDist * 0.9f)
			{
				// Try small angle adjustments
				bool bFoundBetter = false;
				const float flOffsets[] = { 2.0f, 4.0f, 6.0f };
				const float flYawOffsets[] = { 0.0f, 90.0f, 180.0f, 270.0f };
				
				for (float flPitchOff : flOffsets)
				{
					if (bFoundBetter) break;
					for (float flYawOff : flYawOffsets)
					{
						Vec3 vTestAngles = vAimAngles;
						vTestAngles.x -= flPitchOff; // Aim higher
						Math::ClampAngles(vTestAngles);
						
						std::vector<Vec3> vTestPath;
						bool bTestHits = false;
						SimulateReflectedProjectilePath(vProjectilePos, vTestAngles, flProjSpeed, flGravity, nProjType, vTestPath, bTestHits);
						
						if (!bTestHits || (vTestPath.size() > 0 && vTestPath.back().DistTo(vProjectilePos) >= flTargetDist * 0.85f))
						{
							vAimAngles = vTestAngles;
							outProjectilePath = vTestPath;
							bFoundBetter = true;
							break;
						}
					}
				}
			}
		}

		outAngles = vAimAngles;
		outPlayerPath = vPlayerPath;
		return true;
	}

	return false;
}


// ============================================================================
// FIND TARGET PROJECTILE (Original logic)
// ============================================================================

static bool FindTargetProjectile(C_TFPlayer* local, TargetProjectile& outTarget)
{
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
	{
		if (!pEntity)
			continue;

		const auto pProjectile = pEntity->As<C_BaseProjectile>();
		if (!pProjectile)
			continue;

		if (CFG::Triggerbot_AutoAirblast_Ignore_Rocket && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_Rocket)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_SentryRocket && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_SentryRocket)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_Jar && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_Jar)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_JarGas && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_JarGas)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_JarMilk && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_JarMilk)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_Arrow && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_Arrow)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_Flare && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_Flare)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_Cleaver && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_Cleaver)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_HealingBolt && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_HealingBolt)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_PipebombProjectile && pProjectile->GetClassId() == ETFClassIds::CTFGrenadePipebombProjectile)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_BallOfFire && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_BallOfFire)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_EnergyRing && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_EnergyRing)
			continue;
		if (CFG::Triggerbot_AutoAirblast_Ignore_EnergyBall && pProjectile->GetClassId() == ETFClassIds::CTFProjectile_EnergyBall)
			continue;

		Vec3 vel{};
		pProjectile->EstimateAbsVelocity(vel);

		if (pProjectile->GetClassId() == ETFClassIds::CTFGrenadePipebombProjectile
			&& (pProjectile->As<C_TFGrenadePipebombProjectile>()->m_bTouched()
				|| pProjectile->As<C_TFGrenadePipebombProjectile>()->m_iType() == TF_PROJECTILE_PIPEBOMB_PRACTICE))
			continue;

		if (pProjectile->GetClassId() == ETFClassIds::CTFProjectile_Arrow && fabsf(vel.Length()) <= 10.0f)
			continue;

		auto pos = pProjectile->m_vecOrigin() + (vel * SDKUtils::GetLatency());

		if (pos.DistTo(local->GetShootPos()) > 160.0f)
			continue;

		if (CFG::Triggerbot_AutoAirblast_Mode == 0
			&& Math::CalcFov(I::EngineClient->GetViewAngles(), Math::CalcAngle(local->GetShootPos(), pos)) > 60.0f)
			continue;

		CTraceFilterWorldCustom filter{};
		trace_t trace{};

		H::AimUtils->Trace(local->GetShootPos(), pos, MASK_SOLID, &filter, &trace);

		if (trace.fraction < 1.0f || trace.allsolid || trace.startsolid)
			continue;

		outTarget.Projectile = pProjectile;
		outTarget.Position = pos;

		return true;
	}

	return false;
}


// ============================================================================
// VISUALIZATION - Draw prediction paths
// ============================================================================

static void DrawAirblastVisualization(C_TFPlayer* pLocal, const Vec3& vAimAngles, 
	const std::vector<Vec3>& vPlayerPath, const std::vector<Vec3>& vProjectilePath)
{
	if (!pLocal)
		return;

	constexpr float flDrawDuration = 2.0f; // 2 seconds so paths stay visible

	// Draw movement prediction path
	if (CFG::Visuals_Draw_Movement_Path_Style > 0 && !vPlayerPath.empty())
	{
		const auto& col = CFG::Color_Simulation_Movement;
		const int r = col.r, g = col.g, b = col.b;

		if (CFG::Visuals_Draw_Movement_Path_Style == 1) // Line
		{
			for (size_t i = 1; i < vPlayerPath.size(); i++)
				I::DebugOverlay->AddLineOverlay(vPlayerPath[i - 1], vPlayerPath[i], r, g, b, false, flDrawDuration);
		}
		else if (CFG::Visuals_Draw_Movement_Path_Style == 2) // Dashed
		{
			for (size_t i = 1; i < vPlayerPath.size(); i++)
			{
				if (i % 2 == 0)
					continue;
				I::DebugOverlay->AddLineOverlay(vPlayerPath[i - 1], vPlayerPath[i], r, g, b, false, flDrawDuration);
			}
		}
		else if (CFG::Visuals_Draw_Movement_Path_Style == 3) // With markers
		{
			for (size_t i = 1; i < vPlayerPath.size(); i++)
			{
				I::DebugOverlay->AddLineOverlay(vPlayerPath[i - 1], vPlayerPath[i], r, g, b, false, flDrawDuration);
				if (i % 5 == 0)
				{
					I::DebugOverlay->AddBoxOverlay(vPlayerPath[i], Vec3(-2, -2, -2), Vec3(2, 2, 2), 
						Vec3(0, 0, 0), r, g, b, 100, flDrawDuration);
				}
			}
		}

		// Draw box at predicted position
		if (!vPlayerPath.empty())
		{
			I::DebugOverlay->AddBoxOverlay(vPlayerPath.back(), Vec3(-8, -8, -8), Vec3(8, 8, 8),
				Vec3(0, 0, 0), r, g, b, 100, flDrawDuration);
		}
	}

	// Draw projectile trajectory
	if (CFG::Visuals_Draw_Predicted_Path_Style > 0 && !vProjectilePath.empty())
	{
		const auto& col = CFG::Color_Simulation_Projectile;
		const int r = col.r, g = col.g, b = col.b;

		if (CFG::Visuals_Draw_Predicted_Path_Style == 1) // Line
		{
			for (size_t i = 1; i < vProjectilePath.size(); i++)
				I::DebugOverlay->AddLineOverlay(vProjectilePath[i - 1], vProjectilePath[i], r, g, b, false, flDrawDuration);
		}
		else if (CFG::Visuals_Draw_Predicted_Path_Style == 2) // Dashed
		{
			for (size_t i = 1; i < vProjectilePath.size(); i++)
			{
				if (i % 2 == 0)
					continue;
				I::DebugOverlay->AddLineOverlay(vProjectilePath[i - 1], vProjectilePath[i], r, g, b, false, flDrawDuration);
			}
		}
		else if (CFG::Visuals_Draw_Predicted_Path_Style == 3) // With markers
		{
			for (size_t i = 1; i < vProjectilePath.size(); i++)
			{
				I::DebugOverlay->AddLineOverlay(vProjectilePath[i - 1], vProjectilePath[i], r, g, b, false, flDrawDuration);
			}
		}

		// Draw box at impact point
		if (!vProjectilePath.empty())
		{
			I::DebugOverlay->AddBoxOverlay(vProjectilePath.back(), Vec3(-4, -4, -4), Vec3(4, 4, 4),
				Vec3(0, 0, 0), r, g, b, 100, flDrawDuration);
		}
	}
}


// ============================================================================
// MAIN RUN FUNCTION
// ============================================================================

void CAutoAirblast::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!CFG::Triggerbot_AutoAirblast_Active)
		return;

	if (!G::bCanSecondaryAttack || (pWeapon->GetWeaponID() != TF_WEAPON_FLAMETHROWER && pWeapon->GetWeaponID() != TF_WEAPON_FLAME_BALL)
		|| pWeapon->m_iItemDefinitionIndex() == Pyro_m_ThePhlogistinator)
		return;

	TargetProjectile targetProjectile{};
	if (!FindTargetProjectile(pLocal, targetProjectile))
		return;

	pCmd->buttons |= IN_ATTACK2;

	// Rage airblast mode
	if (CFG::Triggerbot_AutoAirblast_Mode == 1)
	{
		// ================================================================
		// AIMBOT SUPPORT - Use projectile aimbot to aim at enemies
		// ================================================================
		if (CFG::Triggerbot_AutoAirblast_Aimbot_Support && SupportsAimbot(targetProjectile.Projectile))
		{
			Vec3 vAimAngles;
			std::vector<Vec3> vPlayerPath;
			std::vector<Vec3> vProjectilePath;
			bool bFoundTarget = false;

			EReflectProjectileType projType = GetProjectileType(targetProjectile.Projectile);

			if (projType == EReflectProjectileType::Rocket)
			{
				// Straight line projectiles (rockets, sentry rockets, energy balls)
				// Pass the projectile's current position so we can simulate the reflected path correctly
				bFoundTarget = FindAimbotTarget(pLocal, targetProjectile.Projectile, targetProjectile.Position, 
					vAimAngles, vPlayerPath, vProjectilePath);
			}
			else if (projType == EReflectProjectileType::Arc)
			{
				// Arc projectiles (arrows, pipes, flares)
				bFoundTarget = FindAimbotTargetArc(pLocal, targetProjectile.Projectile, targetProjectile.Position,
					vAimAngles, vPlayerPath, vProjectilePath);
			}

			if (bFoundTarget)
			{
				Math::ClampAngles(vAimAngles);
				pCmd->viewangles = vAimAngles;

				// Draw visualization
				DrawAirblastVisualization(pLocal, vAimAngles, vPlayerPath, vProjectilePath);

				// Silent aim
				if (CFG::Triggerbot_AutoAirblast_Aim_Mode == 1)
					G::bSilentAngles = true;

				return;
			}
		}

		// No target found - just aim at the projectile
		pCmd->viewangles = Math::CalcAngle(pLocal->GetShootPos(), targetProjectile.Position);

		// Silent
		if (CFG::Triggerbot_AutoAirblast_Aim_Mode == 1)
			G::bSilentAngles = true;
	}
}
