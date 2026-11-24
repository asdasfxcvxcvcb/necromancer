#include "Misc.h"

#include "../CFG.h"
#include "AntiCheatCompat/AntiCheatCompat.h"
#include "../Aimbot/AimbotMelee/AimbotMelee.h"
#include "../FakeAngle/FakeAngle.h"
#include "../LagRecords/LagRecords.h"
#include "../Players/Players.h"

MAKE_SIGNATURE(ReportPlayerAccount, "client.dll", "48 89 5C 24 ? 57 48 83 EC 60 48 8B D9 8B FA 48 C1 E9 20 44 8B C1 41 C1 E8 14 41 83 E0 0F", 0x0);
MAKE_SIGNATURE(Trading_RequestTrade, "client.dll", "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 48 8D AC 24 ? ? ? ? 48 81 EC A0 08 00 00", 0x0);
MAKE_SIGNATURE(TradeSpam_ShowWaitingDialog, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 41 56 48 83 EC 50", 0x0);

MAKE_HOOK(TradeSpam_ShowWaitingDialog, Signatures::TradeSpam_ShowWaitingDialog.Get(), void, __fastcall,
	void* pDialog, const char* pText, bool bAnimate, bool bShowCancel, float flMaxDuration)
{
		if (CFG::Misc_Trade_Spam && pText && !strncmp(pText, "#TF_Trading_", 12))
			return;

	CALL_ORIGINAL(pDialog, pText, bAnimate, bShowCancel, flMaxDuration);
}

MAKE_SIGNATURE(TradeSpam_InitiateTradeResponse, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC 50 48 8B DA E8", 0x0);

MAKE_HOOK(TradeSpam_InitiateTradeResponse, Signatures::TradeSpam_InitiateTradeResponse.Get(), bool, __fastcall,
	void* ecx, void* pNetPacket)
{
	uint32_t nResponse = 0xFFFFFFFF;
	if (pNetPacket)
	{
		const auto pVtable = *reinterpret_cast<void***>(pNetPacket);
		if (pVtable && pVtable[4])
		{
			const auto pData = reinterpret_cast<void*(__fastcall*)(void*)>(pVtable[4])(pNetPacket);
			if (pData)
				nResponse = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pData) + 0x1E);
		}
	}

	F::Misc->TradeSpamResponseCodeSeen(nResponse);
	return true;
}

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
		static bool bLastSentJump = false;
		static int iBhopCount = 0;

		const bool bGrounded = (pLocal->m_fFlags() & FL_ONGROUND) != 0;
		const bool bJumping = (pCmd->buttons & IN_JUMP) != 0;

		if (pCmd->buttons & IN_JUMP)
		{
			if (!bJumpState && !(pLocal->m_fFlags() & FL_ONGROUND))
				pCmd->buttons &= ~IN_JUMP;

			else if (bJumpState)
				bJumpState = false;

			else if (bLastGrounded && bGrounded && bLastSentJump)
				pCmd->buttons &= ~IN_JUMP;
		}
		else if (!bJumpState)
		{
			bJumpState = true;
		}

		if (CFG::Misc_AntiCheat_Enabled && F::AntiCheatCompat->ShouldLimitBhop(iBhopCount, bGrounded, bLastGrounded, bJumping))
		{
			pCmd->buttons &= ~IN_JUMP;
		}

		bLastSentJump = (pCmd->buttons & IN_JUMP) != 0;
		bLastGrounded = bGrounded;
	}
}

void CMisc::CrouchWhileAirborne(CUserCmd* pCmd)
{
	if (!CFG::Misc_Crouch_While_Airborne || m_bJumpbugEngaged)
		return;

	if (const auto pLocal = H::Entities->GetLocal())
	{
		if (pLocal->deadflag())
			return;

		// Duck prediction is now handled consistently across all aimbot types
		// (GetProjectileFireSetupRebuilt, projectile aimbot, hitscan aimbot all predict
		// the duck-adjusted shoot position when IN_DUCK is set while airborne)
		F::AimbotMelee->CrouchWhileAirborne(pCmd, pLocal);
	}
}

void CMisc::AutoJumpbug(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	m_bJumpbugEngaged = false;

	if (!CFG::Misc_AutoJumpbug || !pLocal || pLocal->deadflag())
		return;

	if (pLocal->m_hGroundEntity() || pLocal->m_vecVelocity().z > -650.f)
		return;

	if (!(pCmd->buttons & IN_DUCK) && !CFG::Misc_Crouch_While_Airborne)
		return;

	const float flUnduckHeight = 20 * pLocal->m_flModelScale();
	const float flTraceDistance = flUnduckHeight + 2;

	trace_t trace = {};
	CTraceFilterWorldAndProps filter = {};

	const Vec3 vOrigin = pLocal->m_vecOrigin();
	H::AimUtils->TraceHull(vOrigin, vOrigin - Vec3(0, 0, flTraceDistance), pLocal->m_vecMins(), pLocal->m_vecMaxs(), MASK_PLAYERSOLID, &filter, &trace);
	if (!trace.DidHit() || trace.fraction * flTraceDistance < flUnduckHeight)
		return;

	pCmd->buttons &= ~IN_DUCK;
	pCmd->buttons |= IN_JUMP;
	m_bJumpbugEngaged = true;
	m_flLastJumpbug = I::EngineClient->Time();
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
				// Throttle TraceRays to every 5 ticks - wall geometry doesn't change between ticks
				static int nLastTraceTick = 0;
				static Vec3 vCachedWallNormal = Vec3(0, 0, 0);
				static float flCachedClosestDist = 999999.0f;
				static bool bCachedWallDetected = false;

				const int nCurrentTick = I::GlobalVars->tickcount;

				// Reset cache on map change (tickcount drops when new map loads)
				if (nCurrentTick < nLastTraceTick)
				{
					nLastTraceTick = 0;
					vCachedWallNormal = Vec3(0, 0, 0);
					flCachedClosestDist = 999999.0f;
					bCachedWallDetected = false;
				}

				if (nCurrentTick - nLastTraceTick >= 5 || nLastTraceTick == 0)
				{
					nLastTraceTick = nCurrentTick;

					Vec3 vLocalPos = pLocal->m_vecOrigin();
					CTraceFilterWorldCustom filter{};

					const int numRays = 8;
					const float flDetectRadius = 80.0f;

					vCachedWallNormal = Vec3(0, 0, 0);
					flCachedClosestDist = 999999.0f;
					bCachedWallDetected = false;

					for (int i = 0; i < numRays; i++)
					{
						float flAngle = (360.0f / numRays) * i;
						float flRad = DEG2RAD(flAngle);

						Vec3 vDir;
						vDir.x = cosf(flRad);
						vDir.y = sinf(flRad);
						vDir.z = 0.0f;

						Vec3 vStart = vLocalPos;
						vStart.z += 32.0f;

						Vec3 vEnd = vStart + (vDir * flDetectRadius);

						trace_t trace{};
						H::AimUtils->Trace(vStart, vEnd, MASK_SOLID, &filter, &trace);

						if (trace.DidHit())
						{
							float flDist = trace.fraction * flDetectRadius;

							if (flDist > 5.0f && flDist < flCachedClosestDist)
							{
								flCachedClosestDist = flDist;
								vCachedWallNormal = trace.plane.normal;
								vCachedWallNormal.z = 0.0f;
								vCachedWallNormal.Normalize();
								bCachedWallDetected = true;
							}
						}
					}
				}

				// Use cached wall data (still recompute avoidance from current velocity each tick)
				const Vec3& vWallNormal = vCachedWallNormal;
				const float flClosestDist = flCachedClosestDist;
				const bool bWallDetected = bCachedWallDetected;
				
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

	// Skip on attack, doubletap, recharge, or every other tick
	// Also skip when anti-aim is active (like Amalgam) - they're incompatible
	if (G::Attacking == 1 || Shifting::bRecharging || Shifting::bStickyDTWantShift || I::GlobalVars->tickcount % 2)
		return;
	
	// Skip when anti-aim is active - FastAccelerate and anti-aim both manipulate
	// viewangles and movement in incompatible ways
	if (F::FakeAngle->AntiAimOn())
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
	G::bSilentAngles = true;
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
	// =========================================================================
	// AutoMedigun - smart beam lock implementation
	// 
	// Key insight: As long as IN_ATTACK is held and we have a valid target
	// in range, the medigun maintains the beam. No need to constantly aim.
	//
	// Bug fixes from original:
	// 1. User priority: if user presses IN_ATTACK, let them control
	// 2. Target switching: search for better targets even while beam attached
	// 3. Uber: don't release beam when target is invulnerable from uber
	// 4. No pSilent: use bSilentAngles (view restore only, no packet choke)
	// 5. No static pointers: search every tick, no stale cache
	// =========================================================================

	if (!CFG::AutoUber_AutoHeal_Active)
		return;

	if (!CFG::AutoHeal_Always_On && !H::Input->IsDown(CFG::Triggerbot_Key))
		return;

	if (I::EngineVGui->IsGameUIVisible() || I::MatSystemSurface->IsCursorVisible() || SDKUtils::BInEndOfMatch())
		return;

	const auto local = H::Entities->GetLocal();
	if (!local || local->deadflag() || local->m_iClass() != TF_CLASS_MEDIC)
		return;

	if (local->InCond(TF_COND_TAUNTING) || local->InCond(TF_COND_HALLOWEEN_GHOST_MODE) ||
		local->InCond(TF_COND_HALLOWEEN_BOMB_HEAD) || local->InCond(TF_COND_HALLOWEEN_KART))
		return;

	const auto weapon = H::Entities->GetWeapon();
	if (!weapon || weapon->GetWeaponID() != TF_WEAPON_MEDIGUN)
		return;

	const auto medigun = weapon->As<C_WeaponMedigun>();
	if (!medigun)
		return;

	const float flOverhealMult = (medigun->GetMedigunType() == MEDIGUN_QUICKFIX) ? 1.25f : 1.5f;
	constexpr float FL_CONNECT_RANGE = 450.0f;
	constexpr float FL_STICK_RANGE = 540.0f;

	static int nUserOverrideTicks = 0;
	static bool bUserHeld = false;

	const bool bHeld{ (cmd->buttons & IN_ATTACK) != 0 };
	const bool bPressEdge{ bHeld && !bUserHeld };
	bUserHeld = bHeld;

	if (bHeld)
	{
		nUserOverrideTicks = 5;

		if (bPressEdge && medigun->m_bHealing())
		{
			const auto pBeam{ medigun->m_hHealingTarget().Get() };
			const auto pBeamPlayer{ pBeam ? pBeam->As<C_TFPlayer>() : nullptr };
			if (pBeamPlayer && !pBeamPlayer->deadflag())
			{
				Vec3 vDir{};
				Math::AngleVectors(cmd->viewangles, &vDir);
				const Vec3 vSrc{ local->GetShootPos() };
				const Vec3 vEnd{ vSrc + vDir * FL_CONNECT_RANGE };

				CTraceFilterHitscan filter{};
				trace_t tr{};
				H::AimUtils->Trace(vSrc, vEnd, (MASK_SHOT & ~CONTENTS_HITBOX), &filter, &tr);

				const auto pHit{ tr.m_pEnt && tr.m_pEnt->GetClassId() == ETFClassIds::CTFPlayer ? tr.m_pEnt->As<C_TFPlayer>() : nullptr };
				if (pHit && pHit != pBeamPlayer && pHit != local && !pHit->deadflag()
					&& pHit->m_iTeamNum() == local->m_iTeamNum() && !pHit->InCond(TF_COND_STEALTHED))
					cmd->buttons &= ~IN_ATTACK;
			}
		}
		return;
	}

	if (nUserOverrideTicks > 0)
	{
		nUserOverrideTicks--;
		return;
	}

	auto getHealthPercent = [&](C_TFPlayer* pl) -> float
	{
		float flMaxOverheal = static_cast<float>(pl->GetMaxHealth()) * flOverhealMult;
		return static_cast<float>(pl->m_iHealth()) / flMaxOverheal;
	};

	auto isFriend = [](C_TFPlayer* pl) -> bool
	{
		return pl->IsPlayerOnSteamFriendsList();
	};

	// Check current beam target
	auto pCurrentTarget = medigun->m_hHealingTarget().Get();
	C_TFPlayer* pCurrentHealPlayer = nullptr;

	if (pCurrentTarget && H::Entities->IsEntityValid(pCurrentTarget))
	{
		auto pAsPlayer = pCurrentTarget->As<C_TFPlayer>();
		if (pAsPlayer && !pAsPlayer->deadflag())
			pCurrentHealPlayer = pAsPlayer;
	}

	const bool bBeamAttached = pCurrentHealPlayer && medigun->m_bHealing();
	const bool bUberActive = medigun->m_bChargeRelease();

	auto traceToTarget = [&](C_TFPlayer* pl, const Vec3& vFrom, bool& bPlayerBlocked) -> bool
	{
		bPlayerBlocked = false;

		CTraceFilterHitscan filter{};
		trace_t tr{};
		H::AimUtils->Trace(vFrom, pl->GetCenter(), (MASK_SHOT & ~CONTENTS_HITBOX), &filter, &tr);

		if (tr.fraction > 0.99f || tr.m_pEnt == pl)
			return true;

		const int nEntIndex = tr.m_pEnt ? tr.m_pEnt->entindex() : 0;
		bPlayerBlocked = nEntIndex > 0 && nEntIndex <= I::EngineClient->GetMaxClients();
		return false;
	};

	auto aimAt = [&](C_TFPlayer* pl)
	{
		const auto angle = Math::CalcAngle(local->GetShootPos(), pl->GetCenter());
		H::AimUtils->FixMovement(cmd, angle);
		cmd->viewangles = angle;

		if (CFG::Misc_Accuracy_Improvements)
			cmd->tick_count = TIME_TO_TICKS(pl->m_flSimulationTime() + F::LagRecords->GetServerLerp());

		G::bSilentAngles = true;
	};

	auto wouldAttach = [&](C_TFPlayer* pGoalTarget) -> bool
	{
		const Vec3 vFrom = local->GetShootPos();
		Vec3 vAimDir = pGoalTarget->GetCenter() - vFrom;
		if (vAimDir.IsZero())
			return false;
		vAimDir.Normalize();

		C_TFPlayer* pBest = nullptr;
		float flBestDist = FL_CONNECT_RANGE;

		const int nMaxClients = I::EngineClient->GetMaxClients();
		for (int n = 1; n <= nMaxClients; n++)
		{
			auto pClientEntity = I::ClientEntityList->GetClientEntity(n);
			if (!pClientEntity || pClientEntity->IsDormant())
				continue;

			auto pl = pClientEntity->As<C_TFPlayer>();
			if (!pl || pl->deadflag() || pl == local)
				continue;

			if (pl->m_iTeamNum() != local->m_iTeamNum())
				continue;

			if (pl->InCond(TF_COND_STEALTHED))
				continue;

			const Vec3 vCenter = pl->GetCenter();
			const float flDist = vCenter.DistTo(vFrom);
			if (flDist > flBestDist)
				continue;

			Vec3 vToTarget = vCenter - vFrom;
			if (vToTarget.IsZero())
				continue;
			vToTarget.Normalize();
			if (vAimDir.Dot(vToTarget) < 0.93f)
				continue;

			CTraceFilterHitscan filter{};
			trace_t tr{};
			H::AimUtils->Trace(vFrom, vCenter, MASK_SOLID, &filter, &tr);
			if (!(tr.fraction > 0.99f || tr.m_pEnt == pl))
				continue;

			pBest = pl;
			flBestDist = flDist;
		}

		return pBest == pGoalTarget;
	};

	if (bUberActive && bBeamAttached)
	{
		const float flDist = pCurrentHealPlayer->GetCenter().DistTo(local->GetShootPos());
		if (flDist > FL_STICK_RANGE)
			cmd->buttons &= ~IN_ATTACK;
		else
			cmd->buttons |= IN_ATTACK;
		return;
	}

	C_TFPlayer* pGoal = nullptr;
	float flThreshold = 1.0f;

	if (bBeamAttached
		&& pCurrentHealPlayer->GetCenter().DistTo(local->GetShootPos()) <= FL_STICK_RANGE
		&& (!CFG::AutoHeal_Friends_Only || isFriend(pCurrentHealPlayer)))
	{
		pGoal = pCurrentHealPlayer;
		flThreshold = getHealthPercent(pCurrentHealPlayer) - 0.02f;
	}

	C_TFPlayer* pClosestClear = nullptr;
	float flClosestDist = FLT_MAX;

	const int nMaxClients = I::EngineClient->GetMaxClients();
	for (int n = 1; n <= nMaxClients; n++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(n);
		if (!pClientEntity || pClientEntity->IsDormant())
			continue;

		auto pl = pClientEntity->As<C_TFPlayer>();
		if (!pl || pl->deadflag() || pl == local || pl == pGoal)
			continue;

		if (pl->m_iTeamNum() != local->m_iTeamNum())
			continue;

		const float flDist = pl->GetCenter().DistTo(local->GetShootPos());
		if (flDist > FL_CONNECT_RANGE)
			continue;

		if (pl->InCond(TF_COND_STEALTHED) || pl->IsInvulnerable())
			continue;

		if (CFG::AutoHeal_Friends_Only && !isFriend(pl))
			continue;

		const float flPercent = getHealthPercent(pl);

		bool bPlayerBlocked = false;
		const bool bClear = traceToTarget(pl, local->GetShootPos(), bPlayerBlocked);

		if (bClear && flDist < flClosestDist)
		{
			pClosestClear = pl;
			flClosestDist = flDist;
		}

		if (flPercent < 0.99f && flPercent < flThreshold && (bClear || bPlayerBlocked))
		{
			pGoal = pl;
			flThreshold = flPercent - 0.02f;
		}
	}

	if (!pGoal)
		pGoal = pClosestClear;

	if (!pGoal)
	{
		cmd->buttons &= ~IN_ATTACK;
		return;
	}

	static int nSwitchCooldown = 0;
	if (nSwitchCooldown > 0)
		nSwitchCooldown--;

	if (bBeamAttached && pGoal != pCurrentHealPlayer)
	{
		aimAt(pGoal);

		if (nSwitchCooldown > 0 || !wouldAttach(pGoal))
			cmd->buttons |= IN_ATTACK;
		else
		{
			cmd->buttons &= ~IN_ATTACK;
			nSwitchCooldown = 60;
		}
	}
	else if (!bBeamAttached)
	{
		aimAt(pGoal);
		cmd->buttons |= IN_ATTACK;
	}
	else
	{
		cmd->buttons |= IN_ATTACK;
	}
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
	const bool bKeyHeld = H::Input->IsDown(CFG::Misc_MVM_Instant_Respawn_Key);

	// auto path: every death in MvM self-revives - the marker is created on
	// every human MvM death and ReviveOwner validates nothing, so a retried
	// accepted=1 lands as soon as the marker exists (no-op without one)
	static float flLastAutoSend = 0.0f;
	bool bAutoSend = false;

	if (CFG::Misc_MvM_Auto_Revive && !bKeyHeld
		&& !I::EngineVGui->IsGameUIVisible()
		&& !I::MatSystemSurface->IsCursorVisible()
		&& !SDKUtils::BInEndOfMatch())
	{
		if (const auto pLocal = H::Entities->GetLocal())
		{
			static int nMvMOffset = 0;
			if (nMvMOffset <= 0)
			{
				nMvMOffset = NetVars::GetNetVar("CTFGameRulesProxy", "m_bPlayingMannVsMachine");
				if (nMvMOffset <= 0)
					nMvMOffset = NetVars::GetNetVar("CTeamplayRoundBasedRulesProxy", "m_bPlayingMannVsMachine");
			}

			const auto pGameRules = I::TFGameRules();
			const bool bMvM = pGameRules && nMvMOffset > 0
				&& *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(pGameRules) + nMvMOffset);

			if (bMvM && pLocal->deadflag() && Plat_FloatTime() - flLastAutoSend >= 0.25f)
			{
				bAutoSend = true;
				flLastAutoSend = static_cast<float>(Plat_FloatTime());
			}
		}
	}

	if (!bKeyHeld && !bAutoSend)
		return;

	auto* kv{new KeyValues("MVM_Revive_Response")};

	kv->SetInt("accepted", 1);

	I::EngineClient->ServerCmdKeyValues(kv);
}

void CMisc::ApplyFakeInterp()
{
	const auto pLocal = H::Entities->GetLocal();
	const bool bAlive = pLocal && !pLocal->deadflag() && pLocal->m_iTeamNum() >= 2;

	// THE SERVER GATES THE APPLY (gameinterface.cpp:2825): ClientSettingsChanged
	// wraps the whole m_fLerpTime read in IsConnectedUserInfoChangeAllowed - only
	// when dead, not on a team, or <2s after spawn. our reliable NET_SetConVar
	// always updates the userinfo table, but m_fLerpTime only re-reads while the
	// gate is open and nothing re-runs it later - so a mid-life send is stored
	// but never applied, and the aim math would claim a lerp the server doesn't
	// have (every rewind lands shifted off the record, near the rendered model).
	// delivery therefore happens exactly at gate-open moments, and the math
	// claims only what the server has actually applied
	const bool bWasAlive = m_bInterpWasAlive;
	m_bInterpWasAlive = bAlive;
	if (bAlive && !bWasAlive)
		m_flInterpSpawnTime = static_cast<float>(Plat_FloatTime());

	const bool bGateOpen = !bAlive || (Plat_FloatTime() - m_flInterpSpawnTime) < 2.0f;
	if (!bGateOpen)
		return;

	const float flLocalLerp = SDKUtils::GetLerp();
	float flTarget = std::clamp(CFG::Aimbot_Hitscan_Fake_Interp / 1000.0f, flLocalLerp, F::LagRecords->GetMaxUnlag());
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
		flTarget = std::min(flTarget, 0.1f);

	// keep re-delivering while the gate is open: the apply only happens when a
	// setconvar ARRIVES during the window, so one send can be lost to the
	// respawn race - a 0.5s resend cadence guarantees the server applies it
	static float flLastSend = 0.0f;
	const float flNow = static_cast<float>(Plat_FloatTime());
	if (flTarget == F::LagRecords->GetFakeInterp() && flNow - flLastSend < 0.5f)
		return;
	flLastSend = flNow;

	auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
	if (!pNetChan || !I::EngineClient->IsConnected())
		return;

	NET_SetConVar kvInterp("cl_interp", std::to_string(flTarget).c_str());
	NET_SetConVar kvRatio("cl_interp_ratio", "1");
	NET_SetConVar kvInterp2("cl_interpolate", "1");

	pNetChan->SendNetMsg(kvInterp);
	pNetChan->SendNetMsg(kvRatio);
	pNetChan->SendNetMsg(kvInterp2);

	F::LagRecords->SetFakeInterp(flTarget);
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

void CMisc::PDAExploit(CUserCmd* pCmd)
{
	if (!CFG::Misc_PDA_Exploit_Sniper)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag() || pLocal->m_iClass() != TF_CLASS_SNIPER)
		return;

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon)
		return;

	const int iWeaponID = pWeapon->GetWeaponID();
	if (iWeaponID != TF_WEAPON_SNIPERRIFLE && iWeaponID != TF_WEAPON_SNIPERRIFLE_DECAP && iWeaponID != TF_WEAPON_SNIPERRIFLE_CLASSIC)
		return;

	// State machine: 0 = idle, 1 = waiting after shot (5 ticks), 2 = PDA open sent (5 ticks), 3 = PDA close sent (cooldown)
	static int iState = 0;
	static int iStateTick = 0;

	const int iCurTick = I::GlobalVars->tickcount;

	// Detect shot: zoomed + pressing attack (matches bind: +attack while zoomed)
	const bool bFiring = pLocal->IsZoomed() && (pCmd->buttons & IN_ATTACK);

	// Start exploit when we fire while zoomed
	if (iState == 0 && bFiring)
	{
		iState = 1;
		iStateTick = iCurTick;
	}

	// Send PDA open 5 ticks after shot
	if (iState == 1 && (iCurTick - iStateTick) >= 5)
	{
		I::EngineClient->ServerCmd("cyoa_pda_open 1");
		iState = 2;
		iStateTick = iCurTick;
	}

	// Close PDA after 5 ticks
	if (iState == 2 && (iCurTick - iStateTick) >= 5)
	{
		I::EngineClient->ServerCmd("cyoa_pda_open 0");
		iState = 3;
		iStateTick = iCurTick;
	}

	// Cooldown before allowing next exploit
	if (iState == 3 && (iCurTick - iStateTick) >= 5)
	{
		iState = 0;
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

	m_bFaNRunning = true;
	m_flLastFaN = I::EngineClient->Time();
	
	// Execute the FaN jump
	pCmd->buttons |= IN_JUMP;
	
	// Set silent angles to prevent view from snapping (same as rocket jump)
	G::bSilentAngles = true;
	pCmd->viewangles = vShootAngles;
	pCmd->buttons |= IN_ATTACK;
}

// === Class Switch on Death ===
// State machine: IDLE → SWITCHING_BACK → IDLE
//
// On instant-respawn servers, switching class triggers an immediate respawn
// and bypasses the killcam wait.
// 1. Detect death → joinclass scout;joinclass <original>;joinclass <original>;...
//    The semicolons let the server process multiple commands in one tick.
//    If the server only processes the first, we respawn as scout → SWITCHING_BACK
// 2. SWITCHING_BACK: keep sending joinclass <original> every tick until we respawn
//    as the original class. Since joining the class you already are does nothing,
//    spamming is harmless and ensures the server eventually gets the message.
void CMisc::FastClassSwitch()
{
	if (!CFG::Misc_Auto_FastClassSwitch)
	{
		m_iFCSState = FCS_IDLE;
		m_bFCSWasAlive = false;
		m_iFCSOriginalClass = 0;
		return;
	}

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
	{
		m_iFCSState = FCS_IDLE;
		m_bFCSWasAlive = false;
		m_iFCSOriginalClass = 0;
		return;
	}

	// Only run if on a valid team (Blue or Red) — don't send joinclass while in team selection
	int iTeam = pLocal->m_iTeamNum();
	if (iTeam != TF_TEAM_BLUE && iTeam != TF_TEAM_RED)
	{
		m_iFCSState = FCS_IDLE;
		m_bFCSWasAlive = false;
		m_iFCSOriginalClass = 0;
		return;
	}

	bool bIsAlive = pLocal->IsAlive();
	int iCurrentClass = pLocal->m_iClass();

	static const char* szClassNames[] = {
		"scout", "sniper", "soldier", "demoman",
		"medic", "heavyweapons", "pyro", "spy", "engineer"
	};

	switch (m_iFCSState)
	{
	case FCS_IDLE:
	{
		// Detect death: was alive last tick, now dead
		if (m_bFCSWasAlive && !bIsAlive)
		{
			if (iCurrentClass >= TF_CLASS_SCOUT && iCurrentClass <= TF_CLASS_ENGINEER)
			{
				int iSwitchClass = (iCurrentClass == TF_CLASS_SCOUT) ? TF_CLASS_SOLDIER : TF_CLASS_SCOUT;

				m_iFCSOriginalClass = iCurrentClass;
				m_iFCSState = FCS_SWITCHING_BACK;
				m_flFCSSendTime = I::GlobalVars->curtime;

				// Send joinclass scout followed by joinclass <original> multiple times.
				// The server may process multiple semicolon-separated commands per tick.
				// If it only processes the first one, we'll respawn as scout and the
				// SWITCHING_BACK state will keep trying every tick.
				// Sending joinclass <original> multiple times is safe because if we're
				// already that class, the server just ignores it.
				I::EngineClient->ClientCmd_Unrestricted(std::format("joinclass {};joinclass {};joinclass {};joinclass {}",
					szClassNames[iSwitchClass - 1],
					szClassNames[iCurrentClass - 1],
					szClassNames[iCurrentClass - 1],
					szClassNames[iCurrentClass - 1]).c_str());
			}
		}
		break;
	}

	case FCS_SWITCHING_BACK:
	{
		if (bIsAlive)
		{
			if (iCurrentClass == m_iFCSOriginalClass)
			{
				// Respawned as the right class — done!
				m_iFCSState = FCS_IDLE;
				m_iFCSOriginalClass = 0;
			}
			else if (I::GlobalVars->curtime >= m_flFCSSendTime + 0.5f)
			{
				// Respawned as wrong class — send joinclass original again (throttled).
				// On instant-respawn servers this triggers another respawn.
				if (m_iFCSOriginalClass >= TF_CLASS_SCOUT && m_iFCSOriginalClass <= TF_CLASS_ENGINEER)
				{
					I::EngineClient->ClientCmd_Unrestricted(std::format("joinclass {}", szClassNames[m_iFCSOriginalClass - 1]).c_str());
					m_flFCSSendTime = I::GlobalVars->curtime;
				}
			}
		}
		else if (I::GlobalVars->curtime >= m_flFCSSendTime + 0.5f)
		{
			// Still dead — resend joinclass original (throttled to 0.5s).
			if (m_iFCSOriginalClass >= TF_CLASS_SCOUT && m_iFCSOriginalClass <= TF_CLASS_ENGINEER)
			{
				I::EngineClient->ClientCmd_Unrestricted(std::format("joinclass {}", szClassNames[m_iFCSOriginalClass - 1]).c_str());
				m_flFCSSendTime = I::GlobalVars->curtime;
			}
		}
		break;
	}
	}

	// Update alive tracking
	m_bFCSWasAlive = bIsAlive;
}

void CMisc::ReportSpamRun()
{
	if (!CFG::Misc_Report_Spam || !I::EngineClient->IsInGame())
	{
		m_flRSLastTick = 0.f;
		m_flRSAccum = 0.f;
		m_iRSNextTarget = 1;
		return;
	}

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	const auto pReportFn = Signatures::ReportPlayerAccount.Get();
	if (!pReportFn)
		return;

	if (!m_pRSReportedCount)
	{
		const auto pInstr = reinterpret_cast<std::uint8_t*>(pReportFn) + 0x7E;
		if (pInstr[0] != 0x44 || pInstr[1] != 0x8B || pInstr[2] != 0x05)
			return;
		const std::int32_t nDisp = *reinterpret_cast<const std::int32_t*>(pInstr + 3);
		m_pRSReportedCount = reinterpret_cast<int*>(pInstr + 7 + nDisp);
	}

	const float flNow = I::GlobalVars->realtime;
	float flDelta = flNow - m_flRSLastTick;
	m_flRSLastTick = flNow;
	if (flDelta < 0.f || flDelta > 1.f)
		flDelta = 0.f;

	const int nRate = std::clamp(CFG::Misc_Report_Spam_Rate, 1, 1000);
	m_flRSAccum += nRate * flDelta;
	int nSend = static_cast<int>(m_flRSAccum);
	m_flRSAccum -= nSend;
	if (nSend <= 0)
		return;
	nSend = std::min(nSend, 200);

	const auto pResource = GetTFPlayerResource();
	if (!pResource)
		return;

	static int nConnectedOffset = NetVars::GetNetVar("CPlayerResource", "m_bConnected");
	static int nAccountOffset = NetVars::GetNetVar("CPlayerResource", "m_iAccountID");
	if (nConnectedOffset == 0 || nAccountOffset == 0)
		return;

	const int iLocalIndex = pLocal->entindex();
	const int iMaxClients = I::EngineClient->GetMaxClients();

	for (int n = 0; n < nSend; n++)
	{
		uint32_t unAccount = 0;
		bool bFound = false;
		for (int iChecked = 0; iChecked < iMaxClients; iChecked++)
		{
			const int i = m_iRSNextTarget;
			m_iRSNextTarget = (m_iRSNextTarget >= iMaxClients) ? 1 : m_iRSNextTarget + 1;

			if (i == iLocalIndex)
				continue;
			if (!*reinterpret_cast<bool*>((reinterpret_cast<std::uintptr_t>(pResource) + nConnectedOffset) + i))
				continue;
			const uint32_t unAcc = *reinterpret_cast<uint32_t*>((reinterpret_cast<std::uintptr_t>(pResource) + nAccountOffset) + (i * sizeof(uint32_t)));
			if (unAcc == 0)
				continue;

			const auto pEntity = I::ClientEntityList->GetClientEntity(i);
			if (pEntity && pEntity->GetClassId() == ETFClassIds::CTFPlayer)
			{
				if (pEntity->As<C_TFPlayer>()->IsPlayerOnSteamFriendsList())
					continue;
			}
			if (H::Entities->GetPartyIndex(i) == 1)
				continue;

			unAccount = unAcc;
			bFound = true;
			break;
		}

		if (!bFound)
			break;

		*m_pRSReportedCount = 0;
		reinterpret_cast<bool(__fastcall*)(uint64_t, int)>(pReportFn)(0x0110000100000000ULL | unAccount, 1);
	}
}

void CMisc::TradeSpamResponseCodeSeen(uint32_t nCode)
{
	const float flNow = I::GlobalVars->realtime;

	uint32_t unTarget = 0;
	if (!m_qTSRecent.empty())
	{
		unTarget = m_qTSRecent.front().first;
		m_qTSRecent.pop_front();
	}

	switch (nCode)
	{
	case 3:
	case 11:
	case 15:
	case 19:
		if (unTarget)
			m_setTSBlacklist.insert(unTarget);
		break;
	case 4:
		if (unTarget)
			m_mapTSRetryAfter[unTarget] = flNow + 60.0f;
		break;
	case 8:
		m_flTSRateScale = std::max(0.02f, m_flTSRateScale * 0.5f);
		m_flTSBackoffUntil = flNow + 5.0f;
		break;
	case 9:
		m_flTSRateScale = std::max(0.02f, m_flTSRateScale * 0.25f);
		m_flTSBackoffUntil = flNow + 60.0f;
		break;
	}
}

void CMisc::TradeSpamRun()
{
	if (!CFG::Misc_Trade_Spam || !I::EngineClient->IsInGame())
	{
		m_flTSLastTick = 0.f;
		m_flTSAccum = 0.f;
		m_iTSNextTarget = 1;
		if (!I::EngineClient->IsInGame())
		{
			m_qTSRecent.clear();
			m_mapTSRetryAfter.clear();
		}
		return;
	}

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	if (!Signatures::Trading_RequestTrade.Get())
		return;

	const float flNow = I::GlobalVars->realtime;
	float flDelta = flNow - m_flTSLastTick;
	m_flTSLastTick = flNow;
	if (flDelta < 0.f || flDelta > 1.f)
		flDelta = 0.f;

	if (flNow > m_flTSBackoffUntil && m_flTSRateScale < 1.0f)
		m_flTSRateScale = std::min(1.0f, m_flTSRateScale + flDelta * 0.1f);

	if (flNow < m_flTSBackoffUntil)
	{
		m_flTSAccum = 0.f;
		return;
	}

	const float flRate = std::clamp(static_cast<float>(CFG::Misc_Trade_Spam_Rate) * m_flTSRateScale, 0.05f, 1000.0f);
	m_flTSAccum += flRate * flDelta;
	int nSend = static_cast<int>(m_flTSAccum);
	m_flTSAccum -= nSend;
	if (nSend <= 0)
		return;
	nSend = std::min(nSend, 200);

	const auto pResource = GetTFPlayerResource();
	if (!pResource)
		return;

	static int nConnectedOffset = NetVars::GetNetVar("CPlayerResource", "m_bConnected");
	static int nAccountOffset = NetVars::GetNetVar("CPlayerResource", "m_iAccountID");
	if (nConnectedOffset == 0 || nAccountOffset == 0)
		return;

	const int iLocalIndex = pLocal->entindex();
	const int iMaxClients = I::EngineClient->GetMaxClients();

	for (int n = 0; n < nSend; n++)
	{
		uint32_t unAccount = 0;
		bool bFound = false;
		for (int iChecked = 0; iChecked < iMaxClients; iChecked++)
		{
			const int i = m_iTSNextTarget;
			m_iTSNextTarget = (m_iTSNextTarget >= iMaxClients) ? 1 : m_iTSNextTarget + 1;

			if (i == iLocalIndex)
				continue;
			if (!*reinterpret_cast<bool*>((reinterpret_cast<std::uintptr_t>(pResource) + nConnectedOffset) + i))
				continue;
			const uint32_t unAcc = *reinterpret_cast<uint32_t*>((reinterpret_cast<std::uintptr_t>(pResource) + nAccountOffset) + (i * sizeof(uint32_t)));
			if (unAcc == 0)
				continue;
			if (m_setTSBlacklist.find(unAcc) != m_setTSBlacklist.end())
				continue;
			const auto itRetry = m_mapTSRetryAfter.find(unAcc);
			if (itRetry != m_mapTSRetryAfter.end() && itRetry->second > flNow)
				continue;

			const auto pEntity = I::ClientEntityList->GetClientEntity(i);
			if (pEntity && pEntity->GetClassId() == ETFClassIds::CTFPlayer)
			{
				if (pEntity->As<C_TFPlayer>()->IsPlayerOnSteamFriendsList())
					continue;
			}
			if (H::Entities->GetPartyIndex(i) == 1)
				continue;

			if (CFG::Misc_Trade_Spam_Tagged_Only)
			{
				PlayerPriority priority{};
				if (!F::Players->GetInfo(i, priority))
					continue;
				if (!priority.Cheater && !priority.RetardLegit && !priority.Targeted)
					continue;
			}

			unAccount = unAcc;
			bFound = true;
			break;
		}

		if (!bFound)
			break;

		uint64_t ullSteamID = 0x0110000100000000ULL | unAccount;
		Signatures::Trading_RequestTrade.Call<void, uint64_t*>(&ullSteamID);
		m_qTSRecent.emplace_back(unAccount, flNow);
		if (m_qTSRecent.size() > 64)
			m_qTSRecent.pop_front();
	}
}

void CMisc::AutoBalanceRun()
{
	const bool bDodge = CFG::Misc_Auto_Balance_Dodge;
	const bool bWarn = CFG::Misc_Auto_Balance_Warn;
	if (!bDodge && !bWarn)
	{
		m_iABState = 0;
		m_flABDodgeAt = 0.f;
		m_flABLastTick = 0.f;
		std::fill(std::begin(m_flABSeen), std::end(m_flABSeen), 0.f);
		return;
	}

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || m_iFCSState != FCS_IDLE)
	{
		m_iABState = 0;
		m_flABDodgeAt = 0.f;
		m_flABLastTick = 0.f;
		std::fill(std::begin(m_flABSeen), std::end(m_flABSeen), 0.f);
		return;
	}

	const int iTeam = pLocal->m_iTeamNum();
	if (iTeam != TF_TEAM_BLUE && iTeam != TF_TEAM_RED)
	{
		m_iABState = 0;
		m_flABDodgeAt = 0.f;
		m_flABLastTick = 0.f;
		std::fill(std::begin(m_flABSeen), std::end(m_flABSeen), 0.f);
		return;
	}

	if (!bDodge)
		m_flABDodgeAt = 0.f;

	const float flNow = I::GlobalVars->curtime;
	const float flRealNow = I::GlobalVars->realtime;
	const int iLocalIndex = pLocal->entindex();
	const bool bContinuous = (flRealNow - m_flABLastTick) <= 1.f && m_flABLastTick > 0.f;

	if (flNow < m_flABLastCurTime)
	{
		m_flABSafeTime = 0.f;
		m_iABState = 0;
		std::fill(std::begin(m_flABSeen), std::end(m_flABSeen), 0.f);
	}
	m_flABLastCurTime = flNow;

	static auto svUnbalanceLimit = I::CVar->FindVar("mp_teams_unbalance_limit");
	const int nLimit = svUnbalanceLimit ? std::max(svUnbalanceLimit->GetInt(), 0) : 1;

	const auto pResource = GetTFPlayerResource();
	if (!pResource)
	{
		m_iABState = 0;
		m_flABLastTick = flRealNow;
		return;
	}

	static int nConnStateOffset = NetVars::GetNetVar("CTFPlayerResource", "m_iConnectionState");
	static int nConnectTimeOffset = NetVars::GetNetVar("CTFPlayerResource", "m_flConnectTime");
	static int nClassOffset = NetVars::GetNetVar("CTFPlayerResource", "m_iPlayerClass");
	static int nConnectedOffset = NetVars::GetNetVar("CPlayerResource", "m_bConnected");
	if (nConnStateOffset == 0)
	{
		m_iABState = 0;
		m_flABLastTick = flRealNow;
		return;
	}

	int iMine = 0;
	int iOther = 0;
	int nConnecting = 0;
	int nConnectingNoTeam = 0;
	float flMyScore = -1e9f;
	float flBestOtherScore = -1e9f;

	bool abPresent[33] = {};
	for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
	{
		if (nConnectedOffset != 0 && !*reinterpret_cast<bool*>((reinterpret_cast<std::uintptr_t>(pResource) + nConnectedOffset) + i))
			continue;

		const int nState = *reinterpret_cast<int*>((reinterpret_cast<std::uintptr_t>(pResource) + nConnStateOffset) + (i * sizeof(int)));

		const bool bConnecting = nState != MM_CONNECTED;
		if (bConnecting && i != iLocalIndex)
			nConnecting++;

		const int nPlayerTeam = pResource->GetTeam(i);
		if (nPlayerTeam != TF_TEAM_BLUE && nPlayerTeam != TF_TEAM_RED)
		{
			if (i != iLocalIndex)
			{
				nConnecting++;
				nConnectingNoTeam++;
			}
			continue;
		}

		abPresent[i] = true;

		if (m_flABSeen[i] <= 0.f)
			m_flABSeen[i] = flNow;

		float flSeen = m_flABSeen[i];
		if (nConnectTimeOffset != 0)
		{
			const float flConnect = *reinterpret_cast<float*>((reinterpret_cast<std::uintptr_t>(pResource) + nConnectTimeOffset) + (i * sizeof(float)));
			if (flConnect > 0.f)
				flSeen = flConnect;
		}

		if (nPlayerTeam != iTeam)
		{
			iOther++;
			continue;
		}

		iMine++;
		float flScore = -(flNow - flSeen);
		if (nClassOffset != 0)
		{
			const int nClass = *reinterpret_cast<int*>((reinterpret_cast<std::uintptr_t>(pResource) + nClassOffset) + (i * sizeof(int)));
			if (nClass == TF_CLASS_ENGINEER)
				flScore -= 120.f;
		}

		if (i == iLocalIndex)
		{
			flMyScore = flScore;
		}
		else if (flScore > flBestOtherScore)
		{
			flBestOtherScore = flScore;
		}
	}

	for (int i = 1; i < 33; i++)
	{
		if (!abPresent[i])
			m_flABSeen[i] = 0.f;
	}

	if (nConnectingNoTeam > 0 && iMine != iOther)
	{
		if (iMine < iOther)
			iMine += nConnectingNoTeam;
		else
			iOther += nConnectingNoTeam;
	}

	const int iDiff = iMine - iOther;
	const bool bUnbalanced = iOther > 0 && (iDiff > nLimit || -iDiff > nLimit);
	const bool bHeavy = iDiff > 0;
	const bool bTarget = bUnbalanced && bHeavy && nConnecting == 0 && flMyScore > -1e9f && flMyScore >= flBestOtherScore;

	if (bDodge && m_flABDodgeAt > 0.f && flRealNow >= m_flABDodgeAt)
	{
		m_flABDodgeAt = 0.f;
		if (bUnbalanced && bHeavy)
			I::EngineClient->ClientCmd_Unrestricted("retry");
	}

	int iState = 0;
	if (bTarget)
		iState = pLocal->IsAlive() ? 2 : 3;

	if (bContinuous)
	{
		if (iState == 0 && m_iABState != 0)
			m_flABSafeTime = flNow;

		if (iState != m_iABState)
			m_flABStateChange = flRealNow;
	}

	m_iABState = iState;
	m_iABMine = iMine;
	m_iABOther = iOther;
	m_flABLastTick = flRealNow;
}

void CMisc::AutoBalanceWarningSeen()
{
	if (!CFG::Misc_Auto_Balance_Dodge)
		return;

	m_flABDodgeAt = I::GlobalVars->realtime + 3.0f;
}

void CMisc::AutoBalanceWarnDraw()
{
	if (!CFG::Misc_Auto_Balance_Warn)
		return;

	if (!I::EngineClient->IsInGame())
		return;

	if (m_flABLastTick <= 0.f || I::GlobalVars->realtime - m_flABLastTick > 1.f)
		return;

	const float flNow = I::GlobalVars->curtime;
	const float flReal = I::GlobalVars->realtime;

	Color_t clrText = {};
	Color_t clrBorder = {};
	std::string sText;
	bool bPulse = false;
	float flFade = 1.f;
	if (m_iABState == 2)
	{
		clrText = {255, 90, 90, 255};
		clrBorder = {255, 60, 60, 255};
		sText = "AUTOBALANCE: SERVER WAITING FOR YOU TO DIE - DON'T DIE";
	}
	else if (m_iABState == 3)
	{
		clrText = {255, 90, 90, 255};
		clrBorder = {255, 60, 60, 255};
		sText = "AUTOBALANCE: SWAP ON RESPAWN - RETRY NOW";
		bPulse = true;
	}
	else
	{
		const float flElapsed = flNow - m_flABSafeTime;
		if (flElapsed >= 2.5f || flElapsed < 0.f || m_flABSafeTime <= 0.f)
			return;
		flFade = flElapsed < 1.f ? 1.f : 1.f - (flElapsed - 1.f) / 1.5f;
		clrText = {80, 255, 120, 255};
		clrBorder = {60, 230, 110, 255};
		sText = "AUTOBALANCE: YOU'RE GOOD";
	}

	float flSince = flReal - m_flABStateChange;
	float flAppear = std::clamp(flSince / 0.3f, 0.f, 1.f);
	float flEase = 1.f - (1.f - flAppear) * (1.f - flAppear);
	const float flVis = flEase * flFade;

	const auto& font = H::Fonts->Get(EFonts::ESP);
	int nTextW = 0;
	int nTextH = 0;
	I::MatSystemSurface->GetTextSize(font.m_dwFont, Utils::ConvertUtf8ToWide(sText).c_str(), nTextW, nTextH);

	const int nPadX = 14;
	const int nPadY = 7;
	const int nBoxW = nTextW + nPadX * 2;
	const int nBoxH = nTextH + nPadY * 2;
	const int nCx = H::Draw->GetScreenW() / 2;
	const int nCy = static_cast<int>(H::Draw->GetScreenH() * 0.32f);
	const int nYOff = static_cast<int>((1.f - flEase) * -35.f);
	const int nX = nCx - nBoxW / 2;
	const int nY = nCy - nBoxH / 2 + nYOff;

	byte nBorderA = 255;
	byte nTextA = 255;
	if (bPulse)
	{
		const float flPulse = std::sinf(flReal * 7.f) * 0.5f + 0.5f;
		nBorderA = static_cast<byte>(120.f + flPulse * 135.f);
		nTextA = static_cast<byte>(190.f + flPulse * 65.f);
	}

	const Color_t clrBg = {15, 15, 20, static_cast<byte>(165.f * flVis)};
	const Color_t clrGlowOuter = {clrBorder.r, clrBorder.g, clrBorder.b, static_cast<byte>(45.f * flVis)};
	const Color_t clrBorderDraw = {clrBorder.r, clrBorder.g, clrBorder.b, static_cast<byte>(nBorderA * flVis)};
	const Color_t clrTextGlow = {clrText.r, clrText.g, clrText.b, static_cast<byte>(70.f * flVis)};
	const Color_t clrTextDraw = {clrText.r, clrText.g, clrText.b, static_cast<byte>(nTextA * flVis)};

	H::Draw->Rect(nX, nY, nBoxW, nBoxH, clrBg);
	H::Draw->OutlinedRect(nX - 2, nY - 2, nBoxW + 4, nBoxH + 4, clrGlowOuter);
	H::Draw->OutlinedRect(nX, nY, nBoxW, nBoxH, clrBorderDraw);

	H::Draw->String(font, nCx, nY + nPadY - 1, clrTextGlow, POS_CENTERX, sText.c_str());
	H::Draw->String(font, nCx, nY + nPadY + 1, clrTextGlow, POS_CENTERX, sText.c_str());
	H::Draw->String(font, nCx, nY + nPadY, clrTextDraw, POS_CENTERX, sText.c_str());
}

void CMisc::VoiceCommandSpam()
{
	if (!CFG::Misc_Auto_VoiceCommand_Spam)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
		return;

	static float flLastVoiceTime = 0.0f;
	float flCurrentTime = static_cast<float>(Plat_FloatTime());
	if (flCurrentTime - flLastVoiceTime < 6.5f)
		return;

	flLastVoiceTime = flCurrentTime;

	switch (CFG::Misc_Auto_VoiceCommand_Spam_Command)
	{
	case 0: // Random
	{
		int iMenu = rand() % 3;
		int iCommand = rand() % 9;
		std::string sCmd = "voicemenu " + std::to_string(iMenu) + " " + std::to_string(iCommand);
		I::EngineClient->ClientCmd_Unrestricted(sCmd.c_str());
		break;
	}
	case 1:  I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 0"); break; // Medic
	case 2:  I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 1"); break; // Thanks
	case 3:  I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 6"); break; // Nice Shot
	case 4:  I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 2"); break; // Cheers
	case 5:  I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 3"); break; // Jeers
	case 6:  I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 2"); break; // Go Go Go
	case 7:  I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 3"); break; // Move Up
	case 8:  I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 4"); break; // Go Left
	case 9:  I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 5"); break; // Go Right
	case 10: I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 6"); break; // Yes
	case 11: I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 7"); break; // No
	case 12: I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 0"); break; // Incoming
	case 13: I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 1"); break; // Spy
	case 14: I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 2"); break; // Sentry Ahead
	case 15: I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 3"); break; // Need Teleporter
	case 16: I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 4"); break; // Pootis
	case 17: I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 5"); break; // Need Sentry
	case 18: I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 6"); break; // Activate Charge
	case 19: I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 0"); break; // Help
	case 20: I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 1"); break; // Battle Cry
	default: break;
	}
}
