#include "Misc.h"

#include "../CFG.h"
#include "AntiCheatCompat/AntiCheatCompat.h"
#include "../Aimbot/AimbotMelee/AimbotMelee.h"

void CMisc::Bunnyhop(CUserCmd* pCmd)
{
	if (!CFG::Misc_Bunnyhop)
		return;

	if (const auto pLocal = H::Entities->GetLocal())
	{
		if (pLocal->deadflag() || pLocal->m_nWaterLevel() > static_cast<byte>(WL_Feet))
			return;

		static bool bJumpState = false;
		static bool bLastGrounded = false;
		static int iBhopCount = 0;

		const bool bGrounded = (pLocal->m_fFlags() & FL_ONGROUND) != 0;
		const bool bJumping = (pCmd->buttons & IN_JUMP) != 0;

		if (pCmd->buttons & IN_JUMP)
		{
			if (!bJumpState && !(pLocal->m_fFlags() & FL_ONGROUND))
				pCmd->buttons &= ~IN_JUMP;

			else if (bJumpState)
				bJumpState = false;
		}

		else if (!bJumpState)
		{
			bJumpState = true;
		}

		// Anti-cheat compatibility: limit consecutive bhops to 9
		if (CFG::Misc_AntiCheat_Enabled && F::AntiCheatCompat->ShouldLimitBhop(iBhopCount, bGrounded, bLastGrounded, bJumping))
		{
			pCmd->buttons &= ~IN_JUMP;
		}

		bLastGrounded = bGrounded;
	}
}

void CMisc::CrouchWhileAirborne(CUserCmd* pCmd)
{
	if (!CFG::Misc_Crouch_While_Airborne)
		return;

	if (const auto pLocal = H::Entities->GetLocal())
	{
		if (pLocal->deadflag())
			return;

		// Don't crouch while projectile aimbot is actively aiming
		// This prevents messing up the shoot position calculation
		// G::nTargetIndex is set by aimbot when it has a valid target
		if (G::nTargetIndex > 0)
		{
			// Check if we're using a projectile weapon
			if (const auto pWeapon = H::Entities->GetWeapon())
			{
				switch (pWeapon->GetWeaponID())
				{
				case TF_WEAPON_ROCKETLAUNCHER:
				case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
				case TF_WEAPON_PARTICLE_CANNON:
				case TF_WEAPON_GRENADELAUNCHER:
				case TF_WEAPON_PIPEBOMBLAUNCHER:
				case TF_WEAPON_CANNON:
				case TF_WEAPON_FLAREGUN:
				case TF_WEAPON_FLAREGUN_REVENGE:
				case TF_WEAPON_COMPOUND_BOW:
				case TF_WEAPON_CROSSBOW:
				case TF_WEAPON_SYRINGEGUN_MEDIC:
				case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
					// Projectile weapon with active target - don't crouch
					return;
				}
			}
		}

		// Use the same crouch logic as melee aimbot
		F::AimbotMelee->CrouchWhileAirborne(pCmd, pLocal);
	}
}

void CMisc::AutoStrafer(CUserCmd* pCmd)
{
	//credits: KGB

	if (!CFG::Misc_Auto_Strafe)
		return;

	if (const auto pLocal = H::Entities->GetLocal())
	{
		if (pLocal->deadflag() || (pLocal->m_fFlags() & FL_ONGROUND))
			return;

		if (pLocal->m_nWaterLevel() > static_cast<byte>(WL_Feet) || pLocal->GetMoveType() != MOVETYPE_WALK)
			return;

		if (!(pLocal->m_afButtonLast() & IN_JUMP) && (pCmd->buttons & IN_JUMP))
			return;

		// Wall avoidance - ALWAYS check when airborne, even without movement keys
		if (CFG::Misc_Auto_Strafe_Avoid_Walls)
			{
				Vec3 vLocalPos = pLocal->m_vecOrigin();
				
				CTraceFilterWorldCustom filter{};
				
				// Cast rays in a circle to find closest wall
				const int numRays = 8;
				const float flDetectRadius = 80.0f; // Increased detection range
				
				Vec3 vWallNormal = Vec3(0, 0, 0);
				float flClosestDist = 999999.0f;
				bool bWallDetected = false;
				
				for (int i = 0; i < numRays; i++)
				{
					float flAngle = (360.0f / numRays) * i;
					float flRad = DEG2RAD(flAngle);
					
					Vec3 vDir;
					vDir.x = cosf(flRad);
					vDir.y = sinf(flRad);
					vDir.z = 0.0f;
					
					// Start trace from player center
					Vec3 vStart = vLocalPos;
					vStart.z += 32.0f; // Eye level
					
					// End trace at detection radius
					Vec3 vEnd = vStart + (vDir * flDetectRadius);
					
					trace_t trace{};
					H::AimUtils->Trace(vStart, vEnd, MASK_SOLID, &filter, &trace);
					
					if (trace.DidHit())
					{
						float flDist = trace.fraction * flDetectRadius;
						
						// Only consider walls that are actually in front of us (not inside player)
						if (flDist > 5.0f && flDist < flClosestDist)
						{
							flClosestDist = flDist;
							// Use wall's surface normal - it points AWAY from wall
							vWallNormal = trace.plane.normal;
							vWallNormal.z = 0.0f;
							vWallNormal.Normalize();
							bWallDetected = true;
						}
					}
				}
				
			// Simple approach: only avoid if wall is close AND we're moving towards it
			if (bWallDetected && flClosestDist < 60.0f)
			{
				// Get current velocity
				Vec3 vVelocity = pLocal->m_vecVelocity();
				vVelocity.z = 0.0f;
				float flSpeed = vVelocity.Length2D();
				
				// Only apply avoidance if we have velocity
				if (flSpeed > 50.0f)
				{
					// Normalize velocity
					vVelocity.Normalize();
					
					// Check if we're moving towards the wall
					// Dot product: negative = moving towards wall, positive = moving away
					float flDotVelNormal = vVelocity.Dot(vWallNormal);
					
					// ONLY apply avoidance if actively moving TOWARDS the wall
					if (flDotVelNormal < -0.1f)
					{
						// Wall normal already points away from wall surface
						Vec3 vAvoidDir = vWallNormal;
						
						// Project onto view angles
						Vec3 vViewForward, vViewRight;
						Math::AngleVectors(pCmd->viewangles, &vViewForward, &vViewRight, nullptr);
						vViewForward.z = 0.0f;
						vViewRight.z = 0.0f;
						vViewForward.Normalize();
						vViewRight.Normalize();
						
						float flAvoidForward = vAvoidDir.Dot(vViewForward);
						float flAvoidRight = vAvoidDir.Dot(vViewRight);
						
						// Apply strong avoidance
						pCmd->forwardmove = flAvoidForward * 450.0f;
						pCmd->sidemove = flAvoidRight * 450.0f;
						
						// Skip normal autostrafe when avoiding walls
						return;
					}
				}
			}
		}
		
		// Normal autostrafe - only if pressing movement keys
		if (pCmd->buttons & IN_MOVELEFT || pCmd->buttons & IN_MOVERIGHT || pCmd->buttons & IN_FORWARD || pCmd->buttons & IN_BACK)
		{
			const float flForwardMove = pCmd->forwardmove;
			const float flSideMove = pCmd->sidemove;

			Vec3 vForward = {}, vRight = {};
			Math::AngleVectors(pCmd->viewangles, &vForward, &vRight, nullptr);

			vForward.z = vRight.z = 0.0f;

			vForward.Normalize();
			vRight.Normalize();

			Vec3 vWishDir = {};
			Math::VectorAngles({(vForward.x * flForwardMove) + (vRight.x * flSideMove), (vForward.y * flForwardMove) + (vRight.y * flSideMove), 0.0f}, vWishDir);

			Vec3 vCurDir = {};
			Math::VectorAngles(pLocal->m_vecVelocity(), vCurDir);

			float flDirDelta = Math::NormalizeAngle(vWishDir.y - vCurDir.y);
			
			// Check max delta - don't strafe if direction change is too large
			if (fabsf(flDirDelta) > CFG::Misc_Auto_Strafe_Max_Delta)
				return;
			
			const float flTurnScale = Math::RemapValClamped(CFG::Misc_Auto_Strafe_Turn_Scale, 0.0f, 1.0f, 0.9f, 1.0f);
			const float flRotation = DEG2RAD((flDirDelta > 0.0f ? -90.0f : 90.f) + (flDirDelta * flTurnScale));

			const float flCosRot = cosf(flRotation);
			const float flSinRot = sinf(flRotation);

			pCmd->forwardmove = (flCosRot * flForwardMove) - (flSinRot * flSideMove);
			pCmd->sidemove = (flSinRot * flForwardMove) + (flCosRot * flSideMove);
		}
	}
}

void CMisc::NoiseMakerSpam()
{
	if (!CFG::Misc_NoiseMaker_Spam)
		return;

	if (const auto pLocal = H::Entities->GetLocal())
	{
		if (pLocal->deadflag() || pLocal->m_flNextNoiseMakerTime() >= I::GlobalVars->curtime)
			return;

		const auto kv = new KeyValues("use_action_slot_item_server");
		I::EngineClient->ServerCmdKeyValues(kv);
	}
}

void CMisc::FastStop(CUserCmd* pCmd)
{
	if (!CFG::Misc_Fast_Stop)
		return;

	if (const auto pLocal = H::Entities->GetLocal())
	{
		if (!pLocal->deadflag()
			&& pLocal->GetMoveType() == MOVETYPE_WALK
			&& (pLocal->m_fFlags() & FL_ONGROUND)
			&& pLocal->m_vecVelocity().Length2D() >= 10.0f
			&& !(pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT)))
		{
			const Vector velocity = pLocal->m_vecVelocity();
			QAngle direction;

			Math::VectorAngles(velocity, direction);

			const float speed = velocity.Length();

			direction.y = I::EngineClient->GetViewAngles().y - direction.y;

			Vec3 forward{};

			Math::AngleVectors(direction, &forward);

			const Vector negatedDirection = forward * -speed;

			pCmd->forwardmove = negatedDirection.x;
			pCmd->sidemove = negatedDirection.y;
		}
	}
}

void CMisc::FastAccelerate(CUserCmd* pCmd)
{
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag() || pLocal->GetMoveType() != MOVETYPE_WALK)
		return;

	if (!(pLocal->m_fFlags() & FL_ONGROUND))
		return;

	const bool bIsDucking = (pLocal->m_fFlags() & FL_DUCKING) != 0;
	
	// Check if the appropriate feature is enabled based on duck state
	if (bIsDucking ? !CFG::Misc_Duck_Speed : !CFG::Misc_Fast_Accelerate)
		return;

	// Skip if anti-cheat compatibility is enabled
	if (CFG::Misc_AntiCheat_Enabled)
		return;

	// Skip on attack, doubletap, speedhack, recharge, anti-aim, or every other tick
	if (G::Attacking == 1 || I::GlobalVars->tickcount % 2)
		return;

	// Only apply when pressing movement keys
	if (!(pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))
		return;

	const float flSpeed = pLocal->m_vecVelocity().Length2D();
	const float flMaxSpeed = std::min(pLocal->m_flMaxspeed() * 0.9f, 520.0f) - 10.0f;

	// Only apply when below max speed (accelerating)
	if (flSpeed >= flMaxSpeed)
		return;

	Vec3 vMove = { pCmd->forwardmove, pCmd->sidemove, 0.0f };
	Vec3 vAngMoveReverse;
	Math::VectorAngles(vMove * -1.0f, vAngMoveReverse);
	
	pCmd->forwardmove = -vMove.Length();
	pCmd->sidemove = 0.0f;
	pCmd->viewangles.y = fmodf(pCmd->viewangles.y - vAngMoveReverse.y, 360.0f);
	pCmd->viewangles.z = 270.0f;
	G::bPSilentAngles = true;
}

bool CMisc::SetRocketJumpAngles(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_vRJAngles = pCmd->viewangles;
	
	Vec3 vLocalPos = pLocal->GetShootPos();
	Vec3 vOrigin = pLocal->m_vecOrigin();
	
	// Weapon offset from player center (critical for accurate angle calculation)
	Vec3 vWeaponOffset = { 23.5f, 12.0f, -3.0f };
	
	Vec3 vPoint;
	
	// Determine effective RJ mode (0=High, 1=Forward, 2=Dynamic chooses based on pitch)
	int nRJMode = CFG::Misc_Auto_Rocket_Jump_Mode;
	if (nRJMode == 2)
	{
		const float flPitch = pCmd->viewangles.x;
		// angle high = -13.1 to -89, angle forward = -13 to 89
		nRJMode = (flPitch <= -13.1f) ? 0 : 1;
	}

	if (nRJMode == 0)
	{
		// === HIGH JUMP: Quadratic angle correction with forward bias ===
		// Use slider value (0-100): Lower = more forward, Higher = more vertical
		float flBias = CFG::Misc_Auto_Rocket_Jump_High_Forward_Bias / 100.0f; // Convert 0-100 to 0.0-1.0
		float flOffset = sqrtf(2 * powf(vWeaponOffset.y, 2.f) + powf(vWeaponOffset.z, 2.f)) * flBias;
		
		if (pLocal->m_fFlags() & FL_ONGROUND)
		{
			// Ground: Calculate point behind player
			Vec3 vWishVel = { pCmd->forwardmove, pCmd->sidemove, 0.f };
			Vec3 vDir;
			
			if (vWishVel.IsZero())
			{
				vDir = { 0.f, m_vRJAngles.y, 0.f };
			}
			else
			{
				Vec3 vWishAng = Math::VelocityToAngles(vWishVel);
				vDir = { 0.f, m_vRJAngles.y - vWishAng.y, 0.f };
			}
			
			Vec3 vForward;
			Math::AngleVectors(vDir, &vForward);
			vPoint = pLocal->m_vecOrigin() - vForward * flOffset;
			
			// Ground check for high jump (same logic as forward mode)
			trace_t groundTrace{};
			CTraceFilterWorldCustom filter{};
			
			// Trace down from the point behind us to find ground
			Vec3 vTraceStart = vPoint;
			vTraceStart.z = pLocal->m_vecOrigin().z + 100.0f;
			Vec3 vTraceEnd = vTraceStart;
			vTraceEnd.z -= 400.0f;
			
			H::AimUtils->Trace(vTraceStart, vTraceEnd, MASK_SOLID, &filter, &groundTrace);
			
			// Validate ground exists
			if (!groundTrace.DidHit() || (groundTrace.surface.flags & SURF_SKY))
				return false;
			
			// Check if ground normal is upward-facing (not a wall)
			if (groundTrace.plane.normal.z < 0.7f)
				return false;
			
			// Validate ground is close to player (within close-up splash range)
			// For reliable rocket jump, ground must be very close
			Vec3 vGroundToPlayer = pLocal->m_vecOrigin() - groundTrace.endpos;
			vGroundToPlayer.z = 0.0f;
			float flHorizontalDist = vGroundToPlayer.Length();
			
			// Also check vertical distance - ground shouldn't be too far below
			float flVerticalDist = fabsf(pLocal->m_vecOrigin().z - groundTrace.endpos.z);
			
			// Strict distance check: max 50 units horizontal, 100 units vertical
			if (flHorizontalDist > 50.0f || flVerticalDist > 100.0f)
				return false;
		}
		else
		{
			// Airborne
			Vec3 vVelocity = pLocal->m_vecVelocity();
			if (vVelocity.Length2D() < 10.0f)
				return false;
			
			Vec3 vForward = vVelocity;
			vForward.z = 0.0f;
			float flLength = sqrtf(vForward.x * vForward.x + vForward.y * vForward.y);
			if (flLength > 0.0f)
			{
				vForward.x /= flLength;
				vForward.y /= flLength;
			}
			
			vPoint = pLocal->m_vecOrigin() - vForward * flOffset;
		}
		
		// Step 1: Basic trajectory calculation
		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPoint);
		float flPitch = vAngleTo.x;
		float flYaw = vAngleTo.y;
		
		// Step 2: Apply weapon offset correction (only for pitch, keep yaw straight back)
		// The weapon spawns offset from player center, we need to account for this
		
		// Get projectile spawn position (approximation)
		Vec3 vForward, vRight, vUp;
		Math::AngleVectors({ flPitch, flYaw, 0.0f }, &vForward, &vRight, &vUp);
		Vec3 vProjSpawn = vLocalPos + (vForward * vWeaponOffset.x) + (vRight * vWeaponOffset.y) + (vUp * vWeaponOffset.z);
		
		// Skip yaw correction - keep shooting straight back
		
		// Pitch correction: Account for vertical offset
		// Rotate everything to align with corrected yaw
		float flCosYaw = cosf(DEG2RAD(-flYaw));
		float flSinYaw = sinf(DEG2RAD(-flYaw));
		
		Vec3 vShootPosRotated;
		vShootPosRotated.x = (vProjSpawn.x - vLocalPos.x) * flCosYaw - (vProjSpawn.y - vLocalPos.y) * flSinYaw;
		vShootPosRotated.y = 0.0f;
		vShootPosRotated.z = vProjSpawn.z - vLocalPos.z;
		
		Vec3 vTargetRotated;
		vTargetRotated.x = (vPoint.x - vLocalPos.x) * flCosYaw - (vPoint.y - vLocalPos.y) * flSinYaw;
		vTargetRotated.y = 0.0f;
		vTargetRotated.z = vPoint.z - vLocalPos.z;
		
		// Recalculate forward vector with corrected yaw
		Math::AngleVectors({ flPitch, flYaw, 0.0f }, &vForward, nullptr, nullptr);
		Vec3 vForwardXZ = { vForward.x, 0.0f, vForward.z };
		vForwardXZ.Normalize();
		
		float flA = 1.0f;
		float flB = 2.0f * (vShootPosRotated.x * vForwardXZ.x + vShootPosRotated.z * vForwardXZ.z);
		float flC = (vShootPosRotated.x * vShootPosRotated.x + vShootPosRotated.z * vShootPosRotated.z) - 
		      (vTargetRotated.x * vTargetRotated.x + vTargetRotated.z * vTargetRotated.z);
		float flDiscriminant = flB * flB - 4.0f * flA * flC;
		
		if (flDiscriminant >= 0.0f)
		{
			float flSolution = (-flB + sqrtf(flDiscriminant)) / (2.0f * flA);
			vShootPosRotated += vForwardXZ * flSolution;
			flPitch = RAD2DEG(atan2f(-vShootPosRotated.z, vShootPosRotated.x));
		}
		
		m_vRJAngles.x = flPitch;
		m_vRJAngles.y = flYaw;
		// Use roll to center the rocket spawn (offset Y=12 becomes negligible)
		// 90° roll rotates the weapon offset so rocket spawns from center instead of right side
		// Skip for The Original since it already fires from center (has centerfire_projectile attribute)
		if (pWeapon->m_iItemDefinitionIndex() != Soldier_m_TheOriginal)
			m_vRJAngles.z = 90.0f;
	}
	else
	{
		// === FORWARD STYLE: Shoot behind for forward momentum ===
		float flOffset = 70.0f;
		
		if (pLocal->m_fFlags() & FL_ONGROUND)
		{
			Vec3 vWishVel = { pCmd->forwardmove, pCmd->sidemove, 0.f };
			Vec3 vDir;
			
			if (vWishVel.IsZero())
			{
				vDir = { 0.f, m_vRJAngles.y, 0.f };
			}
			else
			{
				Vec3 vWishAng = Math::VelocityToAngles(vWishVel);
				vDir = { 0.f, m_vRJAngles.y - vWishAng.y, 0.f };
			}
			
			Vec3 vForward;
			Math::AngleVectors(vDir, &vForward);
			
			// CRITICAL: Ensure we're shooting BEHIND the player
			// vForward should point in the direction we're moving/facing
			// We want to shoot opposite to that direction (behind us)
			vPoint = pLocal->m_vecOrigin() - vForward * flOffset;
			
			// CRITICAL: Check if there's ground behind us that the rocket will hit
			trace_t groundTrace{};
			CTraceFilterWorldCustom filter{};
			
			// Trace down from the point behind us to find ground
			// Start from HIGH above the point to catch uphill slopes
			Vec3 vTraceStart = vPoint;
			vTraceStart.z = pLocal->m_vecOrigin().z + 100.0f; // Start 100 units above player
			Vec3 vTraceEnd = vTraceStart;
			vTraceEnd.z -= 400.0f; // Trace down 400 units total (catches steep hills)
			
			H::AimUtils->Trace(vTraceStart, vTraceEnd, MASK_SOLID, &filter, &groundTrace);
			
			// Validate ground exists and is close enough
			if (!groundTrace.DidHit() || (groundTrace.surface.flags & SURF_SKY))
				return false; // No ground found
			
			// Check if ground normal is upward-facing (not a wall)
			if (groundTrace.plane.normal.z < 0.7f)
				return false; // Too steep or it's a wall
			
			// Update target point to actual ground position
			vPoint.z = groundTrace.endpos.z + 5.0f; // Aim slightly above ground
			
			// Validate ground is close to player (within close-up splash range)
			// For reliable rocket jump, ground must be very close
			Vec3 vGroundToPlayer = pLocal->m_vecOrigin() - groundTrace.endpos;
			vGroundToPlayer.z = 0.0f;
			float flHorizontalDist = vGroundToPlayer.Length();
			
			// Also check vertical distance - ground shouldn't be too far below
			float flVerticalDist = fabsf(pLocal->m_vecOrigin().z - groundTrace.endpos.z);
			
			// Strict distance check: max 80 units horizontal (offset + margin), 100 units vertical
			if (flHorizontalDist > 80.0f || flVerticalDist > 100.0f)
				return false; // Ground too far away
		}
		else
		{
			Vec3 vVelocity = pLocal->m_vecVelocity();
			if (vVelocity.Length2D() < 10.0f)
				return false;
			
			Vec3 vForward = vVelocity;
			vForward.z = 0.0f;
			float flLength = sqrtf(vForward.x * vForward.x + vForward.y * vForward.y);
			if (flLength > 0.0f)
			{
				vForward.x /= flLength;
				vForward.y /= flLength;
			}
			
			vPoint = pLocal->m_vecOrigin() - vForward * flOffset;
		}
		
		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPoint);
		m_vRJAngles.x = vAngleTo.x;
		m_vRJAngles.y = vAngleTo.y;
		// Use roll to center the rocket spawn (offset Y=12 becomes negligible)
		// 90° roll rotates the weapon offset so rocket spawns from center instead of right side
		// Skip for The Original since it already fires from center (has centerfire_projectile attribute)
		if (pWeapon->m_iItemDefinitionIndex() != Soldier_m_TheOriginal)
			m_vRJAngles.z = 90.0f;
	}
	
	return true;
}

void CMisc::AutoRocketJump(CUserCmd* cmd)
{
	const auto pLocal = H::Entities->GetLocal();
	const auto pWeapon = H::Entities->GetWeapon();
	
	// Reset state if conditions not met
	if (!H::Input->IsDown(CFG::Misc_Auto_Rocket_Jump_Key)
		|| I::EngineVGui->IsGameUIVisible()
		|| I::MatSystemSurface->IsCursorVisible()
		|| SDKUtils::BInEndOfMatch()
		|| !pLocal
		|| pLocal->deadflag())
	{
		m_iRJFrame = -1;
		m_bRJCancelingReload = false;
		m_bRJDisableFakeLag = false;
		return;
	}
	
	// Check if valid weapon
	bool bValidWeapon = false;
	if (pWeapon)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
			bValidWeapon = true;
			break;
		}
	}
	
	if (!bValidWeapon || pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
	{
		m_iRJFrame = -1;
		m_bRJCancelingReload = false;
		m_bRJDisableFakeLag = false;
		return;
	}
	
	// Track ground state
	static bool bStaticGrounded = false;
	bool bLastGrounded = bStaticGrounded;
	bool bCurrGrounded = bStaticGrounded = (pLocal->m_fFlags() & FL_ONGROUND);
	
	// Cancel reload if user wants to rocket jump
	if (pWeapon->m_iReloadMode() != 0)
	{
		cmd->buttons |= IN_ATTACK; // Cancel reload
		m_bRJCancelingReload = true; // Mark that we're canceling reload
		m_bRJDisableFakeLag = true; // Mark RJ as running so AntiCheatCompat skips this tick
		return; // Wait one tick for reload to cancel
	}
	
	// If we just canceled reload, continue with RJ automatically
	if (m_bRJCancelingReload)
	{
		m_bRJCancelingReload = false; // Reset flag
		// Skip duck check and continue to RJ logic
	}
	else
	{
		// Check duck state for frame initialization (only if not coming from reload cancel)
		if (m_iRJFrame == -1)
		{
			bool bDuck = (pLocal->m_fFlags() & FL_DUCKING);
			if ((bCurrGrounded ? bDuck : !bDuck))
				return;
		}
	}
	
	// Initialize rocket jump sequence
	if (m_iRJFrame == -1 && G::bCanPrimaryAttack && SetRocketJumpAngles(pLocal, pWeapon, cmd))
	{
		// Only allow rocket jump when grounded or just left ground
		// Don't allow rocket jump when fully airborne (been in air for multiple ticks)
		if (!bCurrGrounded && !bLastGrounded)
		{
			// Fully airborne - don't start rocket jump
			return;
		}
		
		// Disable fakelag for rocket jump
		m_bRJDisableFakeLag = true;
		m_iRJDelay = 2; // Standard C-tap timing
		
		if (bCurrGrounded && bCurrGrounded == bLastGrounded)
		{
			// Start C-tap sequence when on ground
			m_iRJFrame = 0;
			m_bRJFull = true;
		}
		else if (!bCurrGrounded && bLastGrounded)
		{
			// Airborne speedshot - ONLY if we just left the ground
			if (pWeapon->m_iItemDefinitionIndex() != Soldier_m_TheBeggarsBazooka)
				cmd->buttons |= IN_ATTACK;
			
			if (G::bCanPrimaryAttack)
			{
				G::bSilentAngles = true;
				cmd->viewangles = m_vRJAngles;
			}
		}
		
		// Handle reload interrupt
		if (m_iRJFrame != -1 && pWeapon->m_iReloadMode() != 0)
		{
			m_iRJFrame = -1;
			m_bRJFull = false;
			cmd->buttons |= IN_ATTACK;
		}
	}
	
	// Execute C-tap sequence
	if (m_iRJFrame != -1)
	{
		m_iRJFrame++;
		cmd->buttons &= ~IN_JUMP;
		
		// Frame 0: Cancel reload right before shooting
		if (m_iRJFrame == 0)
		{
			if (pWeapon->m_iReloadMode() != 0)
			{
				cmd->buttons |= IN_ATTACK; // Interrupt reload
			}
		}
		
		// Frame 1: Attack and set angles
		if (m_iRJFrame == 1)
		{
			// Cancel reload again if still reloading
			if (pWeapon->m_iReloadMode() != 0)
			{
				cmd->buttons |= IN_ATTACK; // Interrupt reload
			}
			
			if (pWeapon->m_iItemDefinitionIndex() != Soldier_m_TheBeggarsBazooka)
				cmd->buttons |= IN_ATTACK;
			
			if (m_bRJFull)
			{
				G::bSilentAngles = true;
				cmd->viewangles = m_vRJAngles;
			}
		}
		
		// C-tap timing
		if (m_iRJDelay > 1)
		{
			switch (m_iRJFrame - m_iRJDelay + 1)
			{
			case 0:
				cmd->buttons |= IN_DUCK;
				break;
			case 1:
				cmd->buttons |= IN_JUMP;
				break;
			}
		}
		else
		{
			// No time for C-tap - do normal jump
			cmd->buttons |= IN_DUCK | IN_JUMP;
		}
		
		// End sequence
		if (m_iRJFrame == m_iRJDelay + 3)
		{
			m_iRJFrame = -1;
			m_bRJFull = false;
			m_bRJDisableFakeLag = false; // Re-enable fakelag after rocket jump
		}
	}
}

void CMisc::AutoDisguise(CUserCmd* cmd)
{
	if (!CFG::Misc_Auto_Disguise || I::GlobalVars->tickcount % 20 != 0)
	{
		return;
	}

	const auto local{H::Entities->GetLocal()};

	if (!local || local->deadflag() || local->m_iClass() != TF_CLASS_SPY || local->InCond(TF_COND_DISGUISED) || local->InCond(TF_COND_DISGUISING))
	{
		return;
	}

	I::EngineClient->ClientCmd_Unrestricted("lastdisguise");
}

void CMisc::AutoMedigun(CUserCmd* cmd)
{
	// Static cache to reduce trace spam
	static C_TFPlayer* pCachedTarget = nullptr;
	static int nLastUpdateTick = 0;
	
	// Check if Auto Heal is enabled
	if (!CFG::AutoUber_AutoHeal_Active)
	{
		pCachedTarget = nullptr;
		return;
	}
	
	// Check if enabled: Always On OR triggerbot master switch is active
	if (!CFG::AutoUber_Always_On && !H::Input->IsDown(CFG::Triggerbot_Key))
	{
		pCachedTarget = nullptr;
		return;
	}

	if (I::EngineVGui->IsGameUIVisible() || I::MatSystemSurface->IsCursorVisible() || SDKUtils::BInEndOfMatch())
	{
		pCachedTarget = nullptr;
		return;
	}

	const auto local = H::Entities->GetLocal();
	if (!local || local->deadflag() || local->m_iClass() != TF_CLASS_MEDIC)
	{
		pCachedTarget = nullptr;
		return;
	}

	if (local->InCond(TF_COND_TAUNTING) || local->InCond(TF_COND_HALLOWEEN_GHOST_MODE) ||
		local->InCond(TF_COND_HALLOWEEN_BOMB_HEAD) || local->InCond(TF_COND_HALLOWEEN_KART))
	{
		pCachedTarget = nullptr;
		return;
	}

	const auto weapon = H::Entities->GetWeapon();
	if (!weapon || weapon->GetWeaponID() != TF_WEAPON_MEDIGUN)
	{
		pCachedTarget = nullptr;
		return;
	}

	const auto medigun = weapon->As<C_WeaponMedigun>();
	if (!medigun)
	{
		pCachedTarget = nullptr;
		return;
	}

	// Get max overheal multiplier based on medigun type
	float flOverhealMult = 1.5f;
	if (medigun->GetMedigunType() == MEDIGUN_QUICKFIX)
		flOverhealMult = 1.25f;

	// Quick validity check (no trace)
	auto isQuickValid = [&](C_TFPlayer* pl) -> bool
	{
		if (!pl || pl->deadflag() || pl == local)
			return false;
		if (pl->GetCenter().DistTo(local->GetShootPos()) > 449.0f)
			return false;
		if (pl->InCond(TF_COND_STEALTHED) || pl->IsInvulnerable())
			return false;
		return true;
	};

	// Full validity check with trace (expensive)
	auto isFullyValid = [&](C_TFPlayer* pl) -> bool
	{
		if (!isQuickValid(pl))
			return false;
		
		CTraceFilterHitscan filter{};
		trace_t tr{};
		H::AimUtils->Trace(local->GetShootPos(), pl->GetCenter(), (MASK_SHOT & ~CONTENTS_HITBOX), &filter, &tr);
		return tr.fraction > 0.99f || tr.m_pEnt == pl;
	};

	auto getHealthPercent = [&](C_TFPlayer* pl) -> float
	{
		float flMaxOverheal = static_cast<float>(pl->GetMaxHealth()) * flOverhealMult;
		return static_cast<float>(pl->m_iHealth()) / flMaxOverheal;
	};

	auto isFriend = [](C_TFPlayer* pl) -> bool
	{
		return pl->IsPlayerOnSteamFriendsList();
	};

	// Current heal target from medigun
	auto pCurrentTarget = medigun->m_hHealingTarget().Get();
	C_TFPlayer* pCurrentHealPlayer = pCurrentTarget ? pCurrentTarget->As<C_TFPlayer>() : nullptr;

	// Only do full target search every 5 ticks to reduce lag
	const int nCurrentTick = I::GlobalVars->tickcount;
	const bool bShouldUpdateTarget = (nCurrentTick - nLastUpdateTick) >= 5;

	// If we have a cached target, check if still valid
	if (pCachedTarget && isQuickValid(pCachedTarget))
	{
		float flPercent = getHealthPercent(pCachedTarget);
		
		// If cached target is fully healed, force update
		if (flPercent >= 0.99f)
		{
			pCachedTarget = nullptr;
		}
	}
	else
	{
		pCachedTarget = nullptr;
	}

	// Full target search (only every 5 ticks)
	if (bShouldUpdateTarget)
	{
		nLastUpdateTick = nCurrentTick;
		
		C_TFPlayer* pBestTarget = nullptr;
		float flLowestHealth = 1.0f;

		for (const auto ent : H::Entities->GetGroup(EEntGroup::PLAYERS_TEAMMATES))
		{
			if (!ent) continue;
			auto pl = ent->As<C_TFPlayer>();
			
			// Quick check first (no trace)
			if (!isQuickValid(pl)) continue;
			
			float flPercent = getHealthPercent(pl);
			if (flPercent >= 0.99f) continue;
			
			// Skip non-friends if Friends Only is enabled
			if (CFG::AutoUber_AutoHeal_Friends_Only && !isFriend(pl))
				continue;
			
			if (flPercent < flLowestHealth && isFullyValid(pl))
			{
				pBestTarget = pl;
				flLowestHealth = flPercent;
			}
		}
		
		pCachedTarget = pBestTarget;
		
		// Only send attack command on update ticks to reduce server spam
		if (pCachedTarget)
		{
			const auto angle = Math::CalcAngle(local->GetShootPos(), pCachedTarget->GetCenter());
			H::AimUtils->FixMovement(cmd, angle);
			cmd->viewangles = angle;
			cmd->buttons |= IN_ATTACK;
			G::bPSilentAngles = true;
			
			if (CFG::Misc_Accuracy_Improvements)
				cmd->tick_count = TIME_TO_TICKS(pCachedTarget->m_flSimulationTime() + SDKUtils::GetLerp());
		}
	}
	
	// On non-update ticks, don't send any commands - let the medigun continue healing naturally
}

void CMisc::MovementLock(CUserCmd* cmd)
{
	static auto active{false};

	if (!CFG::Misc_Movement_Lock_Key)
	{
		active = false;

		return;
	}

	const auto local{H::Entities->GetLocal()};

	if (!local || (local->m_fFlags() & FL_ONGROUND))
	{
		active = false;

		return;
	}

	const auto& vel{local->m_vecVelocity()};
	static Vec3 lastTickVel{};

	static auto angles{cmd->viewangles};

	if (!active
		&& static_cast<int>(vel.x) == 0 && static_cast<int>(vel.y) == 0 && static_cast<int>(vel.z) == -6
		&& static_cast<int>(lastTickVel.x) == 0 && static_cast<int>(lastTickVel.y) == 0 && static_cast<int>(lastTickVel.z) == -6)
	{
		active = true;

		angles = cmd->viewangles;

		if (cmd->buttons & IN_MOVELEFT)
		{
			angles.y += 45.0f;
		}

		if (cmd->buttons & IN_MOVERIGHT)
		{
			angles.y -= 45.0f;
		}
	}

	lastTickVel = vel;

	if (active && static_cast<int>(vel.x) != 0 && static_cast<int>(vel.y) != 0 && static_cast<int>(vel.z) != -6)
	{
		active = false;
	}

	if (H::Input->IsPressed(CFG::Misc_Movement_Lock_Key) && !I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible())
	{
		active = false;
	}

	if (!active)
	{
		return;
	}

	const auto angle = DEG2RAD((angles.y - cmd->viewangles.y) + 90.0f);

	cmd->forwardmove = sinf(angle) * 450.0f;
	cmd->sidemove = cosf(angle) * 450.0f;
}

void CMisc::MvmInstaRespawn()
{
	if (!H::Input->IsDown(CFG::Misc_MVM_Instant_Respawn_Key)
		|| I::EngineVGui->IsGameUIVisible()
		|| I::MatSystemSurface->IsCursorVisible()
		|| SDKUtils::BInEndOfMatch())
	{
		return;
	}

	auto* kv{new KeyValues("MVM_Revive_Response")};

	kv->SetInt("accepted", 1);

	I::EngineClient->ServerCmdKeyValues(kv);
}

void CMisc::AntiAFK(CUserCmd* pCmd)
{
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	static float flLastMoveTime = static_cast<float>(Plat_FloatTime());

	// Reset timer if player is moving or dead (matches Amalgam's tTimer.Update())
	if ((pCmd->buttons & (IN_MOVELEFT | IN_MOVERIGHT | IN_FORWARD | IN_BACK)) || pLocal->deadflag())
	{
		flLastMoveTime = static_cast<float>(Plat_FloatTime());
	}
	// Press forward every 25 seconds to prevent AFK kick (matches Amalgam's else if)
	else if (CFG::Misc_Anti_AFK)
	{
		const float flCurrentTime = static_cast<float>(Plat_FloatTime());
		if (flCurrentTime - flLastMoveTime >= 25.0f)
		{
			pCmd->buttons |= IN_FORWARD;
			flLastMoveTime = flCurrentTime;
		}
	}
}

void CMisc::AutoUber(CUserCmd* cmd)
{
    if (!CFG::AutoUber_Active)
        return;

    // Check if enabled: Always On OR triggerbot master switch is active
    if (!CFG::AutoUber_Always_On && !H::Input->IsDown(CFG::Triggerbot_Key))
        return;

    if (I::EngineVGui->IsGameUIVisible() || I::MatSystemSurface->IsCursorVisible() || SDKUtils::BInEndOfMatch())
        return;

    const auto pLocal = H::Entities->GetLocal();
    if (!pLocal || pLocal->deadflag() || pLocal->m_iClass() != TF_CLASS_MEDIC)
        return;

    const auto pWeapon = H::Entities->GetWeapon();
    if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_MEDIGUN)
        return;

    const auto medigun = pWeapon->As<C_WeaponMedigun>();
    if (!medigun)
        return;

    // Only stock Uber (invulnerability) medigun - it's the only one that can pop uber
    if (medigun->GetChargeType() != MEDIGUN_CHARGE_INVULN)
        return;

    auto healTargetEnt = medigun->m_hHealingTarget().Get();
    C_TFPlayer* pHealTarget = healTargetEnt ? healTargetEnt->As<C_TFPlayer>() : nullptr;

    const int hpLocal = pLocal->m_iHealth();
    const int hpTarget = pHealTarget ? pHealTarget->m_iHealth() : 0;

    auto canPop = [&]() -> bool {
        return medigun->m_flChargeLevel() >= 1.0f && !medigun->m_bChargeRelease();
    };

    // TF2 damage falloff calculation
    // At 512 units (optimal range): 100% damage
    // At 0 units (point blank): 150% damage  
    // At 1024 units (max range): 50% damage
    auto calcDamageFalloff = [](float flBaseDamage, float flDistance) -> float {
        constexpr float kOptimalRange = 512.0f;
        constexpr float kMaxFalloffRange = 1024.0f;
        
        if (flDistance <= 0.0f)
            return flBaseDamage * 1.5f; // Point blank = 150%
        
        if (flDistance <= kOptimalRange)
        {
            // Ramp up: 150% at 0 -> 100% at 512
            float flRampUp = 1.5f - (0.5f * (flDistance / kOptimalRange));
            return flBaseDamage * flRampUp;
        }
        else if (flDistance <= kMaxFalloffRange)
        {
            // Falloff: 100% at 512 -> 50% at 1024
            float flFalloff = 1.0f - (0.5f * ((flDistance - kOptimalRange) / (kMaxFalloffRange - kOptimalRange)));
            return flBaseDamage * flFalloff;
        }
        else
        {
            // Beyond max range: 50%
            return flBaseDamage * 0.5f;
        }
    };

    // Helper to check if a weapon is hitscan
    auto isHitscanWeapon = [](int weaponID) -> bool {
        switch (weaponID)
        {
        case TF_WEAPON_SHOTGUN_PRIMARY:
        case TF_WEAPON_SHOTGUN_SOLDIER:
        case TF_WEAPON_SHOTGUN_HWG:
        case TF_WEAPON_SHOTGUN_PYRO:
        case TF_WEAPON_SCATTERGUN:
        case TF_WEAPON_SNIPERRIFLE:
        case TF_WEAPON_MINIGUN:
        case TF_WEAPON_SMG:
        case TF_WEAPON_PISTOL:
        case TF_WEAPON_PISTOL_SCOUT:
        case TF_WEAPON_REVOLVER:
        case TF_WEAPON_SENTRY_REVENGE:
        case TF_WEAPON_HANDGUN_SCOUT_PRIMARY:
        case TF_WEAPON_HANDGUN_SCOUT_SECONDARY:
        case TF_WEAPON_SODA_POPPER:
        case TF_WEAPON_SNIPERRIFLE_DECAP:
        case TF_WEAPON_PEP_BRAWLER_BLASTER:
        case TF_WEAPON_CHARGED_SMG:
        case TF_WEAPON_SNIPERRIFLE_CLASSIC:
        case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
            return true;
        default:
            return false;
        }
    };

    // Get base damage for hitscan weapons
    auto getHitscanBaseDamage = [](int weaponID) -> float {
        switch (weaponID)
        {
        case TF_WEAPON_SCATTERGUN:
        case TF_WEAPON_SODA_POPPER:
        case TF_WEAPON_PEP_BRAWLER_BLASTER:
            return 60.0f; // 6 pellets * 10 damage (assuming most hit)
        case TF_WEAPON_SHOTGUN_PRIMARY:
        case TF_WEAPON_SHOTGUN_SOLDIER:
        case TF_WEAPON_SHOTGUN_HWG:
        case TF_WEAPON_SHOTGUN_PYRO:
        case TF_WEAPON_SENTRY_REVENGE:
        case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
            return 60.0f; // 10 pellets * 6 damage
        case TF_WEAPON_MINIGUN:
            return 50.0f; // ~5 bullets in quick succession
        case TF_WEAPON_PISTOL:
        case TF_WEAPON_PISTOL_SCOUT:
            return 15.0f;
        case TF_WEAPON_SMG:
        case TF_WEAPON_CHARGED_SMG:
            return 12.0f;
        case TF_WEAPON_REVOLVER:
            return 40.0f;
        case TF_WEAPON_HANDGUN_SCOUT_PRIMARY: // Shortstop
            return 48.0f; // 4 pellets * 12
        case TF_WEAPON_HANDGUN_SCOUT_SECONDARY: // Winger/Pretty Boy's
            return 15.0f;
        default:
            return 40.0f;
        }
    };

    // Check if a projectile is actually heading towards the player and will hit soon
    // Returns estimated damage if lethal threat, 0 otherwise
    // Now includes proper TF2 damage falloff calculation
    auto getLethalProjectileThreat = [&](C_TFPlayer* who) -> float {
        if (!who) return 0.0f;

        float flTotalLethalDamage = 0.0f;
        
        // Splash radii
        constexpr float kRocketSplashRadius = 146.0f;
        constexpr float kStickySplashRadius = 146.0f; // Stickies have same splash as rockets
        constexpr float kBaseRocketDamage = 90.0f;
        constexpr float kBasePipeDamage = 100.0f;
        constexpr float kBaseStickyDamage = 117.0f; // Stickies do 100 damage, 300 crit
        
        // Only react to projectiles that will hit within this time (in seconds)
        // Increased to 0.25s to catch faster projectiles and give more reaction time
        constexpr float kMaxTimeToImpact = 0.25f;

        for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
        {
            if (!pEntity)
                continue;

            const auto nClassID = pEntity->GetClassId();
            
            const bool bIsRocket = (nClassID == ETFClassIds::CTFProjectile_Rocket || 
                                    nClassID == ETFClassIds::CTFProjectile_SentryRocket ||
                                    nClassID == ETFClassIds::CTFProjectile_EnergyBall);
            const bool bIsPipebomb = (nClassID == ETFClassIds::CTFGrenadePipebombProjectile);
            
            if (!bIsRocket && !bIsPipebomb)
                continue;
            
            // Check if it's a sticky (has sticky effects) vs regular pipe
            bool bIsSticky = false;
            if (bIsPipebomb)
            {
                if (const auto pipe = pEntity->As<C_TFGrenadePipebombProjectile>())
                    bIsSticky = pipe->HasStickyEffects();
            }

            Vec3 vel{};
            pEntity->EstimateAbsVelocity(vel);
            const float flSpeed = vel.Length();
            
            // Skip stationary projectiles (stickies on ground) - handled separately
            if (flSpeed < 100.0f)
                continue;

            const Vec3 projPos = pEntity->m_vecOrigin();
            const Vec3 playerPos = who->GetCenter();
            const float flCurrentDist = projPos.DistTo(playerPos);
            
            // Calculate if projectile is heading towards player
            Vec3 toPlayer = playerPos - projPos;
            toPlayer.Normalize();
            
            Vec3 projDir = vel;
            projDir.Normalize();
            
            // Dot product: 1.0 = heading directly at player, 0 = perpendicular, -1 = away
            float flDotToPlayer = projDir.Dot(toPlayer);
            
            // Only consider projectiles heading towards us (dot > 0.5 means within ~60 degree cone)
            if (flDotToPlayer < 0.5f)
                continue;
            
            // Calculate time to impact (approximate - assumes straight line)
            float flTimeToImpact = flCurrentDist / flSpeed;
            
            // Only react to imminent threats
            if (flTimeToImpact > kMaxTimeToImpact)
                continue;
            
            // Visibility check
            const auto visibleFromCenter = H::AimUtils->TraceEntityAutoDet(pEntity, playerPos, pEntity->GetCenter());
            if (!visibleFromCenter)
                continue;

            // Calculate base damage based on projectile type
            float flBaseDamage = kBasePipeDamage;
            if (bIsRocket)
                flBaseDamage = kBaseRocketDamage;
            else if (bIsSticky)
                flBaseDamage = kBaseStickyDamage;
            
            bool isCrit = false;

            if (bIsRocket)
            {
                if (CFG::AutoUber_CritProjectile_Check)
                {
                    if (const auto rocket = pEntity->As<C_TFProjectile_Rocket>())
                        isCrit = rocket->m_bCritical();
                }
            }
            else if (bIsPipebomb)
            {
                if (const auto pipe = pEntity->As<C_TFGrenadePipebombProjectile>())
                {
                    if (CFG::AutoUber_CritProjectile_Check)
                        isCrit = pipe->m_bCritical();
                }
            }

            // Apply TF2 damage falloff based on distance from shooter
            // For projectiles, we use current distance as approximation
            float flDamage = calcDamageFalloff(flBaseDamage, flCurrentDist);
            
            // Apply crit multiplier after falloff (crits ignore falloff in TF2, but we apply it for safety)
            if (isCrit)
                flDamage = flBaseDamage * TF_DAMAGE_CRIT_MULTIPLIER; // Crits do full damage
            
            // Use appropriate splash radius
            float flSplashRadius = bIsRocket ? kRocketSplashRadius : kStickySplashRadius;
            
            // If very close (< splash radius), it's a serious threat
            if (flCurrentDist < flSplashRadius)
            {
                flTotalLethalDamage += flDamage;
                
                const char* projType = bIsRocket ? "ROCKET" : (bIsSticky ? "STICKY" : "PIPE");
                I::CVar->ConsoleColorPrintf({ 255, 100, 100, 255 }, 
                    "[AutoUber] LETHAL THREAT: %s dist=%.0f time=%.2fs dmg=%.0f crit=%d\n", 
                    projType, flCurrentDist, flTimeToImpact, flDamage, isCrit ? 1 : 0);
            }
        }

        return flTotalLethalDamage;
    };

    // Check for sticky trap (multiple stickies nearby that could detonate)
    // Simplified: 100 damage per sticky, crit = 300 damage per sticky
    auto getStickyTrapThreat = [&](C_TFPlayer* who) -> float {
        if (!who) return 0.0f;

        float flTotalDamage = 0.0f;
        int nNearbyStickies = 0;
        constexpr float kStickyDetectRadius = 150.0f; // Slightly larger than splash radius
        constexpr float kBaseStickyDamage = 100.0f;   // Simplified: 100 damage per sticky

        for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
        {
            if (!pEntity)
                continue;

            if (pEntity->GetClassId() != ETFClassIds::CTFGrenadePipebombProjectile)
                continue;

            const auto pipe = pEntity->As<C_TFGrenadePipebombProjectile>();
            if (!pipe || !pipe->HasStickyEffects())
                continue;

            Vec3 vel{};
            pEntity->EstimateAbsVelocity(vel);
            
            // Only count stationary stickies (on ground)
            if (vel.Length() > 50.0f)
                continue;

            const float flDist = pEntity->m_vecOrigin().DistTo(who->GetCenter());
            if (flDist > kStickyDetectRadius)
                continue;

            // Visibility check
            const auto visible = H::AimUtils->TraceEntityAutoDet(pEntity, who->GetCenter(), pEntity->GetCenter());
            if (!visible)
                continue;

            bool isCrit = false;
            if (CFG::AutoUber_CritProjectile_Check)
                isCrit = pipe->m_bCritical();

            // Simple calculation: 100 damage per sticky, 300 if crit
            float flDamage = isCrit ? (kBaseStickyDamage * TF_DAMAGE_CRIT_MULTIPLIER) : kBaseStickyDamage;

            flTotalDamage += flDamage;
            nNearbyStickies++;
        }

        // Only consider it a trap if there are multiple stickies
        if (nNearbyStickies >= 2 && flTotalDamage > 0.0f)
        {
            I::CVar->ConsoleColorPrintf({ 255, 165, 0, 255 }, 
                "[AutoUber] STICKY TRAP: %d stickies, total dmg=%.0f\n", nNearbyStickies, flTotalDamage);
        }

        return (nNearbyStickies >= 2) ? flTotalDamage : 0.0f;
    };

    // Check for nearby enemies with hitscan weapons aiming at us
    // Only considers close-range enemies where damage falloff is minimal
    auto getHitscanThreat = [&](C_TFPlayer* who) -> float {
        if (!who) return 0.0f;
        if (who->IsInvulnerable()) return 0.0f;

        float flTotalDamage = 0.0f;
        
        // Only consider enemies within this range (close enough that falloff doesn't help much)
        constexpr float kMaxThreatRange = 400.0f;

        for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
        {
            if (!pEntity)
                continue;

            const auto enemy = pEntity->As<C_TFPlayer>();
            if (!enemy || enemy->deadflag())
                continue;

            // Skip snipers - they're handled separately
            if (enemy->m_iClass() == TF_CLASS_SNIPER)
                continue;

            const auto pWpnBase = enemy->m_hActiveWeapon().Get();
            if (!pWpnBase)
                continue;

            const auto pWeaponBase = pWpnBase->As<C_TFWeaponBase>();
            if (!pWeaponBase)
                continue;

            const int weaponID = pWeaponBase->GetWeaponID();
            
            // Only check hitscan weapons
            if (!isHitscanWeapon(weaponID))
                continue;

            const float flDist = enemy->GetShootPos().DistTo(who->GetCenter());
            
            // Only consider close-range threats (falloff is minimal at close range)
            if (flDist > kMaxThreatRange)
                continue;

            // Check if enemy is aiming at us
            auto mins = who->m_vecMins();
            auto maxs = who->m_vecMaxs();
            // Expand hitbox slightly for prediction
            mins.x *= 1.5f; mins.y *= 1.5f; mins.z *= 1.2f;
            maxs.x *= 1.5f; maxs.y *= 1.5f; maxs.z *= 1.2f;

            Vec3 fwd{};
            Math::AngleVectors(enemy->GetEyeAngles(), &fwd);
            
            if (!Math::RayToOBB(enemy->GetShootPos(), fwd, who->m_vecOrigin(), mins, maxs, who->RenderableToWorldTransform()))
                continue;

            // LOS check
            const bool visible = H::AimUtils->TraceEntityAutoDet(pEntity, who->GetCenter(), enemy->GetShootPos());
            if (!visible)
                continue;

            // Calculate damage with falloff
            float flBaseDamage = getHitscanBaseDamage(weaponID);
            float flDamage = calcDamageFalloff(flBaseDamage, flDist);

            // Check for crits/minicrits
            if (enemy->IsCritBoosted())
                flDamage = flBaseDamage * TF_DAMAGE_CRIT_MULTIPLIER;
            else if (enemy->IsMiniCritBoosted())
                flDamage = flBaseDamage * TF_DAMAGE_MINICRIT_MULTIPLIER;

            flTotalDamage += flDamage;

            I::CVar->ConsoleColorPrintf({ 255, 200, 100, 255 }, 
                "[AutoUber] HITSCAN THREAT: dist=%.0f dmg=%.0f weaponID=%d\n", flDist, flDamage, weaponID);
        }

        return flTotalDamage;
    };

    bool bShouldPop = false;

    // Sniper sightline check - only pop if sniper can one-shot us
    auto isSniperSightlineDanger = [&](C_TFPlayer* who) -> bool
    {
        if (!CFG::AutoUber_SniperSightline_Check || !who)
            return false;

        if (who->IsInvulnerable())
            return false;

        for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
        {
            if (!pEntity)
                continue;

            const auto enemy = pEntity->As<C_TFPlayer>();
            if (!enemy || enemy->deadflag() || enemy->m_iClass() != TF_CLASS_SNIPER)
                continue;

            const auto pWpnBase = enemy->m_hActiveWeapon().Get()->As<C_TFWeaponBase>();
            if (!pWpnBase || pWpnBase->GetSlot() != WEAPON_SLOT_PRIMARY || pWpnBase->GetWeaponID() == TF_WEAPON_COMPOUND_BOW)
                continue;

            bool bZoomed = enemy->InCond(TF_COND_ZOOMED);
            if (pWpnBase->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC)
            {
                if (const auto pClassic = pWpnBase->As<C_TFSniperRifleClassic>())
                    bZoomed = pClassic->m_bCharging();
            }

            if (!bZoomed)
                continue;

            // Sightline check
            auto mins = who->m_vecMins();
            auto maxs = who->m_vecMaxs();
            mins.x *= 2.0f; mins.y *= 2.0f; mins.z *= 1.2f;
            maxs.x *= 2.0f; maxs.y *= 2.0f; maxs.z *= 1.2f;

            Vec3 fwd{}; Math::AngleVectors(enemy->GetEyeAngles(), &fwd);
            if (!Math::RayToOBB(enemy->GetShootPos(), fwd, who->m_vecOrigin(), mins, maxs, who->RenderableToWorldTransform()))
                continue;

            // LOS check
            const bool visible = H::AimUtils->TraceEntityAutoDet(pEntity, who->GetCenter(), enemy->GetShootPos());
            if (!visible)
                continue;

            const auto pSniperRifle = pWpnBase->As<C_TFSniperRifle>();
            if (!pSniperRifle)
                continue;

            const bool bSydneySleeper = (pWpnBase->m_iItemDefinitionIndex() == Sniper_m_TheSydneySleeper);
            float flCharge = std::min(150.0f, pSniperRifle->m_flChargedDamage() * 1.15f);
            const float flBase = std::max(flCharge, 50.0f);
            const float flMult = bSydneySleeper ? TF_DAMAGE_MINICRIT_MULTIPLIER : TF_DAMAGE_CRIT_MULTIPLIER;
            const float flHsDmg = std::min(450.0f, flBase * flMult);

            // Only pop if it would actually kill us
            if (static_cast<float>(who->m_iHealth()) <= flHsDmg)
            {
                I::CVar->ConsoleColorPrintf({ 255, 0, 0, 255 }, 
                    "[AutoUber] SNIPER THREAT: charge=%.0f dmg=%.0f hp=%d\n", flCharge, flHsDmg, who->m_iHealth());
                return true;
            }
        }

        return false;
    };

    // Evaluate threats for a player
    auto shouldPopFor = [&](C_TFPlayer* who, int hp) -> bool
    {
        if (!who || who->IsInvulnerable())
            return false;

        const float flHP = static_cast<float>(hp);

        // Check for incoming lethal projectiles
        float flProjectileDamage = getLethalProjectileThreat(who);
        if (flProjectileDamage > 0.0f && flHP <= flProjectileDamage + 10.0f)
            return true;

        // Check for sticky traps
        float flStickyDamage = getStickyTrapThreat(who);
        if (flStickyDamage > 0.0f && flHP <= flStickyDamage + 20.0f)
            return true;

        // Check for nearby hitscan threats
        float flHitscanDamage = getHitscanThreat(who);
        if (flHitscanDamage > 0.0f && flHP <= flHitscanDamage + 10.0f)
            return true;

        return false;
    };

    if (shouldPopFor(pLocal, hpLocal) || (pHealTarget && shouldPopFor(pHealTarget, hpTarget)))
    {
        bShouldPop = true;
    }

    // Sniper sightline lethal check last (non-invasive with previous logic)
    if (!bShouldPop && CFG::AutoUber_SniperSightline_Check)
    {
        if (isSniperSightlineDanger(pLocal) || (pHealTarget && isSniperSightlineDanger(pHealTarget)))
            bShouldPop = true;
    }

    // DEBUG: Print when we decide to pop
    if (bShouldPop)
    {
        bool bCanPopNow = canPop();
        I::CVar->ConsoleColorPrintf({ 255, 0, 255, 255 }, "[AutoUber] SHOULD POP! canPop=%d charge=%.2f releasing=%d canAttack2=%d\n", 
            bCanPopNow ? 1 : 0, medigun->m_flChargeLevel(), medigun->m_bChargeRelease() ? 1 : 0, G::bCanSecondaryAttack ? 1 : 0);
    }

    if (bShouldPop && canPop())
    {
        I::CVar->ConsoleColorPrintf({ 0, 255, 0, 255 }, "[AutoUber] POPPING UBER!\n");
        cmd->buttons |= IN_ATTACK2;
    }
}




int CMisc::GetHPThresholdForClass(int nClass)
{
	switch (nClass)
	{
		case TF_CLASS_SCOUT: return CFG::Misc_Auto_Call_Medic_HP_Scout;
		case TF_CLASS_SOLDIER: return CFG::Misc_Auto_Call_Medic_HP_Soldier;
		case TF_CLASS_PYRO: return CFG::Misc_Auto_Call_Medic_HP_Pyro;
		case TF_CLASS_DEMOMAN: return CFG::Misc_Auto_Call_Medic_HP_Demoman;
		case TF_CLASS_HEAVYWEAPONS: return CFG::Misc_Auto_Call_Medic_HP_Heavy;
		case TF_CLASS_ENGINEER: return CFG::Misc_Auto_Call_Medic_HP_Engineer;
		case TF_CLASS_SNIPER: return CFG::Misc_Auto_Call_Medic_HP_Sniper;
		case TF_CLASS_SPY: return CFG::Misc_Auto_Call_Medic_HP_Spy;
		case TF_CLASS_MEDIC: return CFG::Misc_Auto_Call_Medic_HP_Medic;
		default: return 100;
	}
}

void CMisc::AutoCallMedic()
{
	// Return early if both features are disabled
	if (!CFG::Misc_Auto_Call_Medic_On_Damage && !CFG::Misc_Auto_Call_Medic_Low_HP)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
		return;
	
	// If spy is invisible/cloaked, disable all auto medic calling
	if (pLocal->m_iClass() == TF_CLASS_SPY && pLocal->InCond(TF_COND_STEALTHED))
		return;
	
	// Track previous health and last call time
	static int nPrevHealth = -1;
	static float flLastCallTime = 0.0f;
	
	// Get current health
	const int nCurrentHealth = pLocal->m_iHealth();
	
	// Initialize on first run
	if (nPrevHealth == -1)
	{
		nPrevHealth = nCurrentHealth;
		return;
	}
	
	bool bShouldCallMedic = false;
	
	// Low HP feature takes priority over damage feature
	if (CFG::Misc_Auto_Call_Medic_Low_HP)
	{
		// Get threshold for current class
		const int nThreshold = GetHPThresholdForClass(pLocal->m_iClass());
		
		// If HP is at or below threshold, call medic continuously (with cooldown)
		if (nCurrentHealth <= nThreshold)
		{
			bShouldCallMedic = true;
		}
	}
	// Only check damage if low HP feature is disabled
	else if (CFG::Misc_Auto_Call_Medic_On_Damage)
	{
		// Check if we took damage (health decreased)
		if (nCurrentHealth < nPrevHealth)
		{
			bShouldCallMedic = true;
		}
	}
	
	// Call medic with 5 second cooldown
	if (bShouldCallMedic)
	{
		const float flCurrentTime = I::GlobalVars->curtime;
		if ((flCurrentTime - flLastCallTime) > 5.0f)
		{
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 0");
			flLastCallTime = flCurrentTime;
		}
	}
	
	// Always update previous health for damage detection
	nPrevHealth = nCurrentHealth;
}


// Auto Force-A-Nature (FaN) - Automatically shoots when double jumping as Scout
void CMisc::AutoFaN(CUserCmd* pCmd)
{
	// Reset FaN running flag at start of each tick
	m_bFaNRunning = false;
	
	// Check if key is bound and pressed
	if (!H::Input->IsDown(CFG::Misc_AutoFaN_Key))
		return;
	
	const auto pLocal = H::Entities->GetLocal();
	const auto pWeapon = H::Entities->GetWeapon();

	if (!pLocal || !pWeapon)
		return;

	if (pLocal->deadflag())
		return;

	// Must be Scout
	if (pLocal->m_iClass() != TF_CLASS_SCOUT)
		return;

	// Must be Force-A-Nature (item definition index 45)
	if (pWeapon->m_iItemDefinitionIndex() != 45)
		return;

	// Must have ammo
	if (!G::bCanPrimaryAttack)
		return;

	// Don't override manual shooting
	if (pCmd->buttons & IN_ATTACK)
		return;

	// CRITICAL: Only work when on ground
	const bool bIsOnGround = pLocal->m_fFlags() & FL_ONGROUND;
	if (!bIsOnGround)
		return;

	// Check velocity - must be at least 360 units/sec
	const Vec3 vVelocity = pLocal->m_vecVelocity();
	const float flSpeed = sqrtf(vVelocity.x * vVelocity.x + vVelocity.y * vVelocity.y);
	
	if (flSpeed < 360.0f)
		return;

	// Check if user is pressing movement keys
	const bool bForward = pCmd->buttons & IN_FORWARD;
	const bool bBack = pCmd->buttons & IN_BACK;
	const bool bLeft = pCmd->buttons & IN_MOVELEFT;
	const bool bRight = pCmd->buttons & IN_MOVERIGHT;
	
	// Must have movement input
	if (!bForward && !bBack && !bLeft && !bRight)
		return;

	// Calculate yaw offset based on movement direction
	float flYawOffset = 0.0f;

	if (bForward && !bBack && !bLeft && !bRight)
	{
		// W - Forward: Shoot forward (0 degrees)
		flYawOffset = 0.0f;
	}
	else if (bBack && !bForward && !bLeft && !bRight)
	{
		// S - Backward: Shoot backward (180 degrees)
		flYawOffset = 180.0f;
	}
	else if (bLeft && !bRight && !bForward && !bBack)
	{
		// A - Left: Shoot RIGHT (90 degrees)
		flYawOffset = 90.0f;
	}
	else if (bRight && !bLeft && !bForward && !bBack)
	{
		// D - Right: Shoot LEFT (-90 degrees)
		flYawOffset = -90.0f;
	}
	else if (bForward && bLeft && !bBack && !bRight)
	{
		// W+A - Forward-Left: Shoot forward-right (45 degrees)
		flYawOffset = 45.0f;
	}
	else if (bForward && bRight && !bBack && !bLeft)
	{
		// W+D - Forward-Right: Shoot forward-left (-45 degrees)
		flYawOffset = -45.0f;
	}
	else if (bBack && bLeft && !bForward && !bRight)
	{
		// S+A - Back-Left: Shoot back-right (135 degrees)
		flYawOffset = 135.0f;
	}
	else if (bBack && bRight && !bForward && !bLeft)
	{
		// S+D - Back-Right: Shoot back-left (-135 degrees)
		flYawOffset = -135.0f;
	}
	else
	{
		// Complex movement combination - don't execute
		return;
	}

	Vec3 vShootAngles = pCmd->viewangles;

	// Adjust pitch based on movement speed (looking DOWN to shoot at feet)
	if (flSpeed < 100.0f)
		vShootAngles.x = 60.0f;
	else if (flSpeed < 200.0f)
		vShootAngles.x = 75.0f;
	else if (flSpeed < 300.0f)
		vShootAngles.x = 60.0f;
	else if (flSpeed < 400.0f)
		vShootAngles.x = 45.0f;
	else if (flSpeed < 500.0f)
		vShootAngles.x = 30.0f;
	else
		vShootAngles.x = 15.0f;

	// Apply yaw offset to shoot in the direction of movement
	vShootAngles.y += flYawOffset;

	// Normalize yaw to -180 to 180 range
	while (vShootAngles.y > 180.0f)
		vShootAngles.y -= 360.0f;
	while (vShootAngles.y < -180.0f)
		vShootAngles.y += 360.0f;

	// Mark FaN as running so AntiCheatCompat and other features skip this tick
	m_bFaNRunning = true;
	
	// Execute the FaN jump
	pCmd->buttons |= IN_JUMP;
	
	// Set silent angles to prevent view from snapping (same as rocket jump)
	G::bSilentAngles = true;
	pCmd->viewangles = vShootAngles;
	pCmd->buttons |= IN_ATTACK;
}
