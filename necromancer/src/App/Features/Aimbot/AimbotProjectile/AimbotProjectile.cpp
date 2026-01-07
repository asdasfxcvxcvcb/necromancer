#include "AimbotProjectile.h"

#include "../../CFG.h"
#include "../../MovementSimulation/MovementSimulation.h"
#include "../../ProjectileSim/ProjectileSim.h"

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
// i hate physics but here we are
static bool SolveParabolic(const Vec3& vFrom, const Vec3& vTo, float flSpeed, float flGravity, float& flPitchOut, float& flYawOut, float& flTimeOut)
{
	const Vec3 v = vTo - vFrom;
	const float dx = v.Length2D();
	const float dy = v.z;

	if (dx < 0.001f)
		return false;

	flYawOut = RAD2DEG(atan2f(v.y, v.x));

	if (flGravity > 0.001f)
	{
		const float v2 = flSpeed * flSpeed;
		const float v4 = v2 * v2;
		const float root = v4 - flGravity * (flGravity * dx * dx + 2.0f * dy * v2);

		if (root < 0.0f)
			return false;

		flPitchOut = -RAD2DEG(atanf((v2 - sqrtf(root)) / (flGravity * dx)));
		flTimeOut = dx / (cosf(-DEG2RAD(flPitchOut)) * flSpeed);
	}
	else
	{
		const Vec3 vAngle = Math::CalcAngle(vFrom, vTo);
		flPitchOut = vAngle.x;
		flTimeOut = vFrom.DistTo(vTo) / flSpeed;
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
				if (bOnGround)
				{
					vPos.z += (flMaxZ * 0.2f);
					m_LastAimPos = 0;
				}

				else
				{
					vPos.z += (flMaxZ * 0.5f);
					m_LastAimPos = 1;
				}

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
			if (info.m_pos.DistTo(trace.endpos) > info.m_pos.DistTo(vTo))
			{
				return true;
			}

			if (trace.endpos.DistTo(vTo) > 40.0f)
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

		for (int nTick = 0; nTick < TIME_TO_TICKS(CFG::Aimbot_Projectile_Max_Simulation_Time); nTick++)
		{
			m_TargetPath.push_back(F::MovementSimulation->GetOrigin());

			F::MovementSimulation->RunTick(TICKS_TO_TIME(nTick));

			Vec3 vTarget = F::MovementSimulation->GetOrigin();

			OffsetPlayerPosition(pWeapon, vTarget, pPlayer, bDucked, bOnGround, vLocalPos);

			float flTimeToTarget = 0.0f;

			if (!CalcProjAngle(vLocalPos, vTarget, target.AngleTo, flTimeToTarget))
				continue;

			target.TimeToTarget = flTimeToTarget;

			int nTargetTick = TIME_TO_TICKS(flTimeToTarget + SDKUtils::GetLatency());

			//fuck you KGB
			/*if (CFG::Aimbot_Projectile_Aim_Type == 1)
				nTargetTick += 1;*/

				// credits: m-fed for the original code
				// todellinen menninkäinen: "crazy cant u do me like kgb"
				// no we cant lol

			if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
			{
				const auto sticky_arm_time{ SDKUtils::AttribHookValue(0.8f, "sticky_arm_time", pLocal) };
				if (TICKS_TO_TIME(nTargetTick) < sticky_arm_time)
				{
					nTargetTick += TIME_TO_TICKS(fabsf(flTimeToTarget - sticky_arm_time));
				}
			}

			if ((nTargetTick == nTick || nTargetTick == nTick - 1))
			{
				// Helper to get splash radius for weapon
				// splash radius is 146 for most weapons, but can be modified by attributes
				auto getSplashRadius = [&]() -> float
					{
						float flRadius = 0.0f;
						switch (pWeapon->GetWeaponID())
						{
						case TF_WEAPON_PIPEBOMBLAUNCHER:
						case TF_WEAPON_GRENADELAUNCHER:
						case TF_WEAPON_CANNON:
							flRadius = 146.0f;
							break;
						case TF_WEAPON_FLAREGUN:
						case TF_WEAPON_FLAREGUN_REVENGE:
							// Scorch Shot has splash
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

						// fibonacci sphere go brrrr
						constexpr int numPoints = 80;

						std::vector<Vec3> potential{};
						for (int n = 0; n < numPoints; n++)
						{
							auto a1{ acosf(1.0f - 2.0f * (static_cast<float>(n) / static_cast<float>(numPoints))) };
							auto a2{ (static_cast<float>(PI) * (3.0f - sqrtf(5.0f))) * static_cast<float>(n) };

							auto point{ center + Vec3{ sinf(a1) * cosf(a2), sinf(a1) * sinf(a2), cosf(a1) }.Scale(radius) };

							CTraceFilterWorldCustom filter{};
							trace_t trace{};

							H::AimUtils->Trace(center, point, MASK_SOLID, &filter, &trace);

							if (trace.fraction > 0.99f)
							{
								continue;
							}

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
		auto getSplashRadius = [&]() -> float
			{
				float flRadius = 0.0f;
				switch (pWeapon->GetWeaponID())
				{
				case TF_WEAPON_PIPEBOMBLAUNCHER:
				case TF_WEAPON_GRENADELAUNCHER:
				case TF_WEAPON_CANNON:
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

				constexpr auto numPoints{ 80 };

				std::vector<Vec3> potential{};

				for (int n = 0; n < numPoints; n++)
				{
					const auto a1{ acosf(1.0f - 2.0f * (static_cast<float>(n) / static_cast<float>(numPoints))) };
					const auto a2{ (static_cast<float>(PI) * (3.0f - sqrtf(5.0f))) * static_cast<float>(n) };

					auto point{ center + Vec3{ sinf(a1) * cosf(a2), sinf(a1) * sinf(a2), cosf(a1) }.Scale(radius) };

					CTraceFilterWorldCustom filter{};
					trace_t trace{};

					H::AimUtils->Trace(center, point, MASK_SOLID, &filter, &trace);

					if (trace.fraction > 0.99f)
					{
						continue;
					}

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
		if (target.Position.DistTo(vLocalPos) > 400.0f && targetsScanned >= maxTargets)
		{
			continue;
		}

		if (!SolveTarget(pLocal, pWeapon, pCmd, target))
		{
			targetsScanned++;

			continue;
		}

		if (CFG::Aimbot_Projectile_Sort == 0 && Math::CalcFov(vLocalAngles, target.AngleTo) > CFG::Aimbot_Projectile_FOV)
		{
			continue;
		}

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
		if (m_CurProjInfo.Flamethrower ? true : G::bCanPrimaryAttack)
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

void CAimbotProjectile::HandleFire(CUserCmd* pCmd, C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal, const ProjTarget_t& target)
{
	const bool bIsBazooka = pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka;
	if (!bIsBazooka && !pWeapon->HasPrimaryAmmoForShot())
		return;

	const int nWeaponID = pWeapon->GetWeaponID();
	if (nWeaponID == TF_WEAPON_COMPOUND_BOW || nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		if (pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() > 0.0f)
			pCmd->buttons &= ~IN_ATTACK;

		else pCmd->buttons |= IN_ATTACK;
	}

	else if (nWeaponID == TF_WEAPON_CANNON)
	{
		if (CFG::Aimbot_Projectile_Auto_Double_Donk)
		{
			const float flDetonateTime = pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime();
			const float flDetonateMaxTime = SDKUtils::AttribHookValue(0.0f, "grenade_launcher_mortar_mode", pWeapon);
			float flCharge = Math::RemapValClamped(flDetonateTime - I::GlobalVars->curtime, 0.0f, flDetonateMaxTime, 0.0f, 1.0f);

			if (I::GlobalVars->curtime > flDetonateTime)
				flCharge = 1.0f;

			if (flCharge < target.TimeToTarget * 0.8f)
				pCmd->buttons &= ~IN_ATTACK;

			else pCmd->buttons |= IN_ATTACK;
		}

		else
		{
			if (pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() > 0.0f)
				pCmd->buttons &= ~IN_ATTACK;

			else pCmd->buttons |= IN_ATTACK;
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
	if (!CFG::Aimbot_Projectile_Active)
		return;

	// Set FOV circle BEFORE weapon check so it shows for all projectile weapons
	if (CFG::Aimbot_Projectile_Sort == 0)
		G::flAimbotFOV = CFG::Aimbot_Projectile_FOV;

	if (!GetProjectileInfo(pWeapon))
		return;

	if (Shifting::bShifting && !Shifting::bShiftingWarp)
		return;

	if (!H::Input->IsDown(CFG::Aimbot_Key))
		return;

	ProjTarget_t target = {};
	if (GetTarget(pLocal, pWeapon, pCmd, target) && target.Entity)
	{
		G::nTargetIndexEarly = target.Entity->entindex();

		G::nTargetIndex = target.Entity->entindex();

		if (ShouldFire(pCmd, pLocal, pWeapon))
			HandleFire(pCmd, pWeapon, pLocal, target);

		const bool bIsFiring = IsFiring(pCmd, pLocal, pWeapon);

		G::bFiring = bIsFiring;

		if (ShouldAim(pCmd, pLocal, pWeapon) || bIsFiring)
		{
			/*const Vec3 ang_center{ Math::CalcAngle(pLocal->GetShootPos(), Target.m_vPosition) };
			const Vec3 ang_offset{ Math::CalcAngle(getOffsetShootPos(pLocal, pWeapon, pCmd), Target.m_vPosition)};

			const float correction_scale{ Math::RemapValClamped(pLocal->GetShootPos().DistTo(Target.m_vPosition), 0.0f, 19202.0f, 0.0f, 1.0f) };

			const float base_val{ pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? 5.3f : 6.5f };

			const Vec3 correction{ (ang_offset - ang_center) * (base_val * correction_scale) };

			Target.m_vAngleTo -= correction;*/

			Aim(pCmd, pLocal, pWeapon, target.AngleTo);

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
