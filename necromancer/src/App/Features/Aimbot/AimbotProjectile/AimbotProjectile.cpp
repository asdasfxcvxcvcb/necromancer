#include "AimbotProjectile.h"

#include "../../CFG.h"
#include "../../MovementSimulation/MovementSimulation.h"
#include "../../ProjectileSim/ProjectileSim.h"
#include "../../Players/Players.h"

namespace
{
	constexpr int MaxSplashPointCount = 400;
	constexpr float SplashGoldenAngle = 2.39996323f; // PI * (3 - sqrt(5))
	constexpr int StickySplashMinPoints = 20;
	constexpr int StickySplashMaxPoints = 96;
	constexpr int StickySplashCandidateBudget = 24;

	int GetSplashPointCount()
	{
		return std::clamp(CFG::Aimbot_Amalgam_Projectile_SplashPoints, 50, MaxSplashPointCount);
	}

	Vec3 GetSplashDirection(const int nPoint, const int nPointCount)
	{
		const float t = static_cast<float>(nPoint) / static_cast<float>(nPointCount);
		const float a1 = acosf(1.0f - 2.0f * t);
		const float a2 = SplashGoldenAngle * static_cast<float>(nPoint);
		const float sinA1 = sinf(a1);

		return { sinA1 * cosf(a2), sinA1 * sinf(a2), cosf(a1) };
	}

	const std::vector<Vec3>& GetCachedSplashDirections(const int nPointCount)
	{
		static std::vector<Vec3> s_CachedDirections[MaxSplashPointCount + 1];
		if (nPointCount <= 0 || nPointCount > MaxSplashPointCount)
		{
			static const std::vector<Vec3> s_Empty;
			return s_Empty;
		}

		auto& directions = s_CachedDirections[nPointCount];
		if (directions.empty())
		{
			directions.reserve(nPointCount);
			for (int n = 0; n < nPointCount; n++)
				directions.push_back(GetSplashDirection(n, nPointCount));
		}

		return directions;
	}

	Vec3 RotateSplashDirection(const Vec3& vDirection, float flSin, float flCos)
	{
		return
		{
			vDirection.x * flCos - vDirection.y * flSin,
			vDirection.x * flSin + vDirection.y * flCos,
			vDirection.z
		};
	}

	bool SameFloat(const float a, const float b)
	{
		return fabsf(a - b) <= 0.001f;
	}

	bool CanInterruptReload(C_TFWeaponBase* pWeapon)
	{
		return pWeapon && pWeapon->HasPrimaryAmmoForShot() && pWeapon->m_iClip1() > 0 && pWeapon->IsInReload() && pWeapon->m_bReloadsSingly();
	}
}

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
	// Lob arcs need more ticks than the parabolic estimate due to drag slowing the projectile
	const float flDrawTime = (pCmd->viewangles.x < -45.0f) ? time * 1.5f : time;
	const int nDrawTicks = TIME_TO_TICKS(flDrawTime);
	vPath.reserve(nDrawTicks + 1);

	vPath.push_back(F::ProjectileSim->GetOrigin());

	for (int n = 0; n < nDrawTicks; n++)
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
	const int nWeaponID = pWeapon->GetWeaponID();
	const int nItemDef = pWeapon->m_iItemDefinitionIndex();

	m_CurProjInfo = {};
	m_CurProjInfo.WeaponID = nWeaponID;
	m_CurProjInfo.ItemDef = nItemDef;

	auto curTime = [&]() -> float
		{
			if (const auto pLocal = H::Entities->GetLocal())
			{
				return static_cast<float>(pLocal->m_nTickBase()) * I::GlobalVars->interval_per_tick;
			}

			return I::GlobalVars->curtime;
		};

	switch (nWeaponID)
	{
	case TF_WEAPON_GRENADELAUNCHER:
	{
		m_CurProjInfo.Speed = 1200.0f;
		m_CurProjInfo.GravityMod = 1.0f;
		m_CurProjInfo.Pipes = true;
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
		m_CurProjInfo.Speed = 1454.0f;
		m_CurProjInfo.GravityMod = 1.0f;
		m_CurProjInfo.Pipes = true;
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
		m_CurProjInfo.Speed = 2400.0f;
		m_CurProjInfo.GravityMod = 0.2f;
		break;
	}

	case TF_WEAPON_SYRINGEGUN_MEDIC:
	{
		m_CurProjInfo.Speed = 1000.0f;
		m_CurProjInfo.GravityMod = 0.3f;
		break;
	}

	case TF_WEAPON_FLAREGUN:
	{
		m_CurProjInfo.Speed = 2000.0f;
		m_CurProjInfo.GravityMod = 0.3f;
		break;
	}

	case TF_WEAPON_FLAREGUN_REVENGE:
	{
		m_CurProjInfo.Speed = 3000.0f;
		m_CurProjInfo.GravityMod = 0.45f;
		break;
	}

	case TF_WEAPON_FLAME_BALL:
	{
		m_CurProjInfo.Speed = 3000.0f;
		m_CurProjInfo.GravityMod = 0.0f;
		break;
	}

	case TF_WEAPON_FLAMETHROWER:
	{
		m_CurProjInfo.Speed = 2000.0f;
		m_CurProjInfo.GravityMod = 0.0f;
		m_CurProjInfo.Flamethrower = true;
		break;
	}

	case TF_WEAPON_RAYGUN:
	case TF_WEAPON_DRG_POMSON:
	{
		m_CurProjInfo.Speed = 1200.0f;
		m_CurProjInfo.GravityMod = 0.0f;

		break;
	}

	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	{
		// Amalgam: base speed is always 1100 for ALL rocket types.
		// The Direct Hit's 1.8x speed comes from its "mult_projectile_speed" attribute,
		// so AttribHookValue(1100, ...) correctly yields 1100*1.8=1980.
		// Using 1980 as base would apply 1.8x AGAIN → 3564 (way too fast).
		// Mannpower precision rune overrides to 3000.
		const auto pLocal = H::Entities->GetLocal();
		const float flBaseSpeed = (pLocal && pLocal->InCond(TF_COND_RUNE_PRECISION)) ? 3000.0f : 1100.0f;
		m_CurProjInfo.Speed = SDKUtils::AttribHookValue(flBaseSpeed, "mult_projectile_speed", pWeapon);
		m_CurProjInfo.GravityMod = 0.0f;
		break;
	}

	case TF_WEAPON_PARTICLE_CANNON:
	{
		m_CurProjInfo.Speed = SDKUtils::AttribHookValue(1200.0f, "mult_projectile_speed", pWeapon);
		m_CurProjInfo.GravityMod = 0.0f;
		break;
	}

	default: break;
	}

	if (m_CurProjInfo.Speed <= 0.0f)
		return false;

	m_CurProjInfo.NeedsOffsetCorrection =
		m_CurProjInfo.WeaponID == TF_WEAPON_COMPOUND_BOW ||
		m_CurProjInfo.WeaponID == TF_WEAPON_CROSSBOW ||
		m_CurProjInfo.WeaponID == TF_WEAPON_SHOTGUN_BUILDING_RESCUE ||
		m_CurProjInfo.WeaponID == TF_WEAPON_FLAREGUN ||
		m_CurProjInfo.WeaponID == TF_WEAPON_FLAREGUN_REVENGE ||
		m_CurProjInfo.WeaponID == TF_WEAPON_SYRINGEGUN_MEDIC ||
		m_CurProjInfo.WeaponID == TF_WEAPON_FLAME_BALL ||
		// Rockets need it too: without muzzle-relative time the projectile leads ~1.4 ticks
		// too far (eye->target vs muzzle->target is ~24u/1100u/s) and lands ahead of the sim line.
		m_CurProjInfo.WeaponID == TF_WEAPON_ROCKETLAUNCHER ||
		m_CurProjInfo.WeaponID == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT ||
		m_CurProjInfo.WeaponID == TF_WEAPON_PARTICLE_CANNON;

	UpdateArcCache(pWeapon);

	return true;
}

void CAimbotProjectile::UpdateArcCache(C_TFWeaponBase* pWeapon)
{
	const int nWeaponEntity = pWeapon->entindex();

	if (m_ArcCache.Valid &&
		m_ArcCache.WeaponEntity == nWeaponEntity &&
		m_ArcCache.WeaponID == m_CurProjInfo.WeaponID &&
		m_ArcCache.ItemDef == m_CurProjInfo.ItemDef &&
		SameFloat(m_ArcCache.Speed, m_CurProjInfo.Speed) &&
		SameFloat(m_ArcCache.GravityMod, m_CurProjInfo.GravityMod) &&
		m_ArcCache.Pipes == m_CurProjInfo.Pipes)
	{
		m_CurProjInfo.Gravity = m_ArcCache.Gravity;
		m_CurProjInfo.DragLow = m_ArcCache.DragLow;
		m_CurProjInfo.DragLob = m_ArcCache.DragLob;
		return;
	}

	m_CurProjInfo.Gravity = SDKUtils::GetGravity() * m_CurProjInfo.GravityMod;
	m_CurProjInfo.DragLow = 0.0f;
	m_CurProjInfo.DragLob = 0.0f;

	if (m_CurProjInfo.Pipes)
	{
		const float v0 = std::min(m_CurProjInfo.Speed, k_flMaxVelocity);

		if (m_CurProjInfo.WeaponID == TF_WEAPON_GRENADELAUNCHER)
		{
			m_CurProjInfo.DragLow = m_CurProjInfo.ItemDef == Demoman_m_TheLochnLoad ? 0.070f : Math::RemapValClamped(v0, 1217.0f, k_flMaxVelocity, 0.120f, 0.200f);
			m_CurProjInfo.DragLob = m_CurProjInfo.ItemDef == Demoman_m_TheLochnLoad ? 0.030f : Math::RemapValClamped(v0, 1217.0f, k_flMaxVelocity, 0.056f, 0.062f);
		}
		else if (m_CurProjInfo.WeaponID == TF_WEAPON_PIPEBOMBLAUNCHER)
		{
			m_CurProjInfo.DragLow = Math::RemapValClamped(v0, 922.0f, k_flMaxVelocity, 0.090f, 0.190f);
			m_CurProjInfo.DragLob = Math::RemapValClamped(v0, 922.0f, k_flMaxVelocity, 0.048f, 0.060f);
		}
		else if (m_CurProjInfo.WeaponID == TF_WEAPON_CANNON)
		{
			m_CurProjInfo.DragLow = Math::RemapValClamped(v0, 1454.0f, k_flMaxVelocity, 0.385f, 0.530f);
			m_CurProjInfo.DragLob = Math::RemapValClamped(v0, 1454.0f, k_flMaxVelocity, 0.099f, 0.092f);
		}
	}

	m_ArcCache.WeaponEntity = nWeaponEntity;
	m_ArcCache.WeaponID = m_CurProjInfo.WeaponID;
	m_ArcCache.ItemDef = m_CurProjInfo.ItemDef;
	m_ArcCache.Speed = m_CurProjInfo.Speed;
	m_ArcCache.GravityMod = m_CurProjInfo.GravityMod;
	m_ArcCache.Pipes = m_CurProjInfo.Pipes;
	m_ArcCache.Valid = true;
	m_ArcCache.Gravity = m_CurProjInfo.Gravity;
	m_ArcCache.DragLow = m_CurProjInfo.DragLow;
	m_ArcCache.DragLob = m_CurProjInfo.DragLob;
}

// Helper to solve parabolic trajectory
// bHighArc = false: low arc (- sqrtRoot), faster arrival, used by default
// bHighArc = true: high arc (+ sqrtRoot), lob angle for targets behind cover or steep angles
static bool SolveParabolic(const Vec3& vFrom, const Vec3& vTo, float flSpeed, float flGravity, float& flPitchOut, float& flYawOut, float& flTimeOut, bool bHighArc = false)
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

		if (bHighArc)
			flPitchOut = -RAD2DEG(atanf((v2 + sqrtRoot) / gDxSqrt)); // High arc (lob)
		else
			flPitchOut = -RAD2DEG(atanf((v2 - sqrtRoot) / gDxSqrt)); // Low arc (default)

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

bool CAimbotProjectile::CalcProjAngle(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const Vec3& vFrom, const Vec3& vTo, Vec3& vAngleOut, float& flTimeOut, bool bLob)
{
	if (!pWeapon || !pLocal)
		return false;

	const int nWeaponID = m_CurProjInfo.WeaponID;
	float v0 = m_CurProjInfo.Speed;
	const float g = m_CurProjInfo.Gravity;

	if (m_CurProjInfo.Pipes && v0 > k_flMaxVelocity)
		v0 = k_flMaxVelocity;

	// First pass: calculate from eye position
	float flPitch, flYaw, flTime;
	if (!SolveParabolic(vFrom, vTo, v0, g, flPitch, flYaw, flTime, bLob))
		return false;

	// For pipes, apply drag correction (Amalgam-style two-pass with exponential decay)
	// Lob arcs need much more accurate drag correction because the longer flight time
	// causes errors to compound — a simple linear correction makes the projectile fall short.
	if (m_CurProjInfo.Pipes)
	{
		float flDrag = bLob ? m_CurProjInfo.DragLob : m_CurProjInfo.DragLow;

		if (flDrag <= 0.0f && bLob)
		{
			// Lob-specific drag — velocity-dependent, from Amalgam's fGetLobDrag
			if (nWeaponID == TF_WEAPON_GRENADELAUNCHER)
				flDrag = (m_CurProjInfo.ItemDef == Demoman_m_TheLochnLoad) ? 0.030f : Math::RemapValClamped(v0, 1217.0f, k_flMaxVelocity, 0.056f, 0.062f);
			else if (nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER)
				flDrag = Math::RemapValClamped(v0, 922.0f, k_flMaxVelocity, 0.048f, 0.060f);
			else if (nWeaponID == TF_WEAPON_CANNON)
				flDrag = Math::RemapValClamped(v0, 1454.0f, k_flMaxVelocity, 0.099f, 0.092f);
		}
		else if (flDrag <= 0.0f)
		{
			// Regular low arc drag — velocity-dependent, from Amalgam's fGetRegularDrag
			if (nWeaponID == TF_WEAPON_GRENADELAUNCHER)
				flDrag = (m_CurProjInfo.ItemDef == Demoman_m_TheLochnLoad) ? 0.07f : Math::RemapValClamped(v0, 1217.0f, k_flMaxVelocity, 0.120f, 0.200f);
			else if (nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER)
				flDrag = Math::RemapValClamped(v0, 922.0f, k_flMaxVelocity, 0.090f, 0.190f);
			else if (nWeaponID == TF_WEAPON_CANNON)
				flDrag = Math::RemapValClamped(v0, 1454.0f, k_flMaxVelocity, 0.385f, 0.530f);
		}

		if (flDrag > 0.0f)
		{
			// First pass: linear drag correction, then re-solve parabolic
			float v0_adjusted = v0 * (1.0f - flDrag * flTime);

			if (!SolveParabolic(vFrom, vTo, v0_adjusted, g, flPitch, flYaw, flTime, bLob))
				return false;

			// Second pass: exponential decay + height corrections (NO re-solve)
			// These correct the velocity for time calculation only — re-solving would
			// cause the reduced velocity to fail the parabolic root check.
			const float flExpTerm = 1.0f - expf(-flDrag * flTime);
			v0_adjusted = v0_adjusted / (1.0f + 0.5f * flExpTerm);

			// Height compensation: drag affects vertical velocity more than horizontal
			const Vec3 vDelta = vTo - vFrom;
			v0_adjusted *= 1.0f + vDelta.z / v0_adjusted * TICK_INTERVAL * flTime;

			// Recalculate time with corrected velocity (pitch stays from re-solve)
			const float flDist2D = (vTo - vFrom).Length2D();
			flTime = flDist2D / (v0_adjusted * cosf(DEG2RAD(flPitch)));
		}
	}

	// Second pass for projectiles with spawn offset (huntsman, crossbow, flares, etc.)
	// This corrects for the fact that projectiles spawn from a different position than the eye
	// i hate this shit but it makes huntsman actually hit at long range so whatever
	if (m_CurProjInfo.NeedsOffsetCorrection)
	{
		// Get the projectile spawn offset
		Vec3 vOffset;
		if (nWeaponID == TF_WEAPON_COMPOUND_BOW)
			vOffset = { 23.5f, 8.0f, -3.0f };
		else if (nWeaponID == TF_WEAPON_CROSSBOW || nWeaponID == TF_WEAPON_SHOTGUN_BUILDING_RESCUE)
			vOffset = { 23.5f, 8.0f, -3.0f };
		else if (nWeaponID == TF_WEAPON_PARTICLE_CANNON)
			vOffset = { 23.5f, 8.0f, (pLocal->m_fFlags() & FL_DUCKING) ? 8.0f : -3.0f };
		else
			// Rocket / Direct Hit / flares: { 23.5, 12, ducking?8:-3 }
			vOffset = { 23.5f, 12.0f, (pLocal->m_fFlags() & FL_DUCKING) ? 8.0f : -3.0f };

		// Calculate actual projectile spawn position using first-pass angles
		Vec3 vFirstPassAngle = { flPitch, flYaw, 0.0f };
		Vec3 vForward, vRight, vUp;
		Math::AngleVectors(vFirstPassAngle, &vForward, &vRight, &vUp);

		Vec3 vProjSpawn = vFrom + (vForward * vOffset.x) + (vRight * vOffset.y) + (vUp * vOffset.z);

		// Recalculate trajectory from actual spawn position
		float flPitch2, flYaw2, flTime2;
		if (!SolveParabolic(vProjSpawn, vTo, v0, g, flPitch2, flYaw2, flTime2, bLob))
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

	// Time limit checks — lob arcs are allowed longer flight times
	if (m_CurProjInfo.Pipes && !bLob)
	{
		if (nWeaponID == TF_WEAPON_CANNON && flTimeOut > 0.95f)
			return false;
		else if (m_CurProjInfo.ItemDef == Demoman_m_TheIronBomber && flTimeOut > 1.4f)
			return false;
		else if (flTimeOut > 2.3f)
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

			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
			{
				// PrioritizeFeet: rockets splash at ground level, aim low when target is grounded
				if (CFG::Aimbot_Amalgam_Projectile_Hitbox_PrioritizeFeet && bOnGround)
				{
					vPos.z += (flMaxZ * 0.1f);
					m_LastAimPos = 0;
				}
				else
				{
					vPos.z += (flMaxZ * 0.5f);
					m_LastAimPos = 1;
				}
				break;
			}

			default:
			{
				// All other weapons (flares, syringes, crossbows, etc.) — body aim only
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

bool CAimbotProjectile::CanArcReach(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const Vec3& vFrom, const Vec3& vTo, const Vec3& vAngleTo, float flTargetTime, C_BaseEntity* pTarget)
{
	if (!pLocal)
	{
		return false;
	}

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

	// Determine hull size once before the loop (Amalgam-accurate projectile hulls)
	Vec3 mins{ -6.0f, -6.0f, -6.0f };
	Vec3 maxs{ 6.0f, 6.0f, 6.0f };
	switch (info.m_type)
	{
	case TF_PROJECTILE_PIPEBOMB:
	case TF_PROJECTILE_PIPEBOMB_REMOTE:
	case TF_PROJECTILE_PIPEBOMB_PRACTICE:
	case TF_PROJECTILE_CANNONBALL:
		mins = { -8.0f, -8.0f, -8.0f };
		maxs = { 8.0f, 8.0f, 20.0f };
		break;
	// Rockets / cow mangler energy ball have NO collision hull in TF2 — point trace.
	// The corner is caught by the path itself (per-tick trace along the converged direction),
	// NOT by an inflated hull. The old 6u default was a band-aid for the wrong-path bug.
	case TF_PROJECTILE_ROCKET:
	case TF_PROJECTILE_ENERGY_BALL:
		mins = { 0.0f, 0.0f, 0.0f };
		maxs = { 0.0f, 0.0f, 0.0f };
		break;
	case TF_PROJECTILE_FLARE:
		mins = { 0.0f, 0.0f, 0.0f };
		maxs = { 0.0f, 0.0f, 0.0f };
		break;
	default:
		break;
	}

	// Lob arcs need more simulation ticks because the parabolic time estimate
	// underestimates actual flight time due to per-tick drag compounding over longer arcs.
	// Steep pitch = lob, use 1.5x multiplier. Normal arc uses 1.2x.
	const float flSimMultiplier = (vAngleTo.x < -45.0f) ? 1.5f : 1.2f;
	const int nMaxTicks = TIME_TO_TICKS(flTargetTime * flSimMultiplier);

	// Pre-compute once — avoids recomputing the same value inside the hot loop.
	const float flTargetDist2D = (vTo - info.m_pos).Length2D();

	for (auto n = 0; n < nMaxTicks; n++)
	{
		auto pre{ F::ProjectileSim->GetOrigin() };

		F::ProjectileSim->RunTick();

		auto post{ F::ProjectileSim->GetOrigin() };

		// Early-exit: projectile passed the target's XY range AND has descended to
		// the target's altitude — the arc is clear of geometry for the entire path.
		// The Z check is critical for lob arcs: without it, the exit fires while the
		// projectile is still rising (passed target XY but not yet at target height),
		// which would skip the descending phase where floor/ceiling hits are detected.
		if ((post - info.m_pos).Length2D() > flTargetDist2D + 30.0f
			&& post.z <= vTo.z + 30.0f)
			return true;

		trace_t trace{};

		H::AimUtils->TraceHull(pre, post, mins, maxs, MASK_SOLID, &filter, &trace);

		if (trace.m_pEnt == pTarget)
		{
			return true;
		}

		if (trace.DidHit())
		{
			const Vec3 vShooterToHit = trace.endpos - info.m_pos;
			const float flHitDist2D = vShooterToHit.Length2D();
			const float flHitToTarget2D = (trace.endpos - vTo).Length2D();

			// If we hit something past the target in XY, we're good
			// Use 2D distance comparison — 3D comparison is wrong for lob arcs because
			// a ground hit (Z≈0) has larger 3D distance than the elevated target (Z=player height),
			// causing the check to accept solutions that overshoot in XY.
			if (flHitDist2D > flTargetDist2D)
			{
				return true;
			}

			// If we hit something close enough to target in XY, check if we can splash them
			// Use 2D distance — the projectile hits ground level while the target is at player height,
			// so 3D distance includes the Z gap which isn't relevant for splash proximity.
			if (flHitToTarget2D > 30.0f)
			{
				return false;
			}

			H::AimUtils->Trace(trace.endpos, vTo, MASK_SOLID, &filter, &trace);

			const bool bCanSee = !trace.DidHit() || trace.m_pEnt == pTarget;
			return bCanSee;
		}

		//I::DebugOverlay->AddBoxOverlay(post, mins, maxs, Math::CalcAngle(pre, post), 255, 255, 255, 2, 60.0f);
	}

	return true;
}

bool CAimbotProjectile::CanSee(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const Vec3& vFrom, const Vec3& vTo, const ProjTarget_t& target, float flTargetTime)
{
	Vec3 vLocalPos = vFrom;
	const int nWeaponID = m_CurProjInfo.WeaponID;

	switch (nWeaponID)
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

	if (m_CurProjInfo.Flamethrower)
	{
		return H::AimUtils->TraceFlames(target.Entity, vLocalPos, vTo);
	}

	// ALL projectiles (arc AND straight-line rockets) validate via CanArcReach, which
	// traces per-tick along the ACTUAL simulated path. For rockets that path follows the
	// converged info.m_ang (GetProjectileFireSetup aims the rocket from the muzzle toward
	// where eye-forward hits, exactly like the real TF2 rocket) — so the validation line is
	// identical to the drawn sim line and the real trajectory. A straight muzzle->target
	// trace would check a DIFFERENT line and miss corners the real rocket flies through.
	// Roll (neckbreaker) shifts the muzzle inside GetInfo, so different rolls test different paths.
	return CanArcReach(pLocal, pWeapon, vFrom, vTo, target.AngleTo, flTargetTime, target.Entity);
}

bool CAimbotProjectile::SolveTarget(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const CUserCmd* pCmd, ProjTarget_t& target)
{
	Vec3 vLocalPos = pLocal->GetShootPos();
	const int nWeaponID = m_CurProjInfo.WeaponID;
	int nMaxSimTicks = TIME_TO_TICKS(std::min(CFG::Aimbot_Projectile_Max_Simulation_Time, 7.0f));
	const float flLatency = SDKUtils::GetLatency();
	const bool bIsSticky = nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER;
	const float flStickyArmTime = bIsSticky ? SDKUtils::AttribHookValue(0.8f, "sticky_arm_time", pLocal) : 0.0f;
	const bool bUseEarlyTickSkip = bIsSticky && pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() > 0.0f;
	const float flSolveSpeed = std::max(m_CurProjInfo.Pipes ? std::min(m_CurProjInfo.Speed, k_flMaxVelocity) : m_CurProjInfo.Speed, 1.0f);

	auto isTickTooEarly = [&](const Vec3& vTo, int nTick) -> bool
		{
			if (!bUseEarlyTickSkip)
				return false;

			const float flMinTravelTime = (vTo - vLocalPos).Length2D() / flSolveSpeed;
			int nEarliestTick = TIME_TO_TICKS(flMinTravelTime + flLatency);

			if (bIsSticky && TICKS_TO_TIME(nEarliestTick) < flStickyArmTime)
				nEarliestTick += TIME_TO_TICKS(fabsf(flMinTravelTime - flStickyArmTime));

			return nEarliestTick > nTick + 2;
		};

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

		// Adaptive prediction horizon: strafing/antiaim targets are harder to predict,
		// but we must NOT prevent the aimbot from finding long-range solutions.
		// Instead of hard-capping sim ticks (which makes long-range targets unshootable),
		// only shorten the horizon for truly unpredictable states (explosive launch).
		// Strafing/antiaim still get the full horizon — the sim handles them via
		// rigid ground mode and yaw decay rather than tick count truncation.
		{
			const auto& simState = F::MovementSimulation->GetStorage();

			// Phase 6: explosive launch (rocket/sticky jump) — trajectory is
			// fundamentally unpredictable. Cap hard to avoid wasting ticks.
			if (simState.m_bExplosiveLaunch)
				nMaxSimTicks = std::min(nMaxSimTicks, TIME_TO_TICKS(0.5f));

			// Phase 5: shorten horizon on recent reversals — extrapolating
			// a turn-rate that just reversed is worse than a short straight prediction.
			// But still allow enough ticks for medium-range shots.
			if (simState.m_bRecentReversal)
				nMaxSimTicks = std::min(nMaxSimTicks, TIME_TO_TICKS(0.5f));

		}

		// Stationary enemies: skip RunTick() — their position doesn't change,
		// so GetOrigin() always returns their current position. This lets the
		// full logic (splash, multipoint, lob, path drawing) still work.
		const bool bStationary = F::MovementSimulation->IsStationary();
		// Phase 6: also capture reversal state so the aimbot gate can widen.
		const bool bReversal = F::MovementSimulation->GetStorage().m_bRecentReversal;

		// Pre-calculate splash radius (weapon doesn't change between ticks)
		float flSplashRadius = 0.0f;
		{
			float flBaseRadius = 0.0f;
			switch (nWeaponID)
			{
			case TF_WEAPON_PIPEBOMBLAUNCHER:
				flBaseRadius = 146.0f;
				break;
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE:
				flBaseRadius = 110.0f;
				break;
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
				flBaseRadius = 146.0f;
				break;
			default:
				break;
			}
			if (flBaseRadius > 0.0f)
				flSplashRadius = SDKUtils::AttribHookValue(flBaseRadius, "mult_explosion_radius", pWeapon);
		}

		auto runSplash = [&](const Vec3& vTarget) -> bool
			{
				if (flSplashRadius <= 0.0f)
					return false;

				const float radius = std::min(flSplashRadius - 16.0f, 130.0f);

				Vec3 mins{ target.Entity->m_vecMins() };
				Vec3 maxs{ target.Entity->m_vecMaxs() };

				auto center{ F::MovementSimulation->GetOrigin() + Vec3(0.0f, 0.0f, (mins.z + maxs.z) * 0.5f) };

				// Generate points from the current setting only; no world-space splash points are retained.
				const float flRotation = static_cast<float>(I::GlobalVars->tickcount % 360) * DEG2RAD(1.0f);
				const float flSinRotation = sinf(flRotation);
				const float flCosRotation = cosf(flRotation);
				// Sticky splash is expensive: cap points and checked candidates to avoid frame spikes.
				const int nSplashPointCount = bIsSticky
					? std::clamp(GetSplashPointCount() / 2, StickySplashMinPoints, StickySplashMaxPoints)
					: GetSplashPointCount();
				const auto& splashDirections = GetCachedSplashDirections(nSplashPointCount);

				// Stack array instead of heap vector — avoids allocation per call
				Vec3 potential[MaxSplashPointCount];
				int nPotentialCount = 0;

				CTraceFilterWorldCustom filter{};

				for (int n = 0; n < nSplashPointCount; n++)
				{
					const Vec3 point = center + RotateSplashDirection(splashDirections[n], flSinRotation, flCosRotation).Scale(radius);

					trace_t trace{};
					H::AimUtils->Trace(center, point, MASK_SOLID, &filter, &trace);

					if (trace.fraction > 0.99f)
						continue;

					potential[nPotentialCount++] = trace.endpos;
				}

				// Sort by distance to target origin
				const Vec3 vOrigin = F::MovementSimulation->GetOrigin();
				std::sort(potential, potential + nPotentialCount, [&](const Vec3& a, const Vec3& b)
					{
						return a.DistToSqr(vOrigin) < b.DistToSqr(vOrigin);
					});

				// Hoist shoot pos outside inner loop
				const Vec3 vShootPos = GetOffsetShootPos(pLocal, pWeapon, pCmd);
				const Vec3 hull_min = { -8.0f, -8.0f, -8.0f };
				const Vec3 hull_max = { 8.0f,  8.0f,  8.0f };

				const int nCandidateCount = bIsSticky ? std::min(nPotentialCount, StickySplashCandidateBudget) : nPotentialCount;
				for (int i = 0; i < nCandidateCount; i++)
				{
					const Vec3& point = potential[i];

					if (!CalcProjAngle(pLocal, pWeapon, vLocalPos, point, target.AngleTo, target.TimeToTarget))
						continue;

					trace_t trace = {};
					CTraceFilterWorldCustom traceFilter = {};

					H::AimUtils->TraceHull
					(
						vShootPos,
						point,
						hull_min,
						hull_max,
						MASK_SOLID,
						&traceFilter,
						&trace
					);

					trace_t grateTrace{};
					CTraceFilterWorldCustom grateFilter{};
					H::AimUtils->Trace(trace.endpos, point, CONTENTS_GRATE, &grateFilter, &grateTrace);
					H::AimUtils->Trace(trace.endpos, point, MASK_SOLID, &traceFilter, &trace);

					if (grateTrace.fraction < 1.0f)
					{
						if (!CanArcReach(pLocal, pWeapon, vLocalPos, point, target.AngleTo, target.TimeToTarget, target.Entity))
							continue;

						return true;
					}

					if (trace.fraction < 1.0f)
						continue;

					if (!CanArcReach(pLocal, pWeapon, vLocalPos, point, target.AngleTo, target.TimeToTarget, target.Entity))
						continue;

					return true;
				}

				return false;
			};

		for (int nTick = 0; nTick < nMaxSimTicks; nTick++)
		{
			m_TargetPath.push_back(F::MovementSimulation->GetOrigin());

			// Skip RunTick() for stationary enemies — their position doesn't change,
			// so GetOrigin() always returns their current position. Running ticks
			// for them is wasted CPU and would produce identical positions anyway.
			if (!bStationary)
				F::MovementSimulation->RunTick();

			Vec3 vTarget = F::MovementSimulation->GetOrigin();

			OffsetPlayerPosition(pWeapon, vTarget, pPlayer, bDucked, bOnGround, vLocalPos);

			if (isTickTooEarly(vTarget, nTick))
				continue;

			float flTimeToTarget = 0.0f;

			if (!CalcProjAngle(pLocal, pWeapon, vLocalPos, vTarget, target.AngleTo, flTimeToTarget))
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

			// Off-by-1 correction: RunTick is called BEFORE vTarget is read, so at nTick=N
			// the position is after N+1 RunTick calls. We want the position that is exactly
			// nTargetTick RunTick calls ahead, which happens when nTick = nTargetTick - 1.
			if ((nTargetTick == nTick + 1 || nTargetTick == nTick)
				|| (bStationary && nTargetTick <= nTick + 1)
				|| (bReversal && nTargetTick >= nTick - 1 && nTargetTick <= nTick + 3))
			{
				if (CFG::Aimbot_Projectile_Rocket_Splash == 2 && runSplash(vTarget))
				{
					F::MovementSimulation->Restore();

					return true;
				}

				if (CanSee(pLocal, pWeapon, vLocalPos, vTarget, target, flTimeToTarget))
				{
					F::MovementSimulation->Restore();

					return true;
				}

				// Neckbreaker: try roll angles when main shot is blocked (silent aim only).
				// For non-rocket arc weapons, CanSee recomputes the spawn offset using
				// the full angle (pitch+yaw+roll) via GetProjectileFireSetup, so different
				// roll values can produce slightly different spawn positions that clear geometry.
				if (CFG::Aimbot_Projectile_Neckbreaker && CFG::Aimbot_Projectile_Aim_Type == 1)
				{
					const int iStep = std::max(CFG::Aimbot_Projectile_NeckbreakerStep, 15);
					for (int iRoll = iStep; iRoll < 360; iRoll += iStep)
					{
						ProjTarget_t rollTarget = target;
						rollTarget.AngleTo.z = static_cast<float>(iRoll);
						if (CanSee(pLocal, pWeapon, vLocalPos, vTarget, rollTarget, flTimeToTarget))
						{
							target.AngleTo.z = static_cast<float>(iRoll);
							F::MovementSimulation->Restore();
							return true;
						}
					}
				}

				if (CFG::Aimbot_Projectile_BBOX_Multipoint && nWeaponID != TF_WEAPON_COMPOUND_BOW)
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

						if (CalcProjAngle(pLocal, pWeapon, vLocalPos, vTargetMp, target.AngleTo, target.TimeToTarget))
						{
							if (CanSee(pLocal, pWeapon, vLocalPos, vTargetMp, target, target.TimeToTarget))
							{
								F::MovementSimulation->Restore();

								return true;
							}
						}
					}
				}

				if (CFG::Aimbot_Projectile_Rocket_Splash == 1 && runSplash(vTarget))
				{
					F::MovementSimulation->Restore();

					return true;
				}

				// Stationary target: its world position never changes, so the same angle
				// will be computed for every remaining tick. If all checks (splash, CanSee,
				// multipoint) failed once, they will fail on every subsequent tick too.
				// Break immediately instead of burning N*CanArcReach calls for nothing.
				if (bStationary)
					break;
			}

		}

		F::MovementSimulation->Restore();

		// Midpoint Aim: for grounded rocket targets, aim at the ground below the midpoint of the
		// target's predicted path for a splash shot that catches players moving between positions.
		if (CFG::Aimbot_Projectile_Midpoint_Aim && bOnGround && m_TargetPath.size() > 2 &&
			(nWeaponID == TF_WEAPON_ROCKETLAUNCHER || nWeaponID == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT || nWeaponID == TF_WEAPON_PARTICLE_CANNON))
		{
			const float flMaxDist = CFG::Aimbot_Projectile_Midpoint_Max_Distance * 16.0f;
			float flTotal = 0.f;
			for (size_t n = 1; n < m_TargetPath.size(); n++)
				flTotal += m_TargetPath[n].DistTo(m_TargetPath[n - 1]);

			if (flTotal > 8.0f && flTotal <= flMaxDist)
			{
				float flHalf = flTotal * 0.5f, flAcc = 0.f;
				int nMidTick = 0;
				for (size_t n = 1; n < m_TargetPath.size(); n++)
				{
					flAcc += m_TargetPath[n].DistTo(m_TargetPath[n - 1]);
					if (flAcc >= flHalf) { nMidTick = static_cast<int>(n); break; }
				}

				if (nMidTick > 0 && nMidTick < static_cast<int>(m_TargetPath.size()))
				{
					const Vec3 vMidOrigin = m_TargetPath[nMidTick];
					trace_t groundTrace{};
					CTraceFilterWorldCustom groundFilter{};
					H::AimUtils->TraceHull(vMidOrigin, vMidOrigin - Vec3(0, 0, 256.f),
						{ -8.f, -8.f, -8.f }, { 8.f, 8.f, 8.f }, MASK_SOLID, &groundFilter, &groundTrace);

					if (groundTrace.DidHit() && !(groundTrace.surface.flags & SURF_SKY) && groundTrace.plane.normal.z > 0.7f)
					{
						const Vec3 vGround = groundTrace.endpos + Vec3(0, 0, 1.f);
						Vec3 vMidAngle{};
						float flMidTime = 0.f;
						trace_t midTrace{};
						CTraceFilterWorldCustom midFilter{};
						H::AimUtils->TraceHull(vLocalPos, vGround,
							{ -6.f, -6.f, -6.f }, { 6.f, 6.f, 6.f }, MASK_SOLID, &midFilter, &midTrace);
						if (CalcProjAngle(pLocal, pWeapon, vLocalPos, vGround, vMidAngle, flMidTime) &&
							midTrace.fraction >= 0.99f)
						{
							target.AngleTo = vMidAngle;
							target.TimeToTarget = flMidTime;
							return true;
						}
					}
				}
			}
		}

		// Lob angle fallback — only try AFTER the full simulation loop failed to find a low arc solution.
		// This ensures low arc is always preferred. Lob is a last resort for targets behind cover.
		// Excluded: grenade launcher, cannon (their -200 up velocity makes lob inaccurate)
		const bool bCanLob = m_CurProjInfo.GravityMod > 0.0f
			&& nWeaponID != TF_WEAPON_GRENADELAUNCHER
			&& nWeaponID != TF_WEAPON_CANNON;

		if (bCanLob && bOnGround)
		{
			// Clear the movement path from the failed low-arc loop — we don't need those points
			m_TargetPath.clear();

			// Re-initialize movement sim so we can iterate predicted positions with lob angles
			if (F::MovementSimulation->Initialize(pPlayer))
			{
				const bool bLobStationary = F::MovementSimulation->IsStationary();

				for (int nTick = 0; nTick < nMaxSimTicks; nTick++)
				{
					// Build the path for visualization
					m_TargetPath.push_back(F::MovementSimulation->GetOrigin());

					if (!bLobStationary)
						F::MovementSimulation->RunTick();

					Vec3 vLobTarget = F::MovementSimulation->GetOrigin();
					OffsetPlayerPosition(pWeapon, vLobTarget, pPlayer, bDucked, bOnGround, vLocalPos);

					if (isTickTooEarly(vLobTarget, nTick))
						continue;

					float flTimeLob = 0.0f;
					Vec3 vAngleLob{};

					if (!CalcProjAngle(pLocal, pWeapon, vLocalPos, vLobTarget, vAngleLob, flTimeLob, true))
						continue;

					// Check sticky arm time
					if (bIsSticky && flTimeLob < flStickyArmTime)
						continue;

					if (flTimeLob > std::min(CFG::Aimbot_Projectile_Max_Simulation_Time, 7.0f))
						continue;

					// The predicted tick must match the flight time
					int nTargetTick = TIME_TO_TICKS(flTimeLob + flLatency);
					if (bIsSticky && TICKS_TO_TIME(nTargetTick) < flStickyArmTime)
						nTargetTick += TIME_TO_TICKS(fabsf(flTimeLob - flStickyArmTime));

					if (!(nTargetTick == nTick || nTargetTick == nTick - 1))
						continue;

					target.AngleTo = vAngleLob;
					target.TimeToTarget = flTimeLob;

					if (CanSee(pLocal, pWeapon, vLocalPos, vLobTarget, target, flTimeLob))
					{
						F::MovementSimulation->Restore();
						return true;
					}
				}

				F::MovementSimulation->Restore();
			}
		}
	}

	else
	{
		const Vec3 vTarget = target.Position;

		float flTimeToTarget = 0.0f;

		float flSplashRadius = 0.0f;
		{
			float flBaseRadius = 0.0f;
			switch (nWeaponID)
			{
			case TF_WEAPON_PIPEBOMBLAUNCHER:
				flBaseRadius = 146.0f;
				break;
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE:
				flBaseRadius = 110.0f;
				break;
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
				flBaseRadius = 146.0f;
				break;
			default:
				break;
			}

			if (flBaseRadius > 0.0f)
				flSplashRadius = SDKUtils::AttribHookValue(flBaseRadius, "mult_explosion_radius", pWeapon);
		}

		auto runSplash = [&]()
			{
				if (flSplashRadius <= 0.0f)
					return false;

				const float radius = std::min(flSplashRadius - 16.0f, 130.0f);

				const auto center{ target.Entity->GetCenter() };

				// Rotate sphere each tick
				const float flRotation = static_cast<float>(I::GlobalVars->tickcount % 360) * DEG2RAD(1.0f);
				const float flSinRotation = sinf(flRotation);
				const float flCosRotation = cosf(flRotation);
				// Sticky splash is expensive: cap points and checked candidates to avoid frame spikes.
				const int nSplashPointCount = bIsSticky
					? std::clamp(GetSplashPointCount() / 2, StickySplashMinPoints, StickySplashMaxPoints)
					: GetSplashPointCount();
				const auto& splashDirections = GetCachedSplashDirections(nSplashPointCount);

				Vec3 potential[MaxSplashPointCount];
				int nPotentialCount = 0;
				
				CTraceFilterWorldCustom filter{};

				for (int n = 0; n < nSplashPointCount; n++)
				{
					const Vec3 point = center + RotateSplashDirection(splashDirections[n], flSinRotation, flCosRotation).Scale(radius);

					trace_t trace{};
					H::AimUtils->Trace(center, point, MASK_SOLID, &filter, &trace);

					if (trace.fraction > 0.99f)
						continue;

					potential[nPotentialCount++] = trace.endpos;
				}

				std::sort(potential, potential + nPotentialCount, [&](const Vec3& a, const Vec3& b)
					{
						return a.DistToSqr(center) < b.DistToSqr(center);
					});

				const Vec3 vShootPos = GetOffsetShootPos(pLocal, pWeapon, pCmd);
				const Vec3 hull_min = { -8.0f, -8.0f, -8.0f };
				const Vec3 hull_max = { 8.0f,  8.0f,  8.0f };

				const int nCandidateCount = bIsSticky ? std::min(nPotentialCount, StickySplashCandidateBudget) : nPotentialCount;
				for (int i = 0; i < nCandidateCount; i++)
				{
					const Vec3& point = potential[i];
					if (!CalcProjAngle(pLocal, pWeapon, vLocalPos, point, target.AngleTo, target.TimeToTarget))
					{
						continue;
					}

					trace_t trace = {};
					CTraceFilterWorldCustom filter = {};

					H::AimUtils->TraceHull
					(
						vShootPos,
						point,
						hull_min,
						hull_max,
						MASK_SOLID,
						&filter,
						&trace
					);

					trace_t grateTrace{};
					CTraceFilterWorldCustom grateFilter{};
					H::AimUtils->Trace(trace.endpos, point, CONTENTS_GRATE, &grateFilter, &grateTrace);
					H::AimUtils->Trace(trace.endpos, point, MASK_SOLID, &filter, &trace);

					if (grateTrace.fraction < 1.0f)
					{
						if (!CanArcReach(pLocal, pWeapon, vLocalPos, point, target.AngleTo, target.TimeToTarget, target.Entity))
							continue;

						return true;
					}

					if (trace.fraction < 1.0f)
					{
						continue;
					}

					if (!CanArcReach(pLocal, pWeapon, vLocalPos, point, target.AngleTo, target.TimeToTarget, target.Entity))
						continue;

					return true;
				}

				return false;
			};

		if (!CalcProjAngle(pLocal, pWeapon, vLocalPos, vTarget, target.AngleTo, flTimeToTarget))
			return false;

		target.TimeToTarget = flTimeToTarget;

		int nTargetTick = TIME_TO_TICKS(flTimeToTarget + flLatency);

		if (bIsSticky)
		{
			nTargetTick += TIME_TO_TICKS(fabsf(flTimeToTarget - flStickyArmTime));
		}

		if (nTargetTick <= nMaxSimTicks)
		{
			if (CanSee(pLocal, pWeapon, vLocalPos, vTarget, target, flTimeToTarget))
			{
				return true;
			}
			if (CFG::Aimbot_Projectile_Rocket_Splash && runSplash())
			{
				return true;
			}

			// Lob angle fallback for buildings — try high arc if low arc can't reach
			// Excluded: grenade launcher, cannon (their -200 up velocity makes lob inaccurate)
			const bool bCanLobBuilding = m_CurProjInfo.GravityMod > 0.0f
				&& nWeaponID != TF_WEAPON_GRENADELAUNCHER
				&& nWeaponID != TF_WEAPON_CANNON;

			if (bCanLobBuilding)
			{
				float flTimeLob = 0.0f;
				if (CalcProjAngle(pLocal, pWeapon, vLocalPos, vTarget, target.AngleTo, flTimeLob, true))
				{
					target.TimeToTarget = flTimeLob;

					int nLobTargetTick = TIME_TO_TICKS(flTimeLob + flLatency);
					if (bIsSticky)
						nLobTargetTick += TIME_TO_TICKS(fabsf(flTimeLob - flStickyArmTime));

					if (nLobTargetTick <= nMaxSimTicks)
					{
						if (CanSee(pLocal, pWeapon, vLocalPos, vTarget, target, flTimeLob))
							return true;
					}
				}
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
	const int nWeaponID = pWeapon->GetWeaponID();
	const int nSortMode = CFG::Aimbot_Projectile_Sort;
	const bool bSortByFOV = nSortMode == 0;
	const float flFOVLimit = CFG::Aimbot_Projectile_FOV;

	m_vecTargets.clear();
	if (m_vecTargets.capacity() < 64)
		m_vecTargets.reserve(64);

	if (CFG::Aimbot_Target_Players)
	{
		const auto nGroup = nWeaponID == TF_WEAPON_CROSSBOW ? EEntGroup::PLAYERS_ALL : EEntGroup::PLAYERS_ENEMIES;

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
				if (nWeaponID == TF_WEAPON_CROSSBOW)
				{
					if (pPlayer->m_iHealth() >= pPlayer->GetMaxHealth() || pPlayer->IsInvulnerable())
					{
						continue;
					}
				}
			}

			Vec3 vPos = pPlayer->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = bSortByFOV ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;

			if (bSortByFOV && flFOVTo > flFOVLimit)
				continue;

			const float flDistTo = nSortMode == 1 ? vLocalPos.DistTo(vPos) : 0.0f;
			m_vecTargets.emplace_back(AimTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	if (CFG::Aimbot_Target_Buildings)
	{
		const auto isRescueRanger{ nWeaponID == TF_WEAPON_SHOTGUN_BUILDING_RESCUE };

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
			const float flFOVTo = bSortByFOV ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;

			if (bSortByFOV && flFOVTo > flFOVLimit)
				continue;

			const float flDistTo = nSortMode == 1 ? vLocalPos.DistTo(vPos) : 0.0f;
			m_vecTargets.emplace_back(AimTarget_t{ pBuilding, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	if (m_vecTargets.empty())
		return false;

	// Sort by target priority
	F::AimbotCommon->Sort(m_vecTargets, nSortMode);

	// OPTIMIZATION: Resize vector to max targets AFTER sorting to avoid processing extras
	// This prevents unnecessary SolveTarget() calls on targets we'll never use
	const int nMaxTargets = CFG::Aimbot_Projectile_Max_Processing_Targets;
	if (static_cast<int>(m_vecTargets.size()) > nMaxTargets)
		m_vecTargets.resize(nMaxTargets);
	
	// Process only the targets we kept after resize
	for (auto& target : m_vecTargets)
	{
		if (!SolveTarget(pLocal, pWeapon, pCmd, target))
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

	// Neckbreaker: preserve the roll component set during target solving
	if (CFG::Aimbot_Projectile_Neckbreaker && vAngles.z != 0.f)
		vAngleTo.z = vAngles.z;

	switch (CFG::Aimbot_Projectile_Aim_Type)
	{
	case 0:
	{
		pCmd->viewangles = vAngleTo;
		break;
	}

	case 1:
	{
		// Silent — apply aimbot angles, hide from local view.
		// Only use pSilent (packet choke) when actually firing AND not reloading.
		// pSilent during reload is unreliable - by the time the choked packet reaches
		// the server, the reload animation may have progressed past the interrupt point.
		// Amalgam returns G::Attacking=2 during reload, which doesn't trigger pSilent.
		H::AimUtils->FixMovement(pCmd, vAngleTo);
		pCmd->viewangles = vAngleTo;

		if (m_CurProjInfo.Flamethrower)
			G::bSilentAngles = true;  // Flamethrower: continuous, never choke
		else if (Shifting::bShifting && Shifting::bShiftingWarp)
			G::bSilentAngles = true;  // Warp: choke handled by warp system
		else if (G::bFiring && !G::bReloading)
			G::bPSilentAngles = true;  // Firing tick (not during reload): pSilent choke
		else if (G::bFiring && G::bReloading)
			G::bSilentAngles = true;  // Firing during reload: use silent but don't choke (Amalgam style)
		else
			G::bSilentAngles = true;  // Not firing: just hide local view, no choke

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

	// Clear cached prediction result only. Used before re-predict and on stale cache.
	// Add new cache members here — all three clear sites (predict-start, stale-cache, full-reset) depend on this.
	auto ClearCacheData = [&]()
	{
		m_bCachedHasTarget = false;
		m_CachedTarget = {};              // null out Entity ptr — prevents UAF on dangling pointer
		m_iCachedTargetIndex = 0;
		m_vCachedTargetPath.clear();
	};

	// Full session reset — clears cache + throttle + charge state.
	// Used on early returns (aimkey release, shifting, weapon change) where the
	// entire aimbot session is ending, not just refreshing a cache entry.
	auto InvalidateCache = [&]()
	{
		ClearCacheData();
		m_bPredictionSession = false;
		m_bWasReadyToFire = false;
		m_bChargePending = false;         // stale pending flag causes false cancel of manual charges
		m_vChargeAngles = {};             // stale angles from previous session
	};

	if (!GetProjectileInfo(pWeapon))
	{
		InvalidateCache();
		m_iCachedWeaponID = 0;
		return;
	}

	// Invalidate cache if weapon changed (e.g., rocket → grenade launcher)
	// Cached angles are computed for a specific projectile speed/arc
	const int nWeaponID = pWeapon->GetWeaponID();
	if (m_iCachedWeaponID != 0 && m_iCachedWeaponID != nWeaponID)
		InvalidateCache();
	m_iCachedWeaponID = nWeaponID;

	if (Shifting::bShifting && !Shifting::bShiftingWarp)
	{
		InvalidateCache(); // shift may last multiple ticks, leaving throttle state stale
		return;
	}

	if (!CFG::Aimbot_Always_On && !H::Input->IsDown(CFG::Aimbot_Key))
	{
		InvalidateCache();
		return;
	}

	// Handle charge weapons (sticky and loose cannon)
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

	// Throttle expensive prediction (GetTarget) to ~17Hz.
	// Force fresh prediction when weapon just became ready to fire (transition tick)
	// or during active charge — stale angles cause missed shots.
	// Firing checks, aim, and drawing still run every tick using cached results.
	const int nPredictInterval = TIME_TO_TICKS(1.0f / 17.0f); // ~59ms = ~17Hz at 66tick

	// Check if weapon can fire now
	const int nSavedTickBaseEarly = pLocal->m_nTickBase();
	pLocal->m_nTickBase() = nSavedTickBaseEarly + 1;
	const bool bCanFireNowEarly = pWeapon->CanPrimaryAttack(pLocal) && pWeapon->HasPrimaryAmmoForShot();
	pLocal->m_nTickBase() = nSavedTickBaseEarly;
	const bool bCanInterruptReloadEarly = CanInterruptReload(pWeapon);
	const bool bCanFireEarly = bCanFireNowEarly || bCanInterruptReloadEarly;

	// Force prediction on the tick the weapon BECOMES ready (transition from can't-fire to can-fire)
	const bool bJustBecameReady = bCanFireEarly && !m_bWasReadyToFire;
	m_bWasReadyToFire = bCanFireEarly;

	const bool bForcePredict = (bIsCharging && m_bChargePending) || bJustBecameReady;
	const bool bShouldPredict = bForcePredict || (I::GlobalVars->tickcount - m_nLastPredictTick >= nPredictInterval) || !m_bPredictionSession;

	ProjTarget_t target = {};
	bool bHasTarget = false;

	if (bShouldPredict)
	{
		m_nLastPredictTick = I::GlobalVars->tickcount;
		ClearCacheData();                  // clear stale prediction before re-running GetTarget
		m_bPredictionSession = true; // mark session as started even if no target found

		bHasTarget = GetTarget(pLocal, pWeapon, pCmd, target) && target.Entity;

		if (bHasTarget)
		{
			// Cache prediction result for intermediate ticks
			m_bCachedHasTarget = true;
			m_CachedTarget = target;
			m_iCachedTargetIndex = target.Entity ? target.Entity->entindex() : 0;
			m_vCachedTargetPath = m_TargetPath;
		}
	}
	else
	{
		// Non-prediction tick: use cached result
		bool bCacheValid = false;

		if (m_bCachedHasTarget && m_CachedTarget.Entity && H::Entities->SafeIsEntityValid(m_CachedTarget.Entity, m_iCachedTargetIndex))
		{
			// Pointer is valid — check if entity is still alive and not dormant.
			// Must check type via GetClassId before casting — As<T>() is static_cast
			// and casting a building to C_TFPlayer is UB (unrelated hierarchy).
			const auto eClassId = m_CachedTarget.Entity->GetClassId();

			if (eClassId == ETFClassIds::CTFPlayer)
			{
				const auto pPlayer = static_cast<C_TFPlayer*>(m_CachedTarget.Entity);
				if (!pPlayer->deadflag() && !pPlayer->IsDormant())
				{
					bCacheValid = true;
					bHasTarget = true;
					target = m_CachedTarget;
					m_TargetPath = m_vCachedTargetPath;
				}
			}
			else if (eClassId == ETFClassIds::CObjectSentrygun || eClassId == ETFClassIds::CObjectDispenser || eClassId == ETFClassIds::CObjectTeleporter)
			{
				const auto pBuilding = static_cast<C_BaseObject*>(m_CachedTarget.Entity);
				if (pBuilding->m_iHealth() > 0 && !pBuilding->m_bCarried() && !pBuilding->m_bPlacing() && !pBuilding->IsDormant())
				{
					bCacheValid = true;
					bHasTarget = true;
					target = m_CachedTarget;
					m_TargetPath = m_vCachedTargetPath;
				}
			}
			// Unknown entity type — don't use cache, force re-predict
		}

		if (!bCacheValid)
		{
			// Cache is stale/invalid — clear everything and force predict next tick
			ClearCacheData();
			m_nLastPredictTick = 0;
			bHasTarget = false;
		}
	}

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

	if (bHasTarget)
	{
		G::nTargetIndexEarly = target.Entity->entindex();
		G::nTargetIndex = target.Entity->entindex();

		// bCanFireNowEarly and bCanInterruptReloadEarly already computed above for throttle decision
		const bool bOldCanPrimaryAttack = G::bCanPrimaryAttack;
		G::bCanPrimaryAttack = bCanFireNowEarly || bCanInterruptReloadEarly;

		if (ShouldFire(pCmd, pLocal, pWeapon))
		{
			HandleFire(pCmd, pWeapon, pLocal, target);
		}

		const bool bIsFiring = IsFiring(pCmd, pLocal, pWeapon);
		G::bFiring = bIsFiring;

		if (ShouldAim(pCmd, pLocal, pWeapon) || bIsFiring)
		{
			// For charge weapons with pSilent: use saved angles on release tick
			// This ensures the angles match what we calculated when starting the charge
			Vec3 vFinalAngles = target.AngleTo;
			
			// If we're firing a charge weapon and have saved angles, use those instead
			if (bIsFiring && bIsChargeWeapon && !m_vChargeAngles.IsZero())
			{
				vFinalAngles = m_vChargeAngles;
			}
			// Dodge prediction removed with behavior system removal

			Aim(pCmd, pLocal, pWeapon, vFinalAngles);
			
			// Clear saved angles after firing
			if (bIsFiring && bIsChargeWeapon)
				m_vChargeAngles = {};

			if (bIsFiring && !m_TargetPath.empty())
			{
				I::DebugOverlay->ClearAllOverlays();
				DrawProjPath(pCmd, target.TimeToTarget);
				DrawMovePath(m_TargetPath);


				m_TargetPath.clear();
			}
		}

		G::bCanPrimaryAttack = bOldCanPrimaryAttack;
	}
}
