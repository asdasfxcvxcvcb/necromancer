#include "ProjectileDodge.h"
#include "../CFG.h"
#include "../MovementSimulation/MovementSimulation.h"

bool CProjectileDodge::IsProjectileThreat(C_BaseEntity* pProjectile, C_TFPlayer* pLocal, Vec3& vImpactPos, float& flTimeToImpact)
{
	if (!pProjectile || !pLocal)
		return false;

	// Cast to projectile
	auto pProj = pProjectile->As<C_BaseProjectile>();
	if (!pProj)
		return false;

	// Get projectile velocity
	Vec3 vProjVel{};
	pProj->EstimateAbsVelocity(vProjVel);
	if (vProjVel.IsZero())
		return false;

	// Compensate for latency like AutoAirblast does
	Vec3 vProjPos = pProj->m_vecOrigin() + (vProjVel * SDKUtils::GetLatency());
	Vec3 vLocalPos = pLocal->m_vecOrigin();

	// Check if projectile is close enough to be a threat (within 800 units)
	float flDistToProj = vProjPos.DistTo(vLocalPos);
	if (flDistToProj > 800.0f)
		return false; // Too far away

	// Check if projectile is moving towards us
	Vec3 vToLocal = vLocalPos - vProjPos;
	float flDot = vToLocal.Dot(vProjVel);
	if (flDot <= 0.0f)
		return false; // Moving away

	// Simulate projectile path
	return SimulateProjectile(pProjectile, vImpactPos, flTimeToImpact);
}

bool CProjectileDodge::SimulateProjectile(C_BaseEntity* pProjectile, Vec3& vImpactPos, float& flTimeToImpact)
{
	auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return false;

	auto pProj = pProjectile->As<C_BaseProjectile>();
	if (!pProj)
		return false;

	Vec3 vProjVel{};
	pProj->EstimateAbsVelocity(vProjVel);
	
	// Compensate for latency
	Vec3 vProjPos = pProj->m_vecOrigin() + (vProjVel * SDKUtils::GetLatency());
	
	// Get projectile gravity modifier
	float flGravity = 800.0f; // Default TF2 gravity
	float flGravityMod = 0.0f;
	
	// Check projectile type for gravity based on class ID
	const auto nClassID = pProj->GetClassId();
	
	switch (nClassID)
	{
	case ETFClassIds::CTFProjectile_Rocket:
	case ETFClassIds::CTFProjectile_SentryRocket:
	case ETFClassIds::CTFProjectile_EnergyBall:
		flGravityMod = 0.0f; // No gravity
		break;
	case ETFClassIds::CTFGrenadePipebombProjectile:
		flGravityMod = 0.4f; // Grenade gravity
		break;
	case ETFClassIds::CTFProjectile_Arrow:
	case ETFClassIds::CTFProjectile_HealingBolt:
		flGravityMod = 0.5f; // Arrow gravity
		break;
	default:
		flGravityMod = 0.0f;
		break;
	}

	// Simulate up to 1.5 seconds
	const float flMaxTime = 1.5f;
	const float flTimeStep = 0.015f; // One tick
	
	Vec3 vSimPos = vProjPos;
	Vec3 vSimVel = vProjVel;
	
	// Threat detection radius
	const float flThreatRadius = 150.0f; // Detect threats within 150 units

	for (float flTime = 0.0f; flTime < flMaxTime; flTime += flTimeStep)
	{
		// Apply gravity
		if (flGravityMod > 0.0f)
			vSimVel.z -= flGravity * flGravityMod * flTimeStep;
		
		// Update position
		vSimPos += vSimVel * flTimeStep;
		
		// Check if projectile will hit player (distance-based)
		Vec3 vLocalPos = pLocal->m_vecOrigin();
		float flDist = vSimPos.DistTo(vLocalPos);
		
		if (flDist <= flThreatRadius)
		{
			vImpactPos = vSimPos;
			flTimeToImpact = flTime;
			return true; // Threat detected!
		}
		
		// Check if projectile hit ground/wall
		trace_t trace{};
		CTraceFilterWorldCustom filter{};
		Vec3 vTraceEnd = vSimPos + vSimVel * flTimeStep;
		H::AimUtils->Trace(vSimPos, vTraceEnd, MASK_SOLID, &filter, &trace);
		
		if (trace.DidHit())
		{
			// Projectile will hit something before reaching us
			return false;
		}
	}

	return false; // No threat
}

DodgeDirection CProjectileDodge::GetBestDodgeDirection(C_TFPlayer* pLocal, const Vec3& vThreatDir, const Vec3& vImpactPos)
{
	Vec3 vLocalPos = pLocal->m_vecOrigin();
	Vec3 vViewAngles = I::EngineClient->GetViewAngles();
	
	// Get perpendicular directions (left/right relative to threat)
	Vec3 vThreatDir2D = vThreatDir;
	vThreatDir2D.z = 0.0f;
	vThreatDir2D.Normalize();
	
	// Calculate 4 directions: left, right, forward, back (relative to threat)
	Vec3 vRight = Vec3(-vThreatDir2D.y, vThreatDir2D.x, 0.0f);
	Vec3 vLeft = vRight * -1.0f;
	Vec3 vBack = vThreatDir2D * -1.0f; // Away from threat
	Vec3 vForward = vThreatDir2D; // Towards threat (risky but might work)
	
	DodgeDirection directions[4];
	directions[0].vDirection = vLeft;
	directions[1].vDirection = vRight;
	directions[2].vDirection = vBack;
	directions[3].vDirection = vForward;
	
	// Test each direction
	const float flTestDistance = 150.0f; // Distance to check
	
	for (int i = 0; i < 4; i++)
	{
		Vec3 vTestPos = vLocalPos + directions[i].vDirection * flTestDistance;
		
		// Trace to check for walls
		trace_t trace{};
		CTraceFilterWorldCustom filter{};
		H::AimUtils->Trace(vLocalPos, vTestPos, MASK_SOLID, &filter, &trace);
		
		if (trace.DidHit())
		{
			directions[i].bHasWall = true;
			directions[i].flSafety = 0.0f; // Wall blocks this direction
		}
		else
		{
			// Calculate safety score
			Vec3 vDodgePos = vTestPos;
			Vec3 vToImpact = vImpactPos - vDodgePos;
			float flDistToImpact = vToImpact.Length();
			
			// Higher distance = safer
			directions[i].flSafety = flDistToImpact;
			
			// Bonus for moving away from threat
			if (i == 2) // Back direction
				directions[i].flSafety *= 1.5f;
			
			// Penalty for moving towards threat
			if (i == 3) // Forward direction
				directions[i].flSafety *= 0.5f;
		}
	}
	
	// Find best direction
	DodgeDirection best = directions[0];
	for (int i = 1; i < 4; i++)
	{
		if (directions[i].flSafety > best.flSafety)
			best = directions[i];
	}
	
	return best;
}

bool CProjectileDodge::CanWarpInDirection(C_TFPlayer* pLocal, const Vec3& vDirection, float flDistance)
{
	Vec3 vStart = pLocal->m_vecOrigin();
	Vec3 vEnd = vStart + vDirection * flDistance;
	
	// Check if path is clear
	trace_t trace{};
	CTraceFilterWorldCustom filter{};
	H::AimUtils->Trace(vStart, vEnd, MASK_SOLID, &filter, &trace);
	
	return !trace.DidHit();
}

void CProjectileDodge::ExecuteWarp(CUserCmd* pCmd, C_TFPlayer* pLocal, const Vec3& vDirection)
{
	// Convert direction to movement input
	Vec3 vViewAngles = pCmd->viewangles;
	
	// Get forward and right vectors from view angles
	Vec3 vForward, vRight;
	Math::AngleVectors(vViewAngles, &vForward, &vRight, nullptr);
	
	vForward.z = 0.0f;
	vRight.z = 0.0f;
	vForward.Normalize();
	vRight.Normalize();
	
	// Project dodge direction onto forward/right
	float flForward = vDirection.Dot(vForward);
	float flRight = vDirection.Dot(vRight);
	
	// Store original movement
	float flOrigForward = pCmd->forwardmove;
	float flOrigSide = pCmd->sidemove;
	
	// Blend 70% dodge + 30% original movement
	pCmd->forwardmove = (flForward * 450.0f * 0.7f) + (flOrigForward * 0.3f);
	pCmd->sidemove = (flRight * 450.0f * 0.7f) + (flOrigSide * 0.3f);
	
	// Clamp to max speed
	if (pCmd->forwardmove > 450.0f) pCmd->forwardmove = 450.0f;
	if (pCmd->forwardmove < -450.0f) pCmd->forwardmove = -450.0f;
	if (pCmd->sidemove > 450.0f) pCmd->sidemove = 450.0f;
	if (pCmd->sidemove < -450.0f) pCmd->sidemove = -450.0f;
}

void CProjectileDodge::ExecuteStrafe(CUserCmd* pCmd, C_TFPlayer* pLocal, const Vec3& vDirection)
{
	// Convert direction to movement input (same as warp but without rapid fire)
	Vec3 vViewAngles = pCmd->viewangles;
	
	Vec3 vForward, vRight;
	Math::AngleVectors(vViewAngles, &vForward, &vRight, nullptr);
	
	vForward.z = 0.0f;
	vRight.z = 0.0f;
	vForward.Normalize();
	vRight.Normalize();
	
	float flForward = vDirection.Dot(vForward);
	float flRight = vDirection.Dot(vRight);
	
	// Store original movement
	float flOrigForward = pCmd->forwardmove;
	float flOrigSide = pCmd->sidemove;
	
	// Blend 70% dodge + 30% original movement
	pCmd->forwardmove = (flForward * 450.0f * 0.7f) + (flOrigForward * 0.3f);
	pCmd->sidemove = (flRight * 450.0f * 0.7f) + (flOrigSide * 0.3f);
	
	// Clamp to max speed
	if (pCmd->forwardmove > 450.0f) pCmd->forwardmove = 450.0f;
	if (pCmd->forwardmove < -450.0f) pCmd->forwardmove = -450.0f;
	if (pCmd->sidemove > 450.0f) pCmd->sidemove = 450.0f;
	if (pCmd->sidemove < -450.0f) pCmd->sidemove = -450.0f;
}

void CProjectileDodge::Run(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!CFG::Misc_Projectile_Dodge_Enabled)
	{
		bWantWarp = false; // Reset flag when disabled
		return;
	}

	if (!pLocal || pLocal->deadflag())
	{
		bWantWarp = false; // Reset flag when dead
		return;
	}

	// Reset warp flag if we're recharging or already shifting
	if (Shifting::bRecharging || Shifting::bShifting || Shifting::bShiftingWarp)
	{
		bWantWarp = false;
		return;
	}

	// If Only Warp is enabled, do nothing unless warp can be used
	if (CFG::Misc_Projectile_Dodge_Only_Warp)
	{
		if (Shifting::nAvailableTicks <= 0 || !CFG::Misc_Projectile_Dodge_Use_Warp)
		{
			bWantWarp = false;
			return;
		}
	}

	// Do not dodge when invulnerable (Uber)
	if (pLocal->IsInvulnerable())
	{
		bWantWarp = false;
		return;
	}

	// Do not dodge when protected by Vaccinator projectile (blast) resist
	// Vaccinator projectile resist: TF_COND_MEDIGUN_UBER_BLAST_RESIST
	if (pLocal->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST))
	{
		bWantWarp = false;
		return;
	}

	// Only dodge when airborne (commented out for testing - you can enable this later)
	// if (pLocal->m_fFlags() & FL_ONGROUND)
	// 	return;
	// Check for incoming projectiles
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
	{
		if (!pEntity)
			continue;

		auto pProj = pEntity->As<C_BaseProjectile>();
		if (!pProj)
			continue;

		// Skip stickies (they don't move in a straight line and are usually traps)
		const auto nClassID = pProj->GetClassId();
		if (nClassID == ETFClassIds::CTFGrenadePipebombProjectile)
			continue;

		Vec3 vImpactPos{};
		float flTimeToImpact = 0.0f;

		if (IsProjectileThreat(pEntity, pLocal, vImpactPos, flTimeToImpact))
		{
			// Threat detected! Calculate dodge direction
			Vec3 vThreatDir{};
			pProj->EstimateAbsVelocity(vThreatDir);
			vThreatDir.Normalize();

			DodgeDirection dodge = GetBestDodgeDirection(pLocal, vThreatDir, vImpactPos);

			if (dodge.flSafety > 0.0f)
			{
				// Check if we have DT ticks available for warp
				if (Shifting::nAvailableTicks > 0 && 
					CFG::Misc_Projectile_Dodge_Use_Warp)
				{
					// Set warp flag for CL_Move hook to handle
					bWantWarp = true;
					vWarpDirection = dodge.vDirection;
					ExecuteWarp(pCmd, pLocal, dodge.vDirection);
					return;
				}

				// Fallback: Strafe dodge unless Only Warp is enabled
				if (CFG::Misc_Projectile_Dodge_Only_Warp)
				{
					bWantWarp = false;
					return;
				}
				else
				{
					ExecuteStrafe(pCmd, pLocal, dodge.vDirection);
					return;
				}
			}
		}
	}
}
