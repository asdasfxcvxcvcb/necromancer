#include "AimbotProjectile.h"

#include "../../CFG.h"
#include "../../MovementSimulation/MovementSimulation.h"
#include "../../ProjectileSim/ProjectileSim.h"
#include "../../Players/Players.h"

void DrawProjPath(const CUserCmd* pCmd, float time)
{
	if (!pCmd || !G::bFiring)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
		return;

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon)
		return;

	ProjSimInfo info{};
	if (!F::ProjectileSim->GetInfo(pLocal, pWeapon, pCmd->viewangles, info))
		return;

	if (!F::ProjectileSim->Init(info))
		return;

	const auto& col = CFG::Color_Simulation_Projectile; // this method is so unorthodox but it works so whatever
	const int r = col.r, g = col.g, b = col.b; // thanks valve for making colors annoying

	std::vector<Vec3> vPath;
	vPath.reserve(TIME_TO_TICKS(time) + 1);

	vPath.push_back(F::ProjectileSim->GetOrigin());

	for (int n = 0; n < TIME_TO_TICKS(time); n++)
	{
		F::ProjectileSim->RunTick();
		vPath.push_back(F::ProjectileSim->GetOrigin());
	}

	if (CFG::Visuals_Draw_Predicted_Path_Style == 1)
	{
		for (size_t n = 1; n < vPath.size(); n++)
		{
			I::DebugOverlay->AddLineOverlay(vPath[n], vPath[n - 1], r, g, b, false, 10.0f);
		}
	}
	else if (CFG::Visuals_Draw_Predicted_Path_Style == 2)
	{
		for (size_t n = 1; n < vPath.size(); n++)
		{
			if (n % 2 == 0)
				continue;

			I::DebugOverlay->AddLineOverlay(vPath[n], vPath[n - 1], r, g, b, false, 10.0f);
		}
	}
	else if (CFG::Visuals_Draw_Predicted_Path_Style == 3)
	{
		for (size_t n = 1; n < vPath.size(); n++)
		{
			if (n != 1)
			{
				Vec3 right{};
				Math::AngleVectors(
					Math::CalcAngle(vPath[n], vPath[n - 1]),
					nullptr, &right, nullptr
				);

				const Vec3& start = vPath[n - 1];
				I::DebugOverlay->AddLineOverlay(start, start + right * 5.0f, r, g, b, false, 10.0f);
				I::DebugOverlay->AddLineOverlay(start, start - right * 5.0f, r, g, b, false, 10.0f);
			}

			I::DebugOverlay->AddLineOverlay(vPath[n], vPath[n - 1], r, g, b, false, 10.0f);
		}
	}
}

void DrawMovePath(const std::vector<Vec3>& vPath)
{
	const auto& col = CFG::Color_Simulation_Movement; // this method is so unorthodox
	const int r = col.r, g = col.g, b = col.b;

	// Line
	if (CFG::Visuals_Draw_Movement_Path_Style == 1)
	{
		for (size_t n = 1; n < vPath.size(); n++)
		{
			I::DebugOverlay->AddLineOverlay(vPath[n], vPath[n - 1], r, g, b, false, 10.0f);
		}
	}

	// Dashed
	else if (CFG::Visuals_Draw_Movement_Path_Style == 2)
	{
		for (size_t n = 1; n < vPath.size(); n++)
		{
			if (n % 2 == 0)
				continue;

			I::DebugOverlay->AddLineOverlay(vPath[n], vPath[n - 1], r, g, b, false, 10.0f);
		}
	}

	// Alternative
	else if (CFG::Visuals_Draw_Movement_Path_Style == 3)
	{
		for (size_t n = 1; n < vPath.size(); n++)
		{
			if (n != 1)
			{
				Vec3 right{};
				Math::AngleVectors(
					Math::CalcAngle(vPath[n], vPath[n - 1]),
					nullptr, &right, nullptr
				);

				const Vec3& start = vPath[n - 1];
				I::DebugOverlay->AddLineOverlay(start, start + right * 5.0f, r, g, b, false, 10.0f);
				I::DebugOverlay->AddLineOverlay(start, start - right * 5.0f, r, g, b, false, 10.0f);
			}

			I::DebugOverlay->AddLineOverlay(vPath[n], vPath[n - 1], r, g, b, false, 10.0f);
		}
	}
}

Vec3 GetOffsetShootPos(C_TFPlayer* local, C_TFWeaponBase* weapon, const CUserCmd* pCmd)
{
	auto out{ local->GetShootPos() };

	switch (weapon->GetWeaponID())
	{
	case TF_WEAPON_FLAREGUN:
	case TF_WEAPON_FLAREGUN_REVENGE:
	case TF_WEAPON_SYRINGEGUN_MEDIC:
	case TF_WEAPON_FLAME_BALL:
	case TF_WEAPON_CROSSBOW:
	case TF_WEAPON_FLAMETHROWER:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
	{
		Vec3 vOffset = { 23.5f, 12.0f, -3.0f };

		if (local->m_fFlags() & FL_DUCKING)
			vOffset.z = 8.0f;

		H::AimUtils->GetProjectileFireSetup(pCmd->viewangles, vOffset, &out);

		break;
	}

	case TF_WEAPON_COMPOUND_BOW:
	{
		Vec3 vOffset = { 20.5f, 12.0f, -3.0f };

		if (local->m_fFlags() & FL_DUCKING)
			vOffset.z = 8.0f;

		H::AimUtils->GetProjectileFireSetup(pCmd->viewangles, vOffset, &out);

		break;
	}

	default: break;
	}

	return out;
}

bool CAimbotProjectile::GetProjectileInfo(C_TFWeaponBase* pWeapon)
{
	m_CurProjInfo = {};

	auto curTime = [&]() -> float
		{
			if (const auto pLocal = H::Entities->GetLocal())
			{
				return static_cast<float>(pLocal->m_nTickBase()) * I::GlobalVars->interval_per_tick;
			}

			return I::GlobalVars->curtime;
		};

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_GRENADELAUNCHER:
	{
		m_CurProjInfo = { 1200.0f, 1.0f, true };
		m_CurProjInfo.Speed = SDKUtils::AttribHookValue(m_CurProjInfo.Speed, "mult_projectile_speed", pWeapon);

		break;
	}

	case TF_WEAPON_PIPEBOMBLAUNCHER:
	{
		const float flChargeBeginTime = pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime();
		const float flCharge = curTime() - flChargeBeginTime;

		if (flChargeBeginTime)
		{
			m_CurProjInfo.Speed = Math::RemapValClamped
			(
				flCharge,
				0.0f,
				SDKUtils::AttribHookValue(4.0f, "stickybomb_charge_rate", pWeapon),
				900.0f,
				2400.0f
			);
		}

		else
		{
			m_CurProjInfo.Speed = 900.0f;
		}

		m_CurProjInfo.GravityMod = 1.0f;
		m_CurProjInfo.Pipes = true;

		break;
	}

	case TF_WEAPON_CANNON:
	{
		m_CurProjInfo = { 1454.0f, 1.0f, true };
		break;
	}

	case TF_WEAPON_COMPOUND_BOW:
	{
		const float flChargeBeginTime = pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime();
		const float flCharge = curTime() - flChargeBeginTime;

		if (flChargeBeginTime)
		{
			m_CurProjInfo.Speed = 1800.0f + std::clamp<float>(flCharge, 0.0f, 1.0f) * 800.0f;
			m_CurProjInfo.GravityMod = Math::RemapValClamped(flCharge, 0.0f, 1.0f, 0.5f, 0.1f);
		}

		else
		{
			m_CurProjInfo.Speed = 1800.0f;
			m_CurProjInfo.GravityMod = 0.5f;
		}

		break;
	}

	case TF_WEAPON_CROSSBOW:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
	{
		m_CurProjInfo = { 2400.0f, 0.2f };
		break;
	}

	case TF_WEAPON_SYRINGEGUN_MEDIC:
	{
		m_CurProjInfo = { 1000.0f, 0.3f };
		break;
	}

	case TF_WEAPON_FLAREGUN:
	{
		m_CurProjInfo = { 2000.0f, 0.3f };
		break;
	}

	case TF_WEAPON_FLAREGUN_REVENGE:
	{
		m_CurProjInfo = { 3000.0f, 0.45f };
		break;
	}

	case TF_WEAPON_FLAME_BALL:
	{
		m_CurProjInfo = { 3000.0f, 0.0f };
		break;
	}

	case TF_WEAPON_FLAMETHROWER:
	{
		m_CurProjInfo = { 2000.0f, 0.0f };
		m_CurProjInfo.Flamethrower = true;
		break;
	}

	case TF_WEAPON_RAYGUN:
	case TF_WEAPON_DRG_POMSON:
	{
		m_CurProjInfo = { 1200.0f, 0.0f };

		break;
	}

	default: break;
	}

	return m_CurProjInfo.Speed > 0.0f;
}

// Helper to solve basic parabolic trajectory
// Low arc (- sqrt) is used as it's faster and more practical for gameplay
static bool SolveParabolic(const Vec3& vFrom, const Vec3& vTo, float flSpeed, float flGravity, float& flPitchOut, float& flYawOut, float& flTimeOut)
{
	const Vec3 v = vTo - vFrom;
	const float dx = v.x * v.x + v.y * v.y; // Length2D squared
	
	if (dx < 0.000001f)
		return false;

	const float dxSqrt = sqrtf(dx);
	const float dy = v.z;

	flYawOut = RAD2DEG(atan2f(v.y, v.x));

	if (flGravity > 0.001f)
	{
		const float v2 = flSpeed * flSpeed;
		const float v4 = v2 * v2;
		const float gDx2 = flGravity * dx;
		const float root = v4 - flGravity * (gDx2 + 2.0f * dy * v2);

		if (root < 0.0f)
			return false; // Target is out of range - no solution exists

		const float sqrtRoot = sqrtf(root);
		const float gDxSqrt = flGravity * dxSqrt;
		
		// Low arc solution (faster arrival time)
		flPitchOut = -RAD2DEG(atanf((v2 - sqrtRoot) / gDxSqrt));
		flTimeOut = dxSqrt / (cosf(DEG2RAD(flPitchOut)) * flSpeed);
	}
	else
	{
		const float dist = sqrtf(dx + v.z * v.z);
		flPitchOut = -RAD2DEG(atan2f(v.z, dxSqrt));
		flTimeOut = dist / flSpeed;
	}

	return true;
}

bool CAimbotProjectile::CalcProjAngle(const Vec3& vFrom, const Vec3& vTo, Vec3& vAngleOut, float& flTimeOut)
{
	const auto pLocal = H::Entities->GetLocal();
	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon || !pLocal)
		return false;

	float v0 = m_CurProjInfo.Speed;
	const float g = SDKUtils::GetGravity() * m_CurProjInfo.GravityMod;

	if (m_CurProjInfo.Pipes && v0 > k_flMaxVelocity)
		v0 = k_flMaxVelocity;

	// First pass: calculate from eye position
	float flPitch, flYaw, flTime;
	if (!SolveParabolic(vFrom, vTo, v0, g, flPitch, flYaw, flTime))
		return false;

	// For pipes, apply drag correction
	// thanks kal you ass for making me figure this out
	if (m_CurProjInfo.Pipes)
	{
		float magic = 0.0f; // yes its called magic, no i dont know why it works

		if (pWeapon->GetWeaponID() == TF_WEAPON_GRENADELAUNCHER)
			magic = (pWeapon->m_iItemDefinitionIndex() == Demoman_m_TheLochnLoad) ? 0.07f : 0.11f; // lochnload is special
		else if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
			magic = 0.16f; // stickies are weird
		else if (pWeapon->GetWeaponID() == TF_WEAPON_CANNON)
			magic = 0.35f; // loose cannon is extra weird

		float v0_adjusted = v0 - (v0 * flTime) * magic;

		if (!SolveParabolic(vFrom, vTo, v0_adjusted, g, flPitch, flYaw, flTime))
			return false;
	}

	// Second pass for projectiles with spawn offset (huntsman, crossbow, flares, etc.)
	// This corrects for the fact that projectiles spawn from a different position than the eye
	// i hate this shit but it makes huntsman actually hit at long range so whatever
	const int nWeaponID = pWeapon->GetWeaponID();
	const bool bNeedsOffsetCorrection = (nWeaponID == TF_WEAPON_COMPOUND_BOW ||
		nWeaponID == TF_WEAPON_CROSSBOW ||
		nWeaponID == TF_WEAPON_SHOTGUN_BUILDING_RESCUE ||
		nWeaponID == TF_WEAPON_FLAREGUN ||
		nWeaponID == TF_WEAPON_FLAREGUN_REVENGE ||
		nWeaponID == TF_WEAPON_SYRINGEGUN_MEDIC ||
		nWeaponID == TF_WEAPON_FLAME_BALL);

	if (bNeedsOffsetCorrection)
	{
		// Get the projectile spawn offset
		Vec3 vOffset;
		if (nWeaponID == TF_WEAPON_COMPOUND_BOW)
			vOffset = { 23.5f, 8.0f, -3.0f };
		else if (nWeaponID == TF_WEAPON_CROSSBOW || nWeaponID == TF_WEAPON_SHOTGUN_BUILDING_RESCUE)
			vOffset = { 23.5f, 8.0f, -3.0f };
		else
			vOffset = { 23.5f, 12.0f, (pLocal->m_fFlags() & FL_DUCKING) ? 8.0f : -3.0f };

		// Calculate actual projectile spawn position using first-pass angles
		Vec3 vFirstPassAngle = { flPitch, flYaw, 0.0f };
		Vec3 vForward, vRight, vUp;
		Math::AngleVectors(vFirstPassAngle, &vForward, &vRight, &vUp);

		Vec3 vProjSpawn = vFrom + (vForward * vOffset.x) + (vRight * vOffset.y) + (vUp * vOffset.z);

		// Recalculate trajectory from actual spawn position
		float flPitch2, flYaw2, flTime2;
		if (!SolveParabolic(vProjSpawn, vTo, v0, g, flPitch2, flYaw2, flTime2))
			return false;

		// Apply pitch correction
		if (g > 0.001f)
		{
			// For gravity-affected projectiles, adjust pitch based on the difference
			float flPitchCorrection = flPitch2 - flPitch;
			flPitch += flPitchCorrection;
		}
		else
		{
			// For non-gravity projectiles, use the recalculated pitch directly
			flPitch = flPitch2;
		}

		// Apply yaw correction using quadratic solution (like Amalgam)
		// This accounts for the lateral offset of the projectile spawn point
		// quadratic formula from high school finally being useful lmao
		Vec3 vShootPos = (vProjSpawn - vFrom).To2D();
		Vec3 vTarget = (vTo - vFrom).To2D();
		Vec3 vForward2D = vForward.To2D();
		float flForwardLen = vForward2D.Length2D();
		if (flForwardLen > 0.001f)
		{
			vForward2D = vForward2D / flForwardLen;
			
			// Solve quadratic: |vShootPos + t*vForward2D|^2 = |vTarget|^2
			// This finds where along the forward direction we need to aim to hit the target distance
			float flA = 1.0f;
			float flB = 2.0f * (vShootPos.x * vForward2D.x + vShootPos.y * vForward2D.y);
			float flC = vShootPos.Length2DSqr() - vTarget.Length2DSqr();
			
			float flDiscriminant = flB * flB - 4.0f * flA * flC;
			if (flDiscriminant >= 0.0f)
			{
				float flT = (-flB + sqrtf(flDiscriminant)) / (2.0f * flA);
				if (flT > 0.0f)
				{
					Vec3 vAdjusted = vShootPos + vForward2D * flT;
					float flNewYaw = RAD2DEG(atan2f(vAdjusted.y, vAdjusted.x));
					// Apply the yaw correction as the difference
					flYaw = flYaw - (flNewYaw - flYaw);
				}
			}
		}

		flTime = flTime2;
	}

	vAngleOut = { flPitch, flYaw, 0.0f };
	flTimeOut = flTime;

	// Time limit checks
	if (m_CurProjInfo.Pipes)
	{
		if (nWeaponID == TF_WEAPON_CANNON && flTimeOut > 0.95f)
			return false;
		else if (pWeapon->m_iItemDefinitionIndex() == Demoman_m_TheIronBomber && flTimeOut > 1.4f)
			return false;
		else if (flTimeOut > 2.0f)
			return false;
	}

	if ((nWeaponID == TF_WEAPON_FLAME_BALL || nWeaponID == TF_WEAPON_FLAMETHROWER) && flTimeOut > 0.18f)
		return false;

	return true;
}

void CAimbotProjectile::OffsetPlayerPosition(C_TFWeaponBase* pWeapon, Vec3& vPos, C_TFPlayer* pPlayer, bool bDucked, bool bOnGround, const Vec3& vLocalPos)
{
	const Vec3 vMins = pPlayer->m_vecMins();
	const Vec3 vMaxs = pPlayer->m_vecMaxs();
	const float flMaxZ{ (bDucked ? 62.0f : 82.0f) * pPlayer->m_flModelScale() };

	// Helper lambda for huntsman advanced head aim with lerp
	// this is cursed but it works, dont question it
	auto ApplyHuntsmanHeadAim = [&]() -> Vec3
	{
		Vec3 vOffset = {};
		
		if (!CFG::Aimbot_Projectile_Advanced_Head_Aim)
		{
			// Simple head aim - just use top of bbox
			vOffset.z = flMaxZ * 0.92f;
			return vOffset;
		}

		// Get head hitbox position relative to player origin
		const Vec3 vHeadPos = pPlayer->GetHitboxPos(HITBOX_HEAD);
		Vec3 vHeadOffset = vHeadPos - pPlayer->m_vecOrigin();

		// Calculate "low" factor - how much the target is above the shooter
		// This affects how much we lerp towards the top of the bbox
		float flLow = 0.0f;
		Vec3 vTargetEye = vPos + Vec3(0, 0, flMaxZ * 0.85f); // Approximate target eye position
		Vec3 vDelta = vTargetEye - vLocalPos;
		
		if (vDelta.z > 0)
		{
			float flXY = vDelta.Length2D();
			if (flXY > 0.0f)
				flLow = Math::RemapValClamped(vDelta.z / flXY, 0.0f, 0.5f, 0.0f, 1.0f);
			else
				flLow = 1.0f;
		}

		// Interpolate lerp and add values based on low factor
		float flLerp = (CFG::Aimbot_Projectile_Huntsman_Lerp + 
			(CFG::Aimbot_Projectile_Huntsman_Lerp_Low - CFG::Aimbot_Projectile_Huntsman_Lerp) * flLow) / 100.0f;
		float flAdd = CFG::Aimbot_Projectile_Huntsman_Add + 
			(CFG::Aimbot_Projectile_Huntsman_Add_Low - CFG::Aimbot_Projectile_Huntsman_Add) * flLow;

		// Apply add offset and lerp towards top of bbox
		vHeadOffset.z += flAdd;
		vHeadOffset.z = vHeadOffset.z + (vMaxs.z - vHeadOffset.z) * flLerp;

		// Clamp to stay within bbox bounds
		const float flClamp = CFG::Aimbot_Projectile_Huntsman_Clamp;
		vHeadOffset.x = std::clamp(vHeadOffset.x, vMins.x + flClamp, vMaxs.x - flClamp);
		vHeadOffset.y = std::clamp(vHeadOffset.y, vMins.y + flClamp, vMaxs.y - flClamp);
		vHeadOffset.z = std::clamp(vHeadOffset.z, vMins.z + flClamp, vMaxs.z - flClamp);

		return vHeadOffset;
	};

	switch (CFG::Aimbot_Projectile_Aim_Position)
	{
		// Feet
	case 0:
	{
		vPos.z += (flMaxZ * 0.2f);
		m_LastAimPos = 0;
		break;
	}

	// Body
	case 1:
	{
		vPos.z += (flMaxZ * 0.5f);
		m_LastAimPos = 1;
		break;
	}

	// Head
	case 2:
	{
		if (pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW)
		{
			// Use huntsman lerp for compound bow
			Vec3 vOffset = ApplyHuntsmanHeadAim();
			vPos.x += vOffset.x;
			vPos.y += vOffset.y;
			vPos.z += vOffset.z;
		}
		else if (CFG::Aimbot_Projectile_Advanced_Head_Aim)
		{
			const Vec3 vDelta = pPlayer->GetHitboxPos(HITBOX_HEAD) - pPlayer->m_vecOrigin();
			vPos.x += vDelta.x;
			vPos.y += vDelta.y;
			vPos.z += (flMaxZ * 0.85f);
		}
		else
		{
			vPos.z += (flMaxZ * 0.85f);
		}
		m_LastAimPos = 2;
		break;
	}

	// Auto
	case 3:
	{
		if (pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW)
		{
			// Use huntsman lerp for compound bow
			Vec3 vOffset = ApplyHuntsmanHeadAim();
			vPos.x += vOffset.x;
			vPos.y += vOffset.y;
			vPos.z += vOffset.z;
			m_LastAimPos = 2;
		}

		else
		{
			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_GRENADELAUNCHER:
			case TF_WEAPON_CANNON:
			{
				vPos.z += (flMaxZ * 0.5f);
				m_LastAimPos = 1;
				break;
			}
			case TF_WEAPON_PIPEBOMBLAUNCHER:
			{
				vPos.z += (flMaxZ * 0.1f);
				m_LastAimPos = 0;
				break;
			}

			default:
			{
				vPos.z += (flMaxZ * 0.5f);
				m_LastAimPos = 1;
				break;
			}
			}
		}

		break;
	}

	default: break;
	}
}

bool CAimbotProjectile::CanArcReach(const Vec3& vFrom, const Vec3& vTo, const Vec3& vAngleTo, float flTargetTime, C_BaseEntity* pTarget)
{
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
	{
		return false;
	}

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon)
	{
		return false;
	}

	ProjSimInfo info{};
	if (!F::ProjectileSim->GetInfo(pLocal, pWeapon, vAngleTo, info))
	{
		return false;
	}

	if (pWeapon->m_iItemDefinitionIndex() == Demoman_m_TheLochnLoad)
	{
		info.m_speed += 45.0f; // we need this for some fucking reason, thanks valve
	}

	if (!F::ProjectileSim->Init(info, true))
	{
		return false;
	}

	CTraceFilterWorldCustom filter{};
	filter.m_pTarget = pTarget;

	//I::DebugOverlay->ClearAllOverlays();

	for (auto n = 0; n < TIME_TO_TICKS(flTargetTime * 1.2f); n++)
	{
		auto pre{ F::ProjectileSim->GetOrigin() };

		F::ProjectileSim->RunTick();

		auto post{ F::ProjectileSim->GetOrigin() };

		trace_t trace{};

		Vec3 mins{ -6.0f, -6.0f, -6.0f };
		Vec3 maxs{ 6.0f, 6.0f, 6.0f };

		switch (info.m_type)
		{
		case TF_PROJECTILE_PIPEBOMB:
		case TF_PROJECTILE_PIPEBOMB_REMOTE:
		case TF_PROJECTILE_PIPEBOMB_PRACTICE:
		case TF_PROJECTILE_CANNONBALL:
		{
			mins = { -8.0f, -8.0f, -8.0f };
			maxs = { 8.0f, 8.0f, 20.0f };

			break;
		}

		case TF_PROJECTILE_FLARE:
		{
			mins = { -8.0f, -8.0f, -8.0f };
			maxs = { 8.0f, 8.0f, 8.0f };

			break;
		}

		default:
		{
			break;
		}
		}

		H::AimUtils->TraceHull(pre, post, mins, maxs, MASK_SOLID, &filter, &trace);

		if (trace.m_pEnt == pTarget)
		{
			return true;
		}

		if (trace.DidHit())
		{
			// If we hit something past the target, we're good
			if (info.m_pos.DistTo(trace.endpos) > info.m_pos.DistTo(vTo))
			{
				return true;
			}

			// If we hit something close enough to target, check if we can splash them
			if (trace.endpos.DistTo(vTo) > 100.0f) // Increased from 40 to 100 for splash radius
			{
				return false;
			}

			H::AimUtils->Trace(trace.endpos, vTo, MASK_SOLID, &filter, &trace);

			return !trace.DidHit() || trace.m_pEnt == pTarget;
		}

		//I::DebugOverlay->AddBoxOverlay(post, mins, maxs, Math::CalcAngle(pre, post), 255, 255, 255, 2, 60.0f);
	}

	return true;
}

bool CAimbotProjectile::CanSee(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const Vec3& vFrom, const Vec3& vTo, const ProjTarget_t& target, float flTargetTime)
{
	Vec3 vLocalPos = vFrom;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_FLAREGUN:
	case TF_WEAPON_FLAREGUN_REVENGE:
	case TF_WEAPON_SYRINGEGUN_MEDIC:
	case TF_WEAPON_FLAME_BALL:
	case TF_WEAPON_CROSSBOW:
	case TF_WEAPON_FLAMETHROWER:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
	{
		Vec3 vOffset = { 23.5f, 12.0f, -3.0f };

		if (pLocal->m_fFlags() & FL_DUCKING)
			vOffset.z = 8.0f;

		H::AimUtils->GetProjectileFireSetup(target.AngleTo, vOffset, &vLocalPos);

		break;
	}

	case TF_WEAPON_COMPOUND_BOW:
	{
		Vec3 vOffset = { 20.5f, 12.0f, -3.0f };

		if (pLocal->m_fFlags() & FL_DUCKING)
			vOffset.z = 8.0f;

		H::AimUtils->GetProjectileFireSetup(target.AngleTo, vOffset, &vLocalPos);

		break;
	}

	default: break;
	}

	if (m_CurProjInfo.GravityMod != 0.f)
	{
		return CanArcReach(vFrom, vTo, target.AngleTo, flTargetTime, target.Entity);
	}

	if (m_CurProjInfo.Flamethrower)
	{
		return H::AimUtils->TraceFlames(target.Entity, vLocalPos, vTo);
	}
	return H::AimUtils->TraceProjectile(target.Entity, vLocalPos, vTo);
}

bool CAimbotProjectile::SolveTarget(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const CUserCmd* pCmd, ProjTarget_t& target)
{
	Vec3 vLocalPos = pLocal->GetShootPos();

	if (m_CurProjInfo.Pipes)
	{
		const Vec3 vOffset = { 16.0f, 8.0f, -6.0f };
		H::AimUtils->GetProjectileFireSetup(pCmd->viewangles, vOffset, &vLocalPos);
	}

	m_TargetPath.clear();

	if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
	{
		const auto pPlayer = target.Entity->As<C_TFPlayer>();

		const bool bDucked = pPlayer->m_fFlags() & FL_DUCKING;
		const bool bOnGround = pPlayer->m_fFlags() & FL_ONGROUND;

		if (!F::MovementSimulation->Initialize(pPlayer))
			return false;

		// Pre-calculate values outside the loop
		const int nMaxSimTicks = TIME_TO_TICKS(CFG::Aimbot_Projectile_Max_Simulation_Time);
		const float flLatency = SDKUtils::GetLatency();
		const bool bIsSticky = pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER;
		const float flStickyArmTime = bIsSticky ? SDKUtils::AttribHookValue(0.8f, "sticky_arm_time", pLocal) : 0.0f;

		for (int nTick = 0; nTick < nMaxSimTicks; nTick++)
		{
			m_TargetPath.push_back(F::MovementSimulation->GetOrigin());

			F::MovementSimulation->RunTick(TICKS_TO_TIME(nTick));

			Vec3 vTarget = F::MovementSimulation->GetOrigin();

			OffsetPlayerPosition(pWeapon, vTarget, pPlayer, bDucked, bOnGround, vLocalPos);

			float flTimeToTarget = 0.0f;

			if (!CalcProjAngle(vLocalPos, vTarget, target.AngleTo, flTimeToTarget))
				continue;

			target.TimeToTarget = flTimeToTarget;

			int nTargetTick = TIME_TO_TICKS(flTimeToTarget + flLatency);

			// Use pre-calculated sticky arm time
			if (bIsSticky)
			{
				if (TICKS_TO_TIME(nTargetTick) < flStickyArmTime)
				{
					nTargetTick += TIME_TO_TICKS(fabsf(flTimeToTarget - flStickyArmTime));
				}
			}

			if ((nTargetTick == nTick || nTargetTick == nTick - 1))
			{
				// Helper to get splash radius for weapon
				// Only sticky launcher and flare guns use splashbot
				auto getSplashRadius = [&]() -> float
					{
						float flRadius = 0.0f;
						switch (pWeapon->GetWeaponID())
						{
						case TF_WEAPON_PIPEBOMBLAUNCHER:
							flRadius = 146.0f;
							break;
						case TF_WEAPON_FLAREGUN:
						case TF_WEAPON_FLAREGUN_REVENGE:
							flRadius = 110.0f;
							break;
						default:
							return 0.0f;
						}
						// Apply attribute modifiers
						flRadius = SDKUtils::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
						return flRadius;
					};

				auto runSplash = [&]()
					{
						// Get splash radius for this weapon
						const float flSplashRadius = getSplashRadius();
						if (flSplashRadius <= 0.0f)
							return false;

						// Use slightly smaller radius for stability (146 can be unstable)
						const float radius = std::min(flSplashRadius - 16.0f, 130.0f);

						Vec3 mins{ target.Entity->m_vecMins() };
						Vec3 maxs{ target.Entity->m_vecMaxs() };

						auto center{ F::MovementSimulation->GetOrigin() + Vec3(0.0f, 0.0f, (mins.z + maxs.z) * 0.5f) };

						// 45 points with rotation each tick for better coverage
						constexpr int numPoints = 45;
						constexpr float goldenAngle = 2.39996323f; // PI * (3 - sqrt(5))
						
						// Rotate sphere each tick using tick count - spreads points differently each frame
						const float flRotation = static_cast<float>(I::GlobalVars->tickcount % 360) * DEG2RAD(1.0f);
						
						std::vector<Vec3> potential{};
						potential.reserve(numPoints);
						
						CTraceFilterWorldCustom filter{};
						
						for (int n = 0; n < numPoints; n++)
						{
							const float t = static_cast<float>(n) / static_cast<float>(numPoints);
							const float a1 = acosf(1.0f - 2.0f * t);
							const float a2 = goldenAngle * static_cast<float>(n) + flRotation; // Add rotation offset
							
							const float sinA1 = sinf(a1);
							auto point = center + Vec3{ sinA1 * cosf(a2), sinA1 * sinf(a2), cosf(a1) }.Scale(radius);

							trace_t trace{};
							H::AimUtils->Trace(center, point, MASK_SOLID, &filter, &trace);

							if (trace.fraction > 0.99f)
								continue;

							potential.push_back(trace.endpos);
						}

						std::ranges::sort(potential, [&](const Vec3& a, const Vec3& b)
							{
								return a.DistTo(F::MovementSimulation->GetOrigin()) < b.DistTo(F::MovementSimulation->GetOrigin());
							});

						for (auto& point : potential)
						{
							if (!CalcProjAngle(vLocalPos, point, target.AngleTo, target.TimeToTarget))
							{
								continue;
							}

							trace_t trace = {};
							CTraceFilterWorldCustom filter = {};
							trace_t grateTrace{};
							CTraceFilterWorldCustom grateFilter{};
							H::AimUtils->Trace(trace.endpos, point, CONTENTS_GRATE, &grateFilter, &grateTrace);

							Vec3 hull_min = { -8.0f, -8.0f, -8.0f };
							Vec3 hull_max = { 8.0f,  8.0f,  8.0f };

							H::AimUtils->TraceHull
							(
								GetOffsetShootPos(pLocal, pWeapon, pCmd),
								point,
								hull_min,
								hull_max,
								MASK_SOLID,
								&filter,
								&trace
							);

							// For gravity-affected projectiles (pipes, stickies, etc), verify arc can reach
							if (m_CurProjInfo.GravityMod > 0.0f)
							{
								if (!CanArcReach(vLocalPos, point, target.AngleTo, target.TimeToTarget, target.Entity))
									continue;
							}

							H::AimUtils->Trace(trace.endpos, point, MASK_SOLID, &filter, &trace);

							if (grateTrace.fraction < 1.0f)
							{
								return true;
							}

							if (trace.fraction < 1.0f)
							{
								continue;
							}

							return true;
						}

						return false;
					};

				if (CFG::Aimbot_Projectile_Rocket_Splash == 2 && runSplash())
				{
					F::MovementSimulation->Restore();

					return true;
				}

				if (CanSee(pLocal, pWeapon, vLocalPos, vTarget, target, flTimeToTarget))
				{
					F::MovementSimulation->Restore();

					return true;
				}

				if (CFG::Aimbot_Projectile_BBOX_Multipoint && pWeapon->GetWeaponID() != TF_WEAPON_COMPOUND_BOW)
				{
					const int nOld = CFG::Aimbot_Projectile_Aim_Position;

					for (int n = 0; n < 3; n++)
					{
						if (n == m_LastAimPos)
							continue;

						CFG::Aimbot_Projectile_Aim_Position = n;

						Vec3 vTargetMp = F::MovementSimulation->GetOrigin();

						OffsetPlayerPosition(pWeapon, vTargetMp, pPlayer, bDucked, bOnGround, vLocalPos);

						CFG::Aimbot_Projectile_Aim_Position = nOld;

						if (CalcProjAngle(vLocalPos, vTargetMp, target.AngleTo, target.TimeToTarget))
						{
							if (CanSee(pLocal, pWeapon, vLocalPos, vTargetMp, target, target.TimeToTarget))
							{
								F::MovementSimulation->Restore();

								return true;
							}
						}
					}
				}

				if (CFG::Aimbot_Projectile_Rocket_Splash == 1 && runSplash())
				{
					F::MovementSimulation->Restore();

					return true;
				}
			}
		}

		F::MovementSimulation->Restore();
	}

	else
	{
		const Vec3 vTarget = target.Position;

		float flTimeToTarget = 0.0f;

		// Helper to get splash radius for weapon (same as player version)
		// Only sticky launcher and flare guns use splashbot
		auto getSplashRadius = [&]() -> float
			{
				float flRadius = 0.0f;
				switch (pWeapon->GetWeaponID())
				{
				case TF_WEAPON_PIPEBOMBLAUNCHER:
					flRadius = 146.0f;
					break;
				case TF_WEAPON_FLAREGUN:
				case TF_WEAPON_FLAREGUN_REVENGE:
					flRadius = 110.0f;
					break;
				default:
					return 0.0f;
				}
				flRadius = SDKUtils::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
				return flRadius;
			};

		auto runSplash = [&]()
			{
				const float flSplashRadius = getSplashRadius();
				if (flSplashRadius <= 0.0f)
					return false;

				const float radius = std::min(flSplashRadius - 16.0f, 130.0f);

				const auto center{ target.Entity->GetCenter() };

				// 45 points with rotation each tick for better coverage
				constexpr auto numPoints{ 45 };
				constexpr float goldenAngle = 2.39996323f; // PI * (3 - sqrt(5))
				
				// Rotate sphere each tick
				const float flRotation = static_cast<float>(I::GlobalVars->tickcount % 360) * DEG2RAD(1.0f);

				std::vector<Vec3> potential{};
				potential.reserve(numPoints);
				
				CTraceFilterWorldCustom filter{};

				for (int n = 0; n < numPoints; n++)
				{
					const float t = static_cast<float>(n) / static_cast<float>(numPoints);
					const float a1 = acosf(1.0f - 2.0f * t);
					const float a2 = goldenAngle * static_cast<float>(n) + flRotation;
					
					const float sinA1 = sinf(a1);
					auto point = center + Vec3{ sinA1 * cosf(a2), sinA1 * sinf(a2), cosf(a1) }.Scale(radius);

					trace_t trace{};
					H::AimUtils->Trace(center, point, MASK_SOLID, &filter, &trace);

					if (trace.fraction > 0.99f)
						continue;

					potential.push_back(trace.endpos);
				}

				std::ranges::sort(potential, [&](const Vec3& a, const Vec3& b)
					{
						return a.DistTo(center) < b.DistTo(center);
					});

				for (auto& point : potential)
				{
					if (!CalcProjAngle(vLocalPos, point, target.AngleTo, target.TimeToTarget))
					{
						continue;
					}

					trace_t trace = {};
					CTraceFilterWorldCustom filter = {};
					trace_t grateTrace{};
					CTraceFilterWorldCustom grateFilter{};
					H::AimUtils->Trace(trace.endpos, point, CONTENTS_GRATE, &grateFilter, &grateTrace);

					Vec3 hull_min = { -8.0f, -8.0f, -8.0f };
					Vec3 hull_max = { 8.0f,  8.0f,  8.0f };

					H::AimUtils->TraceHull
					(
						GetOffsetShootPos(pLocal, pWeapon, pCmd),
						point,
						hull_min,
						hull_max,
						MASK_SOLID,
						&filter,
						&trace
					);

					// For gravity-affected projectiles, verify arc can reach
					if (m_CurProjInfo.GravityMod > 0.0f)
					{
						if (!CanArcReach(vLocalPos, point, target.AngleTo, target.TimeToTarget, target.Entity))
							continue;
					}

					H::AimUtils->Trace(trace.endpos, point, MASK_SOLID, &filter, &trace);

					if (grateTrace.fraction < 1.0f)
					{
						return true;
					}

					if (trace.fraction < 1.0f)
					{
						continue;
					}

					return true;
				}

				return false;
			};

		if (!CalcProjAngle(vLocalPos, vTarget, target.AngleTo, flTimeToTarget))
			return false;

		target.TimeToTarget = flTimeToTarget;

		int nTargetTick = TIME_TO_TICKS(flTimeToTarget + SDKUtils::GetLatency());

		if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
		{
			nTargetTick += TIME_TO_TICKS(fabsf(flTimeToTarget - SDKUtils::AttribHookValue(0.8f, "sticky_arm_time", pLocal)));
		}

		if (nTargetTick <= TIME_TO_TICKS(CFG::Aimbot_Projectile_Max_Simulation_Time))
		{
			if (CanSee(pLocal, pWeapon, vLocalPos, vTarget, target, flTimeToTarget))
			{
				return true;
			}
			if (CFG::Aimbot_Projectile_Rocket_Splash && runSplash())
			{
				return true;
			}
		}
	}

	m_TargetPath.clear();
	return false;
}

bool CAimbotProjectile::GetTarget(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const CUserCmd* pCmd, ProjTarget_t& outTarget)
{
	const Vec3 vLocalPos = pLocal->GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	m_vecTargets.clear();

	if (CFG::Aimbot_Target_Players)
	{
		const auto nGroup = pWeapon->GetWeaponID() == TF_WEAPON_CROSSBOW ? EEntGroup::PLAYERS_ALL : EEntGroup::PLAYERS_ENEMIES;

		for (const auto pEntity : H::Entities->GetGroup(nGroup))
		{
			if (!pEntity || pEntity == pLocal)
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
			}

			else
			{
				if (pWeapon->GetWeaponID() == TF_WEAPON_CROSSBOW)
				{
					if (pPlayer->m_iHealth() >= pPlayer->GetMaxHealth() || pPlayer->IsInvulnerable())
					{
						continue;
					}
				}
			}

			Vec3 vPos = pPlayer->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = CFG::Aimbot_Projectile_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
			const float flDistTo = vLocalPos.DistTo(vPos);

			if (CFG::Aimbot_Projectile_Sort == 0 && flFOVTo > CFG::Aimbot_Projectile_FOV)
				continue;

			m_vecTargets.emplace_back(AimTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	if (CFG::Aimbot_Target_Buildings)
	{
		const auto isRescueRanger{ pWeapon->GetWeaponID() == TF_WEAPON_SHOTGUN_BUILDING_RESCUE };

		for (const auto pEntity : H::Entities->GetGroup(isRescueRanger ? EEntGroup::BUILDINGS_ALL : EEntGroup::BUILDINGS_ENEMIES))
		{
			if (!pEntity)
				continue;

			const auto pBuilding = pEntity->As<C_BaseObject>();

			if (pBuilding->m_bPlacing())
				continue;

			if (isRescueRanger && pBuilding->m_iTeamNum() == pLocal->m_iTeamNum() && pBuilding->m_iHealth() >= pBuilding->m_iMaxHealth())
			{
				continue;
			}

			Vec3 vPos = pBuilding->GetCenter(); // fuck teleporters when aimed at with pipes lmao

			/*if (pEntity->GetClassId() == ETFClassIds::CObjectTeleporter || pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
			{
				vPos = pBuilding->m_vecOrigin();
			}*/

			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = CFG::Aimbot_Projectile_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
			const float flDistTo = vLocalPos.DistTo(vPos);

			if (CFG::Aimbot_Projectile_Sort == 0 && flFOVTo > CFG::Aimbot_Projectile_FOV)
				continue;

			m_vecTargets.emplace_back(AimTarget_t{ pBuilding, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	if (m_vecTargets.empty())
		return false;

	// Sort by target priority
	F::AimbotCommon->Sort(m_vecTargets, CFG::Aimbot_Projectile_Sort);

	const auto maxTargets{ std::min(CFG::Aimbot_Projectile_Max_Processing_Targets, static_cast<int>(m_vecTargets.size())) };
	auto targetsScanned{ 0 };

	for (auto& target : m_vecTargets)
	{
		if (targetsScanned >= maxTargets)
			break;

		targetsScanned++;

		if (!SolveTarget(pLocal, pWeapon, pCmd, target))
			continue;

		if (CFG::Aimbot_Projectile_Sort == 0 && Math::CalcFov(vLocalAngles, target.AngleTo) > CFG::Aimbot_Projectile_FOV)
			continue;

		outTarget = target;
		return true;
	}

	return false;
}

bool CAimbotProjectile::ShouldAim(const CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	return CFG::Aimbot_Projectile_Aim_Type != 1 || IsFiring(pCmd, pLocal, pWeapon) && pWeapon->HasPrimaryAmmoForShot();
}

void CAimbotProjectile::Aim(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const Vec3& vAngles)
{
	Vec3 vAngleTo = vAngles - pLocal->m_vecPunchAngle();

	if (m_CurProjInfo.Pipes)
	{
		// pipes need special angle adjustment because source engine moment
		Vec3 vAngle = {}, vForward = {}, vUp = {};
		Math::AngleVectors(vAngleTo, &vForward, nullptr, &vUp);
		const Vec3 vVelocity = (vForward * m_CurProjInfo.Speed) - (vUp * 200.0f); // the 200 is hardcoded in tf2, thanks valve
		Math::VectorAngles(vVelocity, vAngle);
		vAngleTo.x = vAngle.x;
	}

	Math::ClampAngles(vAngleTo);

	switch (CFG::Aimbot_Projectile_Aim_Type)
	{
	case 0:
	{
		pCmd->viewangles = vAngleTo;
		break;
	}

	case 1:
	{
		// For pSilent, we need to apply angles when:
		// 1. Flamethrower (always)
		// 2. Normal weapons when G::bCanPrimaryAttack is true
		// 3. Stickies/pipes when releasing charge (G::bFiring is set by IsFiring check)
		const bool bShouldApply = m_CurProjInfo.Flamethrower || G::bCanPrimaryAttack || (m_CurProjInfo.Pipes && G::bFiring);
		
		if (bShouldApply)
		{
			H::AimUtils->FixMovement(pCmd, vAngleTo);

			pCmd->viewangles = vAngleTo;

			if (m_CurProjInfo.Flamethrower)
			{
				G::bSilentAngles = true;
			}

			else
			{
				G::bPSilentAngles = true;
			}
		}

		break;
	}

	default: break;
	}
}

bool CAimbotProjectile::ShouldFire(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (!CFG::Aimbot_AutoShoot)
	{
		// fucking fuck this edge case
		if (pWeapon->GetWeaponID() == TF_WEAPON_FLAME_BALL && pLocal->m_flTankPressure() < 100.0f)
			pCmd->buttons &= ~IN_ATTACK;

		return false;
	}

	return true;
}

void CAimbotProjectile::CancelCharge(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	const int nWeaponID = pWeapon->GetWeaponID();
	
	if (nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || nWeaponID == TF_WEAPON_CANNON)
	{
		// Stickies/Cannon need weapon switch to cancel
		// Only check first 8 slots (primary, secondary, melee, pda, pda2, building, action, misc)
		for (int i = 0; i < 8; i++)
		{
			auto pSwap = pLocal->GetWeaponFromSlot(i);
			if (!pSwap || pSwap == pWeapon)
				continue;

			pCmd->weaponselect = pSwap->entindex();
			m_iCancelWeaponIdx = pWeapon->entindex();
			break;
		}
	}
	
	m_bChargePending = false;
	m_vChargeAngles = {};
}

void CAimbotProjectile::HandleFire(CUserCmd* pCmd, C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal, const ProjTarget_t& target)
{
	const bool bIsBazooka = pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka;
	if (!bIsBazooka && !pWeapon->HasPrimaryAmmoForShot())
		return;

	// Don't fire if we don't have a valid angle calculation
	if (target.AngleTo.IsZero())
		return;

	const int nWeaponID = pWeapon->GetWeaponID();
	if (nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		const float flChargeBeginTime = pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime();
		
		if (flChargeBeginTime > 0.0f)
		{
			// Already charging - release to fire
			// Save angles on the tick we release so pSilent uses correct angles
			m_vChargeAngles = target.AngleTo;
			m_bChargePending = false;
			pCmd->buttons &= ~IN_ATTACK;
		}
		else
		{
			// Not charging - start charging and save angles for next tick
			m_vChargeAngles = target.AngleTo;
			m_bChargePending = true;
			pCmd->buttons |= IN_ATTACK;
		}
	}

	else if (nWeaponID == TF_WEAPON_COMPOUND_BOW)
	{
		// Huntsman - simple charge/release, no cancel logic
		if (pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() > 0.0f)
			pCmd->buttons &= ~IN_ATTACK;
		else
			pCmd->buttons |= IN_ATTACK;
	}

	else if (nWeaponID == TF_WEAPON_CANNON)
	{
		const float flDetonateTime = pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime();
		
		if (CFG::Aimbot_Projectile_Auto_Double_Donk)
		{
			// Amalgam-style improved auto double donk
			// Uses GRENADE_CHECK_INTERVAL (0.195f) for proper grenade physics timing
			constexpr float GRENADE_CHECK_INTERVAL = 0.195f;
			
			const float flTimeToTarget = target.TimeToTarget;
			
			// Calculate network desync compensation (similar to Amalgam's GetDesync)
			const float flServerTime = TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick);
			const float flDesync = I::GlobalVars->curtime - flServerTime - SDKUtils::GetLatency();
			
			if (flDetonateTime > 0.0f)
			{
				// Cannonball is already charging - calculate remaining fuse time
				float flFuseRemaining = flDetonateTime - I::GlobalVars->curtime;
				
				// Quantize to grenade check intervals like Amalgam does for accuracy
				flFuseRemaining = floorf(flFuseRemaining / GRENADE_CHECK_INTERVAL) * GRENADE_CHECK_INTERVAL + flDesync;
				
				// Time needed for projectile to reach target (subtract one interval for timing)
				const float flTimeNeeded = flTimeToTarget - GRENADE_CHECK_INTERVAL;
				
				// If fuse will expire before reaching target, keep charging
				// If fuse time is sufficient, release to fire
				if (flFuseRemaining >= flTimeNeeded && flFuseRemaining > GRENADE_CHECK_INTERVAL)
				{
					// Check if we'll still hit in the next tick (look-ahead)
					// If fuse is about to run out or we're at the right timing, fire now
					const float flNextFuse = flFuseRemaining - GRENADE_CHECK_INTERVAL;
					if (flNextFuse < flTimeNeeded || flFuseRemaining <= flTimeToTarget)
					{
						// Fire now - we won't have a better opportunity
						m_vChargeAngles = target.AngleTo;
						m_bChargePending = false;
						pCmd->buttons &= ~IN_ATTACK;
					}
					else
					{
						// Keep charging - we can wait for better timing
						pCmd->buttons |= IN_ATTACK;
					}
				}
				else if (flFuseRemaining < flTimeNeeded)
				{
					// Fuse too short - keep charging (will reset on next shot)
					pCmd->buttons |= IN_ATTACK;
				}
				else
				{
					// Fire now
					m_vChargeAngles = target.AngleTo;
					m_bChargePending = false;
					pCmd->buttons &= ~IN_ATTACK;
				}
			}
			else
			{
				// Not charging yet - start charging
				m_vChargeAngles = target.AngleTo;
				m_bChargePending = true;
				pCmd->buttons |= IN_ATTACK;
			}
		}
		else
		{
			// Simple mode - track charge state for cancel logic
			if (flDetonateTime > 0.0f)
			{
				m_vChargeAngles = target.AngleTo;
				m_bChargePending = false;
				pCmd->buttons &= ~IN_ATTACK;
			}
			else
			{
				m_vChargeAngles = target.AngleTo;
				m_bChargePending = true;
				pCmd->buttons |= IN_ATTACK;
			}
		}
	}

	else if (nWeaponID == TF_WEAPON_FLAME_BALL)
	{
		if (pLocal->m_flTankPressure() >= 100.0f)
			pCmd->buttons |= IN_ATTACK;
		else
			pCmd->buttons &= ~IN_ATTACK;
	}

	else
	{
		pCmd->buttons |= IN_ATTACK;
	}

	if (bIsBazooka && pWeapon->HasPrimaryAmmoForShot())
		pCmd->buttons &= ~IN_ATTACK;
}

bool CAimbotProjectile::IsFiring(const CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (!pWeapon->HasPrimaryAmmoForShot())
		return false;

	const int nWeaponID = pWeapon->GetWeaponID();
	if (nWeaponID == TF_WEAPON_COMPOUND_BOW || nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || nWeaponID == TF_WEAPON_CANNON)
	{
		return (G::nOldButtons & IN_ATTACK) && !(pCmd->buttons & IN_ATTACK);
	}

	if (nWeaponID == TF_WEAPON_FLAME_BALL)
	{
		return pLocal->m_flTankPressure() >= 100.0f && (pCmd->buttons & IN_ATTACK);
	}

	if (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
		return G::bCanPrimaryAttack;

	if (nWeaponID == TF_WEAPON_FLAMETHROWER)
	{
		return pCmd->buttons & IN_ATTACK;
	}

	// Allow firing during reload (will interrupt reload) - matches Amalgam's logic
	// dont touch this it took forever to get right
	return (pCmd->buttons & IN_ATTACK) && (G::bCanPrimaryAttack || G::bReloading);
}

void CAimbotProjectile::Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	// Switch back to sticky launcher after cancel - do this FIRST before any other checks
	// because we might have switched to a non-projectile weapon
	if (m_iCancelWeaponIdx != 0)
	{
		if (pWeapon->entindex() != m_iCancelWeaponIdx)
		{
			// Not on sticky launcher yet, switch to it
			pCmd->weaponselect = m_iCancelWeaponIdx;
		}
		else
		{
			// We're back on the sticky launcher, reset
			m_iCancelWeaponIdx = 0;
		}
	}

	if (!CFG::Aimbot_Projectile_Active)
		return;

	// Set FOV circle BEFORE weapon check so it shows for all projectile weapons
	if (CFG::Aimbot_Projectile_Sort == 0)
		G::flAimbotFOV = CFG::Aimbot_Projectile_FOV;

	if (!GetProjectileInfo(pWeapon))
		return;

	if (Shifting::bShifting && !Shifting::bShiftingWarp)
		return;

	if (!CFG::Aimbot_Always_On && !H::Input->IsDown(CFG::Aimbot_Key))
		return;

	// Handle charge weapons (sticky and loose cannon)
	const int nWeaponID = pWeapon->GetWeaponID();
	const bool bIsChargeWeapon = (nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || nWeaponID == TF_WEAPON_CANNON);
	
	// Get charge time - sticky uses m_flChargeBeginTime, cannon uses m_flDetonateTime
	float flChargeBeginTime = 0.0f;
	if (nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER)
		flChargeBeginTime = pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime();
	else if (nWeaponID == TF_WEAPON_CANNON)
		flChargeBeginTime = pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime();
	
	const bool bIsCharging = flChargeBeginTime > 0.0f;

	// Check if user started charging BEFORE aimbot key was pressed
	// G::OriginalCmd has the unmodified buttons from user input
	const bool bUserManualCharge = !m_bChargePending && bIsCharging;

	ProjTarget_t target = {};
	const bool bHasTarget = GetTarget(pLocal, pWeapon, pCmd, target) && target.Entity;

	// Only cancel charge if:
	// 1. AutoShoot is on
	// 2. We're charging
	// 3. No valid target
	// 4. We have a pending charge that WE started (m_bChargePending)
	// 5. User didn't start this charge manually
	if (bIsChargeWeapon && bIsCharging && !bHasTarget && CFG::Aimbot_AutoShoot && m_bChargePending && !bUserManualCharge)
	{
		CancelCharge(pCmd, pLocal, pWeapon);
		return;
	}

	// Reset charge pending if not charging
	if (!bIsCharging)
		m_bChargePending = false;

	// DEBUG: Show when charge weapon has no target but is charging
	if (bIsChargeWeapon && bIsCharging && !bHasTarget)
	{
		I::CVar->ConsoleColorPrintf({ 255, 100, 100, 255 },
			"[Charge FAIL] Tick=%d NoTarget! Charging=%d Pending=%d Manual=%d Weapon=%d\n",
			I::GlobalVars->tickcount,
			bIsCharging ? 1 : 0,
			m_bChargePending ? 1 : 0,
			bUserManualCharge ? 1 : 0,
			nWeaponID);
	}

	if (bHasTarget)
	{
		G::nTargetIndexEarly = target.Entity->entindex();
		G::nTargetIndex = target.Entity->entindex();

		if (ShouldFire(pCmd, pLocal, pWeapon))
			HandleFire(pCmd, pWeapon, pLocal, target);

		const bool bIsFiring = IsFiring(pCmd, pLocal, pWeapon);
		G::bFiring = bIsFiring;

		if (ShouldAim(pCmd, pLocal, pWeapon) || bIsFiring)
		{
			// For charge weapons with pSilent: use saved angles on release tick
			// This ensures the angles match what we calculated when starting the charge
			Vec3 vFinalAngles = target.AngleTo;
			const char* szAngleSource = "target.AngleTo";
			
			// If we're firing a charge weapon and have saved angles, use those instead
			if (bIsFiring && bIsChargeWeapon && !m_vChargeAngles.IsZero())
			{
				vFinalAngles = m_vChargeAngles;
				szAngleSource = "m_vChargeAngles (saved)";
			}
			else if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer && CFG::Aimbot_Projectile_Use_Dodge_Prediction)
			{
				// Apply dodge prediction offset for players (only when not using saved angles)
				const int nTargetIdx = target.Entity->entindex();
				auto* pBehavior = F::MovementSimulation->GetPlayerBehavior(nTargetIdx);

				if (pBehavior && pBehavior->GetConfidence() > 0.5f && pBehavior->m_Combat.m_nReactionSamples >= 5)
				{
					const int nDodgeDir = F::MovementSimulation->GetPredictedDodge(nTargetIdx);

					if (nDodgeDir != 0)
					{
						const float flConfidence = pBehavior->GetConfidence();
						const float flReactionRate = pBehavior->m_Combat.m_flReactionToThreat;
						const float flTimeScale = Math::RemapValClamped(target.TimeToTarget, 0.3f, 1.0f, 0.1f, 1.0f);
						const float flBaseOffset = 2.0f;
						const float flOffset = flBaseOffset * flConfidence * flTimeScale * flReactionRate;

						switch (nDodgeDir)
						{
						case -1: vFinalAngles.y -= flOffset; break;
						case 1:  vFinalAngles.y += flOffset; break;
						case 2:  vFinalAngles.x -= flOffset * 0.7f; break;
						case 3:  break;
						}
						szAngleSource = "dodge prediction";
					}
				}
			}

			// DEBUG OUTPUT for charge weapons (sticky and cannon)
			if (bIsChargeWeapon)
			{
				const Vec3 vOrigAngles = pCmd->viewangles;
				const char* szAimType = (CFG::Aimbot_Projectile_Aim_Type == 1) ? "pSilent" : "Plain";
				const char* szWeapon = (nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER) ? "Sticky" : "Cannon";
				
				I::CVar->ConsoleColorPrintf({ 255, 255, 0, 255 },
					"[%s] Tick=%d Fire=%d Charge=%d Pending=%d\n",
					szWeapon,
					I::GlobalVars->tickcount,
					bIsFiring ? 1 : 0,
					bIsCharging ? 1 : 0,
					m_bChargePending ? 1 : 0);
				
				I::CVar->ConsoleColorPrintf({ 0, 255, 255, 255 },
					"  Orig=(%.1f,%.1f) Final=(%.1f,%.1f) Src=%s\n",
					vOrigAngles.x, vOrigAngles.y,
					vFinalAngles.x, vFinalAngles.y,
					szAngleSource);
				
				I::CVar->ConsoleColorPrintf({ 0, 255, 255, 255 },
					"  Saved=(%.1f,%.1f) Type=%s\n",
					m_vChargeAngles.x, m_vChargeAngles.y,
					szAimType);
			}

			Aim(pCmd, pLocal, pWeapon, vFinalAngles);
			
			// DEBUG: Print what angles were actually set after Aim()
			if (bIsChargeWeapon && bIsFiring)
			{
				I::CVar->ConsoleColorPrintf({ 0, 255, 0, 255 },
					"  FIRED! Cmd=(%.1f,%.1f) pSilent=%d Silent=%d\n",
					pCmd->viewangles.x, pCmd->viewangles.y,
					G::bPSilentAngles ? 1 : 0,
					G::bSilentAngles ? 1 : 0);
			}
			
			// Clear saved angles after firing
			if (bIsFiring && bIsChargeWeapon)
				m_vChargeAngles = {};

			// Notify behavior system that we fired at this target
			if (bIsFiring && target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
			{
				F::MovementSimulation->OnShotFired(target.Entity->entindex());
			}

			if (bIsFiring && m_TargetPath.size() > 1)
			{
				I::DebugOverlay->ClearAllOverlays();
				DrawProjPath(pCmd, target.TimeToTarget);
				DrawMovePath(m_TargetPath);
				m_TargetPath.clear();
			}
		}
	}
}
