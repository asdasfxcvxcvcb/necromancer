#include "AimbotProjectileAmalgam.h"

// Use SEOwnedDE's existing features
#include "../../Aimbot/Aimbot.h"
#include "../../MovementSimulation/MovementSimulation.h"
#include "../ProjectileSimulation/ProjectileSimulation.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../Ticks/Ticks.h"
#include "../../Players/Players.h"

std::vector<Target_t> CAmalgamAimbotProjectile::GetTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;
	const auto iSort = Vars::Aimbot::General::TargetSelection.Value;

	const Vec3 vLocalPos = F::AmalgamTicks->GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
	const float flMaxFOV = CFG::Aimbot_Projectile_FOV;

	// Players (enemies only for rockets)
	if (CFG::Aimbot_Target_Players)
	{
		for (auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
		{
			if (!pEntity || pEntity == pLocal)
				continue;

			auto pPlayer = pEntity->As<C_TFPlayer>();
			if (!pPlayer || pPlayer->deadflag())
				continue;

			// Apply ignore filters
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

			// Ignore untagged players when key is held
			if (CFG::Aimbot_Ignore_Untagged_Key != 0 && H::Input->IsDown(CFG::Aimbot_Ignore_Untagged_Key))
			{
				PlayerPriority playerPriority = {};
				if (!F::Players->GetInfo(pPlayer->entindex(), playerPriority) ||
					(!playerPriority.Cheater && !playerPriority.Targeted && !playerPriority.Nigger && !playerPriority.RetardLegit && !playerPriority.Streamer))
					continue;
			}

			Vec3 vPos = pPlayer->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			
			if (flFOVTo > flMaxFOV)
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, 0);
		}
	}

	// Buildings (sentries, dispensers, teleporters)
	if (CFG::Aimbot_Target_Buildings)
	{
		for (auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
		{
			if (!pEntity)
				continue;

			auto pBuilding = pEntity->As<C_BaseObject>();
			if (!pBuilding || pBuilding->m_bPlacing())
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			
			if (flFOVTo > flMaxFOV)
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.emplace_back(pEntity, IsSentrygun(pEntity) ? TargetEnum::Sentry : IsDispenser(pEntity) ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}

std::vector<Target_t> CAmalgamAimbotProjectile::SortTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	auto vTargets = GetTargets(pLocal, pWeapon);

	// Sort by FOV or distance based on config
	const int iSort = CFG::Aimbot_Projectile_Sort;
	std::sort(vTargets.begin(), vTargets.end(), [iSort](const Target_t& a, const Target_t& b) {
		if (iSort == 0) // FOV
			return a.m_flFOVTo < b.m_flFOVTo;
		else // Distance
			return a.m_flDistTo < b.m_flDistTo;
	});

	// Limit to max targets
	const size_t nMaxTargets = std::min(static_cast<size_t>(CFG::Aimbot_Projectile_Max_Processing_Targets), vTargets.size());
	vTargets.resize(nMaxTargets);
	
	return vTargets;
}

float CAmalgamAimbotProjectile::GetSplashRadius(CTFWeaponBase* pWeapon, CTFPlayer* pPlayer)
{
	float flRadius = 0.f;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		flRadius = 146.f;
		break;
	}
	if (!flRadius)
		return 0.f;

	flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
	if (pPlayer->InCond(TF_COND_BLASTJUMPING) && SDK::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
		flRadius *= 0.8f;

	return flRadius * Vars::Aimbot::Projectile::SplashRadius.Value / 100;
}

float CAmalgamAimbotProjectile::GetSplashRadius(C_BaseEntity* pProjectile, CTFWeaponBase* pWeapon, CTFPlayer* pPlayer, float flScale, CTFWeaponBase* pAirblast)
{
	float flRadius = 0.f;
	if (pAirblast)
	{
		pWeapon = pAirblast;
		pPlayer = pWeapon->m_hOwner()->As<CTFPlayer>();
	}
	switch (pProjectile->GetClassId())
	{
	case ETFClassIds::CTFProjectile_Rocket:
	case ETFClassIds::CTFProjectile_SentryRocket:
	case ETFClassIds::CTFProjectile_EnergyBall:
		flRadius = 146.f;
		break;
	}
	if (pPlayer && pWeapon)
	{
		flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
		if (pPlayer->InCond(TF_COND_BLASTJUMPING) && SDK::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
			flRadius *= 0.8f;
	}
	return flRadius * flScale;
}

static inline int GetSplashMode(CTFWeaponBase* pWeapon)
{
	if (Vars::Aimbot::Projectile::RocketSplashMode.Value)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
			return Vars::Aimbot::Projectile::RocketSplashMode.Value;
		}
	}
	return Vars::Aimbot::Projectile::RocketSplashModeEnum::Regular;
}

static inline int GetHitboxPriority(int nHitbox, Target_t& tTarget, Info_t& tInfo, C_BaseEntity* pProjectile = nullptr)
{
	if (!F::AimbotGlobal->IsHitboxValid(nHitbox, Vars::Aimbot::Projectile::Hitboxes.Value))
		return -1;

	int iHeadPriority = 2;
	int iBodyPriority = 0;
	int iFeetPriority = 1;

	if (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Auto)
	{
		bool bLower = Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::PrioritizeFeet
			&& tTarget.m_iTargetType == TargetEnum::Player && IsOnGround(tTarget.m_pEntity->As<CTFPlayer>());

		iHeadPriority = 2;
		iBodyPriority = bLower ? 1 : 0;
		iFeetPriority = bLower ? 0 : 1;
	}

	switch (nHitbox)
	{
	case BOUNDS_HEAD: return iHeadPriority;
	case BOUNDS_BODY: return iBodyPriority;
	case BOUNDS_FEET: return iFeetPriority;
	}

	return -1;
}

std::unordered_map<int, Vec3> CAmalgamAimbotProjectile::GetDirectPoints(Target_t& tTarget, C_BaseEntity* pProjectile)
{
	std::unordered_map<int, Vec3> mPoints = {};

	const Vec3 vMins = tTarget.m_pEntity->m_vecMins(), vMaxs = tTarget.m_pEntity->m_vecMaxs();
	for (int i = 0; i < 3; i++) // Head (0), Body (1), Feet (2)
	{
		int iPriority = GetHitboxPriority(i, tTarget, m_tInfo, pProjectile);
		if (iPriority == -1)
			continue;

		switch (i)
		{
		case BOUNDS_HEAD: mPoints[iPriority] = Vec3(0, 0, vMaxs.z - Vars::Aimbot::Projectile::VerticalShift.Value); break;
		case BOUNDS_BODY: mPoints[iPriority] = Vec3(0, 0, (vMaxs.z - vMins.z) / 2); break;
		case BOUNDS_FEET: mPoints[iPriority] = Vec3(0, 0, vMins.z + Vars::Aimbot::Projectile::VerticalShift.Value); break;
		}
	}

	return mPoints;
}

std::vector<std::pair<Vec3, int>> CAmalgamAimbotProjectile::ComputeSphere(float flRadius, int iSamples)
{
	std::vector<std::pair<Vec3, int>> vPoints;
	vPoints.reserve(iSamples);

	float flRotateX = Vars::Aimbot::Projectile::SplashRotateX.Value < 0.f ? SDK::StdRandomFloat(0.f, 360.f) : Vars::Aimbot::Projectile::SplashRotateX.Value;
	float flRotateY = Vars::Aimbot::Projectile::SplashRotateY.Value < 0.f ? SDK::StdRandomFloat(0.f, 360.f) : Vars::Aimbot::Projectile::SplashRotateY.Value;

	int iPointType = Vars::Aimbot::Projectile::SplashGrates.Value ? PointTypeEnum::Regular | PointTypeEnum::Obscured : PointTypeEnum::Regular;
	if (Vars::Aimbot::Projectile::RocketSplashMode.Value == Vars::Aimbot::Projectile::RocketSplashModeEnum::SpecialHeavy)
		iPointType |= PointTypeEnum::ObscuredExtra | PointTypeEnum::ObscuredMulti;

	float a = PI * (3.f - sqrtf(5.f));
	for (int n = 0; n < iSamples; n++)
	{
		float t = a * n;
		float y = 1 - (n / (iSamples - 1.f)) * 2;
		float r = sqrtf(1 - powf(y, 2));
		float x = cosf(t) * r;
		float z = sinf(t) * r;

		Vec3 vPoint = Vec3(x, y, z) * flRadius;
		vPoint = Math::RotatePoint(vPoint, Vec3(), Vec3(flRotateX, flRotateY, 0.f));

		vPoints.emplace_back(vPoint, iPointType);
	}
	vPoints.emplace_back(Vec3(0.f, 0.f, -1.f) * flRadius, iPointType);

	return vPoints;
}


template <class T>
static inline void TracePoint(Vec3& vPoint, int& iType, Vec3& vTargetEye, Info_t& tInfo, T& vPoints, std::function<bool(CGameTrace& trace, bool& bErase, bool& bNormal)> checkPoint, int i = 0)
{
	int iOriginalType = iType;
	bool bErase = false, bNormal = false;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnlyAmalgam filter = {};

	if (iType & PointTypeEnum::Regular)
	{
		SDK::TraceHull(vTargetEye, vPoint, tInfo.m_vHull * -1, tInfo.m_vHull, MASK_SOLID, &filter, &trace);

		if (checkPoint(trace, bErase, bNormal))
		{
			if (i % Vars::Aimbot::Projectile::SplashNormalSkip.Value)
				vPoints.pop_back();
		}

		if (bErase)
			iType = 0;
		else if (bNormal)
			iType &= ~PointTypeEnum::Regular;
		else
			iType &= ~PointTypeEnum::Obscured;
	}
	if (iType & PointTypeEnum::ObscuredExtra)
	{
		bErase = false, bNormal = false;
		size_t iOriginalSize = vPoints.size();

		{
			if (bNormal = (tInfo.m_vLocalEye - vTargetEye).Dot(vTargetEye - vPoint) > 0.f)
				goto breakOutExtra;

			if (!(iOriginalType & PointTypeEnum::Regular))
			{
				SDK::Trace(vTargetEye, vPoint, MASK_SOLID, &filter, &trace);
				bNormal = !trace.m_pEnt || trace.fraction == 1.f;
				if (bNormal)
					goto breakOutExtra;
			}

			SDK::Trace(trace.endpos - (vTargetEye - vPoint).Normalized(), vPoint, MASK_SOLID | CONTENTS_NOSTARTSOLID, &filter, &trace);
			bNormal = trace.fraction == 1.f || trace.allsolid || (trace.startpos - trace.endpos).IsZero() || trace.surface.flags & SURF_SKY;
			if (bNormal)
				goto breakOutExtra;

			if (checkPoint(trace, bErase, bNormal))
			{
				SDK::Trace(trace.endpos + trace.plane.normal, vTargetEye, MASK_SHOT, &filter, &trace);
				if (trace.fraction < 1.f)
					vPoints.pop_back();
			}
		}

		breakOutExtra:
		if (vPoints.size() != iOriginalSize)
			iType = 0;
		else if (bErase || bNormal)
			iType &= ~PointTypeEnum::ObscuredExtra;
	}
	if (iType & PointTypeEnum::Obscured)
	{
		bErase = false, bNormal = false;
		size_t iOriginalSize = vPoints.size();

		if (bNormal = (tInfo.m_vLocalEye - vTargetEye).Dot(vTargetEye - vPoint) > 0.f)
			goto breakOut;

		if (tInfo.m_iSplashMode == Vars::Aimbot::Projectile::RocketSplashModeEnum::Regular)
		{
			SDK::Trace(vPoint, vTargetEye, MASK_SHOT, &filter, &trace);
			bNormal = trace.DidHit();
			if (bNormal)
				goto breakOut;

			SDK::TraceHull(vPoint, vTargetEye, tInfo.m_vHull * -1, tInfo.m_vHull, MASK_SOLID, &filter, &trace);
			checkPoint(trace, bErase, bNormal);
		}
		else
		{
			SDK::Trace(vPoint, vTargetEye, MASK_SOLID | CONTENTS_NOSTARTSOLID, &filter, &trace);
			bNormal = trace.fraction == 1.f || trace.allsolid || trace.surface.flags & SURF_SKY;
			if (!bNormal && trace.surface.flags & SURF_NODRAW)
			{
				if (bNormal = !(iType & PointTypeEnum::ObscuredMulti))
					goto breakOut;

				CGameTrace trace2 = {};
				SDK::Trace(trace.endpos - (vPoint - vTargetEye).Normalized(), vTargetEye, MASK_SOLID | CONTENTS_NOSTARTSOLID, &filter, &trace2);
				bNormal = trace2.fraction == 1.f || trace.allsolid || (trace2.startpos - trace2.endpos).IsZero() || trace2.surface.flags & (SURF_NODRAW | SURF_SKY);
				if (!bNormal)
					trace = trace2;
			}
			if (bNormal)
				goto breakOut;

			if (checkPoint(trace, bErase, bNormal))
			{
				SDK::Trace(trace.endpos + trace.plane.normal, vTargetEye, MASK_SHOT, &filter, &trace);
				if (trace.fraction < 1.f)
					vPoints.pop_back();
			}
		}

		breakOut:
		if (vPoints.size() != iOriginalSize)
			iType = 0;
		else if (bErase || bNormal)
			iType &= ~PointTypeEnum::Obscured;
		else
			iType &= ~PointTypeEnum::Regular;
	}
}

std::vector<Point_t> CAmalgamAimbotProjectile::GetSplashPoints(Target_t& tTarget, std::vector<std::pair<Vec3, int>>& vSpherePoints, int iSimTime)
{
	std::vector<std::pair<Point_t, float>> vPointDistances = {};

	Vec3 vTargetEye = tTarget.m_vPos + m_tInfo.m_vTargetEye;

	auto checkPoint = [&](CGameTrace& trace, bool& bErase, bool& bNormal)
		{	
			bErase = !trace.m_pEnt || trace.fraction == 1.f || trace.surface.flags & SURF_SKY || !trace.m_pEnt->GetAbsVelocity().IsZero();
			if (bErase)
				return false;

			Point_t tPoint = { trace.endpos, {} };
			if (!m_tInfo.m_flGravity)
			{
				Vec3 vForward = (m_tInfo.m_vLocalEye - trace.endpos).Normalized();
				bNormal = vForward.Dot(trace.plane.normal) <= 0;
			}
			if (!bNormal)
			{
				CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, iSimTime, tPoint.m_tSolution);
				if (m_tInfo.m_flGravity)
				{
					Vec3 vPos = m_tInfo.m_vLocalEye + Vec3(0, 0, (m_tInfo.m_flGravity * 800.f * pow(tPoint.m_tSolution.m_flTime, 2)) / 2);
					Vec3 vForward = (vPos - tPoint.m_vPoint).Normalized();
					bNormal = vForward.Dot(trace.plane.normal) <= 0;
				}
			}
			if (bNormal)
				return false;

			bErase = tPoint.m_tSolution.m_iCalculated == CalculatedEnum::Good;
			if (!bErase || int(tPoint.m_tSolution.m_flTime / TICK_INTERVAL) + 1 != iSimTime)
				return false;

			vPointDistances.emplace_back(tPoint, tPoint.m_vPoint.DistTo(tTarget.m_vPos));
			return true;
		};

	int i = 0;
	for (auto it = vSpherePoints.begin(); it != vSpherePoints.end();)
	{
		Vec3 vPoint = it->first + vTargetEye;
		int& iType = it->second;

		Solution_t solution; CalculateAngle(m_tInfo.m_vLocalEye, vPoint, iSimTime, solution, false);
		
		if (solution.m_iCalculated == CalculatedEnum::Bad)
			iType = 0;
		else if (abs(solution.m_flTime - TICKS_TO_TIME(iSimTime)) < m_tInfo.m_flRadiusTime)
			TracePoint(vPoint, iType, vTargetEye, m_tInfo, vPointDistances, checkPoint, i++);

		if (!(iType & ~PointTypeEnum::ObscuredMulti))
			it = vSpherePoints.erase(it);
		else
			++it;
	}

	std::sort(vPointDistances.begin(), vPointDistances.end(), [&](const auto& a, const auto& b) -> bool
		{
			return a.second < b.second;
		});

	std::vector<Point_t> vPoints = {};
	int iSplashCount = std::min(m_tInfo.m_iSplashCount, int(vPointDistances.size()));
	for (int i = 0; i < iSplashCount; i++)
		vPoints.push_back(vPointDistances[i].first);

	const Vec3 vOriginal = tTarget.m_pEntity->GetAbsOrigin();
	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	for (auto it = vPoints.begin(); it != vPoints.end();)
	{
		auto& vPoint = *it;
		bool bValid = vPoint.m_tSolution.m_iCalculated != CalculatedEnum::Pending;
		if (bValid)
		{
			auto pCollideable = tTarget.m_pEntity->GetCollideable();
			if (pCollideable)
			{
				Vec3 vPos; reinterpret_cast<CCollisionProperty*>(pCollideable)->CalcNearestPoint(vPoint.m_vPoint, &vPos);
				bValid = vPoint.m_vPoint.DistTo(vPos) < m_tInfo.m_flRadius;
			}
			else
				bValid = false;
		}

		if (bValid)
			++it;
		else
			it = vPoints.erase(it);
	}
	tTarget.m_pEntity->SetAbsOrigin(vOriginal);

	return vPoints;
}

void CAmalgamAimbotProjectile::SetupSplashPoints(Target_t& tTarget, std::vector<std::pair<Vec3, int>>& vSpherePoints, std::vector<std::pair<Vec3, Vec3>>& vSimplePoints)
{
	vSimplePoints.clear();
	Vec3 vTargetEye = tTarget.m_vPos + m_tInfo.m_vTargetEye;

	auto checkPoint = [&](CGameTrace& trace, bool& bErase, bool& bNormal)
		{
			bErase = !trace.m_pEnt || trace.fraction == 1.f || trace.surface.flags & SURF_SKY || !trace.m_pEnt->GetAbsVelocity().IsZero();
			if (bErase)
				return false;

			Point_t tPoint = { trace.endpos, {} };
			if (!m_tInfo.m_flGravity)
			{
				Vec3 vForward = (m_tInfo.m_vLocalEye - trace.endpos).Normalized();
				bNormal = vForward.Dot(trace.plane.normal) <= 0;
			}
			if (!bNormal)
			{
				CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, 0, tPoint.m_tSolution, false);
				if (m_tInfo.m_flGravity)
				{
					Vec3 vPos = m_tInfo.m_vLocalEye + Vec3(0, 0, (m_tInfo.m_flGravity * 800.f * pow(tPoint.m_tSolution.m_flTime, 2)) / 2);
					Vec3 vForward = (vPos - tPoint.m_vPoint).Normalized();
					bNormal = vForward.Dot(trace.plane.normal) <= 0;
				}
			}
			if (bNormal)
				return false;

			if (tPoint.m_tSolution.m_iCalculated != CalculatedEnum::Bad)
			{
				vSimplePoints.emplace_back(tPoint.m_vPoint, trace.plane.normal);
				return true;
			}
			return false;
		};

	int i = 0;
	for (auto& vSpherePoint : vSpherePoints)
	{
		Vec3 vPoint = vSpherePoint.first + vTargetEye;
		int& iType = vSpherePoint.second;

		Solution_t solution; CalculateAngle(m_tInfo.m_vLocalEye, vPoint, 0, solution, false);

		if (solution.m_iCalculated != CalculatedEnum::Bad)
			TracePoint(vPoint, iType, vTargetEye, m_tInfo, vSimplePoints, checkPoint, i++);
	}
}

std::vector<Point_t> CAmalgamAimbotProjectile::GetSplashPointsSimple(Target_t& tTarget, std::vector<std::pair<Vec3, Vec3>>& vSpherePoints, int iSimTime)
{
	std::vector<std::pair<Point_t, float>> vPointDistances = {};

	auto checkPoint = [&](Vec3& vPoint, bool& bErase)
		{
			Point_t tPoint = { vPoint, {} };
			CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, iSimTime, tPoint.m_tSolution);

			bErase = tPoint.m_tSolution.m_iCalculated == CalculatedEnum::Good;
			if (!bErase || int(tPoint.m_tSolution.m_flTime / TICK_INTERVAL) + 1 != iSimTime)
				return false;

			vPointDistances.emplace_back(tPoint, tPoint.m_vPoint.DistTo(tTarget.m_vPos));
			return true;
		};

	for (auto it = vSpherePoints.begin(); it != vSpherePoints.end();)
	{
		Vec3& vPoint = it->first;
		bool bErase = false;

		checkPoint(vPoint, bErase);

		if (bErase)
			it = vSpherePoints.erase(it);
		else
			++it;
	}

	std::sort(vPointDistances.begin(), vPointDistances.end(), [&](const auto& a, const auto& b) -> bool
		{
			return a.second < b.second;
		});

	std::vector<Point_t> vPoints = {};
	int iSplashCount = std::min(m_tInfo.m_iSplashCount, int(vPointDistances.size()));
	for (int i = 0; i < iSplashCount; i++)
		vPoints.push_back(vPointDistances[i].first);

	const Vec3 vOriginal = tTarget.m_pEntity->GetAbsOrigin();
	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	for (auto it = vPoints.begin(); it != vPoints.end();)
	{
		auto& vPoint = *it;
		bool bValid = vPoint.m_tSolution.m_iCalculated != CalculatedEnum::Pending;
		if (bValid)
		{
			auto pCollideable = tTarget.m_pEntity->GetCollideable();
			if (pCollideable)
			{
				Vec3 vPos = {}; reinterpret_cast<CCollisionProperty*>(pCollideable)->CalcNearestPoint(vPoint.m_vPoint, &vPos);
				bValid = vPoint.m_vPoint.DistTo(vPos) < m_tInfo.m_flRadius;
			}
			else
				bValid = false;
		}

		if (bValid)
			++it;
		else
			it = vPoints.erase(it);
	}
	tTarget.m_pEntity->SetAbsOrigin(vOriginal);

	return vPoints;
}


void CAmalgamAimbotProjectile::CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& out, bool bAccuracy)
{
	if (out.m_iCalculated != CalculatedEnum::Pending)
		return;

	const float flGrav = m_tInfo.m_flGravity * 800.f;

	float flPitch, flYaw;
	{
		float flVelocity = m_tInfo.m_flVelocity;

		Vec3 vDelta = vTargetPos - vLocalPos;
		float flDist = vDelta.Length2D();

		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vTargetPos);
		if (!flGrav)
			flPitch = -DEG2RAD(vAngleTo.x);
		else
		{
			float flRoot = pow(flVelocity, 4) - flGrav * (flGrav * pow(flDist, 2) + 2.f * vDelta.z * pow(flVelocity, 2));
			if (out.m_iCalculated = flRoot < 0.f ? CalculatedEnum::Bad : CalculatedEnum::Pending)
				return;
			flPitch = atan((pow(flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
		}
		out.m_flTime = flDist / (cos(flPitch) * flVelocity) - m_tInfo.m_flOffsetTime;
		out.m_flPitch = flPitch = -RAD2DEG(flPitch) - m_tInfo.m_vAngFix.x;
		out.m_flYaw = flYaw = vAngleTo.y - m_tInfo.m_vAngFix.y;
	}

	int iTimeTo = int(out.m_flTime / TICK_INTERVAL) + 1;
	if (!m_tInfo.m_vOffset.IsZero())
	{
		if (out.m_iCalculated = iTimeTo > iSimTime ? CalculatedEnum::Time : CalculatedEnum::Pending)
			return;
	}
	else
	{
		out.m_iCalculated = iTimeTo > iSimTime ? CalculatedEnum::Time : CalculatedEnum::Good;
		return;
	}

	int iFlags = (bAccuracy ? ProjSimEnum::Trace : ProjSimEnum::None) | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
	ProjectileInfo tProjInfo = {};
	if (out.m_iCalculated = !F::ProjSim.GetInfo(m_tInfo.m_pLocal, m_tInfo.m_pWeapon, { flPitch, flYaw, 0 }, tProjInfo, iFlags) ? CalculatedEnum::Bad : CalculatedEnum::Pending)
		return;

	{
		float flVelocity = m_tInfo.m_flVelocity;

		Vec3 vDelta = vTargetPos - tProjInfo.m_vPos;
		float flDist = vDelta.Length2D();

		Vec3 vAngleTo = Math::CalcAngle(tProjInfo.m_vPos, vTargetPos);
		if (!flGrav)
			out.m_flPitch = -DEG2RAD(vAngleTo.x);
		else
		{
			float flRoot = pow(flVelocity, 4) - flGrav * (flGrav * pow(flDist, 2) + 2.f * vDelta.z * pow(flVelocity, 2));
			if (out.m_iCalculated = flRoot < 0.f ? CalculatedEnum::Bad : CalculatedEnum::Pending)
				return;
			out.m_flPitch = atan((pow(flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
		}
		out.m_flTime = flDist / (cos(out.m_flPitch) * flVelocity);
	}

	{
		Vec3 vShootPos = (tProjInfo.m_vPos - vLocalPos).To2D();
		Vec3 vTarget = vTargetPos - vLocalPos;
		Vec3 vForward; Math::AngleVectors(tProjInfo.m_vAng, &vForward); vForward.Normalize2D();
		float flB = 2 * (vShootPos.x * vForward.x + vShootPos.y * vForward.y);
		float flC = vShootPos.Length2DSqr() - vTarget.Length2DSqr();
		auto vSolutions = Math::SolveQuadratic(1.f, flB, flC);
		if (!vSolutions.empty())
		{
			vShootPos += vForward * vSolutions.front();
			out.m_flYaw = flYaw - (RAD2DEG(atan2(vShootPos.y, vShootPos.x)) - flYaw);
			flYaw = RAD2DEG(atan2(vShootPos.y, vShootPos.x));
		}
	}

	{
		if (flGrav)
		{
			flPitch -= tProjInfo.m_vAng.x;
			out.m_flPitch = -RAD2DEG(out.m_flPitch) + flPitch - m_tInfo.m_vAngFix.x;
		}
		else
		{
			Vec3 vShootPos = Math::RotatePoint(tProjInfo.m_vPos - vLocalPos, {}, { 0, -flYaw, 0 }); vShootPos.y = 0;
			Vec3 vTarget = Math::RotatePoint(vTargetPos - vLocalPos, {}, { 0, -flYaw, 0 });
			Vec3 vForward; Math::AngleVectors(tProjInfo.m_vAng - Vec3(0, flYaw, 0), &vForward); vForward.y = 0; vForward.Normalize();
			float flB = 2 * (vShootPos.x * vForward.x + vShootPos.z * vForward.z);
			float flC = (powf(vShootPos.x, 2) + powf(vShootPos.z, 2)) - (powf(vTarget.x, 2) + powf(vTarget.z, 2));
			auto vSolutions = Math::SolveQuadratic(1.f, flB, flC);
			if (!vSolutions.empty())
			{
				vShootPos += vForward * vSolutions.front();
				out.m_flPitch = flPitch - (RAD2DEG(atan2(-vShootPos.z, vShootPos.x)) - flPitch);
			}
		}
	}

	iTimeTo = int(out.m_flTime / TICK_INTERVAL) + 1;
	out.m_iCalculated = iTimeTo > iSimTime ? CalculatedEnum::Time : CalculatedEnum::Good;
}

bool CAmalgamAimbotProjectile::TestAngle(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, Target_t& tTarget, Vec3& vPoint, Vec3& vAngles, int iSimTime, bool bSplash, bool* pHitSolid, std::vector<Vec3>* pProjectilePath)
{
	int iFlags = ProjSimEnum::Trace | ProjSimEnum::InitCheck | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
	ProjectileInfo tProjInfo = {};
	
	// Try with InitCheck first, fallback without if it fails
	bool bGotInfo = F::ProjSim.GetInfo(pLocal, pWeapon, vAngles, tProjInfo, iFlags);
	if (!bGotInfo)
	{
		// Try again without InitCheck (workaround for overly strict spawn position validation)
		bGotInfo = F::ProjSim.GetInfo(pLocal, pWeapon, vAngles, tProjInfo, ProjSimEnum::Trace | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum);
		if (!bGotInfo)
			return false;
	}
	
	if (!F::ProjSim.Initialize(tProjInfo))
		return false;

	CGameTrace trace = {};
	CTraceFilterCollideable filter = {};
	filter.pSkip = bSplash ? tTarget.m_pEntity : pLocal;
	filter.iPlayer = bSplash ? PLAYER_NONE : PLAYER_DEFAULT;
	filter.bMisc = !bSplash;
	int nMask = MASK_SOLID;
	if (!bSplash && F::AimbotGlobal->FriendlyFire())
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
			filter.iPlayer = PLAYER_ALL;
		}
	}
	F::ProjSim.SetupTrace(filter, nMask, pWeapon);

	if (!tProjInfo.m_flGravity)
	{
		SDK::TraceHull(tProjInfo.m_vPos, vPoint, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, nMask, &filter, &trace);
		if (trace.fraction < 0.999f && trace.m_pEnt != tTarget.m_pEntity)
			return false;
	}

	if (!tTarget.m_pEntity)
		return false;

	bool bDidHit = false;
	const RestoreInfo_t tOriginal = { tTarget.m_pEntity->GetAbsOrigin(), tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs() };
	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	tTarget.m_pEntity->m_vecMins() = { std::clamp(tTarget.m_pEntity->m_vecMins().x, -24.f, 0.f), std::clamp(tTarget.m_pEntity->m_vecMins().y, -24.f, 0.f), tTarget.m_pEntity->m_vecMins().z };
	tTarget.m_pEntity->m_vecMaxs() = { std::clamp(tTarget.m_pEntity->m_vecMaxs().x, 0.f, 24.f), std::clamp(tTarget.m_pEntity->m_vecMaxs().y, 0.f, 24.f), tTarget.m_pEntity->m_vecMaxs().z };
	
	for (int n = 1; n <= iSimTime; n++)
	{
		Vec3 vOld = F::ProjSim.GetOrigin();
		F::ProjSim.RunTick(tProjInfo);
		Vec3 vNew = F::ProjSim.GetOrigin();

		if (bDidHit)
		{
			trace.endpos = vNew;
			continue;
		}

		if (!bSplash)
		{
			SDK::TraceHull(vOld, vNew, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, nMask, &filter, &trace);
		}
		else
		{
			static Vec3 vStaticPos = {};
			if (n == 1)
				vStaticPos = vOld;
			if (n % Vars::Aimbot::Projectile::SplashTraceInterval.Value && n != iSimTime)
				continue;

			SDK::TraceHull(vStaticPos, vNew, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, nMask, &filter, &trace);
			vStaticPos = vNew;
		}
		
		if (trace.DidHit())
		{
			if (pHitSolid)
				*pHitSolid = true;

			bool bTime = bSplash
				? trace.endpos.DistTo(vPoint) < tProjInfo.m_flVelocity * TICK_INTERVAL + tProjInfo.m_vHull.z
				: iSimTime - n < 5;
			bool bTarget = trace.m_pEnt == tTarget.m_pEntity || bSplash;
			bool bValid = bTarget && bTime;
			
			if (bValid && bSplash)
			{
				bValid = SDK::VisPosWorld(nullptr, tTarget.m_pEntity, trace.endpos, vPoint, nMask);
				if (bValid)
				{
					Vec3 vFrom = trace.endpos + trace.plane.normal;

					CGameTrace eyeTrace = {};
					SDK::Trace(vFrom, tTarget.m_vPos + tTarget.m_pEntity->As<CTFPlayer>()->GetViewOffset(), MASK_SHOT, &filter, &eyeTrace);
					bValid = eyeTrace.fraction == 1.f;
				}
			}

			if (bValid)
			{
				if (bSplash)
				{
					int iPopCount = Vars::Aimbot::Projectile::SplashTraceInterval.Value - trace.fraction * Vars::Aimbot::Projectile::SplashTraceInterval.Value;
					for (int i = 0; i < iPopCount && !tProjInfo.m_vPath.empty(); i++)
						tProjInfo.m_vPath.pop_back();
				}
				bDidHit = true;
			}
			else
				break;

			if (!bSplash)
				trace.endpos = vNew;

			if (!bTarget || bSplash)
				break;
		}
	}
	
	tTarget.m_pEntity->SetAbsOrigin(tOriginal.m_vOrigin);
	tTarget.m_pEntity->m_vecMins() = tOriginal.m_vMins;
	tTarget.m_pEntity->m_vecMaxs() = tOriginal.m_vMaxs;

	if (bDidHit && pProjectilePath)
	{
		tProjInfo.m_vPath.push_back(trace.endpos);
		*pProjectilePath = tProjInfo.m_vPath;
	}

	return bDidHit;
}


int CAmalgamAimbotProjectile::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!tTarget.m_pEntity || !pLocal || !pWeapon)
		return false;

	ProjectileInfo tProjInfo = {};
	bool bGotInfo = F::ProjSim.GetInfo(pLocal, pWeapon, {}, tProjInfo, ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum);
	if (!bGotInfo)
		return false;
	
	bool bInitialized = F::ProjSim.Initialize(tProjInfo, false);
	if (!bInitialized)
		return false;

	// Use SEOwnedDE's movement simulation
	bool bIsPlayer = tTarget.m_iTargetType == TargetEnum::Player && IsPlayer(tTarget.m_pEntity);
	bool bMoveSim = bIsPlayer && F::MovementSimulation->Initialize(tTarget.m_pEntity->As<CTFPlayer>());
	
	std::vector<Vec3> vPlayerPath;
	
	tTarget.m_vPos = tTarget.m_pEntity->m_vecOrigin();

	m_tInfo = { pLocal, pWeapon };
	m_tInfo.m_vLocalEye = F::AmalgamTicks->GetShootPos(); // Use predicted shoot pos that accounts for CrouchWhileAirborne
	
	if (bIsPlayer)
		m_tInfo.m_vTargetEye = tTarget.m_pEntity->As<CTFPlayer>()->GetViewOffset();
	else
		m_tInfo.m_vTargetEye = Vec3(0, 0, tTarget.m_pEntity->m_vecMaxs().z * 0.9f);
	
	m_tInfo.m_flLatency = F::Backtrack.GetReal() + TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke());

	Vec3 vVelocity = F::ProjSim.GetVelocity();
	m_tInfo.m_flVelocity = vVelocity.Length();
	
	if (m_tInfo.m_flVelocity < 1.0f)
	{
		if (bMoveSim) F::MovementSimulation->Restore();
		return false;
	}
	
	m_tInfo.m_vAngFix = Math::VectorAngles(vVelocity);

	m_tInfo.m_vHull = tProjInfo.m_vHull.Min(3);
	m_tInfo.m_vOffset = tProjInfo.m_vPos - m_tInfo.m_vLocalEye; m_tInfo.m_vOffset.y *= -1;
	m_tInfo.m_flOffsetTime = m_tInfo.m_vOffset.Length() / m_tInfo.m_flVelocity;

	float flSize = tTarget.m_pEntity->GetSize().Length();
	m_tInfo.m_flGravity = tProjInfo.m_flGravity;
	m_tInfo.m_iSplashCount = Vars::Aimbot::Projectile::SplashCountDirect.Value;
	m_tInfo.m_flRadius = GetSplashRadius(pWeapon, pLocal);
	m_tInfo.m_flRadiusTime = m_tInfo.m_flRadius / m_tInfo.m_flVelocity;
	m_tInfo.m_flBoundingTime = m_tInfo.m_flRadiusTime + flSize / m_tInfo.m_flVelocity;

	m_tInfo.m_iSplashMode = GetSplashMode(pWeapon);

	int iReturn = false;
	float flMaxSimTime = std::min(std::min(tProjInfo.m_flLifetime, Vars::Aimbot::Projectile::MaxSimulationTime.Value), 5.0f);
	int iMaxTime = TIME_TO_TICKS(flMaxSimTime);
	if (iMaxTime > 330)
		iMaxTime = 330;
	
	int iSplash = Vars::Aimbot::Projectile::SplashPrediction.Value && m_tInfo.m_flRadius ? Vars::Aimbot::Projectile::SplashPrediction.Value : Vars::Aimbot::Projectile::SplashPredictionEnum::Off;
	int iMulti = Vars::Aimbot::Projectile::SplashMode.Value;

	auto mDirectPoints = iSplash == Vars::Aimbot::Projectile::SplashPredictionEnum::Only ? std::unordered_map<int, Vec3>() : GetDirectPoints(tTarget);
	auto vSpherePoints = !iSplash ? std::vector<std::pair<Vec3, int>>() : ComputeSphere(m_tInfo.m_flRadius + flSize, CFG::Aimbot_Amalgam_Projectile_SplashPoints);
	
	Vec3 vAngleTo, vPredicted, vTarget;
	int iLowestPriority = std::numeric_limits<int>::max(); float flLowestDist = std::numeric_limits<float>::max();
	int iLowestSmoothPriority = iLowestPriority; float flLowestSmoothDist = flLowestDist;
	
	// MIDPOINT AIM
	const bool bMidpointEnabled = Vars::Aimbot::Projectile::MidpointAim.Value;
	const int nWeaponID = pWeapon->GetWeaponID();
	const bool bMidpointAllowedWeapon = (nWeaponID == TF_WEAPON_ROCKETLAUNCHER || nWeaponID == TF_WEAPON_PARTICLE_CANNON);
	const float flMaxMidpointFeet = Vars::Aimbot::Projectile::MidpointMaxDistance.Value;
	
	bool bMidpointFound = false;
	Vec3 vMidpointPos = {};
	Vec3 vMidpointAngles = {};
	float flMidpointTime = 0.0f;
	int nMidpointSimTick = 0;
	std::vector<Vec3> vMidpointProjPath = {};
	std::vector<Vec3> vMidpointPlayerPath = {};
	
	for (int i = 1 - TIME_TO_TICKS(m_tInfo.m_flLatency); i <= iMaxTime; i++)
	{
		if (bMoveSim)
		{
			vPlayerPath.push_back(F::MovementSimulation->GetOrigin());
			F::MovementSimulation->RunTick(TICKS_TO_TIME(i));
			tTarget.m_vPos = F::MovementSimulation->GetOrigin();
		}
		if (i < 0)
			continue;

		// MIDPOINT AIM logic
		if (bMidpointEnabled && bMidpointAllowedWeapon && !bMidpointFound &&
			bIsPlayer && vPlayerPath.size() > 2 && m_tInfo.m_flRadius > 0.0f)
		{
			const bool bOnGround = IsOnGround(tTarget.m_pEntity->As<CTFPlayer>());
			
			if (bOnGround)
			{
				float flTotalPathLength = 0.0f;
				for (size_t pathIdx = 1; pathIdx < vPlayerPath.size(); pathIdx++)
					flTotalPathLength += vPlayerPath[pathIdx].DistTo(vPlayerPath[pathIdx - 1]);
				
				const float flPathLengthFeet = flTotalPathLength / 16.0f;
				
				if (flPathLengthFeet > 0.5f && flPathLengthFeet <= flMaxMidpointFeet)
				{
					float flHalfLength = flTotalPathLength * 0.5f;
					float flAccumulated = 0.0f;
					int nMidpointTick = 0;
					
					for (size_t pathIdx = 1; pathIdx < vPlayerPath.size(); pathIdx++)
					{
						flAccumulated += vPlayerPath[pathIdx].DistTo(vPlayerPath[pathIdx - 1]);
						if (flAccumulated >= flHalfLength)
						{
							nMidpointTick = static_cast<int>(pathIdx);
							break;
						}
					}
					
					if (nMidpointTick > 0 && nMidpointTick < static_cast<int>(vPlayerPath.size()))
					{
						Vec3 vMidpointOrigin = vPlayerPath[nMidpointTick];
						Target_t tMidpointTarget = tTarget;
						tMidpointTarget.m_vPos = vMidpointOrigin;
						
						Vec3 vTraceStart = vMidpointOrigin;
						Vec3 vTraceEnd = vMidpointOrigin - Vec3(0, 0, 256.0f);
						
						CGameTrace groundTrace = {};
						CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
						SDK::TraceHull(vTraceStart, vTraceEnd, m_tInfo.m_vHull * -1, m_tInfo.m_vHull, MASK_SOLID, &filter, &groundTrace);
						
						if (groundTrace.DidHit() && groundTrace.m_pEnt && 
							!(groundTrace.surface.flags & SURF_SKY) &&
							groundTrace.plane.normal.z > 0.7f)
						{
							Vec3 vGroundPos = groundTrace.endpos + Vec3(0, 0, 1.0f);
							
							Solution_t midpointSolution;
							CalculateAngle(m_tInfo.m_vLocalEye, vGroundPos, i, midpointSolution);
							
							if (midpointSolution.m_iCalculated == CalculatedEnum::Good)
							{
								Vec3 vTestAngles;
								Aim(G::CurrentUserCmd->viewangles, { midpointSolution.m_flPitch, midpointSolution.m_flYaw, 0.f }, vTestAngles);
								
								std::vector<Vec3> vTestProjLines;
								if (TestAngle(pLocal, pWeapon, tMidpointTarget, vGroundPos, vTestAngles, i, true, nullptr, &vTestProjLines))
								{
									bMidpointFound = true;
									vMidpointPos = vGroundPos;
									vMidpointAngles = vTestAngles;
									flMidpointTime = midpointSolution.m_flTime;
									nMidpointSimTick = nMidpointTick;
									vMidpointPlayerPath = vPlayerPath;
									vMidpointProjPath = vTestProjLines;
								}
							}
						}
					}
				}
			}
		}

		bool bDirectBreaks = true;
		std::vector<Point_t> vSplashPoints = {};
		if (iSplash)
		{
			Solution_t solution; CalculateAngle(m_tInfo.m_vLocalEye, tTarget.m_vPos, i, solution, false);
			if (solution.m_iCalculated != CalculatedEnum::Bad)
			{
				bDirectBreaks = false;

				const float flTimeTo = solution.m_flTime - TICKS_TO_TIME(i);
				if (flTimeTo < m_tInfo.m_flBoundingTime)
				{
					static std::vector<std::pair<Vec3, Vec3>> vSimplePoints = {};
					if (iMulti == Vars::Aimbot::Projectile::SplashModeEnum::Single)
					{
						SetupSplashPoints(tTarget, vSpherePoints, vSimplePoints);
						if (!vSimplePoints.empty())
							iMulti++;
						else
						{
							iSplash = Vars::Aimbot::Projectile::SplashPredictionEnum::Off;
							goto skipSplash;
						}
					}

					if ((iMulti == Vars::Aimbot::Projectile::SplashModeEnum::Multi ? vSpherePoints.empty() : vSimplePoints.empty())
						|| flTimeTo < -m_tInfo.m_flBoundingTime)
						break;
					else
					{
						if (iMulti == Vars::Aimbot::Projectile::SplashModeEnum::Multi)
							vSplashPoints = GetSplashPoints(tTarget, vSpherePoints, i);
						else
							vSplashPoints = GetSplashPointsSimple(tTarget, vSimplePoints, i);
					}
				}
			}
		}
		skipSplash:
		if (bDirectBreaks && mDirectPoints.empty())
			break;
		
		if (bMidpointFound)
			continue;

		std::vector<std::tuple<Point_t, int, int>> vPoints = {};
		for (auto& [iIndex, vPoint] : mDirectPoints)
			vPoints.emplace_back(Point_t(tTarget.m_vPos + vPoint, {}), iIndex + (iSplash == Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer ? m_tInfo.m_iSplashCount : 0), iIndex);
		for (auto& vPoint : vSplashPoints)
			vPoints.emplace_back(vPoint, iSplash == Vars::Aimbot::Projectile::SplashPredictionEnum::Include ? 3 : 0, -1);

		int j = 0;
		for (auto& [vPoint, iPriority, iIndex] : vPoints)
		{
			const bool bSplash = iIndex == -1;
			Vec3 vOriginalPoint = vPoint.m_vPoint;

			float flDist = bSplash ? tTarget.m_vPos.DistTo(vPoint.m_vPoint) : flLowestDist;
			bool bPriority = bSplash ? iPriority <= iLowestPriority : iPriority < iLowestPriority;
			bool bDist = !bSplash || flDist < flLowestDist;
			if (!bSplash && !bPriority)
				mDirectPoints.erase(iIndex);
			if (!bPriority || !bDist)
				continue;

			CalculateAngle(m_tInfo.m_vLocalEye, vPoint.m_vPoint, i, vPoint.m_tSolution);
			if (!bSplash && (vPoint.m_tSolution.m_iCalculated == CalculatedEnum::Good || vPoint.m_tSolution.m_iCalculated == CalculatedEnum::Bad))
				mDirectPoints.erase(iIndex);
			if (vPoint.m_tSolution.m_iCalculated != CalculatedEnum::Good)
				continue;

			std::vector<Vec3> vProjLines; bool bHitSolid = false;
			bool bHitTarget = false;

			// Neckbreaker: if roll 0 fails, try different roll angles to bypass obstructions (silent aim only)
			bool bUseNeckbreaker = Vars::Aimbot::Projectile::Neckbreaker.Value && 
				Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent;
			
			// Try roll 0 first
			Vec3 vAngles; Aim(G::CurrentUserCmd->viewangles, { vPoint.m_tSolution.m_flPitch, vPoint.m_tSolution.m_flYaw, 0.f }, vAngles);
			if (TestAngle(pLocal, pWeapon, tTarget, vPoint.m_vPoint, vAngles, i, bSplash, &bHitSolid, &vProjLines))
			{
				bHitTarget = true;
				iLowestPriority = iPriority; flLowestDist = flDist;
				vAngleTo = vAngles, vPredicted = tTarget.m_vPos, vTarget = vOriginalPoint;
				m_flTimeTo = vPoint.m_tSolution.m_flTime + m_tInfo.m_flLatency;
				m_vPlayerPath = vPlayerPath;
				m_vProjectilePath = vProjLines;
			}
			// If roll 0 failed and neckbreaker is enabled, search for working roll angle
			else if (bUseNeckbreaker)
			{
				const int iStep = Vars::Aimbot::Projectile::NeckbreakerStep.Value;
				
				// Search through roll angles using configured step size
				for (int iRoll = iStep; iRoll < 360; iRoll += iStep)
				{
					Vec3 vRollAngles;
					Aim(G::CurrentUserCmd->viewangles, { vPoint.m_tSolution.m_flPitch, vPoint.m_tSolution.m_flYaw, static_cast<float>(iRoll) }, vRollAngles);
					vRollAngles.z = static_cast<float>(iRoll);
					
					if (TestAngle(pLocal, pWeapon, tTarget, vPoint.m_vPoint, vRollAngles, i, bSplash, &bHitSolid, &vProjLines))
					{
						bHitTarget = true;
						iLowestPriority = iPriority; flLowestDist = flDist;
						vAngleTo = vRollAngles, vPredicted = tTarget.m_vPos, vTarget = vOriginalPoint;
						m_flTimeTo = vPoint.m_tSolution.m_flTime + m_tInfo.m_flLatency;
						m_vPlayerPath = vPlayerPath;
						m_vProjectilePath = vProjLines;
						break;
					}
				}
			}

			if (!bHitTarget) switch (Vars::Aimbot::General::AimType.Value)
			{
			case Vars::Aimbot::General::AimTypeEnum::Smooth:
				if (Vars::Aimbot::General::AssistStrength.Value == 100.f)
					break;
				[[fallthrough]];
			case Vars::Aimbot::General::AimTypeEnum::Assistive:
			{
				bool bPrioritySmooth = bSplash ? iPriority <= iLowestSmoothPriority : iPriority < flLowestSmoothDist;
				bool bDistSmooth = !bSplash || flDist < flLowestDist;
				if (!bPrioritySmooth || !bDistSmooth)
					continue;

				Vec3 vPlainAngles; Aim({}, { vPoint.m_tSolution.m_flPitch, vPoint.m_tSolution.m_flYaw, 0.f }, vPlainAngles, Vars::Aimbot::General::AimTypeEnum::Plain);
				if (TestAngle(pLocal, pWeapon, tTarget, vPoint.m_vPoint, vPlainAngles, i, bSplash, &bHitSolid))
				{
					iLowestSmoothPriority = iPriority; flLowestSmoothDist = flDist;
					Vec3 vAngles; Aim(G::CurrentUserCmd->viewangles, { vPoint.m_tSolution.m_flPitch, vPoint.m_tSolution.m_flYaw, 0.f }, vAngles);
					vAngleTo = vAngles, vPredicted = tTarget.m_vPos;
					m_vPlayerPath = vPlayerPath;
					iReturn = 2;
				}
			}
			}

			if (!j && bHitSolid)
				m_flTimeTo = vPoint.m_tSolution.m_flTime + m_tInfo.m_flLatency;
			j++;
		}
	}
	
	if (bMoveSim)
		F::MovementSimulation->Restore();

	// Use midpoint if found
	if (bMidpointFound)
	{
		iLowestPriority = -1;
		vAngleTo = vMidpointAngles;
		vTarget = vMidpointPos;
		vPredicted = vMidpointPlayerPath.empty() ? tTarget.m_vPos : vMidpointPlayerPath[nMidpointSimTick < static_cast<int>(vMidpointPlayerPath.size()) ? nMidpointSimTick : vMidpointPlayerPath.size() - 1];
		m_flTimeTo = flMidpointTime + m_tInfo.m_flLatency;
		m_vPlayerPath = vMidpointPlayerPath;
		m_vProjectilePath = vMidpointProjPath;
	}

	tTarget.m_vPos = vTarget;
	tTarget.m_vAngleTo = vAngleTo;
	
	bool bMain = iLowestPriority != std::numeric_limits<int>::max();

	if (bMain)
		return true;

	return iReturn;
}


bool CAmalgamAimbotProjectile::Aim(Vec3 vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod)
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
		float flMouseDelta = vMouseDelta.Length2D(), flTargetDelta = vTargetDelta.Length2D();
		vTargetDelta = vTargetDelta.Normalized() * std::min(flMouseDelta, flTargetDelta);
		vOut = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, Vars::Aimbot::General::AssistStrength.Value / 100.f);
		bReturn = true;
		break;
	}
	default:
		vOut = vToAngle;
		break;
	}

	float flRoll = vToAngle.z;
	Math::ClampAngles(vOut);
	
	// Restore roll for Neckbreaker
	if (Vars::Aimbot::Projectile::Neckbreaker.Value && flRoll != 0.f)
		vOut.z = flRoll;
	
	return bReturn;
}

void CAmalgamAimbotProjectile::Aim(CUserCmd* pCmd, Vec3& vAngle, int iMethod, bool bIsFiring)
{
	Vec3 vOut;
	Aim(pCmd->viewangles, vAngle, vOut, iMethod);
	
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
		H::AimUtils->FixMovement(pCmd, vOut);
		pCmd->viewangles = vOut;
		I::EngineClient->SetViewAngles(vOut);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		// For silent aim, apply angles when we're firing
		// Removed G::bCanPrimaryAttack check - projectile weapons can fire even when
		// CanPrimaryAttack returns false (e.g., Beggar's Bazooka loading, reload interrupt)
		// The bIsFiring parameter already indicates we want to shoot
		if (bIsFiring)
		{
			H::AimUtils->FixMovement(pCmd, vOut);
			pCmd->viewangles = vOut;
			G::bPSilentAngles = true;
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::Locking:
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
	default:
		H::AimUtils->FixMovement(pCmd, vOut);
		pCmd->viewangles = vOut;
		I::EngineClient->SetViewAngles(vOut);
		break;
	}
}

bool CAmalgamAimbotProjectile::RunMain(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const int nWeaponID = pWeapon->GetWeaponID();

	// Only handle rocket launchers
	switch (nWeaponID)
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		break;
	default:
		return false;
	}

	// Need at least 1 rocket to proceed
	if (pWeapon->m_iClip1() < 1)
		return false;

	if (F::AimbotGlobal->ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;
	
	// Check aimbot key (skip if Always On is enabled)
	if (!CFG::Aimbot_Always_On && CFG::Aimbot_Key != 0 && !H::Input->IsDown(CFG::Aimbot_Key))
		return false;
	
	// Check aim type is enabled (0=Plain, 1=Silent)
	// Vars::Aimbot::General::AimType maps CFG to Amalgam enum
	if (CFG::Aimbot_Projectile_Aim_Type < 0)
		return false;

	auto vTargets = SortTargets(pLocal, pWeapon);
	if (vTargets.empty())
		return false;

	if (!G::AimTarget.m_iEntIndex)
		G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

	for (auto& tTarget : vTargets)
	{
		m_flTimeTo = std::numeric_limits<float>::max();
		m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();

		const int iResult = CanHit(tTarget, pLocal, pWeapon);
		if (!iResult) 
			continue;
		if (iResult == 2)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			G::nTargetIndex = tTarget.m_pEntity->entindex();
			G::nTargetIndexEarly = tTarget.m_pEntity->entindex();
			// For smooth/assistive aim (iResult == 2), always apply angles
			Aim(pCmd, tTarget.m_vAngleTo, Vars::Aimbot::General::AimType.Value, true);
			break;
		}

		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::nTargetIndex = tTarget.m_pEntity->entindex();
		G::nTargetIndexEarly = tTarget.m_pEntity->entindex();
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

		if (CFG::Aimbot_AutoShoot)
		{
			pCmd->buttons |= IN_ATTACK;
			if (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
			{
				if (pWeapon->m_iClip1() > 0)
					pCmd->buttons &= ~IN_ATTACK;
			}
		}

		// Determine if we're actually firing this tick
		const bool bIsFiring = (pCmd->buttons & IN_ATTACK) && G::bCanPrimaryAttack;
		
		F::AmalgamAimbot.m_bRan = G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		G::bFiring = bIsFiring;

		// Draw paths only on the actual firing tick
		// Track when we transition from "can't fire" to "firing" to detect actual shots
		static bool bWasFiring = false;
		static int nLastDrawTick = 0;
		bool bFiringNow = (G::Attacking == 1);
		bool bJustFired = bFiringNow && !bWasFiring;
		bWasFiring = bFiringNow;
		
		// Only draw on the tick we actually fire, or if enough time has passed since last draw
		if (bJustFired || (bFiringNow && I::GlobalVars->tickcount - nLastDrawTick > 66)) // ~1 second between redraws if holding
		{
			// Notify movement simulation that we fired at this target (for reaction learning)
			if (bJustFired && tTarget.m_iTargetType == TargetEnum::Player && tTarget.m_pEntity)
			{
				F::MovementSimulation->OnShotFired(tTarget.m_pEntity->entindex());
			}
			
			// Clear previous overlays
			I::DebugOverlay->ClearAllOverlays();
			nLastDrawTick = I::GlobalVars->tickcount;
			
			// Draw movement path
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

			// Draw projectile path
			if (CFG::Visuals_Draw_Predicted_Path_Style > 0 && !m_vProjectilePath.empty())
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

		// Apply dodge prediction offset for players
		if (CFG::Aimbot_Projectile_Use_Dodge_Prediction && tTarget.m_iTargetType == TargetEnum::Player && tTarget.m_pEntity)
		{
			const int nTargetIdx = tTarget.m_pEntity->entindex();
			auto* pBehavior = F::MovementSimulation->GetPlayerBehavior(nTargetIdx);
			
			if (pBehavior && pBehavior->GetConfidence() > 0.5f && pBehavior->m_Combat.m_nReactionSamples >= 5)
			{
				const int nDodgeDir = F::MovementSimulation->GetPredictedDodge(nTargetIdx);
				
				// Only apply if they have a clear dodge preference (not "no reaction")
				if (nDodgeDir != 0)
				{
					// Scale offset by:
					// - Confidence: how sure we are about their dodge pattern
					// - Time to target: longer flight = more time to dodge = bigger offset
					// - Reaction rate: how often they actually dodge vs stand still
					const float flConfidence = pBehavior->GetConfidence();
					const float flReactionRate = pBehavior->m_Combat.m_flReactionToThreat;
					
					// Time scaling: 0.3s = minimal offset, 1.0s+ = full offset
					const float flTimeScale = Math::RemapValClamped(m_flTimeTo, 0.3f, 1.0f, 0.1f, 1.0f);
					
					// Base offset in degrees, scaled by all factors
					const float flBaseOffset = 2.0f;
					const float flOffset = flBaseOffset * flConfidence * flTimeScale * flReactionRate;
					
					switch (nDodgeDir)
					{
					case -1: // Dodge left - aim slightly right
						tTarget.m_vAngleTo.y -= flOffset;
						break;
					case 1:  // Dodge right - aim slightly left
						tTarget.m_vAngleTo.y += flOffset;
						break;
					case 2:  // Dodge jump - aim slightly higher
						tTarget.m_vAngleTo.x -= flOffset * 0.7f;
						break;
					case 3:  // Dodge back - already handled by movement prediction
						break;
					}
				}
			}
		}

		Aim(pCmd, tTarget.m_vAngleTo, Vars::Aimbot::General::AimType.Value, bIsFiring);
		return true;
	}

	return false;
}

void CAmalgamAimbotProjectile::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	G::flAimbotFOV = CFG::Aimbot_Projectile_FOV;
	RunMain(pLocal, pWeapon, pCmd);
}

// ============================================
// Stubs for future implementation
// ============================================

bool CAmalgamAimbotProjectile::TestAngle(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, C_BaseEntity* pProjectile, Target_t& tTarget, Vec3& vPoint, Vec3& vAngles, int iSimTime, bool bSplash, std::vector<Vec3>* pProjectilePath)
{
	// Stub for auto airblast
	return false;
}

bool CAmalgamAimbotProjectile::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, C_BaseEntity* pProjectile)
{
	// Stub for auto airblast
	return false;
}

void CAmalgamAimbotProjectile::SetupInfo(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (!pLocal || !pWeapon)
		return;
	
	m_tInfo = { pLocal, pWeapon };
	m_tInfo.m_vLocalEye = F::AmalgamTicks->GetShootPos(); // Use predicted shoot pos that accounts for CrouchWhileAirborne
	m_tInfo.m_flLatency = F::Backtrack.GetReal() + TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke());

	Vec3 vVelocity = F::ProjSim.GetVelocity();
	m_tInfo.m_flVelocity = vVelocity.Length();
	
	if (m_tInfo.m_flVelocity < 1.0f)
		m_tInfo.m_flVelocity = 1.0f;
	
	m_tInfo.m_vAngFix = Math::VectorAngles(vVelocity);

	ProjectileInfo tProjInfo = {};
	if (F::ProjSim.GetInfo(pLocal, pWeapon, I::EngineClient->GetViewAngles(), tProjInfo))
	{
		m_tInfo.m_vHull = tProjInfo.m_vHull.Min(3);
		m_tInfo.m_vOffset = tProjInfo.m_vPos - m_tInfo.m_vLocalEye;
		m_tInfo.m_vOffset.y *= -1;
		m_tInfo.m_flOffsetTime = m_tInfo.m_vOffset.Length() / m_tInfo.m_flVelocity;
		m_tInfo.m_flGravity = tProjInfo.m_flGravity;
	}

	m_tInfo.m_iSplashCount = Vars::Aimbot::Projectile::SplashCountDirect.Value;
	m_tInfo.m_flRadius = GetSplashRadius(pWeapon, pLocal);
	m_tInfo.m_flRadiusTime = m_tInfo.m_flRadius / m_tInfo.m_flVelocity;
	m_tInfo.m_flBoundingTime = m_tInfo.m_flRadiusTime;
	m_tInfo.m_iSplashMode = Vars::Aimbot::Projectile::RocketSplashModeEnum::Regular;
}
