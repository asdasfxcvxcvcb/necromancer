#include "AimbotProjectileAmalgam.h"

#include "../ProjectileSimulation/ProjectileSimulation.h"
#include "../World/World.h"
#include "../Ticks/Ticks.h"
#include "../../Players/Players.h"
#include "../../EnginePrediction/EnginePrediction.h" // RestoreInfo_t
#include "../../CFG.h"
#include "../../MoveSimDebug/MoveSimDebug.h"
#include "../../../../Utils/Math/Math.h" // Math::ClampAngles / CalcFov / AngleVectors

#include <cmath>
#include <functional>
#include <numeric>

// Amalgam's grenade auto-detonation quantum (also used by ProjSim lifetime math)
#ifndef GRENADE_CHECK_INTERVAL
	#define GRENADE_CHECK_INTERVAL 0.195f
#endif

// world coordinate extent (Amalgam constant, used for the underwater priority)
#ifndef COORD_EXTENT
	#define COORD_EXTENT 16384.f
#endif

// explosion radii (Amalgam's Definitions.h)
#ifndef TF_ROCKET_RADIUS
	#define TF_ROCKET_RADIUS 146.f
#endif
#ifndef TF_FLARE_DET_RADIUS
	#define TF_FLARE_DET_RADIUS 110.f
#endif
#ifndef JAR_EXPLODE_RADIUS
	#define JAR_EXPLODE_RADIUS 200.f
#endif

// Euler's number (Amalgam's Math::E)
static constexpr float MATH_E = 2.718281828459f;

// Shoot position via the ported Ticks (accounts for predicted ducking), with a
// fallback for the first ticks before it has saved a position
static Vec3 GetShootPos(C_TFPlayer* pLocal)
{
	Vec3 vPos = F::AmalgamTicks->GetShootPos();
	return vPos.IsZero() ? pLocal->GetShootPos() : vPos;
}

// Amalgam's ArmTime: sticky arm time when the UseArmTime modifier is set
static float ArmTime(C_TFWeaponBase* pWeapon)
{
	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::UseArmTime && pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		static auto tf_grenadelauncher_livetime = U::ConVars.FindVar("tf_grenadelauncher_livetime");
		const float flLiveTime = tf_grenadelauncher_livetime->GetFloat();
		return SDK::AttribHookValue(flLiveTime, "sticky_arm_time", pWeapon);
	}

	return 0.f;
}

static bool ShouldLob(Info_t& tInfo)
{
	return tInfo.m_flGravity && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::LobAngles;
}

static bool ShouldLob(AmalgamMoveStorage& tMoveStorage, Info_t& tInfo)
{
	return tInfo.m_bIgnoreTiming && (tMoveStorage.m_bFailed || tMoveStorage.m_pPlayer->m_fFlags() & FL_ONGROUND);
}

static bool AirSplash(C_TFWeaponBase* pWeapon, Info_t& tInfo)
{
	if (!(Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::AirSplash) || tInfo.m_pProjectile)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_FLAREGUN:
		return true;
	}

	return false;
}

static int GetHitboxPriority(int nHitbox, Target_t& tTarget, Info_t& tInfo, C_BaseEntity* pProjectile = nullptr)
{
	if (!F::AimbotGlobal->IsHitboxValid(nHitbox, Vars::Aimbot::Projectile::Hitboxes.Value))
		return -1;

	int iHeadPriority = 0;
	int iBodyPriority = 1;
	int iFeetPriority = 2;

	if (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Auto)
	{
		bool bHeadshot = Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Head
			&& tTarget.m_iTargetType == TargetEnum::Player;
		if (bHeadshot)
		{
			if (!pProjectile)
				bHeadshot = tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW;
			else
				bHeadshot = pProjectile->GetClassId() == ETFClassIds::CTFProjectile_Arrow && CanHeadshot(pProjectile->As<C_TFProjectile_Arrow>());

			// Amalgam also runs this for the bow; the charge damage check only
			// makes sense for the sticky launcher, so gate it on the weapon
			// instead of reading pipebomb netvars off whatever we hold
			if (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::BodyaimIfLethal
				&& bHeadshot && !pProjectile && tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER
				&& tInfo.m_pWeapon->m_hOwner().GetEntryIndex() == I::EngineClient->GetLocalPlayer())
			{
				float flCharge = I::GlobalVars->curtime - tInfo.m_pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime();
				float flDamage = Math::RemapVal(flCharge, 0.f, 1.f, 50.f, 120.f);
				if (tInfo.m_pLocal->IsMiniCritBoosted())
					flDamage *= 1.36f;
				if (flDamage >= tTarget.m_pEntity->As<C_TFPlayer>()->m_iHealth())
					bHeadshot = false;

				if (tInfo.m_pLocal->IsCritBoosted()) // for reliability
					bHeadshot = false;
			}
		}
		if (bHeadshot)
			tTarget.m_nAimedHitbox = HITBOX_HEAD;

		const bool bLower = Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::PrioritizeFeet
			&& tInfo.m_flRadius > 0.f
			&& tTarget.m_iTargetType == TargetEnum::Player && tTarget.m_pEntity->As<C_TFPlayer>()->m_fFlags() & FL_ONGROUND;

		iHeadPriority = bHeadshot ? 0 : 2;
		iBodyPriority = bHeadshot ? -1 : bLower ? 1 : 0;
		iFeetPriority = bHeadshot ? -1 : bLower ? 0 : 1;
	}

	switch (nHitbox)
	{
	case BOUNDS_HEAD: return iHeadPriority;
	case BOUNDS_BODY: return iBodyPriority;
	case BOUNDS_FEET: return iFeetPriority;
	}

	return -1;
}

// Amalgam's Huntsman head point: bone-accurate head center shifted by the
// aim-bias vars, clamped into the bbox
Directs_t CAmalgamAimbotProjectile::GetDirects()
{
	Directs_t mDirects = {};

	if (Vars::Aimbot::Projectile::SplashPrediction.Value == Vars::Aimbot::Projectile::SplashPredictionEnum::Only && m_tInfo.m_flRadius)
		return mDirects;

	auto& tTarget = *m_tInfo.m_pTarget;
	int iFlags = PointFlagsEnum::Regular;
	if (ShouldLob(m_tInfo))
		iFlags |= PointFlagsEnum::Lob;

	const Vec3 vMins = tTarget.m_pEntity->m_vecMins(), vMaxs = tTarget.m_pEntity->m_vecMaxs();
	for (int i = 0; i < 3; i++)
	{
		const int iPriority = GetHitboxPriority(i, tTarget, m_tInfo, m_tInfo.m_pProjectile);
		if (iPriority == -1)
			continue;

		switch (i)
		{
		case BOUNDS_HEAD:
			if (tTarget.m_nAimedHitbox == HITBOX_HEAD)
			{
				auto aBones = F::Backtrack.GetBones(tTarget.m_pEntity);
				if (!aBones)
					break;

				Vec3 vCenter, vBBoxMins, vBBoxMaxs;
				GetHitboxInfoFromBones(tTarget.m_pEntity->As<C_BaseAnimating>(), aBones, HITBOX_HEAD, &vCenter, &vBBoxMins, &vBBoxMaxs);
				Vec3 vOff = vCenter + (vBBoxMins + vBBoxMaxs) / 2 - tTarget.m_pEntity->m_vecOrigin();

				float flLow = 0.f;
				const Vec3 vDelta = tTarget.m_vPos + m_tInfo.m_vTargetEye - m_tInfo.m_vLocalEye;
				if (vDelta.z > 0)
				{
					const float flXY = vDelta.Length2D();
					if (flXY)
						flLow = Math::RemapVal(vDelta.z / flXY, 0.f, 0.5f, 0.f, 1.f);
					else
						flLow = 1.f;
				}

				const float flLerp = (Vars::Aimbot::Projectile::HuntsmanLerp.Value + (Vars::Aimbot::Projectile::HuntsmanLerpLow.Value - Vars::Aimbot::Projectile::HuntsmanLerp.Value) * flLow) / 100.f;
				const float flAdd = Vars::Aimbot::Projectile::HuntsmanAdd.Value + (Vars::Aimbot::Projectile::HuntsmanAddLow.Value - Vars::Aimbot::Projectile::HuntsmanAdd.Value) * flLow;
				vOff.z += flAdd;
				vOff.z = vOff.z + (vMaxs.z - vOff.z) * flLerp;

				vOff.x = std::clamp(vOff.x, vMins.x + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.x - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				vOff.y = std::clamp(vOff.y, vMins.y + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.y - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				vOff.z = std::clamp(vOff.z, vMins.z + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.z - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				mDirects[iPriority] = { vOff, iFlags };
			}
			else
				mDirects[iPriority] = { Vec3(0, 0, vMaxs.z - Vars::Aimbot::Projectile::VerticalShift.Value), iFlags };
			break;
		case BOUNDS_BODY:
			mDirects[iPriority] = { Vec3(0, 0, (vMaxs.z - vMins.z) / 2), iFlags };
			break;
		case BOUNDS_FEET:
			mDirects[iPriority] = { Vec3(0, 0, vMins.z + Vars::Aimbot::Projectile::VerticalShift.Value), iFlags };
			break;
		}
	}

	return mDirects;
}

Splashes_t CAmalgamAimbotProjectile::GetSplashes()
{
	Splashes_t vSplashes = {};

	if (Vars::Aimbot::Projectile::SplashPrediction.Value == Vars::Aimbot::Projectile::SplashPredictionEnum::Off || !m_tInfo.m_flRadius)
		return vSplashes;

	vSplashes.push_back(PointFlagsEnum::Regular);
	if (ShouldLob(m_tInfo))
		vSplashes.push_back(PointFlagsEnum::Lob);

	return vSplashes;
}

// Fibonacci sphere points around the target (Amalgam's ComputePoints)
static std::vector<Vec3> ComputePoints(float flRadius, int iSamples)
{
	std::vector<Vec3> vPoints = { { Vec3(0.f, 0.f, -1.f) * flRadius } };
	if (!iSamples)
		return vPoints;

	vPoints.reserve(iSamples + 1);

	const float flRotateX = Vars::Aimbot::Projectile::SplashRotateX.Value < 0.f ? SDK::StdRandomFloat(0.f, 360.f) : Vars::Aimbot::Projectile::SplashRotateX.Value;
	const float flRotateY = Vars::Aimbot::Projectile::SplashRotateY.Value < 0.f ? SDK::StdRandomFloat(0.f, 360.f) : Vars::Aimbot::Projectile::SplashRotateY.Value;

	constexpr float MATH_PI = 3.14159265358979f; // not "PI" - Utils/Math/Math.h #defines it
	const float a = MATH_PI * (3.f - sqrtf(5.f));
	for (int n = 0; n < iSamples; n++)
	{
		const float t = a * n;
		const float y = 1 - (n / (iSamples - 1.f)) * 2;
		const float r = sqrtf(1 - powf(y, 2));
		const float x = cosf(t) * r;
		const float z = sinf(t) * r;

		Vec3 vPoint = Vec3(x, y, z) * flRadius;
		vPoint = Math::RotatePoint(vPoint, {}, { flRotateX, flRotateY, 0.f });

		vPoints.push_back(vPoint);
	}

	return vPoints;
}

// Amalgam's HandleTrace: validate a candidate splash point with an outward
// hull trace plus (for points behind the target) inward hull traces
static void HandleTrace(const Vec3& vPoint, std::vector<Setup_t>& vPoints, const Vec3& vTargetEye, Info_t& tInfo, std::function<bool()> fCheckPointTrace, std::function<bool()> fCheckPointAir, CGameTrace& trace, ITraceFilter& filter)
{
	// out
	SDK::TraceHull(vTargetEye, vPoint, -tInfo.m_vHull, tInfo.m_vHull, MASK_SOLID, &filter, &trace);

	if (fCheckPointAir())
		return;

	if (fCheckPointTrace())
		vPoints.emplace_back(trace.endpos);

	// in
	if ((tInfo.m_vLocalEye - vTargetEye).Dot(vTargetEye - vPoint) > 0.f)
		return;

	SDK::Trace(vPoint, vTargetEye, MASK_SHOT, &filter, &trace);
	if (trace.DidHit())
		return;

	SDK::TraceHull(vPoint, vTargetEye, -tInfo.m_vHull, tInfo.m_vHull, MASK_SOLID, &filter, &trace);

	if (fCheckPointTrace())
		vPoints.emplace_back(trace.endpos);
}

// Amalgam's HandleFace: sample splash points off a cached world-geometry
// polygon - closest point, random edge points and random interior points,
// pushed out along the face normal so the projectile lands on the surface
static float s_flTotal = 0.f;
static void HandleFace(Face_t& tFace, std::vector<Setup_t>& vPoints, float flDensity, float flRadius, float flCutoff, const Vec3& vTargetEye, const Vec3& vTargetCenter, const Vec3& vTargetOrigin, Info_t& tInfo, CGameTrace& trace, ITraceFilter& filter)
{
	if (tFace.m_vVertices.size() < 3)
		return;
	float flRadiusSqr = powf(flRadius, 2);
	float flRadius2Sqr = flRadiusSqr * 4;
	int nMask = F::ProjSim.m_bPhysics ? MASK_SHOT | CONTENTS_DISPSOLID : MASK_SHOT;

	std::vector<Vec3> vVertices, vEpsilon;
	if (tFace.m_iType != FaceTypeEnum::Prop)
		vVertices = tFace.m_vVertices;
	else
		Math::ExpandPolygon(vVertices, tFace.m_vVertices, tFace.m_vNormal, SDK::StdRandomFloat(0.f, DIST_EPSILON), &vTargetEye);

	for (int i = 0, n = int(tFace.m_vVertices.size()), o = SDK::StdRandomInt(0, n - 1); ++i < n - 1;)
	{
		Vec3& vVertex1 = tFace.m_vVertices[o], &vVertex2 = tFace.m_vVertices[(o + i) % n], &vVertex3 = tFace.m_vVertices[(o + i + 1) % n];

		Vec3 vDir21 = vVertex2 - vVertex1, vDir31 = vVertex3 - vVertex1;
		float flArea = vDir21.Cross(vDir31).Length() / 2;
		float flSamples = flDensity * flArea / flRadius2Sqr;
		int iSamples = flSamples > flCutoff ? ceilf(flSamples) : fmodf(s_flTotal += flSamples, flCutoff) < flSamples;
		if (!iSamples)
			continue;

		// don't particularly like the hacky random epsilons
		int iFaceClosest = SDK::StdRandomInt(iSamples < 2 ? 0 : iSamples < 4 ? 1 : 2, iSamples < 2 ? 1 : 2);
		//int iEdgeClosest = SDK::StdRandomInt(iSamples < 8 ? 0 : iSamples < 16 ? 1 : 2, iSamples < 8 ? 1 : 2); // eats up a bit too much performance for my liking
		int iEdgeRandom = SDK::StdRandomInt(iSamples < 8 ? 0 : iSamples < 16 ? 1 : 2, iSamples < 3 && iFaceClosest ? 0 : iSamples < 12 ? 1 : 2);
		iEdgeRandom += iFaceClosest; //iEdgeClosest += iFaceClosest, iEdgeRandom += iEdgeClosest; //, iSamples += iEdgeRandom;
		for (int s = 0; s < iSamples; s++)
		{
			Vec3 vPoint, vNormal = tFace.m_vNormal; bool bInside = true;
			if (s < iFaceClosest) // closest point
			{
				float flEpsilon = s == 0 && (tFace.m_iType == FaceTypeEnum::BoxBrush || iFaceClosest != 1 || SDK::StdRandomBool()) ? CALC_EPSILON : DIST_EPSILON;
				vPoint = Math::ClosestPointOnTriangle(vTargetOrigin, vVertex1, vVertex2, vVertex3, &bInside);
				vPoint += Vec3(SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon));
			}
			//else if (s < iEdgeClosest) // closest point on edge
			//{
			//	float flEpsilon = SDK::StdRandomBool() ? CALC_EPSILON : DIST_EPSILON; bInside = false;
			//	switch (SDK::StdRandomInt(0, 2))
			//	{
			//	case 0: vPoint = Math::ClosestPointOnLine(vTargetOrigin, vVertex1, vVertex2); break;
			//	case 1: vPoint = Math::ClosestPointOnLine(vTargetOrigin, vVertex2, vVertex3); break;
			//	case 2: vPoint = Math::ClosestPointOnLine(vTargetOrigin, vVertex3, vVertex1); break;
			//	}
			//	vPoint += { SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon) };
			//}
			else if (s < iEdgeRandom) // random point on edge
			{
				float flEpsilon = SDK::StdRandomBool() ? CALC_EPSILON : DIST_EPSILON; bInside = false;
				switch (SDK::StdRandomInt(0, 2))
				{
				case 0: vPoint = vVertex1.Lerp(vVertex2, SDK::StdRandomFloat()); break;
				case 1: vPoint = vVertex2.Lerp(vVertex3, SDK::StdRandomFloat()); break;
				case 2: vPoint = vVertex3.Lerp(vVertex1, SDK::StdRandomFloat()); break;
				}
				vPoint += Vec3(SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon));
			}
			else // random point on face
			{
				float flRandom1 = SDK::StdRandomFloat(), flRandom2 = SDK::StdRandomFloat();
				if (flRandom1 + flRandom2 > 1)
					flRandom1 = 1 - flRandom1, flRandom2 = 1 - flRandom2;
				vPoint = vVertex1 + vDir21 * flRandom1 + vDir31 * flRandom2;
			}
			vPoint += vNormal * (tInfo.m_vHull + CALC_EPSILON);
			if (vPoint.DistToSqr(vTargetCenter) > flRadiusSqr)
				continue;

			if (!bInside)
			{
				if (vEpsilon.empty())
					Math::ExpandPolygon(vEpsilon = vVertices, tFace.m_vNormal, -DIST_EPSILON);

				Vec3 vPolygonNormal = vNormal;
				vPoint = Math::ClosestPointOnPolygon(vPoint, vEpsilon, vPolygonNormal, &bInside);

				if (tFace.m_iType == FaceTypeEnum::Prop && !bInside)
					vPoint += tFace.m_vNormal * SDK::StdRandomFloat(0.f, DIST_EPSILON);
			}

			if (bInside && I::EngineTrace->GetPointContents(vPoint) & MASK_SOLID)
				continue;

			int nSubMask = nMask;
			if (!bInside)
				vNormal = (vTargetEye - vPoint).Normalized(), nSubMask &= ~CONTENTS_MOVEABLE;

			SDK::Trace(vPoint + vNormal * tInfo.m_flNormalOffset, vTargetEye, nSubMask, &filter, &trace);
			if (trace.fraction != 1.f)
				continue;

			vPoints.emplace_back(vPoint);
		}
	}
}

void CAmalgamAimbotProjectile::SetupSplashPoints(Vec3& vOrigin, std::vector<Setup_t>& vSplashPoints, int iFlags)
{
	vSplashPoints.clear();

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnlyAmalgam filter = {};

	m_tInfo.m_pTarget->m_vPos = vOrigin;
	const Vec3 vTargetEye = vOrigin + m_tInfo.m_vTargetEye;
	const Vec3 vTargetCenter = vOrigin + (m_tInfo.m_pTarget->m_pEntity->m_vecMins() + m_tInfo.m_pTarget->m_pEntity->m_vecMaxs()) / 2;
	const float flRadius = m_tInfo.m_flRadius + m_tInfo.m_pTarget->m_pEntity->GetSize().Length() / 2;
	const bool bAirSplash = AirSplash(m_tInfo.m_pWeapon, m_tInfo);

	auto fCheckNormal = [&](const Vec3& vNormal, const Vec3& vPoint, Vec3* pAngle = nullptr)
	{
		if (pAngle)
		{
			Vec3 vForward, vRight, vUp; Math::AngleVectors(*pAngle, &vForward, &vRight, &vUp);
			const Vec3 vShootPos = m_tInfo.m_vLocalEye + vForward * m_tInfo.m_vOffset.x + vRight * m_tInfo.m_vOffset.y + vUp * m_tInfo.m_vOffset.z;
			vForward = (vShootPos - vPoint).Normalized();
			return vForward.Dot(vNormal) > 0;
		}
		else
		{
			const Vec3 vForward = (m_tInfo.m_vLocalEye - vPoint).Normalized();
			return vForward.Dot(vNormal) > 0;
		}
	};

	// Trace mode samples a sphere around the target with hull traces; Face
	// mode (default) samples the map's own collision geometry through
	// F::AmalgamWorld (Amalgam's F::World port) instead - in Face mode the
	// sphere contributes only its straight-down center point, exactly like
	// Amalgam: the remaining sphere points would just offer candidates on
	// geometry the face samples already cover, further from the enemy
	const bool bFaceMode = Vars::Aimbot::Projectile::SplashMode.Value == Vars::Aimbot::Projectile::SplashModeEnum::Face;
	int iPoints = bFaceMode && !bAirSplash ? 0
		: !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashPointsDirect.Value : Vars::Aimbot::Projectile::SplashPointsArc.Value;
	{
		auto vPoints = ComputePoints(flRadius, iPoints);

		for (int i = 0; i < vPoints.size(); i++)
		{
			auto fCheckPointTrace = [&]()
			{
				if (!trace.m_pEnt || trace.fraction == 1.f || trace.surface.flags & SURF_SKY || !trace.m_pEnt->GetAbsVelocity().IsZero())
					return false;

				Vec3 vPoint = trace.endpos, vAngle;
				if (!m_tInfo.m_flGravity)
					vAngle = Math::CalcAngle(m_tInfo.m_vLocalEye, trace.endpos);
				else
				{
					Point_t tPoint = { vPoint, {} };
					CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, 0, tPoint.m_tSolution, iFlags);
					if (tPoint.m_tSolution.m_iCalculated == CalculatedEnum::Bad)
						return false;
					vPoint -= Vec3(0, 0, m_tInfo.m_flGravity * powf(tPoint.m_tSolution.m_flTime, 2) / 2);
					vAngle = Vec3(tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw, 0.f);
				}
				return fCheckNormal(trace.plane.normal, vPoint, &vAngle);
			};

			auto fCheckPointAir = [&]()
			{
				if (bAirSplash && !trace.DidHit())
				{
					if (!Vars::Aimbot::Projectile::SplashAirCount.Value)
						vSplashPoints.emplace_back(vPoints[i] * SDK::StdRandomFloat(0.f, 1.f) + vTargetCenter, PointTypeEnum::Air);
					else for (float r = 0; r < Vars::Aimbot::Projectile::SplashAirCount.Value; r++)
						vSplashPoints.emplace_back(vPoints[i] * r / Vars::Aimbot::Projectile::SplashAirCount.Value + vTargetCenter, PointTypeEnum::Air);
				}
				if (bFaceMode && i) // Amalgam skips non-center sphere points in Face mode
					return true;
				return !trace.DidHit();
			};

			const Vec3 vPoint = vPoints[i] + vTargetCenter;

			Solution_t tSolution; CalculateAngle(m_tInfo.m_vLocalEye, vPoint, 0, tSolution, iFlags);
			if (tSolution.m_iCalculated == CalculatedEnum::Bad)
				continue;

			HandleTrace(vPoint, vSplashPoints, vTargetEye, m_tInfo, fCheckPointTrace, fCheckPointAir, trace, filter);
		}
	}

	// Face
	if (float flDensity = !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashDensityDirect.Value : Vars::Aimbot::Projectile::SplashDensityArc.Value;
		bFaceMode && flDensity)
	{
		Vec3 vMins = vTargetCenter - flRadius, vMaxs = vTargetCenter + flRadius;

		NormalValidCallback fNormalValid = [&](const std::vector<Vec3>& vVertices, const Vec3& vNormal)
		{
			Vec3 vPoint = (std::accumulate(vVertices.begin(), vVertices.end(), Vec3())) / float(vVertices.size());
			Vec3 vAngle;
			if (!m_tInfo.m_flGravity)
				vAngle = Math::CalcAngle(m_tInfo.m_vLocalEye, vPoint);
			else
			{
				Point_t tPoint = { vPoint, {} };
				CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, 0, tPoint.m_tSolution, iFlags);
				if (tPoint.m_tSolution.m_iCalculated == CalculatedEnum::Bad)
					return false;
				vPoint -= Vec3(0, 0, m_tInfo.m_flGravity * powf(tPoint.m_tSolution.m_flTime, 2) / 2);
				vAngle = Vec3(tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw, 0.f);
			}
			return fCheckNormal(vNormal, vPoint, &vAngle);
		};
		F::AmalgamWorld->SetNormalValidCallback(&fNormalValid);
		std::vector<Face_t> vFaces = F::AmalgamWorld->GetFacesInAABB(vMins, vMaxs, MASK_SOLID, &filter);
		F::AmalgamWorld->SetNormalValidCallback();

		float flCutoff = Vars::Aimbot::Projectile::SplashSamplesCutoff.Value * powf(float(vFaces.size()), 2); s_flTotal = SDK::StdRandomFloat();
		for (auto& tFace : vFaces)
		{
			HandleFace(tFace, vSplashPoints, flDensity, flRadius, flCutoff, vTargetEye, vTargetCenter, vOrigin, m_tInfo, trace, filter);
		}
	}

	if (vSplashPoints.size() > 1)
	{	// Amalgam shuffles everything but the first point (std::shuffle)
		for (int i = int(vSplashPoints.size()) - 1; i > 0; i--)
			std::swap(vSplashPoints[i], vSplashPoints[SDK::StdRandomInt(1, i)]);
	}
}

std::vector<Point_t> CAmalgamAimbotProjectile::GetSplashPoints(Vec3 vOrigin, std::vector<Setup_t>& vSplashPoints, int iSimTime, int iFlags, bool bFirst)
{
	std::vector<Point_t> vPoints = {};

	m_tInfo.m_pTarget->m_vPos = vOrigin;
	const bool bLob = iFlags & CalculateFlagsEnum::LobAngle;
	const int iTolerance = m_tInfo.m_bIgnoreTiming && bLob ? std::numeric_limits<int>::max() : m_tInfo.m_iArmTime ? -1 : 0;
	const int iLimit = !bFirst ? m_tInfo.m_iSplashRestrict : std::max(m_tInfo.m_iSplashRestrict, Vars::Aimbot::Projectile::SplashRestrictFirst.Value);
	const bool bSort = !bLob || !m_tInfo.m_bIgnoreTiming;

	const float flRadiusSqr = powf(m_tInfo.m_flRadius, 2);
	float flRadiusAirSqr = flRadiusSqr;
	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::AirSplash && !m_tInfo.m_pProjectile && m_tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{	// sticky airdet radius ramps in over tf_sticky_radius_ramp_time
		static auto tf_grenadelauncher_livetime = U::ConVars.FindVar("tf_grenadelauncher_livetime");
		static auto tf_sticky_radius_ramp_time = U::ConVars.FindVar("tf_sticky_radius_ramp_time");
		static auto tf_sticky_airdet_radius = U::ConVars.FindVar("tf_sticky_airdet_radius");
		const float flLiveTime = tf_grenadelauncher_livetime->GetFloat();
		const float flRampTime = tf_sticky_radius_ramp_time->GetFloat();
		const float flAirdetRadius = tf_sticky_airdet_radius->GetFloat();
		flRadiusAirSqr = powf(m_tInfo.m_flRadius * Math::RemapVal(TICKS_TO_TIME(iSimTime), flLiveTime, flLiveTime + flRampTime, flAirdetRadius, 1.f), 2);
	}

	for (auto it = vSplashPoints.begin(); it != vSplashPoints.end();)
	{
		Point_t tPoint = { it->m_vPoint, {}, it->m_iType };

		CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, iSimTime, tPoint.m_tSolution, iFlags, iTolerance);
		if (tPoint.m_tSolution.m_iCalculated != CalculatedEnum::Good)
		{
			if (tPoint.m_tSolution.m_iCalculated == CalculatedEnum::Bad)
				it = vSplashPoints.erase(it);
			else
				++it;
			continue;
		}
		else if (tPoint.m_iType == PointTypeEnum::Air && tPoint.m_tSolution.m_flTime < TICKS_TO_TIME(m_tInfo.m_iArmTime))
		{
			it = vSplashPoints.erase(it);
			continue;
		}

		vPoints.push_back(tPoint);
		it = vSplashPoints.erase(it);
		if (!bSort && vPoints.size() == iLimit)
			break;
	}

	if (bSort)
	{
		std::sort(vPoints.begin(), vPoints.end(), [&](const auto& a, const auto& b) -> bool
			{
				return a.m_vPoint.DistToSqr(vOrigin) < b.m_vPoint.DistToSqr(vOrigin);
			});
		vPoints.resize(std::min(iLimit, int(vPoints.size())));
	}

	const Vec3 vOriginal = m_tInfo.m_pTarget->m_pEntity->GetAbsOrigin();
	m_tInfo.m_pTarget->m_pEntity->SetAbsOrigin(vOrigin);
	for (auto it = vPoints.begin(); it != vPoints.end();)
	{
		auto& tPoint = *it;
		bool bValid = tPoint.m_tSolution.m_iCalculated != CalculatedEnum::Pending;
		if (bValid)
		{
			if (auto pCollideable = m_tInfo.m_pTarget->m_pEntity->GetCollideable())
			{
				Vec3 vPos = {}; reinterpret_cast<CCollisionProperty*>(pCollideable)->CalcNearestPoint(tPoint.m_vPoint, &vPos);
				bValid = tPoint.m_vPoint.DistToSqr(vPos) < (tPoint.m_iType == PointTypeEnum::Geometry ? flRadiusSqr : flRadiusAirSqr);
			}
			else
				bValid = false;
		}

		if (bValid)
			++it;
		else
			it = vPoints.erase(it);
	}
	m_tInfo.m_pTarget->m_pEntity->SetAbsOrigin(vOriginal);

	return vPoints;
}

// Amalgam's PullPoint wrapper: raise the local eye by the trajectory droop,
// add the spawn offset, then slide the point onto the target AABB
static Vec3 PullPoint(const Vec3& vPoint, Vec3 vLocalPos, Info_t& tInfo, const Vec3& vTargetPos, const Vec3& vMins, const Vec3& vMaxs)
{
	auto fHeightenLocalPos = [&]()
	{	// basic trajectory pass
		const float flGrav = tInfo.m_flGravity;
		if (!flGrav)
			return vPoint;

		const Vec3 vDelta = vTargetPos - vLocalPos;
		const float flDist = vDelta.Length2D();

		const float flRoot = powf(tInfo.m_flVelocity, 4) - flGrav * (flGrav * powf(flDist, 2) + 2.f * vDelta.z * powf(tInfo.m_flVelocity, 2));
		if (flRoot < 0.f)
			return vPoint;
		const float flPitch = atan((powf(tInfo.m_flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));

		const float flTime = flDist / (cos(flPitch) * tInfo.m_flVelocity) - tInfo.m_flOffsetTime;
		return vLocalPos + Vec3(0, 0, flGrav * powf(flTime, 2) / 2);
	};

	vLocalPos = fHeightenLocalPos();
	Vec3 vForward, vRight, vUp; Math::AngleVectors(Math::CalcAngle(vLocalPos, vPoint), &vForward, &vRight, &vUp);
	vLocalPos += (vForward * tInfo.m_vOffset.x) + (vRight * tInfo.m_vOffset.y) + (vUp * tInfo.m_vOffset.z);
	return Math::PullPoint(vPoint, vLocalPos, vTargetPos, vMins, vMaxs);
}

// vphysics drag compensation factors per projectile model. Amalgam calls these
// "stupid magic values" themselves - ported verbatim so pipes land where
// Amalgam's do.
static float GetDrag(float flVelocity, ProjectileInfo* pInfo, int iFlags)
{
	if (!(iFlags & CalculateFlagsEnum::AccountDrag))
		return 0.f;

	static float flLastVelocity = 0.f, flRegularDrag = 0.f, flLobDrag = 0.f;
	if (flLastVelocity != flVelocity)
	{
		auto fGetDrag = [&](std::function<float()> fGetTypeDrag)
		{
			if (!F::ProjSim.m_bPhysics)
				return 0.f;

			return fGetTypeDrag();
		};
		auto fGetRegularDrag = [&]()
		{
			switch (pInfo->m_uType)
			{
			case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
				if (!SDK::AttribHookValue(0, "grenade_no_spin", pInfo->m_pWeapon))
					return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.120f, 0.200f); // 0.120 normal, 0.200 capped, 0.300 v3000
				else
					return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.060f, 0.085f); // 0.060 normal, 0.085 capped, 0.120 v3000
			case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
				return Math::RemapVal(flVelocity, 1454.f, k_flMaxVelocity, 0.385f, 0.530f); // 0.385 normal, 0.530 capped, 0.790 v3000
			case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
				return Math::RemapVal(flVelocity, 922.f, k_flMaxVelocity, 0.090f, 0.190f); // 0.085 low, 0.190 capped, 0.230 v2400
			case FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl"):
				return 0.310f;
			case FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl"):
				return 0.180f;
			case FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl"):
				return 0.285f;
			case FNV1A::Hash32Const("models/weapons/c_models/urinejar.mdl"):
			case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl"):
			case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster.mdl"):
			case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl"):
				return 0.057f;
			case FNV1A::Hash32Const("models/weapons/c_models/c_gascan/c_gascan.mdl"):
				return 0.530f;
			}
			return 0.f;
		};
		auto fGetLobDrag = [&]()
		{
			if (!(Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::LobAngles))
				return 0.f;

			switch (pInfo->m_uType)
			{
			case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
				if (!SDK::AttribHookValue(0, "grenade_no_spin", pInfo->m_pWeapon))
					return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.056f, 0.062f);
				else
					return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.030f, 0.033f);
			case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
				return Math::RemapVal(flVelocity, 1454.f, k_flMaxVelocity, 0.099f, 0.092f);
			case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
				return Math::RemapVal(flVelocity, 922.f, k_flMaxVelocity, 0.048f, 0.060f);
			case FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl"):
				return 0.075f;
			case FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl"):
				return 0.057f;
			case FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl"):
				return 0.072f;
			case FNV1A::Hash32Const("models/weapons/c_models/urinejar.mdl"):
			case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl"):
			case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster.mdl"):
			case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl"):
				return 0.030f;
			case FNV1A::Hash32Const("models/weapons/c_models/c_gascan/c_gascan.mdl"):
				return 0.089f;
			}
			return 0.f;
		};

		flLastVelocity = flVelocity;
		flRegularDrag = fGetDrag(fGetRegularDrag);
		flLobDrag = fGetDrag(fGetLobDrag);
	}

	return !(iFlags & CalculateFlagsEnum::LobAngle) ? flRegularDrag : flLobDrag;
}

// Ballistic angle from vLocalPos to vTargetPos, with drag compensation and
// the high-arc (lob) solution when flagged
static bool GetAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iFlags, Info_t& tInfo, ProjectileInfo* pInfo, float& flPitch, float& flYaw, float& flTime)
{
	float flVelocity = tInfo.m_flVelocity;
	const float flGrav = tInfo.m_flGravity;
	const Vec3 vDelta = vTargetPos - vLocalPos;
	const float flDist = vDelta.Length2D();
	float flDrag = GetDrag(flVelocity, pInfo, iFlags);

	const Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vTargetPos);

	flYaw = vAngleTo.y;

	if (!flGrav)
		flPitch = -DEG2RAD(vAngleTo.x);
	else
	{
		float flRoot = powf(flVelocity, 4) - flGrav * (flGrav * powf(flDist, 2) + 2.f * vDelta.z * powf(flVelocity, 2));
		if (flRoot < 0.f)
			return false;

		if (!(iFlags & CalculateFlagsEnum::LobAngle))
			flPitch = atan((powf(flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
		else
			flPitch = atan((powf(flVelocity, 2) + sqrt(flRoot)) / (flGrav * flDist));

		if (flDrag)
		{	// drag is handled in a stupid manner, would like something better than this
			float flTime = flDist / (flVelocity * cos(flPitch));
			flVelocity *= 1 - flDrag * flTime;

			flRoot = powf(flVelocity, 4) - flGrav * (flGrav * powf(flDist, 2) + 2.f * vDelta.z * powf(flVelocity, 2));
			if (flRoot < 0.f)
				return false;

			if (!(iFlags & CalculateFlagsEnum::LobAngle))
				flPitch = atan((powf(flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
			else
				flPitch = atan((powf(flVelocity, 2) + sqrt(flRoot)) / (flGrav * flDist));

			flVelocity = flVelocity / (1 + 0.5f * (1 - powf(MATH_E, -flDrag * flTime)));
			flVelocity *= 1 + vDelta.z / flVelocity * TICK_INTERVAL * flTime;
		}
	}

	flTime = flDist / (flVelocity * cos(flPitch));

	if (Vars::Aimbot::Projectile::TimeOverride.Value)
		flTime *= Vars::Aimbot::Projectile::TimeOverride.Value;

	return true;
}

// Amalgam's CalculateAngle: basic trajectory pass, then (when the weapon
// spawns the projectile off the eye) a second pass from the real spawn
// position including the spawn-clip correction
void CAmalgamAimbotProjectile::CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& tOut, int iFlags, int iTolerance, float flRoll)
{
	if (tOut.m_iCalculated != CalculatedEnum::Pending)
		return;

	float flPitch, flYaw;
	{	// basic trajectory pass
		if (!GetAngle(vLocalPos, vTargetPos, iFlags, m_tInfo, &m_tProjInfo, flPitch, flYaw, tOut.m_flTime))
		{
			tOut.m_iCalculated = CalculatedEnum::Bad;
			return;
		}

		tOut.m_flTime -= m_tInfo.m_flOffsetTime;
		tOut.m_flPitch = flPitch = -RAD2DEG(flPitch) - m_tInfo.m_vAngFix.x;
		tOut.m_flYaw = flYaw -= m_tInfo.m_vAngFix.y;
	}

	int iTimeTo = ceilf(tOut.m_flTime / TICK_INTERVAL);
	const bool bGood = iTolerance != -1 ? abs(iTimeTo - iSimTime) <= iTolerance : iTimeTo <= iSimTime;
	if (!(iFlags & CalculateFlagsEnum::TwoPass) || m_tInfo.m_vOffset.IsZero())
	{
		tOut.m_iCalculated = bGood ? CalculatedEnum::Good : CalculatedEnum::Time;
		return;
	}
	else if (!bGood && !flRoll)
	{
		tOut.m_iCalculated = CalculatedEnum::Time;
		return;
	}

	// Amalgam's ProjSimEnum::Redirect (setup clip) maps to our ProjSim's
	// Trace flag, which re-traces the spawn position at the aim angles
	const int iSimFlags = (iFlags & CalculateFlagsEnum::SetupClip ? ProjSimEnum::Trace : ProjSimEnum::None)
		| ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(m_tInfo.m_pLocal, m_tInfo.m_pWeapon, { flPitch, flYaw, flRoll }, m_tProjInfo, iSimFlags))
	{
		tOut.m_iCalculated = CalculatedEnum::Bad;
		return;
	}

	{	// calculate trajectory from projectile origin
		if (!GetAngle(m_tProjInfo.m_vPos, vTargetPos, iFlags, m_tInfo, &m_tProjInfo, tOut.m_flPitch, tOut.m_flYaw, tOut.m_flTime))
		{
			tOut.m_iCalculated = CalculatedEnum::Bad;
			return;
		}

		{	// correct yaw
			Vec3 vShootPos = (m_tProjInfo.m_vPos - vLocalPos).To2D();
			Vec3 vTarget = vTargetPos - vLocalPos;
			Vec3 vForward; Math::AngleVectors(m_tProjInfo.m_vAng, &vForward); vForward.Normalize2D();
			const float flB = 2 * (vShootPos.x * vForward.x + vShootPos.y * vForward.y);
			const float flC = vShootPos.Length2DSqr() - vTarget.Length2DSqr();
			auto vSolutions = Math::SolveQuadratic(1.f, flB, flC);
			if (!vSolutions.empty())
			{
				vShootPos += vForward * vSolutions.front();
				tOut.m_flYaw = flYaw - (RAD2DEG(atan2(vShootPos.y, vShootPos.x)) - flYaw);
				flYaw = RAD2DEG(atan2(vShootPos.y, vShootPos.x));
			}
		}

		{	// correct pitch
			if (m_tInfo.m_flGravity)
			{
				flPitch -= m_tProjInfo.m_vAng.x;
				tOut.m_flPitch = -RAD2DEG(tOut.m_flPitch) + flPitch - m_tInfo.m_vAngFix.x;
			}
			else
			{
				Vec3 vShootPos = Math::RotatePoint(m_tProjInfo.m_vPos - vLocalPos, {}, { 0, -flYaw, 0 }); vShootPos.y = 0;
				Vec3 vTarget = Math::RotatePoint(vTargetPos - vLocalPos, {}, { 0, -flYaw, 0 });
				Vec3 vForward; Math::AngleVectors(m_tProjInfo.m_vAng - Vec3(0, flYaw, 0), &vForward); vForward.y = 0; vForward.Normalize();
				const float flB = 2 * (vShootPos.x * vForward.x + vShootPos.z * vForward.z);
				const float flC = (powf(vShootPos.x, 2) + powf(vShootPos.z, 2)) - (powf(vTarget.x, 2) + powf(vTarget.z, 2));
				auto vSolutions = Math::SolveQuadratic(1.f, flB, flC);
				if (!vSolutions.empty())
				{
					vShootPos += vForward * vSolutions.front();
					tOut.m_flPitch = flPitch - (RAD2DEG(atan2(-vShootPos.z, vShootPos.x)) - flPitch);
				}
			}
		}
	}

	iTimeTo = ceilf(tOut.m_flTime / TICK_INTERVAL);
	tOut.m_iCalculated = (iTolerance != -1 ? abs(iTimeTo - iSimTime) <= iTolerance : iTimeTo <= iSimTime) ? CalculatedEnum::Good : CalculatedEnum::Time;
}

// Amalgam's TestAngle: simulates the projectile along the solution angle and
// requires the first contact to be the point itself (direct), near the point
// on a static surface (geometry), or near the point in mid-air (airdet)
bool CAmalgamAimbotProjectile::TestAngle(const Vec3& vPoint, const Vec3& vAngles, int iSimTime, int iType, int iFlags, bool bSecondTest)
{
	auto pLocal = m_tInfo.m_pLocal;
	auto pWeapon = m_tInfo.m_pWeapon;
	auto& tTarget = *m_tInfo.m_pTarget;

	// reset per attempt: only the immediately-preceding validation may claim
	// "blocked by cover" for the neckbreaker roll search
	m_bObstructed = false;

	int iSimFlags = ProjSimEnum::Trace | ProjSimEnum::InitCheck
		| ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
	m_tProjInfo = {};
	// our ported ProjSim rejects some valid spawn positions through InitCheck;
	// retry without it like the old port did
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, vAngles, m_tProjInfo, iSimFlags))
	{
		if (!F::ProjSim.GetInfo(pLocal, pWeapon, vAngles, m_tProjInfo, ProjSimEnum::Trace | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum))
			return false;
		iSimFlags &= ~ProjSimEnum::InitCheck;
	}
	if (!F::ProjSim.Initialize(m_tProjInfo))
		return false;

	CGameTrace trace = {};
	CTraceFilterCollideable filter = {};
	filter.pSkip = iType == PointTypeEnum::Direct ? pLocal : tTarget.m_pEntity;
	filter.iPlayer = iType == PointTypeEnum::Direct ? PLAYER_DEFAULT : PLAYER_NONE;
	filter.bMisc = iType == PointTypeEnum::Direct;
	int nMask = MASK_SOLID;
	if (iType == PointTypeEnum::Direct && F::AimbotGlobal->FriendlyFire())
	{
		switch (pWeapon->GetWeaponID())
		{	// only weapons that actually hit teammates properly
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
		case TF_WEAPON_DRG_POMSON:
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_SYRINGEGUN_MEDIC:
			filter.iPlayer = PLAYER_ALL;
		}
	}
	// heal targets (crossbow bolts, rescue ranger claws) fly at teammates -
	// the trace must be able to connect with them, enemies still block the bolt
	if (iType == PointTypeEnum::Direct && tTarget.m_pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
		filter.iPlayer = PLAYER_ALL;
	F::ProjSim.SetupTrace(filter, nMask, pWeapon);

	if (!m_tProjInfo.m_flGravity)
	{
		SDK::TraceHull(m_tProjInfo.m_vPos, vPoint, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);
		if (Math::FullFraction(m_tProjInfo.m_vPos, vPoint, trace) < 0.999f && trace.m_pEnt != tTarget.m_pEntity)
		{
			// straight-line shots (rockets etc.) fail here, before the flight
			// loop below ever runs - still counts as "cover blocked the shot"
			// for the neckbreaker roll search
			m_bObstructed = true;
			return false;
		}
	}

	bool bDidHit = false;
	Vec3 vNew = F::ProjSim.GetOrigin();
	const int iTimingTolerance = TIME_TO_TICKS(m_tInfo.m_flBoundsTime);
	const float flRadiusSqr = iType != PointTypeEnum::Direct ? powf(m_tProjInfo.m_flVelocity * TICK_INTERVAL + m_tProjInfo.m_vHull.z, 2) : std::numeric_limits<float>::max();
	const int iTraceInterval = iFlags == PointFlagsEnum::Lob ? Vars::Aimbot::Projectile::LobTraceInterval.Value
		: iType != PointTypeEnum::Direct ? Vars::Aimbot::Projectile::SplashTraceInterval.Value
		: Vars::Aimbot::Projectile::DirectTraceInterval.Value;

	const RestoreInfo_t tOriginal = { tTarget.m_pEntity->GetAbsOrigin(), tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs() };
	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	tTarget.m_pEntity->m_vecMins() = { std::clamp(tTarget.m_pEntity->m_vecMins().x, -24.f, 0.f), std::clamp(tTarget.m_pEntity->m_vecMins().y, -24.f, 0.f), tTarget.m_pEntity->m_vecMins().z };
	tTarget.m_pEntity->m_vecMaxs() = { std::clamp(tTarget.m_pEntity->m_vecMaxs().x, 0.f, 24.f), std::clamp(tTarget.m_pEntity->m_vecMaxs().y, 0.f, 24.f), tTarget.m_pEntity->m_vecMaxs().z };
	for (int n = 1; n <= iSimTime; n++)
	{
		F::ProjSim.RunTick(m_tProjInfo);

		if (bDidHit)
		{
			trace.endpos = F::ProjSim.GetOrigin();
			continue;
		}
		if (iTraceInterval != 1 && n % iTraceInterval && n != iSimTime)
			continue;

		const Vec3 vOld = vNew; vNew = F::ProjSim.GetOrigin();
		SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);

		bool bHit = false;
		switch (iType)
		{
		case PointTypeEnum::Direct:
		case PointTypeEnum::Geometry:
			bHit = trace.DidHit(); break;
		case PointTypeEnum::Air:
			bHit = trace.endpos.DistToSqr(vPoint) < flRadiusSqr || trace.DidHit(); break;
		}

		if (bHit)
		{
			bool bValid = false, bTarget = true;
			switch (iType)
			{
			case PointTypeEnum::Direct:
				bTarget = trace.m_pEnt == tTarget.m_pEntity, bValid = bTarget && iSimTime - n < iTimingTolerance; break;
			case PointTypeEnum::Geometry:
				bValid = trace.endpos.DistToSqr(vPoint) < flRadiusSqr; break;
			case PointTypeEnum::Air:
				bValid = !trace.DidHit(); break;
			}

			// a trace that connected with something other than the point's
			// purpose means cover blocked the shot - the one failure mode a
			// muzzle roll can actually route around (timing misses can't)
			m_bObstructed = bHit && !bTarget;

			if (bValid && iType != PointTypeEnum::Direct)
			{
				CGameTrace trace2 = {};
				SDK::Trace(trace.endpos + trace.plane.normal * m_tInfo.m_flNormalOffset, tTarget.m_vPos + m_tInfo.m_vTargetEye, MASK_SHOT, &filter, &trace2);
				bValid = trace2.fraction == 1.f;
			}

			if (bValid && Vars::Aimbot::Projectile::IntervalRetest.Value && iTraceInterval != 1)
			{	// retest the skipped ticks so a wall crossed between intervals voids the hit
				CGameTrace trace2 = {}; Vec3 vRetestOld, vRetestNew;
				int iTicks = int(m_tProjInfo.m_vPath.size());
				if (m_tInfo.m_flGravity)
					iTicks -= iTimingTolerance;

				for (int i = 1; i < iTicks; i++)
				{
					vRetestOld = m_tProjInfo.m_vPath[i - 1], vRetestNew = m_tProjInfo.m_vPath[i];
					SDK::TraceHull(vRetestOld, vRetestNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace2);
					bValid = !trace2.DidHit();

					if (!bValid)
						break;
				}
			}

			if (bValid)
			{
				if (iTraceInterval != 1 && iType != PointTypeEnum::Direct)
				{
					const int iInterval = n % iTraceInterval ? n % iTraceInterval : iTraceInterval;
					const int iPopCount = ceilf(iInterval - trace.fraction * iInterval);
					for (int i = 0; i < iPopCount && !m_tProjInfo.m_vPath.empty(); i++)
						m_tProjInfo.m_vPath.pop_back();
				}

				// head retest for smooth/assistive aim (Amalgam: closest hitbox
				// to the impact must actually be the aimed head)
				if (tTarget.m_nAimedHitbox == HITBOX_HEAD && !bSecondTest
					&& (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth
					|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Assistive))
				{
					auto aBones = F::Backtrack.GetBones(tTarget.m_pEntity);
					if (!aBones)
						break;

					auto pModel = tTarget.m_pEntity->As<C_TFPlayer>()->GetModel();
					auto pHDR = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
					auto pSet = pHDR ? pHDR->pHitboxSet(tTarget.m_pEntity->As<C_TFPlayer>()->m_nHitboxSet()) : nullptr;
					if (!pSet)
						break;

					const Vec3 vOffset = tOriginal.m_vOrigin - tTarget.m_vPos;
					const Vec3 vPos = trace.endpos + F::ProjSim.GetVelocity().Normalized() * 16 + vOffset;

					float flLowestDistance = std::numeric_limits<float>::max(); int iClosest = -1;
					for (int nHitbox = 0; nHitbox < pSet->numhitboxes; ++nHitbox)
					{
						auto pBox = pSet->pHitbox(nHitbox);
						if (!pBox) continue;

						Vec3 vCenter; Math::VectorTransform({}, aBones[pBox->bone], vCenter);

						const float flDistance = vPos.DistToSqr(vCenter);
						if (flDistance < flLowestDistance)
							iClosest = nHitbox, flLowestDistance = flDistance;
					}
					if (iClosest != HITBOX_HEAD)
						break;
				}

				bDidHit = true;
			}
			else if (bTarget && iType == PointTypeEnum::Direct && m_tInfo.m_iArmTime)
			{	// run for more ticks to check for splash
				iSimTime = n + iTimingTolerance / 2;
				iType = PointTypeEnum::Geometry;
				filter.pSkip = tTarget.m_pEntity;
				filter.iPlayer = PLAYER_NONE;
				filter.bMisc = false;
				continue;
			}
			else
			{
				m_bObstructed = !bTarget || iType != PointTypeEnum::Direct;
				break;
			}

			if (iType == PointTypeEnum::Direct)
				trace.endpos = vNew;

			if (!bTarget || iType != PointTypeEnum::Direct)
				break;
		}
	}
	tTarget.m_pEntity->SetAbsOrigin(tOriginal.m_vOrigin);
	tTarget.m_pEntity->m_vecMins() = tOriginal.m_vMins;
	tTarget.m_pEntity->m_vecMaxs() = tOriginal.m_vMaxs;
	m_tProjInfo.m_vPath.push_back(trace.endpos);

	return bDidHit;
}

bool CAmalgamAimbotProjectile::HandlePoint(const Vec3& vOrigin, int iSimTime, float flPitch, float flYaw, float flTime, const Vec3& vPoint, int iType, int iFlags)
{
	bool bReturn = false;

	Vec3 vAngles; Aim(G::CurrentUserCmd->viewangles, { flPitch, flYaw, 0.f }, vAngles);
	m_tInfo.m_pTarget->m_vPos = vOrigin;

	const int iOriginalSimTime = iSimTime;
	if (m_tInfo.m_iArmTime && iType == PointTypeEnum::Geometry || m_tInfo.m_bIgnoreTiming && iFlags == PointFlagsEnum::Lob)
	{
		if (flTime > m_tProjInfo.m_flLifetime)
			return false;

		iSimTime = std::ceil(flTime / I::GlobalVars->interval_per_tick); //TIME_TO_TICKS(flTime);
	}

	if (!TestAngle(vPoint, vAngles, iSimTime, iType, iFlags))
	{
		// Neckbreaker (re-attached to the port - the feature lived in the native
		// projectile aimbot, which no longer runs): when the flight was
		// physically blocked by cover (m_bObstructed, set inside TestAngle),
		// re-validate with viewangle roll. Roll rotates the right/up basis
		// inside GetProjectileFireSetup, swinging the muzzle around the
		// eye-forward axis and shifting the redirect origin, so each roll tests
		// a different flight path past the edge of the blocking geometry.
		// Each roll re-solves the ballistic solution through the rolled muzzle:
		// the unrolled pitch/yaw lands roughly a muzzle radius off the point,
		// outside splash validation's per-tick tolerance, so rolled splash
		// never validated from the reused solution. Applies to splash points
		// as well as direct ones - a splash flight blocked by the same cover
		// edge is exactly what a rolled muzzle routes around. Silent aim only:
		// the other Aim methods clamp roll back to 0, so the server could never
		// reproduce the muzzle the sim validated (the old native gate read its
		// own projectile aim type, which the port doesn't consume - the general
		// AimType drives the port's writes).
		// Anti-cheat compatibility force-disables this: extreme viewangle roll
		// on an attack command is a trivial community-anticheat detection.
		if (!m_iResult && m_bObstructed
			&& Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent
			&& Vars::Aimbot::Projectile::Neckbreaker.Value && !CFG::Misc_AntiCheat_Enabled)
		{
			const int iStep = std::max(Vars::Aimbot::Projectile::NeckbreakerStep.Value, 15);
			const bool bLob = iFlags & PointFlagsEnum::Lob;
			int iSolveFlags = CalculateFlagsEnum::Accuracy;
			if (bLob)
				iSolveFlags |= CalculateFlagsEnum::LobAngle;
			const int iTolerance = m_tInfo.m_bIgnoreTiming && bLob ? std::numeric_limits<int>::max() : iType == PointTypeEnum::Direct ? -1 : m_tInfo.m_iArmTime ? -1 : 0;
			for (int iRoll = iStep; iRoll < 360 && m_iRollBudget > 0; iRoll += iStep)
			{
				m_iRollBudget--;
				Solution_t tRollSolution;
				CalculateAngle(m_tInfo.m_vLocalEye, vPoint, iSimTime, tRollSolution, iSolveFlags, iTolerance, static_cast<float>(iRoll));
				Vec3 vRollAngles;
				if (tRollSolution.m_iCalculated == CalculatedEnum::Good)
					Aim(G::CurrentUserCmd->viewangles, { tRollSolution.m_flPitch, tRollSolution.m_flYaw, static_cast<float>(iRoll) }, vRollAngles);
				else
					Aim(G::CurrentUserCmd->viewangles, { flPitch, flYaw, static_cast<float>(iRoll) }, vRollAngles);
				if (TestAngle(vPoint, vRollAngles, iSimTime, iType, iFlags))
				{
					vAngles = vRollAngles; // m_vAngleTo below must carry the roll the server has to reproduce
					bReturn = m_iResult = true;
					m_flTimeTo = (tRollSolution.m_iCalculated == CalculatedEnum::Good ? tRollSolution.m_flTime : flTime) + m_tInfo.m_flLatency;
					break;
				}
			}
		}

		// Amalgam only tries the smooth fallback when no direct hit was found
		// yet, so it can't downgrade an already-solved point
		if (!m_iResult)
		{
			switch (Vars::Aimbot::General::AimType.Value)
			{
			case Vars::Aimbot::General::AimTypeEnum::Smooth:
				if (Vars::Aimbot::General::AssistStrength.Value == 100.f)
					break;
				[[fallthrough]];
			case Vars::Aimbot::General::AimTypeEnum::Assistive:
			{
				const Vec3 vPlainAngles = { flPitch, flYaw, 0.f };
				if (TestAngle(vPoint, vPlainAngles, iSimTime, iType, PointFlagsEnum::Regular, true))
					bReturn = m_iResult = 2;
			}
			}
		}
	}
	else
	{
		bReturn = m_iResult = true;
		m_flTimeTo = flTime + m_tInfo.m_flLatency;
	}

	if (bReturn && m_bUpdate)
	{
		m_vAngleTo = vAngles, m_vTarget = vPoint, m_vPredicted = vOrigin;
		m_vPlayerPath.clear(), m_vProjectilePath = m_tProjInfo.m_vPath;
		if (m_tMoveStorage.m_vPath.empty())
			return true;

		if (m_tInfo.m_iArmTime && iType != PointTypeEnum::Air)
		{
			const int iCount = std::min(iOriginalSimTime + TIME_TO_TICKS(m_tInfo.m_flLatency) + 1, int(m_tMoveStorage.m_vPath.size()));
			m_vPlayerPath.insert(m_vPlayerPath.end(), m_tMoveStorage.m_vPath.begin(), m_tMoveStorage.m_vPath.begin() + iCount);
		}
		else
		{
			const size_t iCount = std::min(m_tProjInfo.m_vPath.size() + TIME_TO_TICKS(m_tInfo.m_flLatency), m_tMoveStorage.m_vPath.size());
			m_vPlayerPath.insert(m_vPlayerPath.end(), m_tMoveStorage.m_vPath.begin(), m_tMoveStorage.m_vPath.begin() + iCount);
			m_vPredicted = m_vPlayerPath.back();
		}
	}

	return bReturn && m_iResult == 1;
}

bool CAmalgamAimbotProjectile::HandleDirect(DirectHistory_t& mDirectHistory)
{
	bool bReturn = false;
	if (mDirectHistory.empty())
		return bReturn;

	auto it = mDirectHistory.begin();
	const int iType = it->first;
	auto& vDirectHistory = it->second;

	if (F::ProjSim.m_bPhysics)
	{	// physics projectiles sink in water - prefer the low points there
		for (auto& tHistory : vDirectHistory)
		{
			if (I::EngineTrace->GetPointContents(tHistory.m_vPoint) & CONTENTS_WATER)
				tHistory.m_iPriority = COORD_EXTENT - tHistory.m_vPoint.z;
		}
	}
	std::sort(vDirectHistory.begin(), vDirectHistory.end(), [](const Direct_t& a, const Direct_t& b)
		{
			return a.m_iPriority < b.m_iPriority;
		});
	m_flTimeTo = vDirectHistory.front().m_flTime + m_tInfo.m_flLatency;

	m_iRollBudget = 360 / std::max(Vars::Aimbot::Projectile::NeckbreakerStep.Value, 15);
	for (auto& tHistory : vDirectHistory)
	{
		if (HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tHistory.m_flPitch, tHistory.m_flYaw, tHistory.m_flTime, tHistory.m_vPoint, PointTypeEnum::Direct, iType))
		{
			bReturn = true;
			break;
		}
	}

	mDirectHistory.erase(it);
	return bReturn;
}

bool CAmalgamAimbotProjectile::HandleSplash(SplashHistory_t& mSplashHistory)
{
	bool bReturn = false;
	if (mSplashHistory.empty())
		return bReturn;

	auto it = mSplashHistory.begin();
	const int iType = it->first;
	auto& vSplashHistory = it->second;

	std::sort(vSplashHistory.begin(), vSplashHistory.end(), [](const Splash_t& a, const Splash_t& b)
		{
			return a.m_flTimeTo < b.m_flTimeTo;
		});

	int iFlags = CalculateFlagsEnum::None;
	if (iType == PointFlagsEnum::Lob)
		iFlags |= CalculateFlagsEnum::LobAngle;
	// capped pool of roll attempts shared by every point this evaluation
	m_iRollBudget = 360 / std::max(Vars::Aimbot::Projectile::NeckbreakerStep.Value, 15);
	SetupSplashPoints(vSplashHistory.front().m_vOrigin, m_vSplashPoints, iFlags);
	if (!m_vSplashPoints.empty())
	{
		iFlags |= CalculateFlagsEnum::Accuracy;
		float flLowestDistance = std::numeric_limits<float>::max(); bool bFirst = true;
		for (auto& tHistory : vSplashHistory)
		{
			std::vector<Point_t> vSplashPoints = GetSplashPoints(tHistory.m_vOrigin, m_vSplashPoints, tHistory.m_iSimtime, iFlags, bFirst); bFirst = false;

			for (auto& tPoint : vSplashPoints)
			{
				const float flDistance = tHistory.m_vOrigin.DistToSqr(tPoint.m_vPoint);
				if (flDistance > flLowestDistance)
					continue;

				if (HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw, tPoint.m_tSolution.m_flTime, tPoint.m_vPoint, tPoint.m_iType, iType))
				{
					bReturn = true;
					flLowestDistance = flDistance;
				}
			}
			if (m_tInfo.m_bIgnoreTiming && iType == PointFlagsEnum::Lob)
				break;
		}
	}

	mSplashHistory.erase(it);
	return bReturn;
}

int CAmalgamAimbotProjectile::CanHit(Target_t& tTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, bool bUpdate)
{
	m_tMoveStorage = {};

	const bool bIsPlayer = tTarget.m_iTargetType == TargetEnum::Player && IsPlayer(tTarget.m_pEntity);
	bool bSim = false;
	if (bIsPlayer)
	{
		if (!F::MoveSim->Initialize(tTarget.m_pEntity, m_tMoveStorage))
		{
			// init-failure paths skip Restore internally - free the store here
			F::MoveSim->Restore(m_tMoveStorage);
			return false;
		}
		bSim = true;
	}
	else
		m_tMoveStorage.m_bFailed = true; // buildings: Amalgam's MoveSim declines them too

	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, m_tProjInfo, ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum)
		|| !F::ProjSim.Initialize(m_tProjInfo, false))
	{
		if (bSim)
			F::MoveSim->Restore(m_tMoveStorage);
		return false;
	}

	m_tInfo = { pLocal, pWeapon, &tTarget };
	m_tInfo.m_vLocalEye = GetShootPos(pLocal);
	m_tInfo.m_flLatency = F::Backtrack.GetReal() + TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke());
	tTarget.m_vPos = tTarget.m_pEntity->m_vecOrigin();

	if (bIsPlayer)
		m_tInfo.m_vTargetEye = tTarget.m_pEntity->As<C_TFPlayer>()->m_vecViewOffset();
	else
		m_tInfo.m_vTargetEye = { 0.f, 0.f, tTarget.m_pEntity->m_vecMaxs().z * 0.9f }; // buildings have no view offset (Amalgam reads it through CTFPlayer regardless)

	const Vec3 vVelocity = F::ProjSim.GetVelocity();
	m_tInfo.m_flVelocity = vVelocity.Length();
	if (m_tInfo.m_flVelocity < 1.f)
	{
		if (bSim)
			F::MoveSim->Restore(m_tMoveStorage);
		return false;
	}
	m_tInfo.m_vAngFix = Math::VectorAngles(vVelocity);

	m_tInfo.m_vHull = m_tProjInfo.m_vHull.Min(3);
	m_tInfo.m_vOffset = m_tProjInfo.m_vPos - m_tInfo.m_vLocalEye; m_tInfo.m_vOffset.y *= -1;
	m_tInfo.m_flOffsetTime = m_tInfo.m_vOffset.Length() / m_tInfo.m_flVelocity;

	// ProjSim stores gravity relative to 800 u/s^2 (1.0 on a default server) for
	// its vphysics env; the ballistic equations below need real units/s^2 the way
	// Amalgam's DEFAULT_GRAVITY (800) feeds them. Without this every arcing
	// solution (pipes/stickies) solved flat and the simulated shot fell short.
	m_tInfo.m_flGravity = m_tProjInfo.m_flGravity * 800.f;
	m_tInfo.m_iSplashRestrict = !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashRestrictDirect.Value : Vars::Aimbot::Projectile::SplashRestrictArc.Value;
	m_tInfo.m_flRadius = GetSplashRadius(pWeapon, pLocal, Vars::Aimbot::Projectile::SplashRadius.Value / 100);
	m_tInfo.m_flBoundsTime = tTarget.m_pEntity->GetSize().Length() / m_tInfo.m_flVelocity;
	m_tInfo.m_flRadiusTime = m_tInfo.m_flBoundsTime + m_tInfo.m_flRadius / m_tInfo.m_flVelocity;
	m_tInfo.m_iArmTime = TIME_TO_TICKS(ArmTime(pWeapon));
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		m_tInfo.m_flNormalOffset = 1.f;
	}
	m_tInfo.m_bIgnoreTiming = Vars::Aimbot::Projectile::LobAnglesUnderpredict.Value && m_tInfo.m_flRadius;

	Directs_t mDirects = GetDirects();
	Splashes_t vSplashes = GetSplashes();

	DirectHistory_t mDirectHistory = {};
	SplashHistory_t mSplashHistory = {};

	// Amalgam's exact horizon: min(projectile lifetime, user max sim time)
	const int iMaxTime = TIME_TO_TICKS(std::min(m_tProjInfo.m_flLifetime, Vars::Aimbot::Projectile::MaxSimulationTime.Value));
	for (int i = 1 - TIME_TO_TICKS(m_tInfo.m_flLatency); i <= iMaxTime; i++)
	{
		if (!m_tMoveStorage.m_bFailed)
		{
			F::MoveSim->RunTick(m_tMoveStorage);
			tTarget.m_vPos = m_tMoveStorage.m_vPredictedOrigin;
		}
		if (i < 0)
			continue;

		for (auto it = mDirects.begin(); it != mDirects.end();)
		{
			if (m_tInfo.m_iArmTime && m_tInfo.m_iArmTime > i && !m_tMoveStorage.m_MoveData.m_vecVelocity.IsZero())
				break;

			auto& [iIndex, tOffset] = *it;
			Vec3& vOffset = tOffset.m_vOffset;
			const int iType = tOffset.m_iFlags & -tOffset.m_iFlags;

			Vec3 vPoint = tTarget.m_vPos + vOffset;
			if (Vars::Aimbot::Projectile::HuntsmanPullPoint.Value && tTarget.m_nAimedHitbox == HITBOX_HEAD)
			{
				vPoint = PullPoint(vPoint, m_tInfo.m_vLocalEye, m_tInfo, tTarget.m_vPos, tTarget.m_pEntity->m_vecMins() + m_tInfo.m_vHull, tTarget.m_pEntity->m_vecMaxs() - m_tInfo.m_vHull);
				if (Vars::Aimbot::Projectile::HuntsmanPullNoZ.Value)
					vPoint.z = tTarget.m_vPos.z + vOffset.z;
			}

			int iFlags = CalculateFlagsEnum::Accuracy;
			if (iType == PointFlagsEnum::Lob)
				iFlags |= CalculateFlagsEnum::LobAngle;
			const int iTolerance = m_tInfo.m_bIgnoreTiming && iType == PointFlagsEnum::Lob ? std::numeric_limits<int>::max() : -1;

			Solution_t tSolution;
			switch (iType)
			{
			case PointFlagsEnum::Lob:
				if (ShouldLob(m_tMoveStorage, m_tInfo))
					goto end;
				tSolution.m_iCalculated = CalculatedEnum::Bad; break;
			default: end:
				CalculateAngle(m_tInfo.m_vLocalEye, vPoint, i, tSolution, iFlags, iTolerance);
			}

			switch (tSolution.m_iCalculated)
			{
			case CalculatedEnum::Good:
				mDirectHistory[iType].emplace_back(History_t(tTarget.m_vPos, i), tSolution.m_flPitch, tSolution.m_flYaw, tSolution.m_flTime, vPoint, iIndex);
				[[fallthrough]];
			case CalculatedEnum::Bad:
				tOffset.m_iFlags &= ~iType;
				if (!tOffset.m_iFlags)
				{
					it = mDirects.erase(it);
					continue;
				}
			}
			++it;
		}

		for (auto it = vSplashes.begin(); it != vSplashes.end();)
		{
			int iFlags = CalculateFlagsEnum::AccountDrag;
			if (*it == PointFlagsEnum::Lob && !m_tInfo.m_bIgnoreTiming)
				iFlags |= CalculateFlagsEnum::LobAngle;

			Solution_t tSolution; CalculateAngle(m_tInfo.m_vLocalEye, tTarget.m_vPos, i, tSolution, iFlags);
			if (tSolution.m_iCalculated == CalculatedEnum::Bad && mDirects.empty())
			{
				it = vSplashes.erase(it);
				continue;
			}

			const float flTimeTo = tSolution.m_flTime - TICKS_TO_TIME(i);
			if (flTimeTo > m_tInfo.m_flRadiusTime || m_tInfo.m_iArmTime && m_tInfo.m_iArmTime > i)
			{
				++it;
				continue;
			}
			if (flTimeTo < -m_tInfo.m_flRadiusTime && (!m_tInfo.m_iArmTime || m_tInfo.m_iArmTime < i))
			{
				it = vSplashes.erase(it);
				continue;
			}
			if (*it == PointFlagsEnum::Lob && !ShouldLob(m_tMoveStorage, m_tInfo))
			{
				++it;
				continue;
			}

			mSplashHistory[*it].emplace_back(History_t(tTarget.m_vPos, i), fabsf(flTimeTo));
			++it;
		}

		if (mDirects.empty() && vSplashes.empty())
			break;
	}

	m_iResult = false, m_bUpdate = bUpdate;
	if (!m_tInfo.m_flRadius || Vars::Aimbot::Projectile::SplashPrediction.Value < Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer)
		goto direct;
	else
		goto splash;
	while (!mDirectHistory.empty() || !mSplashHistory.empty())
	{
		direct: if (HandleDirect(mDirectHistory)) break;
		splash: if (HandleSplash(mSplashHistory)) break;
	}

	F::MoveSim->Restore(m_tMoveStorage);

	// Amalgam's ShouldAimAtAngle gate only applies with LeadAndRestrict
	// enabled, which defaults off upstream and has no Necromancer equivalent

	if (!bUpdate)
		return m_iResult;

	tTarget.m_vPos = m_vTarget;
	tTarget.m_vAngleTo = m_vAngleTo;

	// Amalgam stores bound-hitbox draw data here for its visuals; Necromancer
	// has no consumer for it, so it is skipped

	return m_iResult;
}

bool CAmalgamAimbotProjectile::Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod)
{
	bool bReturn = false;
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		vOut = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
		vOut = vCurAngle.LerpAngle(vToAngle, Vars::Aimbot::General::AssistStrength.Value / 100.f);
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
	{
		Vec3 vMouseDelta = G::CurrentUserCmd->viewangles.DeltaAngle(G::LastUserCmd->viewangles);
		Vec3 vTargetDelta = vToAngle.DeltaAngle(G::LastUserCmd->viewangles);
		const float flMouseDelta = vMouseDelta.Length2DSqr(), flTargetDelta = vTargetDelta.Length2DSqr();
		vTargetDelta = vTargetDelta.Normalized() * sqrtf(std::min(flMouseDelta, flTargetDelta));
		vOut = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, Vars::Aimbot::General::AssistStrength.Value / 100.f);
		bReturn = true;
		break;
	}
	default:
		vOut = vToAngle;
		break;
	}

	// silent aim needs the angles unclamped for the packet, unless anticheat
	// compatibility is on (Amalgam's AntiCheatCompatibility::Active)
	if (iMethod != Vars::Aimbot::General::AimTypeEnum::Silent || CFG::Misc_AntiCheat_Enabled)
		Math::ClampAngles(vOut);
	return bReturn;
}

// assumes the angle was calculated with the other overload
void CAmalgamAimbotProjectile::Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod)
{
	const bool bUnsure = F::AmalgamTicks->IsTimingUnsure();
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
		if (G::Attacking != 1 && !bUnsure)
			break;
		[[fallthrough]];
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		pCmd->viewangles = vAngles;
		// hot path every aiming tick - skip instead of AVing if the engine
		// pointer or its vtable is garbage (see Memory::IsInModule comment)
		if (Memory::IsInModule("engine.dll", I::EngineClient, true))
			I::EngineClient->SetViewAngles(vAngles);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (auto pWeapon = H::Entities->GetWeapon();
			G::Attacking == 1 || bUnsure || pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER)
		{
			SDK::FixMovement(pCmd, vAngles);
			pCmd->viewangles = vAngles;
			G::bPSilentAngles = true;
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		SDK::FixMovement(pCmd, vAngles);
		pCmd->viewangles = vAngles;
		G::bSilentAngles = true;
	}
}

static void CancelShot(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd, int& iLastTickCancel)
{
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_COMPOUND_BOW:
	{
		pCmd->buttons |= IN_ATTACK2;
		pCmd->buttons &= ~IN_ATTACK;
		break;
	}
	case TF_WEAPON_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	{
		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pSwap = pLocal->GetWeaponFromSlot(i);
			if (!pSwap || pSwap == pWeapon || !pSwap->CanBeSelected())
				continue;

			pCmd->weaponselect = pSwap->entindex();
			iLastTickCancel = pWeapon->entindex();
			break;
		}
	}
	}
}

std::vector<Target_t> CAmalgamAimbotProjectile::GetTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets = {};

	const Vec3 vLocalPos = GetShootPos(pLocal);
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
	const int iTarget = Vars::Aimbot::General::Target.Value;
	const int nWeaponID = pWeapon->GetWeaponID();
	const float flMaxFOV = CFG::Aimbot_Projectile_FOV;

	// players (+ crossbow/sandvich healing when enabled)
	{
		int eGroup = -1;
		if (iTarget & Vars::Aimbot::General::TargetEnum::Players)
			eGroup = !F::AimbotGlobal->FriendlyFire() ? int(EEntGroup::PLAYERS_ENEMIES) : int(EEntGroup::PLAYERS_ALL);
		const bool bHeal = nWeaponID == TF_WEAPON_CROSSBOW;
		if (nWeaponID == TF_WEAPON_CROSSBOW && Vars::Aimbot::Healing::AutoArrow.Value)
			eGroup = eGroup != -1 ? int(EEntGroup::PLAYERS_ALL) : int(EEntGroup::PLAYERS_TEAMMATES);

		if (eGroup != -1)
		{
			for (auto pEntity : H::Entities->GetGroup(EEntGroup(eGroup)))
			{
				if (!pEntity || pEntity == pLocal)
					continue;

				auto pPlayer = pEntity->As<C_TFPlayer>();
				if (!pPlayer || pPlayer->deadflag())
					continue;

				const bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();

				// the port's ShouldIgnore rejects teammates unconditionally, so
				// heal targets skip it (Amalgam gates it per-flags instead)
				if (!bTeam && F::AimbotGlobal->ShouldIgnore(pEntity, pLocal, pWeapon))
					continue;

				// Necromancer's explicit ignore filters (Amalgam folds these
				// into ShouldIgnore flags) - enemies only, heal targets skip
				// them or auto arrow refuses to heal friends
				if (!bTeam)
				{
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

					// Vaccinator resist conds (Amalgam's ignore flags)
					if (CFG::Aimbot_Ignore_Vaccinator)
					{
						if (nWeaponID == TF_WEAPON_FLAMETHROWER || nWeaponID == TF_WEAPON_FLAME_BALL || nWeaponID == TF_WEAPON_FLAREGUN)
						{
							if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST))
								continue;
						}
						else if (nWeaponID == TF_WEAPON_COMPOUND_BOW)
						{
							if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST))
								continue;
						}
						else
						{
							if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST))
								continue;
						}
					}

					// Ignore untagged players when key is held
					if (CFG::Aimbot_Ignore_Untagged_Key != 0 && H::Input->IsDown(CFG::Aimbot_Ignore_Untagged_Key))
					{
						PlayerPriority playerPriority = {};
						if (!F::Players->GetInfo(pPlayer->entindex(), playerPriority) ||
							(!playerPriority.Cheater && !playerPriority.Targeted && !playerPriority.Nigger && !playerPriority.RetardLegit && !playerPriority.Streamer))
							continue;
					}
				}

				const bool bTeam2 = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
				if (bTeam2 && bHeal)
				{
					// healing targets: full health players are skipped (friend
					// priority needs an entity friend list Necromancer lacks)
					if (pPlayer->m_iHealth() >= pPlayer->GetMaxHealth())
						continue;
					if (Vars::Aimbot::Healing::AutoArrow.Value && CFG::Aimbot_Projectile_AutoArrow_FriendsOnly && !pPlayer->IsPlayerOnSteamFriendsList())
						continue;
				}

				float flFOVTo; Vec3 vPos, vAngleTo;
				if (!F::AimbotGlobal->PlayerBoneInFOV(pPlayer, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
					continue;

				int nTargetPriority = F::AimbotGlobal->GetPriority(pEntity->entindex());
				if (bTeam2 && bHeal && pPlayer->IsPlayerOnSteamFriendsList())
					nTargetPriority += 1000; // friends get healed first

				vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, vLocalPos.DistToSqr(vPos), nTargetPriority);
			}
		}

		if (nWeaponID == TF_WEAPON_LUNCHBOX)
			return vTargets;
	}

	// buildings (+ rescue ranger repair when enabled)
	{
		int eGroup = -1;
		if (iTarget & Vars::Aimbot::General::TargetEnum::Building)
			eGroup = int(EEntGroup::BUILDINGS_ENEMIES);
		if (Vars::Aimbot::Healing::AutoRepair.Value && nWeaponID == TF_WEAPON_SHOTGUN_BUILDING_RESCUE)
			eGroup = eGroup != -1 ? int(EEntGroup::BUILDINGS_ALL) : int(EEntGroup::BUILDINGS_TEAMMATES);

		if (eGroup != -1)
		{
			for (auto pEntity : H::Entities->GetGroup(EEntGroup(eGroup)))
			{
				if (!pEntity)
					continue;

				if (F::AimbotGlobal->ShouldIgnore(pEntity, pLocal, pWeapon))
					continue;

				auto pBuilding = pEntity->As<C_BaseObject>();
				const bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
				if (bTeam && (pBuilding->m_iHealth() >= pBuilding->m_iMaxHealth() || pBuilding->m_bBuilding()))
					continue;

				const Vec3 vPos = pEntity->GetCenter();
				const Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
				const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
				if (flFOVTo > flMaxFOV)
					continue;

				vTargets.emplace_back(pEntity, IsSentrygun(pEntity) ? TargetEnum::Sentry : IsDispenser(pEntity) ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, vLocalPos.DistToSqr(vPos), 0);
			}
		}
	}

	// stickies (detonate-stickies pipes / scorch shot flares)
	if (iTarget & Vars::Aimbot::General::TargetEnum::Stickies)
	{
		bool bShouldAim = false;
		switch (nWeaponID)
		{
		case TF_WEAPON_PIPEBOMBLAUNCHER:
			if (SDK::AttribHookValue(0, "stickies_detonate_stickies", pWeapon) == 1)
				bShouldAim = true;
			break;
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_FLAREGUN_REVENGE:
			if (pWeapon->As<C_TFFlareGun>()->GetFlareGunType() == FLAREGUN_SCORCHSHOT)
				bShouldAim = true;
		}

		if (bShouldAim)
		{
			for (auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
			{
				if (!pEntity)
					continue;

				const bool bSticky = pEntity->GetClassId() == ETFClassIds::CTFGrenadePipebombProjectile
					&& pEntity->As<C_TFGrenadePipebombProjectile>()->HasStickyEffects();
				const bool bFlare = pEntity->GetClassId() == ETFClassIds::CTFProjectile_Flare;
				if (!bSticky && !bFlare)
					continue;

				const Vec3 vPos = pEntity->GetCenter();
				const Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
				const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
				if (flFOVTo > flMaxFOV)
					continue;

				vTargets.emplace_back(pEntity, TargetEnum::Sticky, vPos, vAngleTo, flFOVTo, vLocalPos.DistToSqr(vPos), 0);
			}
		}
	}

	// Amalgam also targets world NPCs, which Necromancer has no entity group for

	return vTargets;
}

std::vector<Target_t> CAmalgamAimbotProjectile::SortTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	auto vTargets = GetTargets(pLocal, pWeapon);

	// Amalgam's ManageTargets: sort by the selection method, cap the count,
	// then apply player-list priority on top
	F::AimbotGlobal->SortTargets(vTargets, Vars::Aimbot::General::TargetSelection.Value);
	vTargets.resize(std::min(vTargets.size(), size_t(Vars::Aimbot::General::MaxTargets.Value)));
	F::AimbotGlobal->SortPriority(vTargets);

	return vTargets;
}

bool CAmalgamAimbotProjectile::RunMain(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const int nWeaponID = pWeapon->GetWeaponID();

	// Necromancer's FOV circle (MiscVisuals) reads G::flAimbotFOV, which the
	// dispatcher zeroes at the start of every tick. The native projectile
	// module re-sets it here before its weapon checks, so the port must too -
	// this port claims every projectile weapon, leaving nothing to draw it.
	if (CFG::Aimbot_Projectile_Sort == 0)
		G::flAimbotFOV = CFG::Aimbot_Projectile_FOV;

	if (F::AimbotGlobal->ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;

	// Necromancer engagement gate: the aimbot only runs while the aim key is
	// held (or the Always On checkbox is set). Amalgam has no keybind system,
	// and since this port claims every projectile weapon from the dispatcher,
	// it must reproduce the same gate the native aimbot checks first.
	const bool bKeyAim = CFG::Aimbot_Always_On || H::Input->IsDown(CFG::Aimbot_Key);
	if (!Vars::Aimbot::General::AimType.Value
		|| !bKeyAim
		|| !F::AimbotGlobal->ShouldAim() && nWeaponID != TF_WEAPON_FLAMETHROWER)
	{
		G::bFiring = false;
		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		return false;
	}

	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon
		&& (nWeaponID == TF_WEAPON_COMPOUND_BOW || nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || nWeaponID == TF_WEAPON_CANNON && G::LastUserCmd->buttons & IN_ATTACK))
	{
		pCmd->buttons |= IN_ATTACK;
		if (!G::CanPrimaryAttack && !G::Reloading && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
		{
			G::bFiring = false;
			G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
			return false;
		}
	}

	auto vTargets = SortTargets(pLocal, pWeapon);
	if (vTargets.empty())
	{
		G::bFiring = false;
		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		return false;
	}

	if (!G::AimTarget.m_iEntIndex && vTargets.front().m_pEntity)
		G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

	for (auto& tTarget : vTargets)
	{
		m_flTimeTo = std::numeric_limits<float>::max();
		m_vPlayerPath.clear(); m_vProjectilePath.clear();

		const int iResult = CanHit(tTarget, pLocal, pWeapon);
		if (iResult != 1 && nWeaponID == TF_WEAPON_CANNON && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon
			&& !(G::OriginalCmd.buttons & (IN_ATTACK | IN_USE)))
		{	// loose cannon: decide now whether the double-detonate will land in time
			float flTime = m_flTimeTo - GRENADE_CHECK_INTERVAL;
			float flCharge = pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() > 0.f
				? pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() - I::GlobalVars->curtime
				: 1.f;
			flCharge = floorf(flCharge / GRENADE_CHECK_INTERVAL) * GRENADE_CHECK_INTERVAL + F::ProjSim.GetDesync();
			if (flCharge < flTime)
			{
				if (pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() > 0.f)
					CancelShot(pLocal, pWeapon, pCmd, m_iLastTickCancel);
			}
			else
			{
				pCmd->buttons |= IN_ATTACK;
				if (m_iLastTickCancel)
					pCmd->weaponselect = m_iLastTickCancel = 0;
				G::OriginalCmd.buttons |= IN_USE;
			}
		}
		if (!iResult) continue;

		// RapidFire (doubletap) keys off these like it does for the native
		// aimbots - without them nTargetIndex stays -1 and the shift never
		// starts on projectile weapons
		G::nTargetIndexEarly = tTarget.m_pEntity->entindex();
		G::nTargetIndex = tTarget.m_pEntity->entindex();

		if (IsPlayer(tTarget.m_pEntity))
			F::MoveSimDebug->NotifyProjectileTarget(tTarget.m_pEntity->As<C_TFPlayer>(), m_tMoveStorage.m_vPath, true);

		if (iResult == 2)
		{	// smooth/assistive fallback - aim only, no swing
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

		if (Vars::Aimbot::General::AutoShoot.Value)
		{
			switch (nWeaponID)
			{
			case TF_WEAPON_COMPOUND_BOW:
			case TF_WEAPON_PIPEBOMBLAUNCHER:
				pCmd->buttons |= IN_ATTACK;
				if (pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() > 0.f)
					pCmd->buttons &= ~IN_ATTACK; // charged - release it
				break;
			case TF_WEAPON_CANNON:
				pCmd->buttons |= IN_ATTACK;
				if (pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() > 0.f)
				{
					if (m_iLastTickCancel)
						pCmd->weaponselect = m_iLastTickCancel = 0;
					if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon)
					{	// rerun, if we won't hit in the future, fire
						int iResult2 = 0;

						float flCharge = pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() - I::GlobalVars->curtime;
						flCharge = floorf(flCharge / GRENADE_CHECK_INTERVAL) * GRENADE_CHECK_INTERVAL + F::ProjSim.GetDesync();
						if (flCharge > GRENADE_CHECK_INTERVAL)
						{
							auto tTarget2 = tTarget;
							const float flOldDetonateTime = pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime();

							pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() -= GRENADE_CHECK_INTERVAL;
							iResult2 = CanHit(tTarget2, pLocal, pWeapon, false);

							pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() = flOldDetonateTime;
						}

						if (iResult2 != 1)
							pCmd->buttons &= ~IN_ATTACK;
					}
					else
						pCmd->buttons &= ~IN_ATTACK;
				}
				break;
			case TF_WEAPON_BAT_WOOD:
			case TF_WEAPON_BAT_GIFTWRAP:
			case TF_WEAPON_LUNCHBOX:
				pCmd->buttons |= IN_ATTACK2, pCmd->buttons &= ~IN_ATTACK;
				break;
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
			case TF_WEAPON_RAYGUN:
			case TF_WEAPON_DRG_POMSON:
			case TF_WEAPON_CROSSBOW:
				pCmd->buttons |= IN_ATTACK, pCmd->buttons &= ~IN_ATTACK2;
				if (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
				{
					if (pWeapon->m_iClip1() > 0)
						pCmd->buttons &= ~IN_ATTACK; // beggar's bazooka loads while held
				}
				break;
			default:
				pCmd->buttons |= IN_ATTACK;
			}
		}

		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		G::bFiring = (pCmd->buttons & IN_ATTACK) != 0 && G::bCanPrimaryAttack;

		if (iResult == 1)
		{
			if (G::Attacking == 1)
				G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
			else
				G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
			m_iLastSolutionTick = I::GlobalVars->tickcount;
		}

		// Necromancer path visuals (Amalgam renders these through its Visuals
		// storage; we draw straight into the debug overlay)
		if (G::Attacking == 1 && !m_vProjectilePath.empty())
		{
			I::DebugOverlay->ClearAllOverlays();

			if (CFG::Visuals_Draw_Movement_Path_Style > 0 && !m_vPlayerPath.empty())
			{
				const auto& col = CFG::Color_Simulation_Movement;
				const int r = col.r, g = col.g, b = col.b;

				for (size_t n = 1; n < m_vPlayerPath.size(); n++)
				{
					if (CFG::Visuals_Draw_Movement_Path_Style == 2 && n % 2 == 0)
						continue;
					I::DebugOverlay->AddLineOverlay(m_vPlayerPath[n], m_vPlayerPath[n - 1], r, g, b, false, 2.0f);
				}
			}

			if (CFG::Visuals_Draw_Predicted_Path_Style > 0)
			{
				const auto& col = CFG::Color_Simulation_Projectile;
				const int r = col.r, g = col.g, b = col.b;

				for (size_t n = 1; n < m_vProjectilePath.size(); n++)
				{
					if (CFG::Visuals_Draw_Predicted_Path_Style == 2 && n % 2 == 0)
						continue;
					I::DebugOverlay->AddLineOverlay(m_vProjectilePath[n], m_vProjectilePath[n - 1], r, g, b, false, 2.0f);
				}
			}
		}

		// steer armed cmds even when this tick can't fire client-side (cooldown,
		// reload, charge hold): the cmd still carries IN_ATTACK, and if
		// prediction desyncs the server runs its attack anyway - without the
		// solved angle it fires raw at the player's view. Silent write without
		// the pSilent choke, so packets keep flowing and the camera stays put.
		// This stays on with anti-cheat compatibility too: a rocket that fires
		// at the view angles is exactly the "shot where I was looking" bug, and
		// these steady writes also make the fire tick's angles continuous with
		// the preceding cooldown ticks instead of a 1-tick snap, which the
		// compat's angle history then has nothing to flag
		if (G::Attacking != 1 && (pCmd->buttons & IN_ATTACK))
		{
			SDK::FixMovement(pCmd, tTarget.m_vAngleTo);
			pCmd->viewangles = tTarget.m_vAngleTo;
			G::bSilentAngles = true;
		}

		Aim(pCmd, tTarget.m_vAngleTo);
		if (G::bPSilentAngles)
		{	// these weapons can't hide their angles, fall back to plain silent
			switch (nWeaponID)
			{
			case TF_WEAPON_FLAMETHROWER:
			case TF_WEAPON_CLEAVER:
			case TF_WEAPON_JAR:
			case TF_WEAPON_JAR_MILK:
			case TF_WEAPON_JAR_GAS:
			case TF_WEAPON_BAT_WOOD:
			case TF_WEAPON_BAT_GIFTWRAP:
				G::bPSilentAngles = false, G::bSilentAngles = true;
			}
		}
		return true;
	}

	// the aimbot had a validated solution moments ago but lost it this tick -
	// validation flickers while targets move. A shot fired into that gap would
	// go out raw at the player's view, so carry the last solved angle instead.
	// Stale solutions (target broke cover / left range) are left untouched.
	// Runs with anti-cheat compatibility too, for the same reason as the armed
	// steering above - a raw shot at the view is worse than a smoothed write
	if ((G::OriginalCmd.buttons & IN_ATTACK) && m_iLastSolutionTick != -1
		&& I::GlobalVars->tickcount - m_iLastSolutionTick <= 3)
	{
		SDK::FixMovement(pCmd, m_vAngleTo);
		pCmd->viewangles = m_vAngleTo;
		G::bSilentAngles = true;
	}

	G::bFiring = false;
	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
	return false;
}

	bool CAmalgamAimbotProjectile::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
	{
		// respect the menu toggle: the port used to claim every projectile
		// weapon even with "Projectile Aimbot Active" unchecked and paid the
		// full per-tick prediction for nothing (the native fallback gates on
		// the same flag and does nothing either way)
		if (!CFG::Aimbot_Projectile_Active)
		{
			m_bLastTickHeld = false;
			m_bLastTickAimbotCharge = false;
			return false;
		}

		const bool bSuccess = RunMain(pLocal, pWeapon, pCmd);

		// charge bars for auto release / cancel charge
		float flAmount = 0.f;
		if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
		{
			const float flCharge = pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() : 0.f;
			flAmount = Math::RemapVal(flCharge, 0.f, SDK::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon), 0.f, 1.f);
		}
		else if (pWeapon->GetWeaponID() == TF_WEAPON_CANNON)
		{
			const float flMortar = SDK::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
			const float flCharge = pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() : -flMortar;
			flAmount = flMortar ? Math::RemapVal(flCharge, -flMortar, 0.f, 0.f, 1.f) : 0.f;
		}

		const bool bKeyAim = CFG::Aimbot_Always_On || H::Input->IsDown(CFG::Aimbot_Key);
		const bool bUserAttacking = G::OriginalCmd.buttons & IN_ATTACK;

		if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER && G::OriginalCmd.buttons & IN_ATTACK && Vars::Aimbot::Projectile::AutoRelease.Value && flAmount > Vars::Aimbot::Projectile::AutoRelease.Value / 100)
			pCmd->buttons &= ~IN_ATTACK;
		else if (G::CanPrimaryAttack && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::CancelCharge)
		{
			const bool bAimbotCharging = !bUserAttacking && (pCmd->buttons & IN_ATTACK);
			const bool bReleased = G::LastUserCmd->buttons & IN_ATTACK && !(pCmd->buttons & IN_ATTACK);
			const bool bRecentSolution = m_iLastSolutionTick != -1 && I::GlobalVars->tickcount - m_iLastSolutionTick <= 3;
			const bool bDisengaged = m_bLastTickHeld && !bKeyAim;
			if (!bUserAttacking)
				m_bManualBailLatch = false;
			const bool bManualBail = bUserAttacking && flAmount > 0.95f && !bRecentSolution;
			if ((m_bLastTickAimbotCharge && ((bReleased && !bSuccess && (bDisengaged || !bRecentSolution)) || (bAimbotCharging && flAmount > 0.95f))) || bManualBail)
			{
				CancelShot(pLocal, pWeapon, pCmd, m_iLastTickCancel);
				if (bManualBail && m_iLastTickCancel)
					m_bManualBailLatch = true;
			}
		}

		m_bLastTickHeld = bKeyAim && Vars::Aimbot::General::AimType.Value;
		m_bLastTickAimbotCharge = !bUserAttacking && (pCmd->buttons & IN_ATTACK);

	// every weapon Necromancer classifies as projectile is claimed here, so
	// the native projectile aimbot stays a pure fallback
	return true;
}

void CAmalgamAimbotProjectile::RestoreCancel(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!m_iLastTickCancel)
		return;

	if (!pLocal || pLocal->deadflag() || !Vars::Aimbot::General::AimType.Value)
	{
		m_iLastTickCancel = 0;
		return;
	}

	if (G::OriginalCmd.weaponselect)
		return;

	if (G::OriginalCmd.buttons & IN_ATTACK && !m_bManualBailLatch)
		return;

	pCmd->weaponselect = m_iLastTickCancel;
	m_iLastTickCancel = 0;
	m_bManualBailLatch = false;
}

// ============================================
// Stubs for the auto airblast path
// ============================================

bool CAmalgamAimbotProjectile::TestAngle(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, C_BaseEntity* pProjectile, Target_t& tTarget, Vec3& vPoint, Vec3& vAngles, int iSimTime, bool bSplash, std::vector<Vec3>* pProjectilePath)
{
	// Stub for auto airblast
	return false;
}

bool CAmalgamAimbotProjectile::CanHit(Target_t& tTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, C_BaseEntity* pProjectile)
{
	// Stub for auto airblast
	return false;
}

float CAmalgamAimbotProjectile::GetSplashRadius(C_TFWeaponBase* pWeapon, C_TFPlayer* pPlayer, float flScale)
{
	float flRadius = 0.f;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
		flRadius = TF_ROCKET_RADIUS;
		break;
	case TF_WEAPON_FLAREGUN:
	case TF_WEAPON_FLAREGUN_REVENGE:
		if (pWeapon->As<C_TFFlareGun>()->GetFlareGunType() == FLAREGUN_SCORCHSHOT)
			flRadius = TF_FLARE_DET_RADIUS;
		break;
	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_JAR_GAS:
		return JAR_EXPLODE_RADIUS * flScale;
	}
	if (!flRadius)
		return 0.f;

	flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		if (pPlayer->InCond(TF_COND_BLASTJUMPING) && SDK::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
			flRadius *= 0.8f;
	}
	return flRadius * flScale;
}

float CAmalgamAimbotProjectile::GetSplashRadius(C_BaseEntity* pProjectile, C_TFWeaponBase* pWeapon, C_TFPlayer* pPlayer, float flScale)
{
	float flRadius = 0.f;
	switch (pProjectile->GetClassId())
	{
	case ETFClassIds::CTFWeaponBaseGrenadeProj:
	case ETFClassIds::CTFWeaponBaseMerasmusGrenade:
	case ETFClassIds::CTFProjectile_Rocket:
	case ETFClassIds::CTFProjectile_SentryRocket:
	case ETFClassIds::CTFProjectile_EnergyBall:
		flRadius = TF_ROCKET_RADIUS;
		break;
	case ETFClassIds::CTFGrenadePipebombProjectile:
		if (pProjectile->As<C_TFGrenadePipebombProjectile>()->HasStickyEffects())
			flRadius = TF_ROCKET_RADIUS;
		break;
	case ETFClassIds::CTFProjectile_Flare:
		if (pWeapon && pWeapon->As<C_TFFlareGun>()->GetFlareGunType() == FLAREGUN_SCORCHSHOT)
			flRadius = TF_FLARE_DET_RADIUS;
		break;
	case ETFClassIds::CTFProjectile_Jar:
	case ETFClassIds::CTFProjectile_JarMilk:
	case ETFClassIds::CTFProjectile_JarGas:
		return JAR_EXPLODE_RADIUS * flScale;
	}
	if (pPlayer && pWeapon)
	{
		flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
		switch (pProjectile->GetClassId())
		{
		case ETFClassIds::CTFProjectile_Rocket:
		case ETFClassIds::CTFProjectile_SentryRocket:
			if (pPlayer->InCond(TF_COND_BLASTJUMPING) && SDK::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
				flRadius *= 0.8f;
		}
	}
	return flRadius * flScale;
}
