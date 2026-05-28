#include "AimbotProjectileAmalgam.h"

// Use SEOwnedDE's existing features
#include "../../Aimbot/Aimbot.h"
#include "../../MovementSimulation/MovementSimulation.h"
#include "../ProjectileSimulation/ProjectileSimulation.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../Ticks/Ticks.h"
#include "../../Players/Players.h"

namespace
{
	bool CanInterruptReload(C_TFWeaponBase* pWeapon)
	{
		return pWeapon && pWeapon->HasPrimaryAmmoForShot() && pWeapon->m_iClip1() > 0 && pWeapon->IsInReload() && pWeapon->m_bReloadsSingly();
	}
}

std::vector<Target_t> CAmalgamAimbotProjectile::GetTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;
	const auto iSort = Vars::Aimbot::General::TargetSelection.Value;

	const Vec3 vLocalPos = pLocal->GetShootPos();
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

			// Vaccinator ignore (ported from Amalgam)
			if (CFG::Aimbot_Ignore_Vaccinator)
			{
				const int nWeaponID = pWeapon->GetWeaponID();
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

	// NOTE: Max target limiting is now done in RunMain() BEFORE CanHit() calculations
	// to avoid unnecessary processing of multiple targets when max is set to 1
	
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
			if (!bErase || ceilf(tPoint.m_tSolution.m_flTime / TICK_INTERVAL) != iSimTime)
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
			if (!bErase || ceilf(tPoint.m_tSolution.m_flTime / TICK_INTERVAL) != iSimTime)
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

	int iTimeTo = ceilf(out.m_flTime / TICK_INTERVAL);
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

	iTimeTo = ceilf(out.m_flTime / TICK_INTERVAL);
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

	if (!tTarget.m_pEntity || !H::Entities->IsEntityValid(tTarget.m_pEntity))
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
	bool bStationary = bMoveSim && F::MovementSimulation->IsStationary();
	
	std::vector<Vec3> vPlayerPath;
	
	tTarget.m_vPos = tTarget.m_pEntity->m_vecOrigin();

	m_tInfo = { pLocal, pWeapon };
	m_tInfo.m_vLocalEye = pLocal->GetShootPos();
	
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
	float flMaxSimTime = std::min(std::min(tProjInfo.m_flLifetime, Vars::Aimbot::Projectile::MaxSimulationTime.Value), 7.0f);

	// Adaptive prediction horizon: strafing/antiaim targets are harder to predict,
	// but we must NOT prevent the aimbot from finding long-range solutions.
	// Only shorten the horizon for truly unpredictable states (explosive launch).
	// Strafing/antiaim still get the full horizon — the sim handles them via
	// rigid ground mode and yaw decay rather than tick count truncation.
	if (bMoveSim)
	{
		const auto& simState = F::MovementSimulation->GetStorage();

		// Phase 6: explosive launch (rocket/sticky jump) — trajectory is
		// fundamentally unpredictable. Cap hard to avoid wasting ticks.
		if (simState.m_bExplosiveLaunch)
			flMaxSimTime = std::min(flMaxSimTime, 0.5f);
		// Phase 5: shorten horizon on recent reversals — extrapolating
		// a turn-rate that just reversed is worse than a short straight prediction.
		// But still allow enough ticks for medium-range shots.
		if (simState.m_bRecentReversal)
			flMaxSimTime = std::min(flMaxSimTime, 0.5f);
	}

	int iMaxTime = TIME_TO_TICKS(flMaxSimTime);
	if (iMaxTime > 462)
		iMaxTime = 462;
	
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
			if (!bStationary)
				F::MovementSimulation->RunTick();
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
		// Silent — only choke packet (pSilent) when actually firing AND not reloading.
		// pSilent during reload is unreliable - the reload may progress past interrupt point.
		// Amalgam returns G::Attacking=2 during reload, which doesn't trigger pSilent.
		H::AimUtils->FixMovement(pCmd, vOut);
		pCmd->viewangles = vOut;

		if (Shifting::bShifting && Shifting::bShiftingWarp)
			G::bSilentAngles = true;  // Warp: choke handled by warp system
		else if (bIsFiring && !G::bReloading)
			G::bPSilentAngles = true;  // Firing tick (not during reload): pSilent choke
		else if (bIsFiring && G::bReloading)
			G::bSilentAngles = true;  // Firing during reload: use silent but don't choke
		else
			G::bSilentAngles = true;  // Not firing: just hide local view, no choke
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
	{
		m_iCachedResult = 0;
		m_bPredictionSession = false;
		m_bWasReadyToFire = false;
		m_pCachedEntity = nullptr;       // null out to prevent dangling pointer
		m_iCachedTargetIndex = 0;
		m_vCachedProjectilePath.clear();
		m_vCachedPlayerPath.clear();
		return false;
	}
	
	// Check aim type is enabled (0=Plain, 1=Silent)
	if (CFG::Aimbot_Projectile_Aim_Type < 0)
		return false;

	// Throttle expensive prediction (SortTargets + CanHit) to ~17Hz.
	// Force fresh prediction when weapon just became ready to fire (transition tick)
	// so we have accurate angles for the shot. On other can-fire ticks, cached is fine.
	const int nPredictInterval = TIME_TO_TICKS(1.0f / 17.0f); // ~59ms = ~17Hz at 66tick

	// Check if weapon can fire now
	const int nSavedTickBase = pLocal->m_nTickBase();
	pLocal->m_nTickBase() = nSavedTickBase + 1;
	const bool bCanFireNow = pWeapon->CanPrimaryAttack(pLocal) && pWeapon->HasPrimaryAmmoForShot();
	pLocal->m_nTickBase() = nSavedTickBase;
	const bool bCanInterruptReload = CanInterruptReload(pWeapon);
	const bool bCanFire = bCanFireNow || bCanInterruptReload;

	// Force prediction on the tick the weapon BECOMES ready (transition from can't-fire to can-fire)
	// This ensures fresh angles for the first shot opportunity without running prediction every can-fire tick
	const bool bJustBecameReady = bCanFire && !m_bWasReadyToFire;
	m_bWasReadyToFire = bCanFire;

	const bool bShouldPredict = bJustBecameReady || (I::GlobalVars->tickcount - m_nLastPredictTick >= nPredictInterval) || !m_bPredictionSession;

	if (bShouldPredict)
	{
		m_nLastPredictTick = I::GlobalVars->tickcount;
		m_iCachedResult = 0;
		m_pCachedEntity = nullptr;       // clear stale cache — prevent use on next non-predict tick
		m_iCachedTargetIndex = 0;
		m_bPredictionSession = true; // mark session started even if no target found

		auto vTargets = SortTargets(pLocal, pWeapon);
		if (vTargets.empty())
			return false;

		const int nMaxTargets = CFG::Aimbot_Projectile_Max_Processing_Targets;
		if (static_cast<int>(vTargets.size()) > nMaxTargets)
			vTargets.resize(nMaxTargets);

		if (!G::AimTarget.m_iEntIndex && vTargets.front().m_pEntity && H::Entities->IsEntityValid(vTargets.front().m_pEntity))
			G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

		for (auto& tTarget : vTargets)
		{
			if (!tTarget.m_pEntity || !H::Entities->IsEntityValid(tTarget.m_pEntity))
				continue;
			m_flTimeTo = std::numeric_limits<float>::max();
			m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();

			const int iResult = CanHit(tTarget, pLocal, pWeapon);
			if (!iResult)
				continue;

			// Cache prediction result for intermediate ticks
			m_iCachedResult = iResult;
			m_iCachedTargetIndex = tTarget.m_pEntity->entindex();
			m_pCachedEntity = tTarget.m_pEntity;
			m_vCachedAngleTo = tTarget.m_vAngleTo;
			m_vCachedTargetPos = tTarget.m_vPos;
			m_vCachedProjectilePath = m_vProjectilePath;
			m_vCachedPlayerPath = m_vPlayerPath;

			if (iResult == 2)
			{
				G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
				G::nTargetIndex = tTarget.m_pEntity->entindex();
				G::nTargetIndexEarly = tTarget.m_pEntity->entindex();
				Aim(pCmd, tTarget.m_vAngleTo, Vars::Aimbot::General::AimType.Value, true);
			}
			else
			{
				G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
				G::nTargetIndex = tTarget.m_pEntity->entindex();
				G::nTargetIndexEarly = tTarget.m_pEntity->entindex();
				G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
			}
			break;
		}

		if (!m_iCachedResult)
			return false;
	}
	else
	{
		// Non-prediction tick: validate cached target is still alive
		bool bCacheValid = m_pCachedEntity && H::Entities->SafeIsEntityValid(m_pCachedEntity, m_iCachedTargetIndex);

		// Check if entity is dead/dormant (still in list but not a valid target)
		// Must check type via GetClassId before casting — As<T>() is static_cast
		// and casting a building to C_TFPlayer is UB (unrelated hierarchy).
		if (bCacheValid)
		{
			const auto eClassId = m_pCachedEntity->GetClassId();

			if (eClassId == ETFClassIds::CTFPlayer)
			{
				const auto pPlayer = static_cast<C_TFPlayer*>(m_pCachedEntity);
				if (pPlayer->deadflag() || pPlayer->IsDormant())
					bCacheValid = false;
			}
			else if (eClassId == ETFClassIds::CObjectSentrygun || eClassId == ETFClassIds::CObjectDispenser || eClassId == ETFClassIds::CObjectTeleporter)
			{
				const auto pBuilding = static_cast<C_BaseObject*>(m_pCachedEntity);
				if (pBuilding->m_iHealth() <= 0 || pBuilding->m_bCarried() || pBuilding->m_bPlacing() || pBuilding->IsDormant())
					bCacheValid = false;
			}
			else
			{
				// Unknown entity type — invalidate cache
				bCacheValid = false;
			}
		}

		if (!bCacheValid)
		{
			// Cache is stale/invalid — clear everything and force predict next tick
			m_iCachedResult = 0;
			m_pCachedEntity = nullptr;
			m_iCachedTargetIndex = 0;
			m_nLastPredictTick = 0;
			return false;
		}

		// Update global aim target from cache
		G::nTargetIndex = m_iCachedTargetIndex;
		G::nTargetIndexEarly = m_iCachedTargetIndex;
		if (m_iCachedResult == 2)
		{
			G::AimTarget = { m_iCachedTargetIndex, I::GlobalVars->tickcount, 0 };
			Aim(pCmd, m_vCachedAngleTo, Vars::Aimbot::General::AimType.Value, true);
		}
		else
		{
			G::AimTarget = { m_iCachedTargetIndex, I::GlobalVars->tickcount };
			G::AimPoint = { m_vCachedTargetPos, I::GlobalVars->tickcount };
		}
	}

	// Firing logic runs every tick — cheap checks using cached prediction
	// bCanFireNow and bCanInterruptReload already computed above for throttle decision
	const bool bOldCanPrimaryAttack = G::bCanPrimaryAttack;
	G::bCanPrimaryAttack = bCanFireNow || bCanInterruptReload;

	if (CFG::Aimbot_AutoShoot && !G::bAutoScopeWaitActive && (bCanFireNow || bCanInterruptReload))
	{
		pCmd->buttons |= IN_ATTACK;
		if (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
		{
			if (pWeapon->m_iClip1() > 0)
				pCmd->buttons &= ~IN_ATTACK;
		}
	}

	const bool bIsFiring = (pCmd->buttons & IN_ATTACK) && (bCanFireNow || bCanInterruptReload);

	F::AmalgamAimbot.m_bRan = G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
	G::bFiring = bIsFiring;
	G::bCanPrimaryAttack = bOldCanPrimaryAttack;

	// Apply aimbot angles when actually firing (autoshoot) or always (manual fire)
	// Smooth/assistive (cached result 2) already called Aim() above
	if (m_iCachedResult != 2 && (bIsFiring || !CFG::Aimbot_AutoShoot))
	{
		Aim(pCmd, m_vCachedAngleTo, Vars::Aimbot::General::AimType.Value, bIsFiring);
	}

	// Draw paths using cached prediction data
	if (bIsFiring && !m_vCachedProjectilePath.empty())
	{
		I::DebugOverlay->ClearAllOverlays();


		if (CFG::Visuals_Draw_Movement_Path_Style > 0 && !m_vCachedPlayerPath.empty())
		{
			const auto& col = CFG::Color_Simulation_Movement;
			const int r = col.r, g = col.g, b = col.b;

			for (size_t n = 1; n < m_vCachedPlayerPath.size(); n++)
			{
				if (CFG::Visuals_Draw_Movement_Path_Style == 2 && n % 2 == 0)
					continue;
				I::DebugOverlay->AddLineOverlay(m_vCachedPlayerPath[n], m_vCachedPlayerPath[n - 1], r, g, b, false, 2.0f);
			}
		}

		if (CFG::Visuals_Draw_Predicted_Path_Style > 0)
		{
			const auto& col = CFG::Color_Simulation_Projectile;
			const int r = col.r, g = col.g, b = col.b;

			for (size_t n = 1; n < m_vCachedProjectilePath.size(); n++)
			{
				if (CFG::Visuals_Draw_Predicted_Path_Style == 2 && n % 2 == 0)
					continue;
				I::DebugOverlay->AddLineOverlay(m_vCachedProjectilePath[n], m_vCachedProjectilePath[n - 1], r, g, b, false, 2.0f);
			}
		}
	}

	return true;
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
	m_tInfo.m_vLocalEye = pLocal->GetShootPos();
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
