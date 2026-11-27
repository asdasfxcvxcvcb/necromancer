#include "AimbotProjectileSim.h"

#include "../../ProjectileSimulation/ProjectileSimulation.h"
#include "../../WorldFaces/WorldFaces.h"
#include "../../Players/Players.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../../LagRecords/LagRecords.h"
#include "../../CFG.h"
#include "Ticks.h"
#include "../../MoveSimDebug/MoveSimDebug.h"
#include "../../../../Utils/Math/Math.h"
#include "../../../../Utils/Math/SimMath.h"
#include "../../../../Utils/Hash/FNV1A.h"

#include <cmath>
#include <functional>
#include <numeric>

#ifndef GRENADE_CHECK_INTERVAL
	#define GRENADE_CHECK_INTERVAL 0.195f
#endif

static constexpr float COORD_EXTENT = 32768.f;

#ifndef TF_ROCKET_RADIUS
	#define TF_ROCKET_RADIUS 146.f
#endif
#ifndef TF_FLARE_DET_RADIUS
	#define TF_FLARE_DET_RADIUS 110.f
#endif
#ifndef JAR_EXPLODE_RADIUS
	#define JAR_EXPLODE_RADIUS 200.f
#endif

static constexpr float MATH_E = 2.718281828459f;

static Vec3 GetShootPos(C_TFPlayer* pLocal)
{
	Vec3 vPos = F::ProjectileTicks->GetShootPos();
	return vPos.IsZero() ? pLocal->GetShootPos() : vPos;
}

static bool GetArrowBones(C_BaseEntity* pEntity, matrix3x4_t* pOut)
{
	if (!pEntity || !IsPlayer(pEntity))
		return false;

	return pEntity->As<C_TFPlayer>()->SetupBones(pOut, 128, BONE_USED_BY_HITBOX, I::GlobalVars->curtime) != 0;
}

static void AddArrowHeadPoints(Directs_t& mDirects, int iPriority, int iFlags, C_TFPlayer* pVictim, matrix3x4_t* aBones, int nHeadBone, const Vec3& vCenter, const Vec3& vMins, const Vec3& vMaxs, const Vec3& vEye, Vec3& vPreferredOut, bool& bPreferredValidOut);

static float FullFraction(const Vec3& vDir, const CGameTrace& trace)
{
	if (!trace.DidHit() || FNV1A::Hash32(trace.surface.name) == FNV1A::Hash32Const("**displacement**"))
		return trace.fraction;

	return trace.fraction - DIST_EPSILON / vDir.Dot(trace.plane.normal);
}
static float FullFraction(const Vec3& vStart, const Vec3& vEnd, const CGameTrace& trace)
{
	return FullFraction(vEnd - vStart, trace);
}

static bool GetHitboxInfoFromBones(C_BaseAnimating* pEntity, matrix3x4_t* pBones, int nHitbox, Vec3* pCenter, Vec3* pMins, Vec3* pMaxs)
{
	if (!pEntity || !pBones)
		return false;

	auto pModel = pEntity->GetModel();
	if (!pModel)
		return false;

	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR)
		return false;

	auto pSet = pHDR->pHitboxSet(pEntity->m_nHitboxSet());
	if (!pSet || nHitbox < 0 || nHitbox >= pSet->numhitboxes)
		return false;

	auto pBox = pSet->pHitbox(nHitbox);
	if (!pBox || pBox->bone < 0 || pBox->bone >= 128)
		return false;

	if (pMins)
		*pMins = pBox->bbmin;
	if (pMaxs)
		*pMaxs = pBox->bbmax;
	if (pCenter)
		Math::VectorTransform((pBox->bbmin + pBox->bbmax) * 0.5f, pBones[pBox->bone], *pCenter);
	return true;
}

static float ArmTime(C_TFWeaponBase* pWeapon)
{
	if (ProjectileModifierFlags() & ModifiersEnum::UseArmTime && pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		static auto tf_grenadelauncher_livetime = I::CVar->FindVar("tf_grenadelauncher_livetime");
		const float flLiveTime = tf_grenadelauncher_livetime->GetFloat();
		return SDKUtils::AttribHookValue(flLiveTime, "sticky_arm_time", pWeapon);
	}

	return 0.f;
}

static bool ShouldLob(Info_t& tInfo)
{
	return tInfo.m_flGravity && ProjectileModifierFlags() & ModifiersEnum::LobAngles;
}

static bool ShouldLob(MoveSimStorage& tMoveStorage, Info_t& tInfo)
{
	return tInfo.m_bIgnoreTiming && (tMoveStorage.m_bFailed || tMoveStorage.m_pPlayer->m_fFlags() & FL_ONGROUND);
}

static bool AirSplash(C_TFWeaponBase* pWeapon, Info_t& tInfo)
{
	if (!(ProjectileModifierFlags() & ModifiersEnum::AirSplash) || tInfo.m_pProjectile)
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
	const int iHitboxFlags = ProjectileHitboxFlags();
	const bool bAirborne = tTarget.m_iTargetType == TargetEnum::Player
		&& !(tTarget.m_pEntity->As<C_TFPlayer>()->m_fFlags() & FL_ONGROUND);

	const bool bValid = F::AimbotGlobal->IsHitboxValid(nHitbox, iHitboxFlags)
		|| bAirborne && nHitbox == BOUNDS_BODY && tInfo.m_flRadius > 0.f && iHitboxFlags & HitboxesEnum::Feet;
	if (!bValid)
		return -1;

	int iHeadPriority = 0;
	int iBodyPriority = 1;
	int iFeetPriority = 2;

	if (iHitboxFlags & HitboxesEnum::Auto)
	{
		bool bHeadshot = ProjectileHitboxFlags() & HitboxesEnum::Head
			&& tTarget.m_iTargetType == TargetEnum::Player;
		if (bHeadshot)
		{
			if (!pProjectile)
				bHeadshot = tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW;
			else
				bHeadshot = pProjectile->GetClassId() == ETFClassIds::CTFProjectile_Arrow && SDKUtils::CanHeadshot(pProjectile->As<C_TFProjectile_Arrow>());

			if (ProjectileHitboxFlags() & HitboxesEnum::BodyaimIfLethal
				&& bHeadshot && !pProjectile && tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER
				&& tInfo.m_pWeapon->m_hOwner().GetEntryIndex() == I::EngineClient->GetLocalPlayer())
			{
				float flCharge = I::GlobalVars->curtime - tInfo.m_pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime();
				float flDamage = Math::RemapVal(flCharge, 0.f, 1.f, 50.f, 120.f);
				if (tInfo.m_pLocal->IsMiniCritBoosted())
					flDamage *= 1.36f;
				if (flDamage >= tTarget.m_pEntity->As<C_TFPlayer>()->m_iHealth())
					bHeadshot = false;

				if (tInfo.m_pLocal->IsCritBoosted())
					bHeadshot = false;
			}
		}
		if (bHeadshot)
			tTarget.m_nAimedHitbox = HITBOX_HEAD;

		const bool bLower = iHitboxFlags & HitboxesEnum::PrioritizeFeet
			&& tInfo.m_flRadius > 0.f
			&& !bAirborne;

		iHeadPriority = bHeadshot ? 0 : 2;
		iBodyPriority = bHeadshot ? -1 : bLower ? 1 : 0;
		iFeetPriority = bHeadshot ? -1 : bLower ? 0 : 1;
	}
	else if (bAirborne && iHitboxFlags & HitboxesEnum::Feet)
	{
		iHeadPriority = 1;
		iBodyPriority = 0;
		iFeetPriority = 3;
	}

	switch (nHitbox)
	{
	case BOUNDS_HEAD: return iHeadPriority;
	case BOUNDS_BODY: return iBodyPriority;
	case BOUNDS_FEET: return iFeetPriority;
	}

	return -1;
}

Directs_t CAimbotProjectileSim::GetDirects()
{
	Directs_t mDirects = {};

	if (CFG::Aimbot_Amalgam_Projectile_Splash == SplashPredictionEnum::Only && m_tInfo.m_flRadius)
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
				m_bArrowBonesValid = GetArrowBones(tTarget.m_pEntity, m_aArrowBones);
				if (!m_bArrowBonesValid)
					break;
				auto* aBones = m_aArrowBones;

				Vec3 vCenter = {}, vBBoxMins = {}, vBBoxMaxs = {};
				if (!GetHitboxInfoFromBones(tTarget.m_pEntity->As<C_BaseAnimating>(), aBones, HITBOX_HEAD, &vCenter, &vBBoxMins, &vBBoxMaxs))
				{
					m_bArrowBonesValid = false;
					break;
				}
				const bool bBow = m_tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW;

				if (bBow)
				{
					const auto pHDR = I::ModelInfoClient->GetStudiomodel(tTarget.m_pEntity->GetModel());
					const auto pSet = pHDR ? pHDR->pHitboxSet(tTarget.m_pEntity->As<C_BaseAnimating>()->m_nHitboxSet()) : nullptr;
					if (!pSet || HITBOX_HEAD >= pSet->numhitboxes)
						break;
					const int nHeadBone = pSet->pHitbox(HITBOX_HEAD)->bone;
					if (nHeadBone < 0 || nHeadBone >= 128)
						break;

					AddArrowHeadPoints(mDirects, iPriority, iFlags, tTarget.m_pEntity->As<C_TFPlayer>(), aBones, nHeadBone, vCenter, vBBoxMins, vBBoxMaxs, m_tInfo.m_vLocalEye, m_vArrowPreferred, m_bArrowPreferredValid);
					break;
				}

				Vec3 vOff = vCenter - tTarget.m_pEntity->m_vecOrigin();
				if (!bBow)
				{
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

					const float flLerp = (50.f + (100.f - 50.f) * flLow) / 100.f;
					vOff.z = vOff.z + (vMaxs.z - vOff.z) * flLerp;
				}

				vOff.x = std::clamp(vOff.x, vMins.x + 5.f, vMaxs.x - 5.f);
				vOff.y = std::clamp(vOff.y, vMins.y + 5.f, vMaxs.y - 5.f);
				vOff.z = std::clamp(vOff.z, vMins.z + 5.f, vMaxs.z - 5.f);
				if (!bBow)
					mDirects[iPriority] = { vOff, iFlags };
			}
			else
				mDirects[iPriority] = { Vec3(0, 0, vMaxs.z - 5.f), iFlags };
			break;
		case BOUNDS_BODY:
			mDirects[iPriority] = { Vec3(0, 0, (vMaxs.z - vMins.z) / 2), iFlags };
			break;
		case BOUNDS_FEET:
			mDirects[iPriority] = { Vec3(0, 0, vMins.z + 5.f), iFlags };
			break;
		}
	}

	return mDirects;
}

Splashes_t CAimbotProjectileSim::GetSplashes()
{
	Splashes_t vSplashes = {};

	if (CFG::Aimbot_Amalgam_Projectile_Splash == SplashPredictionEnum::Off || !m_tInfo.m_flRadius)
		return vSplashes;

	vSplashes.push_back(PointFlagsEnum::Regular);
	if (ShouldLob(m_tInfo))
		vSplashes.push_back(PointFlagsEnum::Lob);

	return vSplashes;
}

static std::vector<Vec3> ComputePoints(float flRadius, int iSamples)
{
	std::vector<Vec3> vPoints = { { Vec3(0.f, 0.f, -1.f) * flRadius } };
	if (!iSamples)
		return vPoints;

	vPoints.reserve(iSamples + 1);

	const float flRotateX = SDKUtils::StdRandomFloat(0.f, 360.f);
	const float flRotateY = SDKUtils::StdRandomFloat(0.f, 360.f);

	constexpr float MATH_PI = 3.14159265358979f;
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

static void HandleTrace(const Vec3& vPoint, std::vector<Setup_t>& vPoints, const Vec3& vTargetEye, Info_t& tInfo, std::function<bool()> fCheckPointTrace, std::function<bool()> fCheckPointAir, CGameTrace& trace, ITraceFilter& filter)
{
	SDKUtils::TraceHull(vTargetEye, vPoint, -tInfo.m_vHull, tInfo.m_vHull, MASK_SOLID, &filter, &trace);

	if (fCheckPointAir())
		return;

	if (fCheckPointTrace())
		vPoints.emplace_back(trace.endpos);

	if ((tInfo.m_vLocalEye - vTargetEye).Dot(vTargetEye - vPoint) > 0.f)
		return;

	SDKUtils::Trace(vPoint, vTargetEye, MASK_SHOT, &filter, &trace);
	if (trace.DidHit())
		return;

	SDKUtils::TraceHull(vPoint, vTargetEye, -tInfo.m_vHull, tInfo.m_vHull, MASK_SOLID, &filter, &trace);

	if (fCheckPointTrace())
		vPoints.emplace_back(trace.endpos);
}

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
		Math::ExpandPolygon(vVertices, tFace.m_vVertices, tFace.m_vNormal, SDKUtils::StdRandomFloat(0.f, DIST_EPSILON), &vTargetEye);

	for (int i = 0, n = int(tFace.m_vVertices.size()), o = SDKUtils::StdRandomInt(0, n - 1); ++i < n - 1;)
	{
		Vec3& vVertex1 = tFace.m_vVertices[o], &vVertex2 = tFace.m_vVertices[(o + i) % n], &vVertex3 = tFace.m_vVertices[(o + i + 1) % n];

		Vec3 vDir21 = vVertex2 - vVertex1, vDir31 = vVertex3 - vVertex1;
		float flArea = vDir21.Cross(vDir31).Length() / 2;
		float flSamples = flDensity * flArea / flRadius2Sqr;
		int iSamples = flSamples > flCutoff ? ceilf(flSamples) : fmodf(s_flTotal += flSamples, flCutoff) < flSamples;
		if (!iSamples)
			continue;

		int iFaceClosest = SDKUtils::StdRandomInt(iSamples < 2 ? 0 : iSamples < 4 ? 1 : 2, iSamples < 2 ? 1 : 2);
		int iEdgeRandom = SDKUtils::StdRandomInt(iSamples < 8 ? 0 : iSamples < 16 ? 1 : 2, iSamples < 3 ? 0 : iSamples < 12 ? 1 : 2);
		iEdgeRandom += iFaceClosest;
		for (int s = 0; s < iSamples; s++)
		{
			Vec3 vPoint, vNormal = tFace.m_vNormal; bool bInside = true;
			if (s < iFaceClosest)
			{
				float flEpsilon = s == 0 && (tFace.m_iType == FaceTypeEnum::BoxBrush || iFaceClosest != 1 || SDKUtils::StdRandomBool()) ? CALC_EPSILON : DIST_EPSILON;
				vPoint = Math::ClosestPointOnTriangle(vTargetOrigin, vVertex1, vVertex2, vVertex3, &bInside);
				vPoint += Vec3(SDKUtils::StdRandomFloat(-flEpsilon, flEpsilon), SDKUtils::StdRandomFloat(-flEpsilon, flEpsilon), SDKUtils::StdRandomFloat(-flEpsilon, flEpsilon));
			}
			else if (s < iEdgeRandom)
			{
				float flEpsilon = SDKUtils::StdRandomBool() ? CALC_EPSILON : DIST_EPSILON; bInside = false;
				switch (SDKUtils::StdRandomInt(0, 2))
				{
				case 0: vPoint = vVertex1.Lerp(vVertex2, SDKUtils::StdRandomFloat()); break;
				case 1: vPoint = vVertex2.Lerp(vVertex3, SDKUtils::StdRandomFloat()); break;
				case 2: vPoint = vVertex3.Lerp(vVertex1, SDKUtils::StdRandomFloat()); break;
				}
				vPoint += Vec3(SDKUtils::StdRandomFloat(-flEpsilon, flEpsilon), SDKUtils::StdRandomFloat(-flEpsilon, flEpsilon), SDKUtils::StdRandomFloat(-flEpsilon, flEpsilon));
			}
			else
			{
				float flRandom1 = SDKUtils::StdRandomFloat(), flRandom2 = SDKUtils::StdRandomFloat();
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
				Math::ClosestPointOnPolygon(vPoint, vEpsilon, vPolygonNormal, &bInside);

				if (tFace.m_iType == FaceTypeEnum::Prop && !bInside)
					vPoint += tFace.m_vNormal * SDKUtils::StdRandomFloat(0.f, DIST_EPSILON);
			}

			if (bInside && I::EngineTrace->GetPointContents(vPoint) & MASK_SOLID)
				continue;

			int nSubMask = nMask;
			if (!bInside)
				vNormal = (vTargetEye - vPoint).Normalized(), nSubMask &= ~CONTENTS_MOVEABLE;

			SDKUtils::Trace(vPoint + vNormal * tInfo.m_flNormalOffset, vTargetEye, nSubMask, &filter, &trace);
			if (trace.fraction != 1.f)
				continue;

			vPoints.emplace_back(vPoint);
		}
	}
}

void CAimbotProjectileSim::SetupSplashPoints(Vec3& vOrigin, std::vector<Setup_t>& vSplashPoints, int iFlags)
{
	vSplashPoints.clear();

	CGameTrace trace = {};
	CTraceFilterWorldAndProps filter = {};

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

	const bool bFaceMode = CFG::Aimbot_Amalgam_Projectile_SplashMode == SplashModeEnum::Face;
	int iPoints = bFaceMode && !bAirSplash ? 0
		: !m_tInfo.m_flGravity ? CFG::Aimbot_Amalgam_Projectile_SplashPoints : CFG::Aimbot_Amalgam_Projectile_SplashPoints;
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
					vSplashPoints.emplace_back(vPoints[i] * SDKUtils::StdRandomFloat(0.f, 1.f) + vTargetCenter, PointTypeEnum::Air);
				if (bFaceMode && i)
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

	if (float flDensity = !m_tInfo.m_flGravity ? CFG::Aimbot_Amalgam_Projectile_SplashDensity_Direct : CFG::Aimbot_Amalgam_Projectile_SplashDensity_Arc;
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
		F::WorldFaces->SetNormalValidCallback(&fNormalValid);
		std::vector<Face_t> vFaces = F::WorldFaces->GetFacesInAABB(vMins, vMaxs, MASK_SOLID, &filter);
		F::WorldFaces->SetNormalValidCallback();

		float flCutoff = 0.0000001f * powf(float(vFaces.size()), 2); s_flTotal = SDKUtils::StdRandomFloat();
		for (auto& tFace : vFaces)
		{
			HandleFace(tFace, vSplashPoints, flDensity, flRadius, flCutoff, vTargetEye, vTargetCenter, vOrigin, m_tInfo, trace, filter);
		}
	}

	if (vSplashPoints.size() > 1)
	{
		for (int i = int(vSplashPoints.size()) - 1; i > 0; i--)
			std::swap(vSplashPoints[i], vSplashPoints[SDKUtils::StdRandomInt(1, i)]);
	}
}

std::vector<Point_t> CAimbotProjectileSim::GetSplashPoints(Vec3 vOrigin, std::vector<Setup_t>& vSplashPoints, int iSimTime, int iFlags, bool bFirst)
{
	std::vector<Point_t> vPoints = {};

	m_tInfo.m_pTarget->m_vPos = vOrigin;
	const bool bLob = iFlags & CalculateFlagsEnum::LobAngle;
	const int iTolerance = m_tInfo.m_bIgnoreTiming && bLob ? std::numeric_limits<int>::max() : m_tInfo.m_iArmTime ? -1 : 0;
	const int iLimit = !bFirst ? m_tInfo.m_iSplashRestrict : std::max(m_tInfo.m_iSplashRestrict, 25);
	const bool bSort = !bLob || !m_tInfo.m_bIgnoreTiming;

	const float flRadiusSqr = powf(m_tInfo.m_flRadius, 2);
	float flRadiusAirSqr = flRadiusSqr;
	if (ProjectileModifierFlags() & ModifiersEnum::AirSplash && !m_tInfo.m_pProjectile && m_tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		static auto tf_grenadelauncher_livetime = I::CVar->FindVar("tf_grenadelauncher_livetime");
		static auto tf_sticky_radius_ramp_time = I::CVar->FindVar("tf_sticky_radius_ramp_time");
		static auto tf_sticky_airdet_radius = I::CVar->FindVar("tf_sticky_airdet_radius");
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
			{
				it = vSplashPoints.erase(it);
			}
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
				if (!SDKUtils::AttribHookValue(0, "grenade_no_spin", pInfo->m_pWeapon))
					return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.120f, 0.200f);
				else
					return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.060f, 0.085f);
			case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
				return Math::RemapVal(flVelocity, 1454.f, k_flMaxVelocity, 0.385f, 0.530f);
			case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
				return Math::RemapVal(flVelocity, 922.f, k_flMaxVelocity, 0.090f, 0.190f);
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
			if (!(ProjectileModifierFlags() & ModifiersEnum::LobAngles))
				return 0.f;

			switch (pInfo->m_uType)
			{
			case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
				if (!SDKUtils::AttribHookValue(0, "grenade_no_spin", pInfo->m_pWeapon))
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
		{
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

	return true;
}

void CAimbotProjectileSim::CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& tOut, int iFlags, int iTolerance, float flRoll)
{
	if (tOut.m_iCalculated != CalculatedEnum::Pending)
		return;

	float flPitch, flYaw;
	{
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

	const int iSimFlags = (iFlags & CalculateFlagsEnum::SetupClip ? ProjSimEnum::Trace : ProjSimEnum::None)
		| ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(m_tInfo.m_pLocal, m_tInfo.m_pWeapon, { flPitch, flYaw, flRoll }, m_tProjInfo, iSimFlags))
	{
		tOut.m_iCalculated = CalculatedEnum::Bad;
		return;
	}

	{
		if (!GetAngle(m_tProjInfo.m_vPos, vTargetPos, iFlags, m_tInfo, &m_tProjInfo, tOut.m_flPitch, tOut.m_flYaw, tOut.m_flTime))
		{
			tOut.m_iCalculated = CalculatedEnum::Bad;
			return;
		}

		{
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

		{
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


static float ArrowRayBoxEntry(const Vec3& vStart, const Vec3& vDir, float flLen, const Vec3& vMins, const Vec3& vMaxs)
{
	float flEnter = 0.0f;
	float flExit = flLen;
	for (int a = 0; a < 3; a++)
	{
		const float flD = vDir[a];
		if (fabsf(flD) < 1e-8f)
		{
			if (vStart[a] < vMins[a] || vStart[a] > vMaxs[a])
				return -1.0f;
			continue;
		}

		float flNear = (vMins[a] - vStart[a]) / flD;
		float flFar = (vMaxs[a] - vStart[a]) / flD;
		if (flNear > flFar)
			std::swap(flNear, flFar);

		flEnter = std::max(flEnter, flNear);
		flExit = std::min(flExit, flFar);
	}

	if (flEnter > flExit)
		return -1.0f;
	return flEnter;
}

static Vec3 ArrowInverseTransform(const Vec3& v, const matrix3x4_t& m)
{
	const Vec3 vDelta = v - Vec3(m[0][3], m[1][3], m[2][3]);
	return Vec3(
		vDelta.Dot(Vec3(m[0][0], m[1][0], m[2][0])),
		vDelta.Dot(Vec3(m[0][1], m[1][1], m[2][1])),
		vDelta.Dot(Vec3(m[0][2], m[1][2], m[2][2])));
}

static void AddArrowHeadPoints(Directs_t& mDirects, int iPriority, int iFlags, C_TFPlayer* pVictim, matrix3x4_t* aBones, int nHeadBone, const Vec3& vCenter, const Vec3& vMins, const Vec3& vMaxs, const Vec3& vEye, Vec3& vPreferredOut, bool& bPreferredValidOut)
{
	const Vec3 vOrigin = pVictim->m_vecOrigin();
	const Vec3 vEntityMins = pVictim->m_vecMins();
	const Vec3 vEntityMaxs = pVictim->m_vecMaxs();
	Vec3 vSimple = vCenter - vOrigin;
	float flLow = 0.f;
	const Vec3 vDelta = vOrigin + pVictim->m_vecViewOffset() - vEye;
	if (vDelta.z > 0.f)
	{
		const float flXY = vDelta.Length2D();
		flLow = flXY ? Math::RemapVal(vDelta.z / flXY, 0.f, 0.5f, 0.f, 1.f) : 1.f;
	}
	const float flLerp = (70.f + 30.f * flLow) / 100.f;
	vSimple.z += (vEntityMaxs.z - vSimple.z) * flLerp;
	vSimple.x = std::clamp(vSimple.x, vEntityMins.x + 5.f, vEntityMaxs.x - 5.f);
	vSimple.y = std::clamp(vSimple.y, vEntityMins.y + 5.f, vEntityMaxs.y - 5.f);
	vSimple.z = std::clamp(vSimple.z, vEntityMins.z + 5.f, vEntityMaxs.z - 5.f);
	mDirects[iPriority] = { vSimple, iFlags, true };

	Vec3 vForward = vCenter - vEye;
	const bool bViewBasis = vForward.LengthSqr() >= 1e-6f;
	Vec3 vRight = {};
	Vec3 vUp = {};
	float flMinX = 0.f;
	float flMaxX = 0.f;
	float flMinY = 0.f;
	float flMaxY = 0.f;
	float flWidth = 0.f;
	float flHeight = 0.f;

	if (bViewBasis)
	{
		vForward = vForward.Normalized();
		vRight = vForward.Cross({ 0.f, 0.f, 1.f });
		if (vRight.LengthSqr() < 1e-6f)
			vRight = { 0.f, 1.f, 0.f };
		else
			vRight = vRight.Normalized();
		vUp = vRight.Cross(vForward).Normalized();

		flMinX = std::numeric_limits<float>::max();
		flMaxX = std::numeric_limits<float>::lowest();
		flMinY = std::numeric_limits<float>::max();
		flMaxY = std::numeric_limits<float>::lowest();
		for (int c = 0; c < 8; c++)
		{
			const Vec3 vLocal(c & 1 ? vMins.x : vMaxs.x, c & 2 ? vMins.y : vMaxs.y, c & 4 ? vMins.z : vMaxs.z);
			Vec3 vWorld;
			Math::VectorTransform(vLocal, aBones[nHeadBone], vWorld);
			const Vec3 vDelta = vWorld - vCenter;
			flMinX = std::min(flMinX, vDelta.Dot(vRight));
			flMaxX = std::max(flMaxX, vDelta.Dot(vRight));
			flMinY = std::min(flMinY, vDelta.Dot(vUp));
			flMaxY = std::max(flMaxY, vDelta.Dot(vUp));
		}
		flWidth = flMaxX - flMinX;
		flHeight = flMaxY - flMinY;
	}

	auto ClampedViewPoint = [&](float flR, float flU, Vec3& vOffsetOut)
	{
		const Vec3 vWorld = vCenter + vRight * flR + vUp * flU;
		const Vec3 vLocal = ArrowInverseTransform(vWorld, aBones[nHeadBone]);
		const Vec3 vClamped(std::clamp(vLocal.x, vMins.x, vMaxs.x),
			std::clamp(vLocal.y, vMins.y, vMaxs.y),
			std::clamp(vLocal.z, vMins.z, vMaxs.z));
		Vec3 vWorldClamped;
		Math::VectorTransform(vClamped, aBones[nHeadBone], vWorldClamped);
		vOffsetOut = vWorldClamped - vOrigin;
	};

	if (bViewBasis && flWidth >= 0.1f && flHeight >= 0.1f)
	{
		const float flMidX = (flMinX + flMaxX) * 0.5f;
		const float flMidY = (flMinY + flMaxY) * 0.5f;
		const float aViewR[5] = { flMidX, flMidX, flMidX, flMidX + flWidth * 0.35f, flMidX - flWidth * 0.35f };
		const float aViewU[5] = { flMinY + flHeight * 0.78f, flMidY, flMinY + flHeight * 0.85f, flMidY, flMidY };

		ClampedViewPoint(aViewR[0], aViewU[0], vPreferredOut);
		bPreferredValidOut = true;
		mDirects[iPriority + 100] = { vPreferredOut, iFlags, true };

		std::vector<Vec3> vAdded = { vPreferredOut };
		for (int n = 1; n < 5; n++)
		{
			Vec3 vOffset;
			ClampedViewPoint(aViewR[n], aViewU[n], vOffset);
			bool bDup = false;
			for (const auto& vExisting : vAdded)
			{
				if (vExisting.DistToSqr(vOffset) < 0.25f)
				{
					bDup = true;
					break;
				}
			}
			if (bDup)
				continue;
			mDirects[iPriority + 100 * (n + 1)] = { vOffset, iFlags, true };
			vAdded.push_back(vOffset);
		}
	}
	else
	{
		const Vec3 vLocalCenter = (vMins + vMaxs) * 0.5f;
		const Vec3 vHalfExtent = (vMaxs - vMins) * 0.5f;
		const Vec3 aLocalPoints[5] = {
			{ vLocalCenter.x, vLocalCenter.y, vMins.z + (vMaxs.z - vMins.z) * 0.78f },
			{ vLocalCenter.x, vLocalCenter.y, vMins.z + (vMaxs.z - vMins.z) * 0.50f },
			{ vLocalCenter.x, vLocalCenter.y, vMins.z + (vMaxs.z - vMins.z) * 0.85f },
			{ vLocalCenter.x + vHalfExtent.x * 0.4f, vLocalCenter.y, vMins.z + (vMaxs.z - vMins.z) * 0.78f },
			{ vLocalCenter.x - vHalfExtent.x * 0.4f, vLocalCenter.y, vMins.z + (vMaxs.z - vMins.z) * 0.78f }
		};
		Vec3 vPreferredWorld;
		Math::VectorTransform(aLocalPoints[0], aBones[nHeadBone], vPreferredWorld);
		vPreferredOut = vPreferredWorld - vOrigin;
		bPreferredValidOut = true;
		for (int n = 0; n < 5; n++)
		{
			Vec3 vWorld;
			Math::VectorTransform(aLocalPoints[n], aBones[nHeadBone], vWorld);
			mDirects[iPriority + 100 * (n + 1)] = { vWorld - vOrigin, iFlags, true };
		}
	}

	if (!bViewBasis || flWidth < 0.1f || flHeight < 0.1f)
		return;

	const float flMidX = (flMinX + flMaxX) * 0.5f;
	const float flMidY = (flMinY + flMaxY) * 0.5f;
	constexpr int nAngles = 8;
	constexpr float flRings[2] = { 0.5f, 1.2f };
	std::vector<std::pair<float, Vec3>> vAround;
	for (const float flRing : flRings)
	{
		const float flExpandX = flWidth * flRing;
		const float flExpandY = flHeight * flRing;
		const float flAX[nAngles] = { flMinX - flExpandX, flMaxX + flExpandX, flMinX - flExpandX, flMaxX + flExpandX, flMidX, flMidX, flMinX - flExpandX, flMaxX + flExpandX };
		const float flAY[nAngles] = { flMinY - flExpandY, flMinY - flExpandY, flMaxY + flExpandY, flMaxY + flExpandY, flMinY - flExpandY, flMaxY + flExpandY, flMidY, flMidY };
		for (int n = 0; n < nAngles; n++)
		{
			const Vec3 vWorld = vCenter + vRight * flAX[n] + vUp * flAY[n];
			Vec3 vRel = vWorld - vOrigin;
			vRel.x = std::clamp(vRel.x, vEntityMins.x - 2.f, vEntityMaxs.x + 2.f);
			vRel.y = std::clamp(vRel.y, vEntityMins.y - 2.f, vEntityMaxs.y + 2.f);
			vRel.z = std::clamp(vRel.z, vEntityMins.z - 2.f, vEntityMaxs.z + 2.f);

			bool bDup = false;
			for (const auto& tExisting : vAround)
			{
				if (tExisting.second.DistToSqr(vRel) < 1.f)
				{
					bDup = true;
					break;
				}
			}
			if (bDup)
				continue;

			const Vec3 vLocal = ArrowInverseTransform(vOrigin + vRel, aBones[nHeadBone]);
			const Vec3 vClamped(std::clamp(vLocal.x, vMins.x, vMaxs.x),
				std::clamp(vLocal.y, vMins.y, vMaxs.y),
				std::clamp(vLocal.z, vMins.z, vMaxs.z));
			vAround.push_back({ (vLocal - vClamped).LengthSqr(), vRel });
		}
	}

	std::sort(vAround.begin(), vAround.end(), [](const auto& a, const auto& b)
		{
			return a.first < b.first;
		});
	int iAround = 0;
	for (auto& [flDistSq, vRel] : vAround)
	{
		if (iAround >= 8)
			break;
		mDirects[iPriority + 100 * (6 + iAround)] = { vRel, iFlags, true };
		iAround++;
	}
}

static int ArrowTouchResolveGroup(C_TFPlayer* pVictim, matrix3x4_t* aBones, const Vec3& vBoneDelta, const Vec3& vTouchOrigin, const Vec3& vVelocity, float flRemainingFraction, int* pOutStep = nullptr, int* pOutBox = nullptr, float* pOutHeadDist = nullptr, float* pOutWinDist = nullptr, float* pOutMargin = nullptr, int* pOutRunnerBox = nullptr)
{
	if (!pVictim || !pVictim->GetModel())
		return HITGROUP_GENERIC;

	const auto pHDR = I::ModelInfoClient->GetStudiomodel(pVictim->GetModel());
	if (!pHDR)
		return HITGROUP_GENERIC;
	const auto pSet = pHDR->pHitboxSet(pVictim->m_nHitboxSet());
	if (!pSet || !pSet->numhitboxes)
		return HITGROUP_GENERIC;

	const Vec3 vDir = vVelocity.Normalized();
	const float flFrameLen = vVelocity.Length() * I::GlobalVars->interval_per_tick * std::clamp(flRemainingFraction, 0.f, 1.f);
	const Vec3 vShift = vTouchOrigin - vBoneDelta;

	int nDirectBox = -1;
	float flBestFrac = flFrameLen;
	float flSecondFrac = flFrameLen;
	int nSecondBox = -1;
	for (int i = 0; i < pSet->numhitboxes; i++)
	{
		const auto pBox = pSet->pHitbox(i);
		if (!pBox || pBox->bone < 0 || pBox->bone >= 128)
			continue;
		const Vec3 vLocalStart = ArrowInverseTransform(vShift, aBones[pBox->bone]);
		const Vec3 vLocalEnd = ArrowInverseTransform(vShift + vDir * flFrameLen, aBones[pBox->bone]);
		Vec3 vLocalDir = vLocalEnd - vLocalStart;
		const float flLocalLen = vLocalDir.Length();
		if (flLocalLen < 1e-8f)
			continue;
		vLocalDir /= flLocalLen;

		const float flEntry = ArrowRayBoxEntry(vLocalStart, vLocalDir, flLocalLen, pBox->bbmin, pBox->bbmax);
		if (flEntry < 0.0f)
			continue;
		if (i == 0 && pOutHeadDist)
			*pOutHeadDist = flEntry;
		if (flEntry < flBestFrac)
		{
			flSecondFrac = flBestFrac;
			nSecondBox = nDirectBox;
			flBestFrac = flEntry;
			nDirectBox = i;
		}
		else if (flEntry < flSecondFrac)
		{
			flSecondFrac = flEntry;
			nSecondBox = i;
		}
	}
	if (nDirectBox >= 0)
	{
		if (pOutStep)
			*pOutStep = 1;
		if (pOutBox)
			*pOutBox = nDirectBox;
		if (pOutWinDist)
			*pOutWinDist = flBestFrac;
		if (pOutMargin)
			*pOutMargin = flSecondFrac - flBestFrac;
		if (pOutRunnerBox)
			*pOutRunnerBox = nSecondBox;
		return pSet->pHitbox(nDirectBox)->group;
	}

	const Vec3 vScanStart = vShift + vDir * 16.f;
	float flClosestDist = std::numeric_limits<float>::max();
	float flSecondDist = std::numeric_limits<float>::max();
	int nClosestBox = -1;
	int nRunnerBox = -1;
	for (int i = 0; i < pSet->numhitboxes; i++)
	{
		const auto pBox = pSet->pHitbox(i);
		if (!pBox || pBox->bone < 0 || pBox->bone >= 128)
			continue;

		const Vec3 vBoneOrigin(aBones[pBox->bone][0][3], aBones[pBox->bone][1][3], aBones[pBox->bone][2][3]);
		const Vec3 vDelta = vBoneOrigin - vScanStart;
		const float flLen = vDelta.Length();
		if (flLen < 1e-8f)
			continue;

		const float flEntry = ArrowRayBoxEntry(vScanStart, vDelta / flLen, flLen, vBoneOrigin + pBox->bbmin, vBoneOrigin + pBox->bbmax);
		const float flDist = flEntry >= 0.f ? flEntry : flLen;
		if (flDist < flClosestDist)
		{
			flSecondDist = flClosestDist;
			nRunnerBox = nClosestBox;
			flClosestDist = flDist;
			nClosestBox = i;
		}
		else if (flDist < flSecondDist)
		{
			flSecondDist = flDist;
			nRunnerBox = i;
		}
	}

	if (nClosestBox >= 0)
	{
		if (pOutStep)
			*pOutStep = 2;
		if (pOutBox)
			*pOutBox = nClosestBox;
		if (pOutWinDist)
			*pOutWinDist = flClosestDist;
		if (pOutMargin)
			*pOutMargin = flSecondDist == std::numeric_limits<float>::max() ? 0.f : flSecondDist - flClosestDist;
		if (pOutRunnerBox)
			*pOutRunnerBox = nRunnerBox;
		return pSet->pHitbox(nClosestBox)->group;
	}

	if (pOutStep)
		*pOutStep = 0;
	return HITGROUP_GENERIC;
}

bool CAimbotProjectileSim::TestAngle(const Vec3& vPoint, const Vec3& vAngles, int iSimTime, int iType, int iFlags, bool bSecondTest, bool bStrictHead)
{
	auto pLocal = m_tInfo.m_pLocal;
	auto pWeapon = m_tInfo.m_pWeapon;
	auto& tTarget = *m_tInfo.m_pTarget;

	const bool bArrowHeadshot = bStrictHead && pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW
		&& tTarget.m_nAimedHitbox == HITBOX_HEAD
		&& tTarget.m_iTargetType == TargetEnum::Player;

	m_bObstructed = false;
	m_flLastHitMargin = -1.f;
	if (bArrowHeadshot && !m_bArrowCommitStage)
	{
		const int iBudgetFloor = m_bArrowFallbackStage ? 0 : 8;
		if (m_iArrowTestBudget <= iBudgetFloor)
			return false;
		m_iArrowTestBudget--;
	}

	int iSimFlags = ProjSimEnum::Trace | ProjSimEnum::InitCheck
		| ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, vAngles, m_tProjInfo, iSimFlags))
		return false;
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
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
		case TF_WEAPON_DRG_POMSON:
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_SYRINGEGUN_MEDIC:
			filter.iPlayer = PLAYER_ALL;
		}
	}
	if (iType == PointTypeEnum::Direct && tTarget.m_pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
		filter.iPlayer = PLAYER_ALL;
	F::ProjSim.SetupTrace(filter, nMask, pWeapon);

	if (!m_tProjInfo.m_flGravity)
	{
		SDKUtils::TraceHull(m_tProjInfo.m_vPos, vPoint, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);
		if (FullFraction(m_tProjInfo.m_vPos, vPoint, trace) < 0.999f && trace.m_pEnt != tTarget.m_pEntity)
		{
			m_bObstructed = true;
			return false;
		}
	}

	bool bDidHit = false;
	Vec3 vNew = F::ProjSim.GetOrigin();
	const int iTimingTolerance = TIME_TO_TICKS(m_tInfo.m_flBoundsTime);
	const float flRadiusSqr = iType != PointTypeEnum::Direct ? powf(m_tProjInfo.m_flVelocity * TICK_INTERVAL + m_tProjInfo.m_vHull.z, 2) : std::numeric_limits<float>::max();
	const int iTraceInterval = iFlags == PointFlagsEnum::Lob ? 20
		: iType != PointTypeEnum::Direct ? 10
		: 1;

	const RestoreInfo_t tOriginal = { tTarget.m_pEntity->GetAbsOrigin(), tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs() };

	matrix3x4_t aVictimBones[128] = {};
	const bool bVictimBones = bArrowHeadshot && m_bArrowBonesValid;
	if (bVictimBones)
		memcpy(aVictimBones, m_aArrowBones, sizeof(aVictimBones));

	const int iLatencyTicks = TIME_TO_TICKS(m_tInfo.m_flLatency);
	const bool bMoveEnemy = bArrowHeadshot && !m_tMoveStorage.m_vPath.empty();

	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	tTarget.m_pEntity->m_vecMins() = { std::clamp(tTarget.m_pEntity->m_vecMins().x, -24.f, 0.f), std::clamp(tTarget.m_pEntity->m_vecMins().y, -24.f, 0.f), tTarget.m_pEntity->m_vecMins().z };
	tTarget.m_pEntity->m_vecMaxs() = { std::clamp(tTarget.m_pEntity->m_vecMaxs().x, 0.f, 24.f), std::clamp(tTarget.m_pEntity->m_vecMaxs().y, 0.f, 24.f), tTarget.m_pEntity->m_vecMaxs().z };
	for (int n = 1; n <= iSimTime; n++)
	{
		F::ProjSim.RunTick(m_tProjInfo);

		if (bMoveEnemy)
		{
			const int iPathIdx = std::clamp(n + iLatencyTicks, 0, int(m_tMoveStorage.m_vPath.size()) - 1);
			tTarget.m_pEntity->SetAbsOrigin(m_tMoveStorage.m_vPath[iPathIdx]);
		}

		if (bDidHit)
		{
			trace.endpos = F::ProjSim.GetOrigin();
			continue;
		}
		if (iTraceInterval != 1 && n % iTraceInterval && n != iSimTime)
			continue;

		const Vec3 vOld = vNew; vNew = F::ProjSim.GetOrigin();
		SDKUtils::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);

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
				bTarget = trace.m_pEnt == tTarget.m_pEntity, bValid = bTarget && iSimTime - n < iTimingTolerance;
				if (bValid && bArrowHeadshot && bVictimBones)
				{
					const Vec3 vTouchVel = (vNew - vOld) / I::GlobalVars->interval_per_tick;
					const Vec3 vBoneDelta = tTarget.m_pEntity->GetAbsOrigin() - tOriginal.m_vOrigin;
					float flTouchMargin = -1.f;
					const int nCenterGroup = ArrowTouchResolveGroup(tTarget.m_pEntity->As<C_TFPlayer>(), aVictimBones, vBoneDelta, trace.endpos, vTouchVel, 1.f, nullptr, nullptr, nullptr, nullptr, &flTouchMargin);
					m_flLastHitMargin = flTouchMargin;
					if (nCenterGroup != HITGROUP_HEAD)
						bValid = false;
				}
				break;
			case PointTypeEnum::Geometry:
				bValid = trace.endpos.DistToSqr(vPoint) < flRadiusSqr; break;
			case PointTypeEnum::Air:
				bValid = !trace.DidHit(); break;
			}

			m_bObstructed = bHit && !bTarget;

			if (bValid && iType != PointTypeEnum::Direct)
			{
				CGameTrace trace2 = {};
				SDKUtils::Trace(trace.endpos + trace.plane.normal * m_tInfo.m_flNormalOffset, tTarget.m_vPos + m_tInfo.m_vTargetEye, MASK_SHOT, &filter, &trace2);
				bValid = trace2.fraction == 1.f;
			}

			if (bValid && true && iTraceInterval != 1)
			{
				CGameTrace trace2 = {}; Vec3 vRetestOld, vRetestNew;
				int iTicks = int(m_tProjInfo.m_vPath.size());
				if (m_tInfo.m_flGravity)
					iTicks -= iTimingTolerance;

				for (int i = 1; i < iTicks; i++)
				{
					vRetestOld = m_tProjInfo.m_vPath[i - 1], vRetestNew = m_tProjInfo.m_vPath[i];
					SDKUtils::TraceHull(vRetestOld, vRetestNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace2);
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

				if (tTarget.m_nAimedHitbox == HITBOX_HEAD && !bSecondTest
					&& (AimTypeValue() == AimTypeEnum::Smooth
					|| AimTypeValue() == AimTypeEnum::Assistive))
				{
					matrix3x4_t aRetestBones[128] = {};
					if (!GetArrowBones(tTarget.m_pEntity, aRetestBones))
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

						Vec3 vCenter; Math::VectorTransform({}, aRetestBones[pBox->bone], vCenter);

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
			{
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

bool CAimbotProjectileSim::HandlePoint(const Vec3& vOrigin, int iSimTime, float flPitch, float flYaw, float flTime, const Vec3& vPoint, int iType, int iFlags, bool bStrictHead)
{
	bool bReturn = false;

	Vec3 vAngles; Aim(G::CurrentUserCmd->viewangles, { flPitch, flYaw, 0.f }, vAngles);
	m_tInfo.m_pTarget->m_vPos = vOrigin;

	const int iOriginalSimTime = iSimTime;
	if (m_tInfo.m_iArmTime && iType == PointTypeEnum::Geometry || m_tInfo.m_bIgnoreTiming && iFlags == PointFlagsEnum::Lob)
	{
		if (flTime > m_tProjInfo.m_flLifetime)
			return false;

		iSimTime = std::ceil(flTime / I::GlobalVars->interval_per_tick);
	}

	if (!TestAngle(vPoint, vAngles, iSimTime, iType, iFlags, false, bStrictHead))
	{
		if (!m_iResult && m_bObstructed && !m_bArrowFallbackStage
			&& AimTypeValue() == AimTypeEnum::Silent
			&& CFG::Aimbot_Projectile_Neckbreaker && !CFG::Misc_AntiCheat_Enabled)
		{
			const int iStep = std::max(CFG::Aimbot_Projectile_NeckbreakerStep, 15);
			const bool bLob = iFlags & PointFlagsEnum::Lob;
			int iSolveFlags = CalculateFlagsEnum::Accuracy;
			if (bLob)
				iSolveFlags |= CalculateFlagsEnum::LobAngle;
			const int iTolerance = m_tInfo.m_bIgnoreTiming && bLob ? std::numeric_limits<int>::max() : iType == PointTypeEnum::Direct ? -1 : m_tInfo.m_iArmTime ? -1 : 0;
			const int iArrowBudgetFloor = bStrictHead && m_tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW && !m_bArrowCommitStage ? (m_bArrowFallbackStage ? 0 : 8) : 0;
			for (int iRoll = iStep; iRoll < 360 && m_iRollBudget > 0 && (!bStrictHead || m_iArrowTestBudget > iArrowBudgetFloor); iRoll += iStep)
			{
				m_iRollBudget--;
				Solution_t tRollSolution;
				CalculateAngle(m_tInfo.m_vLocalEye, vPoint, iSimTime, tRollSolution, iSolveFlags, iTolerance, static_cast<float>(iRoll));
				Vec3 vRollAngles;
				if (tRollSolution.m_iCalculated == CalculatedEnum::Good)
					Aim(G::CurrentUserCmd->viewangles, { tRollSolution.m_flPitch, tRollSolution.m_flYaw, static_cast<float>(iRoll) }, vRollAngles);
				else
					Aim(G::CurrentUserCmd->viewangles, { flPitch, flYaw, static_cast<float>(iRoll) }, vRollAngles);
				if (TestAngle(vPoint, vRollAngles, iSimTime, iType, iFlags, false, bStrictHead))
				{
					vAngles = vRollAngles;
					bReturn = m_iResult = true;
					m_flTimeTo = (tRollSolution.m_iCalculated == CalculatedEnum::Good ? tRollSolution.m_flTime : flTime) + m_tInfo.m_flLatency;
					break;
				}
			}
		}

		if (!m_iResult)
		{
			switch (AimTypeValue())
			{
			case AimTypeEnum::Smooth:
				if (AIM_ASSIST_STRENGTH == 100.f)
					break;
				[[fallthrough]];
			case AimTypeEnum::Assistive:
			{
				const Vec3 vPlainAngles = { flPitch, flYaw, 0.f };
				if (TestAngle(vPoint, vPlainAngles, iSimTime, iType, PointFlagsEnum::Regular, true, bStrictHead))
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
			const int iCount = std::clamp(iOriginalSimTime + TIME_TO_TICKS(m_tInfo.m_flLatency) + 1, 0, int(m_tMoveStorage.m_vPath.size()));
			m_vPlayerPath.insert(m_vPlayerPath.end(), m_tMoveStorage.m_vPath.begin(), m_tMoveStorage.m_vPath.begin() + iCount);
		}
		else
		{
			const size_t iCount = std::min(m_tProjInfo.m_vPath.size() + TIME_TO_TICKS(m_tInfo.m_flLatency), m_tMoveStorage.m_vPath.size());
			m_vPlayerPath.insert(m_vPlayerPath.end(), m_tMoveStorage.m_vPath.begin(), m_tMoveStorage.m_vPath.begin() + iCount);
			if (!m_vPlayerPath.empty())
				m_vPredicted = m_vPlayerPath.back();
		}
	}

	return bReturn && m_iResult == 1;
}

bool CAimbotProjectileSim::HandleDirect(DirectHistory_t& mDirectHistory)
{
	bool bReturn = false;
	if (mDirectHistory.empty())
		return bReturn;

	auto it = mDirectHistory.begin();
	if (m_tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW)
	{
		auto itRegular = mDirectHistory.find(PointFlagsEnum::Regular);
		if (itRegular != mDirectHistory.end())
			it = itRegular;
	}
	const int iType = it->first;
	auto& vDirectHistory = it->second;

	if (F::ProjSim.m_bPhysics)
	{
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

	m_iRollBudget = 360 / std::max(CFG::Aimbot_Projectile_NeckbreakerStep, 15);

	const bool bArrowHeadStage = m_tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW
		&& m_tInfo.m_pTarget->m_iTargetType == TargetEnum::Player
		&& m_tInfo.m_pTarget->m_nAimedHitbox == HITBOX_HEAD;
	if (bArrowHeadStage)
	{
		const bool bOldUpdate = m_bUpdate;
		m_bUpdate = false;
		bool bFound = false;
		float flBestMargin = -1.f;
		float flBestScore = std::numeric_limits<float>::max();
		Direct_t tBest;
		for (const auto& tHistory : vDirectHistory)
		{
			if (tHistory.m_bStrictHead && m_iArrowTestBudget <= (m_bArrowFallbackStage ? 0 : 8))
				break;

			m_iResult = false;
			HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tHistory.m_flPitch, tHistory.m_flYaw, tHistory.m_flTime, tHistory.m_vPoint, PointTypeEnum::Direct, iType, tHistory.m_bStrictHead);
			if (m_iResult == 0)
				continue;

			const float flMargin = std::max(m_flLastHitMargin, 0.f);
			const Vec3 vCandOffset = tHistory.m_vPoint - tHistory.m_vOrigin;
			const float flScoreDist = m_bArrowPreferredValid ? vCandOffset.DistToSqr(m_vArrowPreferred) : 0.f;
			const bool bPosBetter = m_bArrowPreferredValid && flScoreDist < flBestScore - 1.f;
			const bool bPosTie = !m_bArrowPreferredValid || fabsf(flScoreDist - flBestScore) <= 1.f;
			const bool bMarginTie = flMargin > flBestMargin - 0.05f;
			if (!bFound
				|| bPosBetter
				|| bPosTie && flMargin > flBestMargin + 0.05f
				|| bPosTie && bMarginTie && tHistory.m_flTime < tBest.m_flTime
				|| bPosTie && bMarginTie && fabsf(tHistory.m_flTime - tBest.m_flTime) <= 0.001f && tHistory.m_iPriority < tBest.m_iPriority)
			{
				tBest = tHistory;
				flBestScore = flScoreDist;
				flBestMargin = flMargin;
				bFound = true;
			}
		}
		m_bUpdate = bOldUpdate;

		if (bFound)
		{
			m_iRollBudget = 360 / std::max(CFG::Aimbot_Projectile_NeckbreakerStep, 15);
			m_bArrowCommitStage = true;
			m_iResult = false;
			HandlePoint(tBest.m_vOrigin, tBest.m_iSimtime, tBest.m_flPitch, tBest.m_flYaw, tBest.m_flTime, tBest.m_vPoint, PointTypeEnum::Direct, iType, tBest.m_bStrictHead);
			m_bArrowCommitStage = false;
			bReturn = m_iResult != 0;
		}
	}
	else
	{
		for (auto& tHistory : vDirectHistory)
		{
			if (tHistory.m_bStrictHead && m_iArrowTestBudget <= (m_bArrowFallbackStage ? 0 : 8)
				&& m_tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW)
				break;
			if (HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tHistory.m_flPitch, tHistory.m_flYaw, tHistory.m_flTime, tHistory.m_vPoint, PointTypeEnum::Direct, iType, tHistory.m_bStrictHead))
			{
				bReturn = true;
				break;
			}
		}
	}

	mDirectHistory.erase(it);
	return bReturn;
}

bool CAimbotProjectileSim::HandleSplash(SplashHistory_t& mSplashHistory)
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
	m_iRollBudget = 360 / std::max(CFG::Aimbot_Projectile_NeckbreakerStep, 15);
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

int CAimbotProjectileSim::CanHit(Target_t& tTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, bool bUpdate)
{
	m_tMoveStorage = {};
	m_bArrowBonesValid = false;
	m_bArrowPreferredValid = false;
	m_iArrowTestBudget = 20;
	m_bArrowFallbackStage = false;

	const bool bIsPlayer = tTarget.m_iTargetType == TargetEnum::Player && IsPlayer(tTarget.m_pEntity);
	bool bSim = false;
	if (bIsPlayer)
	{
		if (!F::MoveSim->Initialize(tTarget.m_pEntity, m_tMoveStorage))
		{
			F::MoveSim->Restore(m_tMoveStorage);
			return false;
		}
		bSim = true;
	}
	else
		m_tMoveStorage.m_bFailed = true;

	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, m_tProjInfo, ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum)
		|| !F::ProjSim.Initialize(m_tProjInfo, false))
	{
		if (bSim)
			F::MoveSim->Restore(m_tMoveStorage);
		return false;
	}

	m_tInfo = { pLocal, pWeapon, &tTarget };
	m_tInfo.m_vLocalEye = SDKUtils::GetPredictedShootPos(pLocal);
	m_tInfo.m_flLatency = std::max(SDKUtils::GetLatency() - F::LagRecords->GetFakeLatency(), 0.0f)
		+ TICKS_TO_TIME(AnticipatedChokeTicks());
	tTarget.m_vPos = tTarget.m_pEntity->m_vecOrigin();

	if (bIsPlayer)
		m_tInfo.m_vTargetEye = tTarget.m_pEntity->As<C_TFPlayer>()->m_vecViewOffset();
	else
		m_tInfo.m_vTargetEye = { 0.f, 0.f, tTarget.m_pEntity->m_vecMaxs().z * 0.9f };

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

	m_tInfo.m_flGravity = m_tProjInfo.m_flGravity * 800.f;
	m_tInfo.m_iSplashRestrict = !m_tInfo.m_flGravity ? 100 : 5;
	m_tInfo.m_flRadius = GetSplashRadius(pWeapon, pLocal, CFG::Aimbot_Amalgam_Projectile_SplashRadius / 100);
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
	m_tInfo.m_bIgnoreTiming = true && m_tInfo.m_flRadius;

	Directs_t mDirects = GetDirects();
	Splashes_t vSplashes = GetSplashes();

	DirectHistory_t mDirectHistory = {};
	SplashHistory_t mSplashHistory = {};

	const int iMaxTime = TIME_TO_TICKS(std::min(m_tProjInfo.m_flLifetime, CFG::Aimbot_Projectile_Max_Simulation_Time));
	const bool bArrowHead = pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW
		&& tTarget.m_iTargetType == TargetEnum::Player && tTarget.m_nAimedHitbox == HITBOX_HEAD;
	std::vector<Vec3> vArrowOrigins;
	int iArrowOriginCount = 0;
	if (bArrowHead)
	{
		vArrowOrigins.resize(iMaxTime + 1);
		vArrowOrigins[0] = tTarget.m_vPos;
		iArrowOriginCount = 1;
	}
	for (int i = 1 - TIME_TO_TICKS(m_tInfo.m_flLatency); i <= iMaxTime; i++)
	{
		if (!m_tMoveStorage.m_bFailed)
		{
			F::MoveSim->RunTick(m_tMoveStorage);
			tTarget.m_vPos = m_tMoveStorage.m_vPredictedOrigin;
		}
		if (i < 0)
			continue;
		if (bArrowHead)
		{
			vArrowOrigins[i] = tTarget.m_vPos;
			iArrowOriginCount = i + 1;
		}

		for (auto it = mDirects.begin(); it != mDirects.end();)
		{
			if (m_tInfo.m_iArmTime && m_tInfo.m_iArmTime > i && !m_tMoveStorage.m_MoveData.m_vecVelocity.IsZero())
				break;

			auto& [iIndex, tOffset] = *it;
			if (bArrowHead && tOffset.m_bStrictHead && iIndex >= 600)
			{
				++it;
				continue;
			}
			Vec3& vOffset = tOffset.m_vOffset;
			const int iType = tOffset.m_iFlags & -tOffset.m_iFlags;

			Vec3 vPoint = tTarget.m_vPos + vOffset;

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
				mDirectHistory[iType].emplace_back(History_t(tTarget.m_vPos, i), tSolution.m_flPitch, tSolution.m_flYaw, tSolution.m_flTime, vPoint, iIndex, tOffset.m_bStrictHead);
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

		bool bHasActiveDirects = false;
		for (const auto& [iIndex, tOffset] : mDirects)
		{
			if (!bArrowHead || !tOffset.m_bStrictHead || iIndex < 600)
			{
				bHasActiveDirects = true;
				break;
			}
		}
		if (!bHasActiveDirects && vSplashes.empty())
			break;
	}

	m_iResult = false, m_bUpdate = bUpdate;
	if (!m_tInfo.m_flRadius || CFG::Aimbot_Amalgam_Projectile_Splash < SplashPredictionEnum::Prefer)
		goto direct;
	else
		goto splash;
	while (!mDirectHistory.empty() || !mSplashHistory.empty())
	{
		direct: if (HandleDirect(mDirectHistory)) break;
		splash: if (HandleSplash(mSplashHistory)) break;
	}

	if (!m_iResult && bArrowHead && m_iArrowTestBudget > 0)
	{
		m_bArrowFallbackStage = true;
		for (int i = 0; i < iArrowOriginCount; i++)
		{
			for (auto it = mDirects.begin(); it != mDirects.end();)
			{
				auto& [iIndex, tOffset] = *it;
				if (!tOffset.m_bStrictHead || iIndex < 600)
				{
					++it;
					continue;
				}

				const int iType = tOffset.m_iFlags & -tOffset.m_iFlags;
				int iFlags = CalculateFlagsEnum::Accuracy;
				if (iType == PointFlagsEnum::Lob)
					iFlags |= CalculateFlagsEnum::LobAngle;

				Solution_t tSolution;
				const Vec3 vPoint = vArrowOrigins[i] + tOffset.m_vOffset;
				if (iType == PointFlagsEnum::Lob && !ShouldLob(m_tMoveStorage, m_tInfo))
					tSolution.m_iCalculated = CalculatedEnum::Bad;
				else
					CalculateAngle(m_tInfo.m_vLocalEye, vPoint, i, tSolution, iFlags, -1);
				if (tSolution.m_iCalculated == CalculatedEnum::Good)
				{
					mDirectHistory[iType].emplace_back(History_t(vArrowOrigins[i], i), tSolution.m_flPitch, tSolution.m_flYaw, tSolution.m_flTime, vPoint, iIndex, true);
					tOffset.m_iFlags &= ~iType;
				}
				else if (tSolution.m_iCalculated == CalculatedEnum::Bad)
					tOffset.m_iFlags &= ~iType;

				if (!tOffset.m_iFlags)
				{
					it = mDirects.erase(it);
					continue;
				}
				++it;
			}
		}
		while (!mDirectHistory.empty() && m_iArrowTestBudget > 0)
		{
			if (HandleDirect(mDirectHistory))
				break;
		}
	}

	F::MoveSim->Restore(m_tMoveStorage);

	if (!bUpdate)
		return m_iResult;

	tTarget.m_vPos = m_vTarget;
	tTarget.m_vAngleTo = m_vAngleTo;


	return m_iResult;
}

bool CAimbotProjectileSim::Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod)
{
	bool bReturn = false;
	switch (iMethod)
	{
	case AimTypeEnum::Plain:
	case AimTypeEnum::Silent:
	case AimTypeEnum::Locking:
		vOut = vToAngle;
		break;
	case AimTypeEnum::Smooth:
		vOut = vCurAngle.LerpAngle(vToAngle, AIM_ASSIST_STRENGTH / 100.f);
		bReturn = true;
		break;
	case AimTypeEnum::Assistive:
	{
		Vec3 vMouseDelta = G::CurrentUserCmd->viewangles.DeltaAngle(G::LastUserCmd->viewangles);
		Vec3 vTargetDelta = vToAngle.DeltaAngle(G::LastUserCmd->viewangles);
		const float flMouseDelta = vMouseDelta.Length2DSqr(), flTargetDelta = vTargetDelta.Length2DSqr();
		vTargetDelta = vTargetDelta.Normalized() * sqrtf(std::min(flMouseDelta, flTargetDelta));
		vOut = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, AIM_ASSIST_STRENGTH / 100.f);
		bReturn = true;
		break;
	}
	default:
		vOut = vToAngle;
		break;
	}

	if (iMethod != AimTypeEnum::Silent || CFG::Misc_AntiCheat_Enabled)
		Math::ClampAngles(vOut);
	return bReturn;
}

void CAimbotProjectileSim::Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod)
{
	const bool bUnsure = F::ProjectileTicks->IsTimingUnsure();
	switch (iMethod)
	{
	case AimTypeEnum::Plain:
		if (G::Attacking != 1 && !bUnsure)
			break;
		[[fallthrough]];
	case AimTypeEnum::Smooth:
	case AimTypeEnum::Assistive:
		pCmd->viewangles = vAngles;
		if (Memory::IsInModule("engine.dll", I::EngineClient, true))
			I::EngineClient->SetViewAngles(vAngles);
		break;
	case AimTypeEnum::Silent:
		if (auto pWeapon = H::Entities->GetWeapon();
			G::Attacking == 1 || bUnsure || pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER)
		{
			SDKUtils::FixMovement(pCmd, vAngles);
			pCmd->viewangles = vAngles;
			G::bPSilentAngles = true;
		}
		break;
	case AimTypeEnum::Locking:
		SDKUtils::FixMovement(pCmd, vAngles);
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

std::vector<Target_t> CAimbotProjectileSim::GetTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets = {};

	const Vec3 vLocalPos = GetShootPos(pLocal);
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
	const bool bTargetPlayers = CFG::Aimbot_Target_Players;
	const bool bTargetBuildings = CFG::Aimbot_Target_Buildings;
	const int nWeaponID = pWeapon->GetWeaponID();
	const float flMaxFOV = CFG::Aimbot_Projectile_FOV;

	{
		int eGroup = -1;
		if (bTargetPlayers)
			eGroup = !F::AimbotGlobal->FriendlyFire() ? int(EEntGroup::PLAYERS_ENEMIES) : int(EEntGroup::PLAYERS_ALL);
		const bool bHeal = nWeaponID == TF_WEAPON_CROSSBOW;
		if (nWeaponID == TF_WEAPON_CROSSBOW && CFG::Aimbot_Projectile_AutoArrow)
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

				if (!bTeam && F::AimbotGlobal->ShouldIgnore(pEntity, pLocal, pWeapon))
					continue;

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
					if (pPlayer->m_iHealth() >= pPlayer->GetMaxHealth())
						continue;
					if (CFG::Aimbot_Projectile_AutoArrow && CFG::Aimbot_Projectile_AutoArrow_FriendsOnly && !pPlayer->IsPlayerOnSteamFriendsList())
						continue;
				}

				float flFOVTo; Vec3 vPos, vAngleTo;
				if (!F::AimbotGlobal->PlayerBoneInFOV(pPlayer, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
					continue;

				int nTargetPriority = F::AimbotGlobal->GetPriority(pEntity->entindex());
				if (bTeam2 && bHeal && pPlayer->IsPlayerOnSteamFriendsList())
					nTargetPriority += 1000;

				vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, vLocalPos.DistToSqr(vPos), nTargetPriority);
			}
		}

		if (nWeaponID == TF_WEAPON_LUNCHBOX)
			return vTargets;
	}

	{
		int eGroup = -1;
		if (bTargetBuildings)
			eGroup = int(EEntGroup::BUILDINGS_ENEMIES);
		if (CFG::Aimbot_Projectile_AutoRepair && nWeaponID == TF_WEAPON_SHOTGUN_BUILDING_RESCUE)
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


	return vTargets;
}

std::vector<Target_t> CAimbotProjectileSim::SortTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	auto vTargets = GetTargets(pLocal, pWeapon);

	F::AimbotGlobal->SortTargets(vTargets, CFG::Aimbot_Projectile_Sort);
	vTargets.resize(std::min(vTargets.size(), size_t(CFG::Aimbot_Projectile_Max_Processing_Targets)));
	F::AimbotGlobal->SortPriority(vTargets);

	return vTargets;
}

bool CAimbotProjectileSim::RunMain(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const int nWeaponID = pWeapon->GetWeaponID();
	const bool bRocket = nWeaponID == TF_WEAPON_ROCKETLAUNCHER || nWeaponID == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT;
	const int iWeaponIndex = pWeapon->entindex();
	if (!bRocket || m_iRocketPathWeapon != iWeaponIndex)
	{
		m_iRocketPathWeapon = bRocket ? iWeaponIndex : 0;
		m_flRocketPathAttack = -1.f;
	}

	if (CFG::Aimbot_Projectile_Sort == 0)
		G::flAimbotFOV = CFG::Aimbot_Projectile_FOV;

	if (F::AimbotGlobal->ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;

	const bool bKeyAim = CFG::Aimbot_Always_On || H::Input->IsDown(CFG::Aimbot_Key);
	if (!AimTypeValue() || !bKeyAim)
	{
		G::bFiring = false;
		G::Attacking = SDKUtils::IsAttacking(pLocal, pWeapon, pCmd, true);
		return false;
	}
	if (nWeaponID == TF_WEAPON_COMPOUND_BOW)
		pCmd->buttons &= ~IN_ATTACK2;
	if (!F::AimbotGlobal->ShouldAim() && nWeaponID != TF_WEAPON_FLAMETHROWER)
	{
		G::bFiring = false;
		G::Attacking = SDKUtils::IsAttacking(pLocal, pWeapon, pCmd, true);
		return false;
	}

	if (ProjectileModifierFlags() & ModifiersEnum::ChargeWeapon
		&& (nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || nWeaponID == TF_WEAPON_COMPOUND_BOW || nWeaponID == TF_WEAPON_CANNON && G::LastUserCmd->buttons & IN_ATTACK))
	{
		pCmd->buttons |= IN_ATTACK;
		if (!G::bCanPrimaryAttack && !G::bReloading && AimTypeValue() == AimTypeEnum::Silent)
		{
			G::bFiring = false;
			G::Attacking = SDKUtils::IsAttacking(pLocal, pWeapon, pCmd, true);
			return false;
		}
	}

	auto vTargets = SortTargets(pLocal, pWeapon);
	if (vTargets.empty())
	{
		G::bFiring = false;
		G::Attacking = SDKUtils::IsAttacking(pLocal, pWeapon, pCmd, true);
		return false;
	}

	if (!m_tAimTarget.m_iEntIndex && vTargets.front().m_pEntity)
		m_tAimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

	for (auto& tTarget : vTargets)
	{
		m_flTimeTo = std::numeric_limits<float>::max();
		m_vPlayerPath.clear(); m_vProjectilePath.clear();

		const int iResult = CanHit(tTarget, pLocal, pWeapon);
		if (iResult != 1 && nWeaponID == TF_WEAPON_CANNON && ProjectileModifierFlags() & ModifiersEnum::ChargeWeapon
			&& !(G::OriginalCmd.buttons & (IN_ATTACK | IN_USE)))
		{
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

		G::nTargetIndexEarly = tTarget.m_pEntity->entindex();
		G::nTargetIndex = tTarget.m_pEntity->entindex();

		if (IsPlayer(tTarget.m_pEntity))
			F::MoveSimDebug->NotifyProjectileTarget(tTarget.m_pEntity->As<C_TFPlayer>(), m_tMoveStorage.m_vPath, true);

		if (iResult == 2)
		{
			m_tAimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

		if (CFG::Aimbot_AutoShoot)
		{
			switch (nWeaponID)
			{
			case TF_WEAPON_COMPOUND_BOW:
				if (pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() > 0.f)
					pCmd->buttons &= ~IN_ATTACK;
				else
					pCmd->buttons |= IN_ATTACK;
				break;
			case TF_WEAPON_PIPEBOMBLAUNCHER:
				pCmd->buttons |= IN_ATTACK;
				if (pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() > 0.f)
					pCmd->buttons &= ~IN_ATTACK;
				break;
			case TF_WEAPON_CANNON:
				pCmd->buttons |= IN_ATTACK;
				if (pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() > 0.f)
				{
					if (m_iLastTickCancel)
						pCmd->weaponselect = m_iLastTickCancel = 0;
					if (ProjectileModifierFlags() & ModifiersEnum::ChargeWeapon)
					{
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
				if (Shifting::bShifting && !Shifting::bShiftingWarp && !pWeapon->IsInReload()
					&& pWeapon->m_flNextPrimaryAttack() > TICKS_TO_TIME(pLocal->m_nTickBase()))
					break;
				pCmd->buttons |= IN_ATTACK, pCmd->buttons &= ~IN_ATTACK2;
				if (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
				{
					if (pWeapon->m_iClip1() > 0)
						pCmd->buttons &= ~IN_ATTACK;
				}
				break;
			case TF_WEAPON_PARTICLE_CANNON:
			case TF_WEAPON_RAYGUN:
			case TF_WEAPON_DRG_POMSON:
			case TF_WEAPON_CROSSBOW:
				pCmd->buttons |= IN_ATTACK, pCmd->buttons &= ~IN_ATTACK2;
				break;
			default:
				pCmd->buttons |= IN_ATTACK;
			}
		}

		G::Attacking = SDKUtils::IsAttacking(pLocal, pWeapon, pCmd, true);
		G::bFiring = (pCmd->buttons & IN_ATTACK) != 0 && G::bCanPrimaryAttack;

		if (iResult == 1)
		{
			if (G::Attacking == 1)
				m_tAimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
			else
				m_tAimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			m_tAimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
			m_iLastSolutionTick = I::GlobalVars->tickcount;
			m_bFreshSolution = true;
		}

		const float flNextAttack = pWeapon->m_flNextPrimaryAttack();
		const bool bFireReady = !bRocket || flNextAttack <= TICKS_TO_TIME(pLocal->m_nTickBase());
		const bool bNewRocketPath = !bRocket || m_flRocketPathAttack != flNextAttack;
		if (G::Attacking == 1 && bFireReady && bNewRocketPath && !m_vProjectilePath.empty())
		{
			if (bRocket)
				m_flRocketPathAttack = flNextAttack;
			G::PathStorage.clear();
			G::BoxStorage.clear();

			const bool bTimed = CFG::Visuals_Draw_Paths_Timed;
			const float flFlight = TICKS_TO_TIME(m_vProjectilePath.size());
			const float flBoxDuration = flFlight + (bTimed ? m_tInfo.m_flLatency : 0.f);

			if (CFG::Visuals_Draw_Movement_Path_Style > 0 && !m_vPlayerPath.empty())
			{
				const float flPathTime = bTimed ? -static_cast<float>(m_vPlayerPath.size()) : I::GlobalVars->curtime + TICKS_TO_TIME(m_vPlayerPath.size());
				G::PathStorage.push_back({ m_vPlayerPath, flPathTime, CFG::Color_Simulation_Movement, CFG::Visuals_Draw_Movement_Path_Style, false });
				G::PathStorage.push_back({ m_vPlayerPath, flPathTime, CFG::Color_Simulation_Movement, CFG::Visuals_Draw_Movement_Path_Style, true });
			}

			if (CFG::Visuals_Draw_Predicted_Path_Style > 0)
			{
				const float flPathTime = bTimed
					? -static_cast<float>(m_vProjectilePath.size()) - TIME_TO_TICKS(m_tInfo.m_flLatency)
					: I::GlobalVars->curtime + flFlight;
				G::PathStorage.push_back({ m_vProjectilePath, flPathTime, CFG::Color_Simulation_Projectile, CFG::Visuals_Draw_Predicted_Path_Style, false });
				G::PathStorage.push_back({ m_vProjectilePath, flPathTime, CFG::Color_Simulation_Projectile, CFG::Visuals_Draw_Predicted_Path_Style, true });
			}

			if (CFG::Visuals_Draw_BBox)
			{
				const Color_t tEdge = CFG::Color_Simulation_BBox_Edge;
				const Color_t tFace = CFG::Color_Simulation_BBox_Face;
				G::BoxStorage.push_back({ m_vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + flBoxDuration, tEdge, tFace, false });
				G::BoxStorage.push_back({ m_vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + flBoxDuration, tEdge, tFace, true });
				G::BoxStorage.push_back({ m_vTarget, Vec3(-1.f, -1.f, -1.f), Vec3(1.f, 1.f, 1.f), Vec3(), I::GlobalVars->curtime + flBoxDuration, tEdge, { 0, 0, 0, 0 }, true });
			}
		}

		Aim(pCmd, tTarget.m_vAngleTo);
		if (G::bPSilentAngles)
		{
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

	if (((G::OriginalCmd.buttons & IN_ATTACK) || m_bLastTickAimbotCharge) && m_iLastSolutionTick != -1
		&& I::GlobalVars->tickcount - m_iLastSolutionTick <= 3 && fabsf(m_vAngleTo.z) < 0.001f)
	{
		SDKUtils::FixMovement(pCmd, m_vAngleTo);
		pCmd->viewangles = m_vAngleTo;
		G::bSilentAngles = true;
	}

	if (Shifting::bShifting && !Shifting::bShiftingWarp
		&& (nWeaponID == TF_WEAPON_ROCKETLAUNCHER || nWeaponID == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)
		&& pWeapon->m_flNextPrimaryAttack() <= TICKS_TO_TIME(pLocal->m_nTickBase()))
		pCmd->buttons |= IN_ATTACK;

	G::bFiring = false;
	G::Attacking = SDKUtils::IsAttacking(pLocal, pWeapon, pCmd, true);
	return false;
}

	bool CAimbotProjectileSim::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
	{
		if (!CFG::Aimbot_Projectile_Active)
		{
			m_bLastTickHeld = false;
			m_bLastTickAimbotCharge = false;
			m_bFreshSolution = false;
			return false;
		}

		m_bFreshSolution = false;
		const bool bSuccess = RunMain(pLocal, pWeapon, pCmd);

		float flAmount = 0.f;
		if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
		{
			const float flCharge = pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() : 0.f;
			flAmount = Math::RemapVal(flCharge, 0.f, SDKUtils::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon), 0.f, 1.f);
		}
		else if (pWeapon->GetWeaponID() == TF_WEAPON_CANNON)
		{
			const float flMortar = SDKUtils::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
			const float flCharge = pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<C_TFGrenadeLauncher>()->m_flDetonateTime() : -flMortar;
			flAmount = flMortar ? Math::RemapVal(flCharge, -flMortar, 0.f, 0.f, 1.f) : 0.f;
		}
		else if (pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW)
		{
			const float flCharge = pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() : 0.f;
			flAmount = std::min(flCharge, 1.f);
		}

		const bool bKeyAim = CFG::Aimbot_Always_On || H::Input->IsDown(CFG::Aimbot_Key);
		const bool bUserAttacking = G::OriginalCmd.buttons & IN_ATTACK;

		if (G::bCanPrimaryAttack && ProjectileModifierFlags() & ModifiersEnum::CancelCharge)
		{
			const bool bAimbotCharging = !bUserAttacking && (pCmd->buttons & IN_ATTACK);
			const bool bReleased = G::LastUserCmd->buttons & IN_ATTACK && !(pCmd->buttons & IN_ATTACK);
			const bool bRecentSolution = m_iLastSolutionTick != -1 && I::GlobalVars->tickcount - m_iLastSolutionTick <= 3;
			const bool bDisengaged = m_bLastTickHeld && !bKeyAim;
			if (!bUserAttacking)
				m_bManualBailLatch = false;
			const bool bBow = pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW;
			const bool bManualBail = bUserAttacking && flAmount > 0.95f && !bRecentSolution && !bBow;
			if ((m_bLastTickAimbotCharge && ((bReleased && !bSuccess && (bDisengaged || !bRecentSolution || bBow)) || (bAimbotCharging && flAmount > 0.95f))) || bManualBail)
			{
				CancelShot(pLocal, pWeapon, pCmd, m_iLastTickCancel);
				if (bManualBail && m_iLastTickCancel)
					m_bManualBailLatch = true;
			}
		}

		m_bLastTickHeld = bKeyAim && AimTypeValue();
		m_bLastTickAimbotCharge = !bUserAttacking && (pCmd->buttons & IN_ATTACK);

	return true;
}

void CAimbotProjectileSim::RestoreCancel(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!m_iLastTickCancel)
		return;

	if (!pLocal || pLocal->deadflag() || !AimTypeValue())
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


float CAimbotProjectileSim::GetSplashRadius(C_TFWeaponBase* pWeapon, C_TFPlayer* pPlayer, float flScale)
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

	flRadius = SDKUtils::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		if (pPlayer->InCond(TF_COND_BLASTJUMPING) && SDKUtils::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
			flRadius *= 0.8f;
	}
	return flRadius * flScale;
}

float CAimbotProjectileSim::GetSplashRadius(C_BaseEntity* pProjectile, C_TFWeaponBase* pWeapon, C_TFPlayer* pPlayer, float flScale)
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
		flRadius = SDKUtils::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
		switch (pProjectile->GetClassId())
		{
		case ETFClassIds::CTFProjectile_Rocket:
		case ETFClassIds::CTFProjectile_SentryRocket:
			if (pPlayer->InCond(TF_COND_BLASTJUMPING) && SDKUtils::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
				flRadius *= 0.8f;
		}
	}
	return flRadius * flScale;
}
