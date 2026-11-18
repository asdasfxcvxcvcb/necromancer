#include "AutoVaccinator.h"

#include "../../AutoUberPop/AutoUberPop.h"
#include "../../Players/Players.h"
#include "../../CFG.h"

#include <cfloat>
#include <cstdarg>
#include <cstdio>

constexpr float VACC_LOW_HP = 100.f;
constexpr float VACC_SELF_POP_MIN = 0.45f;
constexpr float VACC_CHANGE_WATCHDOG = 2.0f;

namespace
{
	constexpr int VACC_PROJ_TICKS = 33;
	constexpr float VACC_SNIPER_AIM_RADIUS = 40.f;
	constexpr float VACC_PREDICT_MOVE_SPEED = 60.f;
	constexpr float VACC_REACT_BUFFER = 0.02f;
	constexpr float VACC_BLAST_CRIT_RATIO = 0.3f;
	constexpr float VACC_BLAST_CHIP_RATIO = 0.7f;
	constexpr float VACC_BLAST_LOW_RATIO = 0.15f;
	constexpr float VACC_STICKY_LOW_CRIT_RATIO = 0.5f;

	// conservative muzzle speeds for the no-react zone: a faster projectile
	// reaches us sooner, so its zone is the bigger (safer) one per weapon
	float ProjectileWeaponSpeed(int nWeaponID)
	{
		switch (nWeaponID)
		{
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_CANNON:
		case TF_WEAPON_GRENADELAUNCHER:
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_FLAREGUN_REVENGE:
		case TF_WEAPON_FLAME_BALL: return 1200.f;
		case TF_WEAPON_COMPOUND_BOW: return 2000.f;
		case TF_WEAPON_CROSSBOW: return 1800.f;
		case TF_WEAPON_SYRINGEGUN_MEDIC:
		case TF_WEAPON_RAYGUN:
		case TF_WEAPON_SHOTGUN_BUILDING_RESCUE: return 1100.f;
		default: return 1000.f;
		}
	}

	bool IsMarkedPlayer(C_BaseEntity* pEntity)
	{
		PlayerPriority priority{};
		return pEntity && F::Players->GetInfo(pEntity->entindex(), priority)
			&& (priority.Cheater || priority.RetardLegit || priority.Targeted || priority.Streamer || priority.Nigger);
	}

	bool MarkedEnemyAimingAt(C_TFPlayer* pTarget)
	{
		if (!pTarget || pTarget->IsInvulnerable())
			return false;

		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
		{
			const auto pEnemy{ pEntity ? pEntity->As<C_TFPlayer>() : nullptr };
			if (!pEnemy || pEnemy->deadflag() || pEnemy->IsDormant() || pEnemy->m_bFeignDeathReady() || pEnemy->m_flInvisibility() > 0.0f)
				continue;

			if (!IsMarkedPlayer(pEnemy))
				continue;

			const auto pWeapon{ pEnemy->m_hActiveWeapon().Get() ? pEnemy->m_hActiveWeapon().Get()->As<C_TFWeaponBase>() : nullptr };
			if (!pWeapon)
				continue;

			const auto eType{ H::AimUtils->GetWeaponType(pWeapon) };
			if (eType != EWeaponType::HITSCAN && eType != EWeaponType::PROJECTILE)
				continue;

			const bool bZoomed{ pEnemy->InCond(TF_COND_ZOOMED) };
			const Vec3 vTarget{ pTarget->GetCenter() };
			if (Math::CalcFov(pEnemy->GetEyeAngles(), Math::CalcAngle(pEnemy->GetShootPos(), vTarget)) >= (eType == EWeaponType::HITSCAN ? (bZoomed ? 10.f : 30.f) : 90.f))
				continue;

			if (H::AimUtils->TraceEntityAutoDet(pEnemy, vTarget, pEnemy->m_vecOrigin() + Vec3{ 0.0f, 0.0f, pEnemy->m_vecMaxs().z }))
				return true;
		}

		return false;
	}
}

medigun_resist_types_t WeaponIDToResType(int weaponID)
{
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
	case TF_WEAPON_SENTRY_BULLET:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_SENTRY_REVENGE:
	case TF_WEAPON_HANDGUN_SCOUT_PRIMARY:
	case TF_WEAPON_CROSSBOW:
	case TF_WEAPON_HANDGUN_SCOUT_SECONDARY:
	case TF_WEAPON_SODA_POPPER:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_PEP_BRAWLER_BLASTER:
	case TF_WEAPON_CHARGED_SMG:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_RAYGUN:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
	case TF_WEAPON_DRG_POMSON:
	case TF_WEAPON_SYRINGEGUN_MEDIC:
	case TF_WEAPON_LASER_POINTER:
		return MEDIGUN_BULLET_RESIST;

	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_GRENADELAUNCHER:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_FLAMETHROWER_ROCKET:
	case TF_WEAPON_GRENADE_DEMOMAN:
	case TF_WEAPON_SENTRY_ROCKET:
	case TF_WEAPON_PUMPKIN_BOMB:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_CANNON:
	case TF_WEAPON_PARTICLE_CANNON:
		return MEDIGUN_BLAST_RESIST;

	case TF_WEAPON_FLAMETHROWER:
	case TF_WEAPON_FLAREGUN:
	case TF_WEAPON_FLAREGUN_REVENGE:
	case TF_WEAPON_FLAME_BALL:
		return MEDIGUN_FIRE_RESIST;

	default:
		return MEDIGUN_NUM_RESISTS;
	}
}

bool IsPlayerInDanger(C_TFPlayer* player, medigun_resist_types_t& dangerType, const std::vector<Vec3>& vecPath, bool& bMarkedSource, int nStrictLevel, float flPairRatio = 1.f)
{
	bMarkedSource = false;

	if (!player || player->IsInvulnerable())
		return false;

	// Check for dangerous players
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		if (!pEntity)
			continue;

		const auto enemy{ pEntity->As<C_TFPlayer>() };
		if (!enemy || enemy->deadflag())
			continue;

		const auto weapon{ enemy->m_hActiveWeapon().Get() ? enemy->m_hActiveWeapon().Get()->As<C_TFWeaponBase>() : nullptr };
		if (!weapon)
			continue;

		const int nWeaponID = weapon->GetWeaponID();
		const auto eWpnType = H::AimUtils->GetWeaponType(weapon);

		// projectile-holder panic zone (Max/High only): inside this range the
		// enemy can fire a projectile that lands before our pop command even
		// reaches the server (flight time < latency + 20ms buffer), so the pop
		// happens preemptively - the in-flight loop below only covers shots
		// already airborne. Stickies are placed, not reacted to
		if (enemy->m_iClass() != TF_CLASS_SNIPER)
		{
			if (eWpnType != EWeaponType::PROJECTILE || nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || nStrictLevel >= 2)
				continue;

			const Vec3 vEye = enemy->m_vecOrigin() + Vec3{ 0.0f, 0.0f, enemy->m_vecMaxs().z };
			if (enemy->GetShootPos().DistTo(player->GetCenter()) > ProjectileWeaponSpeed(nWeaponID) * (SDKUtils::GetRealLatency() + VACC_REACT_BUFFER))
				continue;
			if (!ProjectileDanger::IsWorldVisible(player->GetCenter(), vEye))
				continue;

			const auto nRes = WeaponIDToResType(nWeaponID);
			if (nRes == MEDIGUN_NUM_RESISTS)
				continue;

			dangerType = nRes;
			bMarkedSource = IsMarkedPlayer(enemy);
			return true;
		}

		if (weapon->GetSlot() != WEAPON_SLOT_PRIMARY)
			continue;

		// huntsman: a sniper holding a DEADLY projectile weapon - it gets the
		// projectile-holder panic zone instead of the aim-ray checks (a charged
		// arrow one-shots the patient, and arrows in flight stay covered by the
		// projectile loop below)
		if (nWeaponID == TF_WEAPON_COMPOUND_BOW)
		{
			const int nSniperStrict = IsMarkedPlayer(enemy) ? 0 : nStrictLevel;
			if (nSniperStrict >= 2)
				continue;

			const Vec3 vEye = enemy->m_vecOrigin() + Vec3{ 0.0f, 0.0f, enemy->m_vecMaxs().z };
			if (enemy->GetShootPos().DistTo(player->GetCenter()) > ProjectileWeaponSpeed(nWeaponID) * (SDKUtils::GetRealLatency() + VACC_REACT_BUFFER))
				continue;
			if (!ProjectileDanger::IsWorldVisible(player->GetCenter(), vEye))
				continue;

			dangerType = MEDIGUN_BULLET_RESIST;
			bMarkedSource = IsMarkedPlayer(enemy);
			return true;
		}

		bool zoomed{ enemy->InCond(TF_COND_ZOOMED) };
		if (nWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC)
			zoomed = weapon->As<C_TFSniperRifleClassic>()->m_bCharging();

		if (!enemy->IsCritBoosted() && !enemy->IsMiniCritBoosted() && !zoomed)
			continue;

		// strictness gates the sniper panic path; a marked sniper always gets
		// Max aggression regardless of the setting
		const int nSniperStrict = IsMarkedPlayer(enemy) ? 0 : nStrictLevel;
		if (nSniperStrict >= 3)
			continue;

		auto mins{ player->m_vecMins() };
		auto maxs{ player->m_vecMaxs() };
		mins.x *= 3.0f; mins.y *= 3.0f; mins.z *= 1.5f;
		maxs.x *= 3.0f; maxs.y *= 3.0f; maxs.z *= 1.5f;

		Vec3 forward{};
		Math::AngleVectors(enemy->GetEyeAngles(), &forward);

		const Vec3 vEye = enemy->m_vecOrigin() + Vec3{ 0.0f, 0.0f, enemy->m_vecMaxs().z };

		bool bAimReach = Math::RayToOBB(enemy->GetShootPos(), forward, player->m_vecOrigin(), mins, maxs, player->RenderableToWorldTransform());
		bool bSightline = false;
		if (bAimReach)
		{
			const auto visibleFromCenter{ ProjectileDanger::IsWorldVisible(player->GetCenter(), vEye) };
			// only the OR of the two rays matters - skip the head ray when the
			// center ray already proved visibility
			const auto visibleFromHead{ visibleFromCenter || ProjectileDanger::IsWorldVisible(player->m_vecOrigin() + Vec3{ 0.0f, 0.0f, player->m_vecMaxs().z }, vEye) };
			bSightline = visibleFromCenter || visibleFromHead;
		}

		// the pop must land before the shot: the aim ray and the sightline
		// against where the player is about to be count on their own, so a
		// covered player actually moving toward the exposure still trips the
		// danger. High requires motion, Medium only trusts the current
		// sightline, and a stationary player can never pre-peek
		if ((!bAimReach || !bSightline) && nSniperStrict <= 1)
		{
			const bool bMoving = player->m_vecVelocity().Length2D() > VACC_PREDICT_MOVE_SPEED;
			if (nSniperStrict == 0 || bMoving)
			{
				for (const auto& vPos : vecPath)
				{
					const Vec3 vCenter = vPos + Vec3{ 0.0f, 0.0f, (mins.z + maxs.z) * 0.25f };
					const Vec3 vDelta = vCenter - enemy->GetShootPos();
					const float flAlong = vDelta.Dot(forward);
					if (flAlong <= 0.f)
						continue;
					if ((enemy->GetShootPos() + forward * flAlong).DistTo(vCenter) > VACC_SNIPER_AIM_RADIUS)
						continue;
					if (!ProjectileDanger::IsWorldVisible(vCenter, vEye))
						continue;
					bAimReach = true;
					bSightline = true;
					break;
				}
			}
		}

		if (!bAimReach || !bSightline)
			continue;

		dangerType = MEDIGUN_BULLET_RESIST;
		bMarkedSource = IsMarkedPlayer(enemy);
		return true;
	}

	const float flCenterZ{ player->m_vecMaxs().z * 0.5f };
	const float flHealth{ static_cast<float>(player->m_iHealth()) };
	const bool bLowHP{ player->m_iHealth() < VACC_LOW_HP };

	size_t numClosePipebombs{};
	bool bPipeMarked = false;

	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
	{
		if (!pEntity)
			continue;

		Vec3 vel{};
		pEntity->EstimateAbsVelocity(vel);
		if (pEntity->m_vecOrigin().DistTo(player->GetCenter()) > 2000.0f)
			continue;

		const auto bomb{ pEntity->GetClassId() == ETFClassIds::CTFGrenadePipebombProjectile ? pEntity->As<C_TFGrenadePipebombProjectile>() : nullptr };
		const bool bPlacedSticky{ bomb && bomb->m_iType() == TF_GL_MODE_REMOTE_DETONATE && bomb->m_bTouched() && vel.Length() < ProjectileDanger::STICKY_VEL_MAX };

		Vec3 vBlastPos{};
		float flMinDist = FLT_MAX;
		if (bPlacedSticky)
		{
			vBlastPos = bomb->m_vecOrigin();
			for (const auto& vPos : vecPath)
			{
				const Vec3 vCenter{ vPos + Vec3{ 0.0f, 0.0f, flCenterZ } };
				flMinDist = std::min(flMinDist, std::min((vBlastPos - vPos).Length(), (vBlastPos - vCenter).Length()));
			}
		}
		else
		{
			const int nProjTicks = std::clamp(VACC_PROJ_TICKS + TIME_TO_TICKS(SDKUtils::GetRealLatency() + VACC_REACT_BUFFER), VACC_PROJ_TICKS, VACC_PROJ_TICKS * 2);
			const auto tApproach = ProjectileDanger::ProjectileClosestApproach(pEntity, vecPath, nProjTicks);
			vBlastPos = tApproach.vBlastPos;
			flMinDist = tApproach.flMinDist;
			if (flMinDist > 250.0f)
				continue;
		}

		const bool bProjMarked = IsMarkedPlayer(pEntity->m_hOwnerEntity().Get());

		switch (pEntity->GetClassId())
		{
		case ETFClassIds::CTFProjectile_Arrow:
		case ETFClassIds::CTFProjectile_HealingBolt:
			if (flMinDist > 150.0f)
				continue;
			if (const auto arrow{ pEntity->As<C_TFProjectile_Arrow>() }; !arrow->m_bCritical() && player->m_iHealth() >= VACC_LOW_HP)
				continue;
			if (!ProjectileDanger::BlastReachable(vBlastPos, vecPath, flCenterZ))
				continue;
			dangerType = MEDIGUN_BULLET_RESIST;
			bMarkedSource = bProjMarked;
			return true;

		case ETFClassIds::CTFProjectile_Rocket:
		case ETFClassIds::CTFProjectile_SentryRocket:
		case ETFClassIds::CTFProjectile_EnergyBall:
		{
			ProjectileDanger::BlastParams_t tBlast{};
			if (!ProjectileDanger::GetBlastParams(pEntity, tBlast))
				continue;

			float flPathWorst = 0.f;
			const float flWorst = ProjectileDanger::WorstBlastDamage(vBlastPos, tBlast.flDamage, tBlast.flRadius, tBlast.bHalfFalloff, tBlast.bCrit ? ProjectileDanger::CRIT_MULT : 1.f, vecPath, flCenterZ, flPathWorst);
			const float flRatio = flWorst / std::max(flHealth, 1.f);
			if (!(flRatio >= 1.f
				|| (tBlast.bCrit && nStrictLevel <= 2 && flRatio >= VACC_BLAST_CRIT_RATIO)
				|| (tBlast.bCharged && nStrictLevel <= 1 && flRatio >= VACC_BLAST_CRIT_RATIO)
				|| (bLowHP && nStrictLevel <= 2 && flRatio >= VACC_BLAST_LOW_RATIO)
				|| (nStrictLevel <= 1 && flRatio >= VACC_BLAST_CHIP_RATIO)))
				continue;

			dangerType = MEDIGUN_BLAST_RESIST;
			bMarkedSource = bProjMarked;
			return true;
		}

		case ETFClassIds::CTFGrenadePipebombProjectile:
		{
			if (!bomb || bomb->m_iType() == TF_GL_MODE_REMOTE_DETONATE_PRACTICE)
				continue;

			ProjectileDanger::BlastParams_t tBlast{};
			if (!ProjectileDanger::GetBlastParams(pEntity, tBlast))
				continue;

			float flPathWorst = 0.f;
			const float flWorst = ProjectileDanger::WorstBlastDamage(vBlastPos, tBlast.flDamage, tBlast.flRadius, false, tBlast.bCrit ? ProjectileDanger::CRIT_MULT : 1.f, vecPath, flCenterZ, flPathWorst);
			const float flRatio = flWorst / std::max(flHealth, 1.f);

			if (bPlacedSticky)
			{
				if (!tBlast.bValid)
					continue;

				const bool bWalkingIn{ flPathWorst > 0.f };
				if (!(flRatio >= 1.f
					|| (nStrictLevel <= 1 && flRatio >= VACC_BLAST_CRIT_RATIO)
					|| (nStrictLevel == 2 && (tBlast.bCrit || bWalkingIn || bLowHP) && flRatio >= VACC_BLAST_CRIT_RATIO)
					|| (nStrictLevel == 3 && tBlast.bCrit && flRatio >= VACC_STICKY_LOW_CRIT_RATIO)))
				{
					if (!tBlast.bCrit && flRatio >= VACC_BLAST_LOW_RATIO)
					{
						numClosePipebombs++;
						bPipeMarked |= bProjMarked;
					}
					continue;
				}

				dangerType = MEDIGUN_BLAST_RESIST;
				bMarkedSource = bProjMarked;
				return true;
			}

			if (!(flRatio >= 1.f
				|| (tBlast.bCrit && nStrictLevel <= 2 && flRatio >= VACC_BLAST_CRIT_RATIO)
				|| (bLowHP && nStrictLevel <= 2 && flRatio >= VACC_BLAST_LOW_RATIO)
				|| (nStrictLevel <= 1 && flRatio >= VACC_BLAST_CHIP_RATIO)))
			{
				if (!tBlast.bCrit && flWorst > 0.f && flRatio >= VACC_BLAST_LOW_RATIO)
				{
					numClosePipebombs++;
					bPipeMarked |= bProjMarked;
				}
				continue;
			}

			dangerType = MEDIGUN_BLAST_RESIST;
			bMarkedSource = bProjMarked;
			return true;
		}

		case ETFClassIds::CTFProjectile_Flare:
			if (flMinDist > 150.0f)
				continue;
			if (const auto flare{ pEntity->As<C_TFProjectile_Flare>() }; !flare->m_bCritical() && !player->InCond(TF_COND_BURNING) && !player->InCond(TF_COND_BURNING_PYRO))
				continue;
			if (!ProjectileDanger::BlastReachable(vBlastPos, vecPath, flCenterZ))
				continue;
			dangerType = MEDIGUN_FIRE_RESIST;
			bMarkedSource = bProjMarked;
			return true;

		case ETFClassIds::CTFProjectile_BallOfFire:
			if (flMinDist > 150.0f)
				continue;
			if (!player->InCond(TF_COND_BURNING) && !player->InCond(TF_COND_BURNING_PYRO))
				continue;
			if (!ProjectileDanger::BlastReachable(vBlastPos, vecPath, flCenterZ))
				continue;
			dangerType = MEDIGUN_FIRE_RESIST;
			bMarkedSource = bProjMarked;
			return true;

		case ETFClassIds::CTFProjectile_EnergyRing:
			if (flMinDist > 150.0f)
				continue;
			if (player->m_iHealth() >= VACC_LOW_HP)
				continue;
			if (!ProjectileDanger::BlastReachable(vBlastPos, vecPath, flCenterZ))
				continue;
			dangerType = MEDIGUN_BULLET_RESIST;
			bMarkedSource = bProjMarked;
			return true;

		default: {}
		}
	}

	if (numClosePipebombs > 1)
	{
		dangerType = MEDIGUN_BLAST_RESIST;
		bMarkedSource = bPipeMarked;
		return true;
	}

	// Check for dangerous sentry guns
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
	{
		if (!pEntity || pEntity->GetClassId() != ETFClassIds::CObjectSentrygun)
			continue;

		C_ObjectSentrygun* sentrygun{ pEntity->As<C_ObjectSentrygun>() };
		if (!sentrygun)
			continue;

		if (sentrygun->m_hAutoAimTarget() != player && sentrygun->m_hEnemy() != player)
			continue;

		if (sentrygun->IsDisabled() || sentrygun->m_bBuilding() || sentrygun->m_bPlacing())
			continue;

		const bool bMarkedBuilder{ IsMarkedPlayer(sentrygun->m_hBuilder().Get()) };
		const int nSentryStrict{ bMarkedBuilder ? 0 : nStrictLevel };

		if (sentrygun->m_iUpgradeLevel() < 2)
			continue;

		if (player->m_iHealth() >= VACC_LOW_HP)
			continue;

		// the heal target usually deletes a half-dead sentry before it does
		// real damage - only a dying sentry against a healthy pair is quiet
		const float flSentryHP{ static_cast<float>(sentrygun->m_iHealth()) / static_cast<float>(std::max(sentrygun->m_iMaxHealth(), 1)) };
		if (flSentryHP < 0.35f && flPairRatio >= 0.5f)
			continue;

		const float flSentryDist{ sentrygun->GetCenter().DistTo(player->GetCenter()) };
		const bool bWrangled{ sentrygun->m_bPlayerControlled() };
		if (!bWrangled && flSentryDist > 600.f + SDKUtils::GetRealLatency() * 400.f && nSentryStrict > 0)
			continue;

		// sentries are perfect aimbots with a tracking delay - the ray runs
		// from the sentry to where we are AND where our predicted movement
		// puts us, so tucking behind cover or strafing out waits instead of
		// burning a charge on bullets that will miss
		if (!ProjectileDanger::BlastReachable(sentrygun->GetCenter(), vecPath, flCenterZ))
			continue;

		dangerType = MEDIGUN_BULLET_RESIST;
		bMarkedSource = bMarkedBuilder;
		return true;
	}

	return false;
}

bool PlayerHasResUber(medigun_resist_types_t res, C_TFPlayer* player)
{
	switch (res)
	{
	case MEDIGUN_BULLET_RESIST:
		return player->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST);
	case MEDIGUN_BLAST_RESIST:
		return player->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST);
	case MEDIGUN_FIRE_RESIST:
		return player->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST);
	default:
		return false;
	}
}

void CAutoVaccinator::Reset()
{
	m_GoalResType = MEDIGUN_NUM_RESISTS;
	m_IsChangingRes = false;
	m_ShouldPop = false;
	m_SwitchDelayTicks = 0;
	m_flChangeStart = 0.f;
}

bool CAutoVaccinator::CanPop(C_WeaponMedigun* medigun, int type) const
{
	if (!medigun || type < MEDIGUN_BULLET_RESIST || type >= MEDIGUN_NUM_RESISTS)
		return false;

	if (medigun->m_flChargeLevel() < 0.25f)
		return false;

	const float flNow = I::GlobalVars->curtime;
	const float flSince = flNow - m_flLastPopTime[type];
	return m_flLastPopTime[type] <= 0.f || flSince < 0.f || flSince >= 0.75f;
}

float CAutoVaccinator::GetLastPopTime() const
{
	return m_flRealtimeLastPop;
}

void CAutoVaccinator::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* cmd)
{
	const bool bKeyHeld = CFG::Triggerbot_Key && H::Input->IsDown(CFG::Triggerbot_Key);

	if (!CFG::Triggerbot_AutoVaccinator_Active)
	{
		Reset();
		return;
	}

	if (pWeapon->GetWeaponID() != TF_WEAPON_MEDIGUN)
	{
		Reset();
		return;
	}

	const auto medigun{ pWeapon->As<C_WeaponMedigun>() };
	if (!medigun || medigun->GetChargeType() < 3)
	{
		Reset();
		return;
	}

	// a latched pop or pending queue must NEVER survive death: Run is not
	// called while dead, so without this the latch froze through the respawn
	// and fired the instant the new life had charge ("immediately popped
	// bullet for this medic") - a fresh life re-decides from scratch
	if (pLocal->deadflag())
	{
		Reset();
		return;
	}

	if (Shifting::bRecharging)
		return;

	// a new match resets the server clock - stale per-type pop cooldowns from
	// the previous map would land minutes in the future and block every pop
	// until the new clock caught up (dj's "cycling but not popping after new
	// matches", popAge was NEGATIVE in the VaccDbg capture)
	static float s_flLastCurtime = 0.f;
	if (I::GlobalVars->curtime < s_flLastCurtime)
	{
		for (int i = 0; i < MEDIGUN_NUM_RESISTS; i++)
			m_flLastPopTime[i] = 0.f;
		m_flRealtimeLastPop = 0.f;
		m_nPredictedResist = -1;
		m_flPredictTime = 0.f;
	}
	s_flLastCurtime = I::GlobalVars->curtime;

	auto PressAttack2 = [&](int nType)
	{
		cmd->buttons |= IN_ATTACK2;
		m_flLastPopTime[nType] = I::GlobalVars->curtime;
		m_flRealtimeLastPop = I::GlobalVars->realtime;
	};

	// the server's actual resist is the ONLY authority - everything else
	// derives from it, so a desync like the old simulated m_SimResType chase
	// ("cycling but never popping") is structurally impossible. The netvar
	// lags a latency window behind our presses though: without local
	// prediction the cycle math re-presses off stale data and oscillates
	// (the "cycling very very fast" bug). RijiN's answer: advance a predicted
	// resist immediately on each press, trust it until the confirmation
	// window passes, then adopt whatever the server says.
	int nActual = medigun->GetResistType();
	if (nActual < MEDIGUN_BULLET_RESIST || nActual >= MEDIGUN_NUM_RESISTS)
		nActual = MEDIGUN_BULLET_RESIST;

	const bool bPredictLive = m_nPredictedResist >= MEDIGUN_BULLET_RESIST
		&& m_nPredictedResist < MEDIGUN_NUM_RESISTS
		&& I::GlobalVars->curtime < m_flPredictTime;
	if (!bPredictLive)
		m_nPredictedResist = nActual;

	if (m_GoalResType < MEDIGUN_BULLET_RESIST || m_GoalResType >= MEDIGUN_NUM_RESISTS)
		m_GoalResType = nActual;

	const bool bNoPop = CFG::Triggerbot_AutoVaccinator_NoPop && !bKeyHeld;

	// 1. latched pop execution - fires only when the server's actual resist
	// already matches the goal, never mid-cycle on the wrong type
	if (m_ShouldPop)
	{
		if (nActual == m_GoalResType)
		{
			if (medigun->m_flChargeLevel() >= 0.25f)
				PressAttack2(m_GoalResType);
			m_ShouldPop = false;
			m_GoalResType = nActual;
			return;
		}
		// else: keep latched, the cycle block below brings the resist over
	}

	// 3. danger decision - only with nothing latched and not already cycling
	// (hysteresis: a mid-cycle goal flip-flop is what makes the vacc churn),
	// so a flickering threat can never stomp a latched pop either. There is
	// deliberately NO blind key-pop: the trigger key doubles as the pop key,
	// and dj holds/spams it for the triggerbot - the old press-queues-a-pop
	// burned charges with zero threats on screen. Holding the key in Cycle
	// Only mode just allows threat pops through the decision below
	if (!m_ShouldPop && !m_IsChangingRes)
	{
		const auto healTargetEnt = medigun->m_hHealingTarget().Get();
		C_TFPlayer* healTarget = healTargetEnt ? healTargetEnt->As<C_TFPlayer>() : nullptr;

		std::vector<Vec3> vecSelfPath;
		ProjectileDanger::GetTargetPath(pLocal, vecSelfPath);
		std::vector<Vec3> vecHealPath;
		if (healTarget && healTarget != pLocal)
			ProjectileDanger::GetTargetPath(healTarget, vecHealPath);

		medigun_resist_types_t panicType{ MEDIGUN_NUM_RESISTS };
		bool bPanicTargetIsHeal = false;
		bool bMarkedThreat = false;
		const int nPanicStrict = std::clamp(CFG::Triggerbot_AutoVaccinator_Tolerance, 0, 3);
		float flLocalRatio = static_cast<float>(pLocal->m_iHealth()) / static_cast<float>(std::max(pLocal->GetMaxHealth(), 1));
		if (healTarget)
			flLocalRatio = std::min(flLocalRatio, static_cast<float>(healTarget->m_iHealth()) / static_cast<float>(std::max(healTarget->GetMaxHealth(), 1)));

		if (healTarget && IsPlayerInDanger(healTarget, panicType, healTarget != pLocal ? vecHealPath : vecSelfPath, bMarkedThreat, nPanicStrict, flLocalRatio) && !PlayerHasResUber(panicType, healTarget))
			bPanicTargetIsHeal = true;
		else if (IsPlayerInDanger(pLocal, panicType, vecSelfPath, bMarkedThreat, nPanicStrict, flLocalRatio) && !PlayerHasResUber(panicType, pLocal))
			bPanicTargetIsHeal = false;
		else
			panicType = MEDIGUN_NUM_RESISTS;

		bMarkedThreat = bMarkedThreat || MarkedEnemyAimingAt(pLocal) || (healTarget && healTarget != pLocal && MarkedEnemyAimingAt(healTarget));

		// spun-up heavy at knife range (Medium to Max): looking at us with a
		// real sightline and either of us at low health -> bullet now. The
		// health bar pushes earlier on high ping (the pop lands later), and
		// WHO the heavy is looking at decides whose health counts - beaming a
		// healthy patient is no reason, beaming a hurt one is
		if (panicType == MEDIGUN_NUM_RESISTS && nPanicStrict <= 2)
		{
			const float flCloseZone = 160.f + SDKUtils::GetRealLatency() * 400.f;
			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
			{
				const auto pEnemy{ pEntity ? pEntity->As<C_TFPlayer>() : nullptr };
				if (!pEnemy || pEnemy->deadflag() || pEnemy->IsDormant() || pEnemy->m_iClass() != TF_CLASS_HEAVY)
					continue;

				const auto pHeavyWpn = pEnemy->m_hActiveWeapon().Get() ? pEnemy->m_hActiveWeapon().Get()->As<C_TFWeaponBase>() : nullptr;
				if (!pHeavyWpn || pHeavyWpn->GetWeaponID() != TF_WEAPON_MINIGUN || !pEnemy->InCond(TF_COND_AIMING))
					continue;

				const Vec3 vEye = pEnemy->m_vecOrigin() + Vec3{ 0.0f, 0.0f, pEnemy->m_vecMaxs().z };
				const bool bLocalInRange = pEnemy->GetShootPos().DistTo(pLocal->GetCenter()) <= flCloseZone;
				const bool bHealInRange = healTarget && pEnemy->GetShootPos().DistTo(healTarget->GetCenter()) <= flCloseZone;
				if (!bLocalInRange && !bHealInRange)
					continue;

				const auto fHeavyOnUs = [&](C_TFPlayer* pProtected) -> bool {
					if (!pProtected || pProtected->IsInvulnerable())
						return false;
					if (Math::CalcFov(pEnemy->GetEyeAngles(), Math::CalcAngle(pEnemy->GetShootPos(), pProtected->GetCenter())) >= 30.f)
						return false;
					if (!ProjectileDanger::IsWorldVisible(pProtected->GetCenter(), vEye))
						return false;
					const float flRatio = static_cast<float>(pProtected->m_iHealth()) / static_cast<float>(std::max(pProtected->GetMaxHealth(), 1));
					return flRatio < std::min(0.5f + SDKUtils::GetRealLatency() * 2.f, 0.8f);
				};

				if (fHeavyOnUs(pLocal) || (healTarget && fHeavyOnUs(healTarget)))
				{
					panicType = MEDIGUN_BULLET_RESIST;
					break;
				}
			}
		}

		float flSelfScore[MEDIGUN_NUM_RESISTS] = {};
		F::AutoUberPop->ScoreDangers(pLocal, medigun, pLocal, flSelfScore[MEDIGUN_BULLET_RESIST], flSelfScore[MEDIGUN_BLAST_RESIST], flSelfScore[MEDIGUN_FIRE_RESIST]);

		float flScore[MEDIGUN_NUM_RESISTS] = {};
		for (int i = 0; i < MEDIGUN_NUM_RESISTS; i++)
			flScore[i] = flSelfScore[i];
		if (healTarget && (CFG::Triggerbot_AutoVaccinator_Pop != 1 || healTarget->IsPlayerOnSteamFriendsList()))
			F::AutoUberPop->ScoreDangers(pLocal, medigun, healTarget, flScore[MEDIGUN_BULLET_RESIST], flScore[MEDIGUN_BLAST_RESIST], flScore[MEDIGUN_FIRE_RESIST]);

		if (panicType != MEDIGUN_NUM_RESISTS)
		{
			m_GoalResType = panicType;
			if (!bNoPop && CanPop(medigun, panicType))
			{
				m_ShouldPop = true;
				if (bPanicTargetIsHeal && CFG::Triggerbot_AutoVaccinator_Pop == 1 && healTarget && !healTarget->IsPlayerOnSteamFriendsList())
					m_ShouldPop = false;
			}
		}
		else
		{
			medigun_resist_types_t scoredType = MEDIGUN_BULLET_RESIST;
			float flMaxScore = flScore[MEDIGUN_BULLET_RESIST];
			for (int i = MEDIGUN_BLAST_RESIST; i < MEDIGUN_NUM_RESISTS; i++)
			{
				if (flScore[i] > flMaxScore)
				{
					flMaxScore = flScore[i];
					scoredType = static_cast<medigun_resist_types_t>(i);
				}
			}

			const int nStrictLevel = bMarkedThreat ? 0 : std::clamp(CFG::Triggerbot_AutoVaccinator_Tolerance, 0, 3);
			float flScoreReq = 1.f;
			switch (nStrictLevel)
			{
			case 1: flScoreReq = 1.5f; break;
			case 2: flScoreReq = 2.f; break;
			case 3: flScoreReq = 999.f; break;
			default: break;
			}

			// dj: even a tiny chip can kill us when our health is low - soften
			// the bar by the WORST protected health ratio (medic + patient),
			// floored at half the normal bar so it never gets too aggressive
			float flWorstRatio = static_cast<float>(pLocal->m_iHealth()) / static_cast<float>(std::max(pLocal->GetMaxHealth(), 1));
			if (healTarget)
				flWorstRatio = std::min(flWorstRatio, static_cast<float>(healTarget->m_iHealth()) / static_cast<float>(std::max(healTarget->GetMaxHealth(), 1)));
			flScoreReq *= std::clamp(flWorstRatio * 1.5f, 0.5f, 1.f);

			// a type already popped-active (2.5s window) on BOTH of us gets no
			// pop - the charge would double-pay for 75% cover we already have
			// (panic and hurt paths already skip it, the scored pop must too);
			// while covered the goal pre-cycles toward an uncovered type so
			// the next pop is ready for blast or fire instead
			const auto fCovered = [&](medigun_resist_types_t eType) -> bool
			{
				if (!PlayerHasResUber(eType, pLocal))
					return false;
				if (healTarget && healTarget != pLocal && !PlayerHasResUber(eType, healTarget))
					return false;
				return true;
			};

			if (flMaxScore >= flScoreReq && !fCovered(scoredType))
			{
				m_GoalResType = scoredType;
				if (!bNoPop && CanPop(medigun, scoredType))
					m_ShouldPop = true;
			}
			else
			{
				medigun_resist_types_t eBest = MEDIGUN_NUM_RESISTS;
				float flBest = 0.f;
				for (int i = MEDIGUN_BULLET_RESIST; i < MEDIGUN_NUM_RESISTS; i++)
				{
					const auto eType{ static_cast<medigun_resist_types_t>(i) };
					if (fCovered(eType))
						continue;
					if (flScore[i] > flBest)
					{
						flBest = flScore[i];
						eBest = eType;
					}
				}

				const bool bGoalValid = m_GoalResType >= MEDIGUN_BULLET_RESIST && m_GoalResType < MEDIGUN_NUM_RESISTS;
				const bool bGoalCovered = bGoalValid && fCovered(static_cast<medigun_resist_types_t>(m_GoalResType));
				const float flGoalScore = bGoalValid ? flScore[m_GoalResType] : 0.f;

				if (eBest != MEDIGUN_NUM_RESISTS)
				{
					if (bGoalCovered || !bGoalValid || flBest > flGoalScore * 1.25f)
						m_GoalResType = eBest;
				}
				else
				{
					medigun_resist_types_t eIdle = MEDIGUN_NUM_RESISTS;
					for (int i = MEDIGUN_BULLET_RESIST; i < MEDIGUN_NUM_RESISTS; i++)
					{
						const auto eType{ static_cast<medigun_resist_types_t>(i) };
						if (!fCovered(eType))
						{
							eIdle = eType;
							break;
						}
					}

					if (eIdle != MEDIGUN_NUM_RESISTS)
						m_GoalResType = eIdle;
					else if (bGoalCovered)
						m_GoalResType = static_cast<medigun_resist_types_t>((m_GoalResType + 1) % MEDIGUN_NUM_RESISTS);
				}
			}
		}

		// the decision landed a pop with the right resist already active
		if (m_ShouldPop && nActual == m_GoalResType)
		{
			if (medigun->m_flChargeLevel() >= 0.25f)
				PressAttack2(m_GoalResType);
			m_ShouldPop = false;
			m_GoalResType = nActual;
			return;
		}
	}

	// 4. cycle toward the goal - the math runs on the PREDICTED resist so a
	// lagging netvar can never trigger overshoot re-presses; the prediction
	// is adopted-then-verified against the server after one latency window
	const int nCycleActual = bPredictLive ? m_nPredictedResist : nActual;
	const int nNeeded = (m_GoalResType - nCycleActual + MEDIGUN_NUM_RESISTS) % MEDIGUN_NUM_RESISTS;
	if (nNeeded > 0)
	{
		if (!m_IsChangingRes)
		{
			m_IsChangingRes = true;
			m_flChangeStart = I::GlobalVars->curtime;
		}

		if (I::GlobalVars->curtime - m_flChangeStart >= VACC_CHANGE_WATCHDOG)
		{
			m_nPredictedResist = nActual;
			m_GoalResType = nActual;
			m_IsChangingRes = false;
			m_SwitchDelayTicks = 0;
		}
		else
		{
			if (m_SwitchDelayTicks > 0)
				m_SwitchDelayTicks--;
			else
			{
				cmd->buttons |= IN_RELOAD;
				m_nPredictedResist = (nCycleActual + 1) % MEDIGUN_NUM_RESISTS;
				m_flPredictTime = I::GlobalVars->curtime + SDKUtils::GetRealLatency() * 1.5f + 0.1f;
				m_SwitchDelayTicks = 2;
			}
			return;
		}
	}
	m_IsChangingRes = false;
}

void CAutoVaccinator::ProcessPlayerHurt(IGameEvent* event)
{
	if (!CFG::Triggerbot_AutoVaccinator_Active)
		return;

	// a pending key-pop queue must NOT veto hurt pops: while the key is held
	// the queue is always pending, which silenced every hurt response (dj's
	// "HURT ignored" spam). The hurt event picks the correct resist anyway,
	// so it replaces the queued pop instead of waiting behind it
	if (m_ShouldPop)
		return;

	const auto pLocal{ H::Entities->GetLocal() };
	if (!pLocal || pLocal->deadflag())
		return;

	const auto weapon{ H::Entities->GetWeapon() };
	if (!weapon || weapon->GetWeaponID() != TF_WEAPON_MEDIGUN)
		return;

	const auto medigun{ weapon->As<C_WeaponMedigun>() };
	if (!medigun || medigun->GetChargeType() < 3)
		return;

	const auto victim{ GET_ENT_FROM_USER_ID(event->GetInt("userid")) };
	const auto attacker{ GET_ENT_FROM_USER_ID(event->GetInt("attacker")) };

	if (!victim || victim == attacker)
		return;

	const auto health{ event->GetInt("health") };
	if (health <= 0)
		return;

	const auto weaponID{ event->GetInt("weaponid") };
	if (!weaponID)
		return;

	if (victim != pLocal && victim != medigun->m_hHealingTarget())
		return;

	const auto victimEnt{ victim->As<C_TFPlayer>() };
	if (!victimEnt)
		return;

	m_GoalResType = WeaponIDToResType(weaponID);

	const bool bTriggerbotKeyPressed = CFG::Triggerbot_Key && H::Input->IsDown(CFG::Triggerbot_Key);
	const bool bNoPop = CFG::Triggerbot_AutoVaccinator_NoPop && !bTriggerbotKeyPressed;

	const bool bSelfDamage = victimEnt == pLocal;
	const bool bPopsAllowed = !bNoPop;
	const bool bMarkedAttacker = attacker && IsMarkedPlayer(attacker->As<C_BaseEntity>());
	const int nStrictLevel = bMarkedAttacker ? 0 : std::clamp(CFG::Triggerbot_AutoVaccinator_Tolerance, 0, 3);
	// class-aware urgency: the old absolute health < 100 gate made every chip
	// on a spy (125 max HP) "urgent" and burned charges for nothing
	const float flHealthRatio = static_cast<float>(health) / static_cast<float>(std::max(victimEnt->GetMaxHealth(), 1));
	const bool bUrgent = event->GetBool("crit") || event->GetBool("minicrit") || (nStrictLevel < 3 && flHealthRatio < 0.5f);

	// Amalgam-style lethal stream: pop on a non-urgent hit only when the
	// attacker's damage RATE can kill the victim within a second (chips and
	// afterburn ticks never qualify - that was the wasteful pop)
	bool bLethalStream = false;
	{
		const auto pAttacker = attacker && attacker->GetClassId() == ETFClassIds::CTFPlayer ? attacker->As<C_TFPlayer>() : nullptr;
		const auto pAttackerWpn = pAttacker ? pAttacker->m_hActiveWeapon().Get() : nullptr;
		const auto pAttackerBase = pAttackerWpn ? pAttackerWpn->As<C_TFWeaponBase>() : nullptr;
		const int iDamage = event->GetInt("damageamount");
		if (pAttackerBase && iDamage > 10 && pAttackerBase->GetFireRate() > 0.01f)
			bLethalStream = static_cast<float>(iDamage) / pAttackerBase->GetFireRate() / static_cast<float>(victimEnt->m_iHealth()) >= 1.f;
	}

	const bool bWorth = bUrgent || (nStrictLevel <= 2 && bLethalStream);
	const bool bChipPop = nStrictLevel <= 0;

	if (bPopsAllowed && CanPop(medigun, m_GoalResType))
	{
		if (m_GoalResType == MEDIGUN_BULLET_RESIST && !PlayerHasResUber(MEDIGUN_BULLET_RESIST, victimEnt) && (bWorth || (bSelfDamage && bChipPop)))
			m_ShouldPop = true;

		if (!m_ShouldPop && m_GoalResType == MEDIGUN_FIRE_RESIST && !PlayerHasResUber(MEDIGUN_FIRE_RESIST, victimEnt) && bWorth)
			m_ShouldPop = true;

		if (!m_ShouldPop && m_GoalResType == MEDIGUN_BLAST_RESIST && !PlayerHasResUber(MEDIGUN_BLAST_RESIST, victimEnt) && (bWorth || (bSelfDamage && bChipPop)))
			m_ShouldPop = true;
	}

	auto healTargetEnt = medigun->m_hHealingTarget().Get();
	if (healTargetEnt && victim == healTargetEnt && CFG::Triggerbot_AutoVaccinator_Pop == 1)
	{
		auto healTargetPlayer = healTargetEnt->As<C_TFPlayer>();
		if (healTargetPlayer && !healTargetPlayer->IsPlayerOnSteamFriendsList())
			m_ShouldPop = false;
	}
}

void CAutoVaccinator::PreventReload(CUserCmd* cmd)
{
	// Only block reload when we're actively changing resistance or have a pending goal
	if (!CFG::Triggerbot_AutoVaccinator_Active)
		return;

	// Allow manual mode changes when not actively switching
	if (!m_IsChangingRes && m_GoalResType == MEDIGUN_NUM_RESISTS)
		return;

	const auto weapon{ H::Entities->GetWeapon() };
	if (!weapon || weapon->GetWeaponID() != TF_WEAPON_MEDIGUN)
		return;

	const auto medigun{ weapon->As<C_WeaponMedigun>() };
	if (!medigun || medigun->GetChargeType() < 3)
		return;

	if (cmd->buttons & IN_RELOAD)
		cmd->buttons &= ~IN_RELOAD;
}
