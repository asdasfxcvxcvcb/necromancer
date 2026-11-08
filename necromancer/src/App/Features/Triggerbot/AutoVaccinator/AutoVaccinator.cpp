#include "AutoVaccinator.h"

#include "../../AutoUberPop/AutoUberPop.h"
#include "../../amalgam_port/MoveSim/MoveSimAmalgam.h"
#include "../../CFG.h"

#include <cfloat>

constexpr float HEALTH_LIMIT = 0.85f;
constexpr float VACC_SELF_POP_MIN = 0.45f;
constexpr float VACC_CHANGE_WATCHDOG = 2.0f;

namespace
{
	constexpr int VACC_SIM_TICKS = 24;
	constexpr int VACC_PROJ_TICKS = 33;
	constexpr float VACC_PROJ_GRAVITY = 0.4f;
	constexpr float VACC_SNIPER_AIM_RADIUS = 40.f;

	// predicted positions of a player for danger checks; falls back to the
	// current origin on any sim failure so behavior degrades to the old checks
	void GetDangerPath(C_TFPlayer* pPlayer, std::vector<Vec3>& vecPath)
	{
		vecPath.clear();
		if (!pPlayer || pPlayer->deadflag() || pPlayer->IsDormant())
			return;
		vecPath.push_back(pPlayer->m_vecOrigin());

		AmalgamMoveStorage tStorage;
		if (!F::MoveSim->Initialize(pPlayer, tStorage, false, true, false))
		{
			F::MoveSim->Restore(tStorage);
			return;
		}
		for (int i = 0; i < VACC_SIM_TICKS; i++)
			F::MoveSim->RunTick(tStorage, true);
		F::MoveSim->Restore(tStorage);

		for (const auto& v : tStorage.m_vPath)
		{
			if ((v - vecPath.back()).Length2D() > 16.f)
				vecPath.push_back(v);
			if (vecPath.size() >= 4)
				break;
		}
	}

	// closest the projectile gets to any protected position within its
	// integrated flight window (straight for rockets, gravity arc for the
	// rest). A world trace from the projectile to its flight end caps the
	// window where the projectile slams into a wall, so arcs can no longer
	// score through geometry
	float ProjectileClosestApproach(C_BaseEntity* pEntity, const std::vector<Vec3>& vecPath, int nTicks)
	{
		if (vecPath.empty())
			return FLT_MAX;

		Vec3 vel{};
		pEntity->EstimateAbsVelocity(vel);
		if (vel.IsZero())
			return FLT_MAX;

		const auto eClassId = pEntity->GetClassId();
		const bool bArcs = eClassId != ETFClassIds::CTFProjectile_Rocket
			&& eClassId != ETFClassIds::CTFProjectile_SentryRocket
			&& eClassId != ETFClassIds::CTFProjectile_EnergyBall;

		const float flGravity = SDKUtils::GetGravity() * VACC_PROJ_GRAVITY;

		auto Integrate = [&](int iTicks) {
			Vec3 vPos = pEntity->m_vecOrigin();
			Vec3 vVel = vel;
			float flMinDist = FLT_MAX;
			for (int i = 0; i < iTicks; i++)
			{
				if (bArcs)
					vVel.z -= flGravity * TICK_INTERVAL;
				vPos += vVel * TICK_INTERVAL;

				for (const auto& vTarget : vecPath)
					flMinDist = std::min(flMinDist, vPos.DistTo(vTarget));

				if (vVel.Length2D() < 50.f && flMinDist > 250.f)
					break;
				if (vPos.DistTo(vecPath.front()) > 1200.f && flMinDist > 250.f)
					break;
			}
			return std::pair(flMinDist, vPos);
		};

		const auto [flFirst, vEnd] = Integrate(nTicks);

		CGameTrace impactTrace = {};
		CTraceFilterWorldAndPropsOnly impactFilter = {};
		SDK::Trace(pEntity->m_vecOrigin(), vEnd, MASK_SOLID, &impactFilter, &impactTrace);
		if (impactTrace.fraction < 1.0f)
		{
			const int nCapped = std::max(1, static_cast<int>(nTicks * impactTrace.fraction));
			if (nCapped < nTicks)
				return Integrate(nCapped).first;
		}
		return flFirst;
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

bool IsPlayerInDanger(C_TFPlayer* player, medigun_resist_types_t& dangerType, const std::vector<Vec3>& vecPath)
{
	if (!player || player->IsInvulnerable())
		return false;

	const auto percentHealth{
		Math::RemapValClamped
		(
			static_cast<float>(player->m_iHealth()), 0.0f, static_cast<float>(player->GetMaxHealth()), 0.0f, 1.0f
		)
	};

	// Check for dangerous players
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		if (!pEntity)
			continue;

		const auto enemy{ pEntity->As<C_TFPlayer>() };
		if (!enemy || enemy->deadflag())
			continue;

		if (enemy->m_iClass() != TF_CLASS_SNIPER)
			continue;

		const auto weapon{ enemy->m_hActiveWeapon().Get()->As<C_TFWeaponBase>() };
		if (!weapon || weapon->GetSlot() != WEAPON_SLOT_PRIMARY || weapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW)
			continue;

		bool zoomed{ enemy->InCond(TF_COND_ZOOMED) };
		if (weapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC)
			zoomed = weapon->As<C_TFSniperRifleClassic>()->m_bCharging();

		if (!enemy->IsCritBoosted() && !enemy->IsMiniCritBoosted() && !zoomed)
			continue;

		auto mins{ player->m_vecMins() };
		auto maxs{ player->m_vecMaxs() };
		mins.x *= 3.0f; mins.y *= 3.0f; mins.z *= 1.5f;
		maxs.x *= 3.0f; maxs.y *= 3.0f; maxs.z *= 1.5f;

		Vec3 forward{};
		Math::AngleVectors(enemy->GetEyeAngles(), &forward);

		bool bAimReach = Math::RayToOBB(enemy->GetShootPos(), forward, player->m_vecOrigin(), mins, maxs, player->RenderableToWorldTransform());
		bool bSightline = false;
		if (bAimReach)
		{
			const auto visibleFromCenter{ H::AimUtils->TraceEntityAutoDet(pEntity, player->GetCenter(), enemy->m_vecOrigin() + Vec3{ 0.0f, 0.0f, enemy->m_vecMaxs().z }) };
			// only the OR of the two rays matters - skip the head ray when the
			// center ray already proved visibility
			const auto visibleFromHead{ visibleFromCenter || H::AimUtils->TraceEntityAutoDet(pEntity, player->m_vecOrigin() + Vec3{ 0.0f, 0.0f, player->m_vecMaxs().z }, enemy->m_vecOrigin() + Vec3{ 0.0f, 0.0f, enemy->m_vecMaxs().z }) };
			bSightline = visibleFromCenter || visibleFromHead;
		}

		// the pop must land before the shot: the aim ray and the sightline
		// against where the player is about to be count on their own, so a
		// fully-covered player about to peek still trips the danger
		if (!bAimReach || !bSightline)
		{
			const Vec3 vEye = enemy->m_vecOrigin() + Vec3{ 0.0f, 0.0f, enemy->m_vecMaxs().z };
			for (const auto& vPos : vecPath)
			{
				const Vec3 vCenter = vPos + Vec3{ 0.0f, 0.0f, (mins.z + maxs.z) * 0.25f };
				const Vec3 vDelta = vCenter - enemy->GetShootPos();
				const float flAlong = vDelta.Dot(forward);
				if (flAlong <= 0.f)
					continue;
				if ((enemy->GetShootPos() + forward * flAlong).DistTo(vCenter) > VACC_SNIPER_AIM_RADIUS)
					continue;
				if (!H::AimUtils->TraceEntityAutoDet(pEntity, vCenter, vEye))
					continue;
				bAimReach = true;
				bSightline = true;
				break;
			}
		}

		if (!bAimReach || !bSightline)
			continue;

		dangerType = MEDIGUN_BULLET_RESIST;
		return true;
	}

	size_t numClosePipebombs{};

	// Check for dangerous projectiles
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
	{
		if (!pEntity)
			continue;

		Vec3 vel{};
		pEntity->EstimateAbsVelocity(vel);
		if (pEntity->m_vecOrigin().DistTo(player->GetCenter()) > 2000.0f)
			continue;

		// arc-aware closest approach vs current + predicted positions
		// (replaces the linear latency extrapolation - pipes/arrows/flares
		// drop, rockets fly straight); window extended by our ping so the
		// pop command lands on the server before the damage does
		const int nProjTicks = std::clamp(VACC_PROJ_TICKS + TIME_TO_TICKS(SDKUtils::GetRealLatency()), VACC_PROJ_TICKS, VACC_PROJ_TICKS * 2);
		const float flMinDist = ProjectileClosestApproach(pEntity, vecPath, nProjTicks);
		if (flMinDist > 250.0f)
			continue;

		// only the OR of the two rays matters - skip the head ray when the
		// center ray already proved visibility
		const auto visibleFromCenter{ H::AimUtils->TraceEntityAutoDet(pEntity, player->GetCenter(), pEntity->GetCenter()) };
		const auto visibleFromHead{ visibleFromCenter || H::AimUtils->TraceEntityAutoDet(pEntity, player->m_vecOrigin() + Vec3{ 0.0f, 0.0f, player->m_vecMaxs().z }, pEntity->GetCenter()) };

		if (!visibleFromCenter && !visibleFromHead)
			continue;

		switch (pEntity->GetClassId())
		{
		case ETFClassIds::CTFProjectile_Arrow:
			if (vel.IsZero() || flMinDist > 150.0f)
				continue;
			if (const auto arrow{ pEntity->As<C_TFProjectile_Arrow>() }; !arrow->m_bCritical() && percentHealth >= 1.0f)
				continue;
			dangerType = MEDIGUN_BULLET_RESIST;
			return true;

		case ETFClassIds::CTFProjectile_HealingBolt:
			if (vel.IsZero() || flMinDist > 150.0f)
				continue;
			if (const auto arrow{ pEntity->As<C_TFProjectile_Arrow>() }; !arrow->m_bCritical() && percentHealth >= HEALTH_LIMIT)
				continue;
			dangerType = MEDIGUN_BULLET_RESIST;
			return true;

		case ETFClassIds::CTFProjectile_Rocket:
		case ETFClassIds::CTFProjectile_SentryRocket:
		case ETFClassIds::CTFProjectile_EnergyBall:
			if (flMinDist > 250.0f)
				continue;
			if (const auto rocket{ pEntity->As<C_TFProjectile_Rocket>() }; !rocket->m_bCritical() && percentHealth >= 1.0f)
				continue;
			dangerType = MEDIGUN_BLAST_RESIST;
			return true;

		case ETFClassIds::CTFGrenadePipebombProjectile:
			if (flMinDist > 250.0f)
				continue;
			if (const auto bomb{ pEntity->As<C_TFGrenadePipebombProjectile>() }; bomb->m_iType() == TF_GL_MODE_REMOTE_DETONATE_PRACTICE)
				continue;
			if (const auto bomb{ pEntity->As<C_TFGrenadePipebombProjectile>() }; !bomb->m_bCritical() && percentHealth >= 1.0f && flMinDist >= 100.0f)
			{
				numClosePipebombs++;
				continue;
			}
			dangerType = MEDIGUN_BLAST_RESIST;
			return true;

		case ETFClassIds::CTFProjectile_Flare:
			if (flMinDist > 150.0f)
				continue;
			if (const auto flare{ pEntity->As<C_TFProjectile_Flare>() }; !flare->m_bCritical() && !player->InCond(TF_COND_BURNING) && !player->InCond(TF_COND_BURNING_PYRO))
				continue;
			dangerType = MEDIGUN_FIRE_RESIST;
			return true;

		case ETFClassIds::CTFProjectile_BallOfFire:
			if (flMinDist > 150.0f)
				continue;
			if (!player->InCond(TF_COND_BURNING) && !player->InCond(TF_COND_BURNING_PYRO))
				continue;
			dangerType = MEDIGUN_FIRE_RESIST;
			return true;

		case ETFClassIds::CTFProjectile_EnergyRing:
			if (flMinDist > 150.0f)
				continue;
			if (percentHealth >= HEALTH_LIMIT)
				continue;
			dangerType = MEDIGUN_BULLET_RESIST;
			return true;

		default: {}
		}
	}

	if (numClosePipebombs > 1)
	{
		dangerType = MEDIGUN_BLAST_RESIST;
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

		dangerType = MEDIGUN_BULLET_RESIST;
		if (percentHealth < HEALTH_LIMIT)
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
	m_bKeyPopQueued = false;
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
	return m_flLastPopTime[type] <= 0.f || flNow - m_flLastPopTime[type] >= 0.75f;
}

float CAutoVaccinator::GetLastPopTime() const
{
	float flLast = 0.f;
	for (int i = 0; i < MEDIGUN_NUM_RESISTS; i++)
	{
		if (m_flLastPopTime[i] > flLast)
			flLast = m_flLastPopTime[i];
	}
	return flLast;
}

void CAutoVaccinator::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* cmd)
{
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

	if (Shifting::bRecharging)
		return;

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

	const bool bTriggerbotKeyPressed = CFG::Triggerbot_Key && H::Input->IsDown(CFG::Triggerbot_Key);
	const bool bNoPop = CFG::Triggerbot_AutoVaccinator_NoPop && !bTriggerbotKeyPressed;

	if (bTriggerbotKeyPressed && H::Input->IsPressed(CFG::Triggerbot_Key))
		m_bKeyPopQueued = true;
	if (m_bKeyPopQueued && bNoPop)
		m_bKeyPopQueued = false;

	// 1. latched pop execution - fires only when the server's actual resist
	// already matches the goal, never mid-cycle on the wrong type
	if (m_ShouldPop)
	{
		if (nActual == m_GoalResType)
		{
			if (medigun->m_flChargeLevel() >= 0.25f)
			{
				cmd->buttons |= IN_ATTACK2;
				m_flLastPopTime[m_GoalResType] = I::GlobalVars->curtime;
			}
			m_ShouldPop = false;
			m_bKeyPopQueued = false;
			m_GoalResType = nActual;
			return;
		}
		// else: keep latched, the cycle block below brings the resist over
	}
	// 2. queued key-pop owns the goal until it fires
	else if (m_bKeyPopQueued && !bNoPop)
	{
		medigun_resist_types_t popType = (m_GoalResType >= MEDIGUN_BULLET_RESIST && m_GoalResType < MEDIGUN_NUM_RESISTS)
			? static_cast<medigun_resist_types_t>(m_GoalResType)
			: static_cast<medigun_resist_types_t>(nActual);

		const auto healTargetEntQ = medigun->m_hHealingTarget().Get();
		C_TFPlayer* healTargetQ = healTargetEntQ ? healTargetEntQ->As<C_TFPlayer>() : nullptr;
		const bool bHealAllowed = healTargetQ && (CFG::Triggerbot_AutoVaccinator_Pop != 1 || healTargetQ->IsPlayerOnSteamFriendsList());
		const bool bSelfHas = PlayerHasResUber(popType, pLocal);
		const bool bAllCovered = bHealAllowed ? (bSelfHas && PlayerHasResUber(popType, healTargetQ)) : bSelfHas;
		if (bAllCovered && nActual == popType)
			popType = static_cast<medigun_resist_types_t>((popType + 1) % MEDIGUN_NUM_RESISTS);

		m_GoalResType = popType;

		if (nActual == popType && CanPop(medigun, popType))
		{
			if (medigun->m_flChargeLevel() >= 0.25f)
			{
				cmd->buttons |= IN_ATTACK2;
				m_flLastPopTime[popType] = I::GlobalVars->curtime;
			}
			m_bKeyPopQueued = false;
			return;
		}
	}
	// 3. danger decision - only with nothing latched and not already cycling
	// (hysteresis: a mid-cycle goal flip-flop is what makes the vacc churn),
	// so a flickering threat can never stomp a queued or latched pop either
	else if (!m_IsChangingRes)
	{
		if (m_bKeyPopQueued)
			m_bKeyPopQueued = false;

		const auto healTargetEnt = medigun->m_hHealingTarget().Get();
		C_TFPlayer* healTarget = healTargetEnt ? healTargetEnt->As<C_TFPlayer>() : nullptr;

		static int s_nLastCmd = 0;
		static std::vector<Vec3> s_vecSelfPath;
		static std::vector<Vec3> s_vecHealPath;
		static int s_iHealIdx = -1;
		if (cmd->command_number != s_nLastCmd)
		{
			s_nLastCmd = cmd->command_number;
			GetDangerPath(pLocal, s_vecSelfPath);
			s_iHealIdx = healTarget && healTarget != pLocal ? healTarget->entindex() : -1;
			if (s_iHealIdx >= 0)
				GetDangerPath(healTarget, s_vecHealPath);
		}
		const std::vector<Vec3>& vecSelfPath = s_vecSelfPath;
		const std::vector<Vec3> vecEmptyPath;
		const std::vector<Vec3>& vecHealPath = s_iHealIdx >= 0 ? s_vecHealPath : vecEmptyPath;

		medigun_resist_types_t panicType{ MEDIGUN_NUM_RESISTS };
		bool bPanicTargetIsHeal = false;
		if (healTarget && IsPlayerInDanger(healTarget, panicType, healTarget != pLocal ? vecHealPath : vecSelfPath) && !PlayerHasResUber(panicType, healTarget))
			bPanicTargetIsHeal = true;
		else if (IsPlayerInDanger(pLocal, panicType, vecSelfPath) && !PlayerHasResUber(panicType, pLocal))
			bPanicTargetIsHeal = false;
		else
			panicType = MEDIGUN_NUM_RESISTS;

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

			medigun_resist_types_t selfType = MEDIGUN_NUM_RESISTS;
			float flSelfMax = 0.f;
			for (int i = 0; i < MEDIGUN_NUM_RESISTS; i++)
			{
				const auto eType = static_cast<medigun_resist_types_t>(i);
				if (flSelfScore[i] > 0.f && !PlayerHasResUber(eType, pLocal) && flSelfScore[i] > flSelfMax)
				{
					flSelfMax = flSelfScore[i];
					selfType = eType;
				}
			}

			if (flMaxScore >= 1.f)
			{
				m_GoalResType = scoredType;
				if (!bNoPop && CanPop(medigun, scoredType))
					m_ShouldPop = true;
			}
			else if (selfType != MEDIGUN_NUM_RESISTS)
			{
				m_GoalResType = selfType;
				// human judgment: a nonzero score is not a threat - only pop
				// when the damage at risk is a real chunk of our health
				// (one midrange scout vs full HP scores ~0.2-0.35, in your
				// face ~0.6-1.2; dj's heavy+scout case must NOT pop)
				if (flSelfMax >= VACC_SELF_POP_MIN && !bNoPop && CanPop(medigun, selfType))
					m_ShouldPop = true;
			}
			else if (flMaxScore > 0.f)
			{
				const float flGoalScore = m_GoalResType >= MEDIGUN_BULLET_RESIST && m_GoalResType < MEDIGUN_NUM_RESISTS ? flScore[m_GoalResType] : 0.f;
				if (flMaxScore > flGoalScore * 1.25f || m_GoalResType == MEDIGUN_NUM_RESISTS)
					m_GoalResType = scoredType;
			}
			else
				m_GoalResType = MEDIGUN_BULLET_RESIST;
		}

		// the decision landed a pop with the right resist already active
		if (m_ShouldPop && nActual == m_GoalResType)
		{
			if (medigun->m_flChargeLevel() >= 0.25f)
			{
				cmd->buttons |= IN_ATTACK2;
				m_flLastPopTime[m_GoalResType] = I::GlobalVars->curtime;
			}
			m_ShouldPop = false;
			m_bKeyPopQueued = false;
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

	if (m_ShouldPop || m_bKeyPopQueued)
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

	const auto percentHealth{
		Math::RemapValClamped(static_cast<float>(health), 0.0f, static_cast<float>(victimEnt->GetMaxHealth()), 0.0f, 1.0f)
	};

	m_GoalResType = WeaponIDToResType(weaponID);

	const bool bTriggerbotKeyPressed = CFG::Triggerbot_Key && H::Input->IsDown(CFG::Triggerbot_Key);
	const bool bNoPop = CFG::Triggerbot_AutoVaccinator_NoPop && !bTriggerbotKeyPressed;

	const bool bSelfDamage = victimEnt == pLocal;
	const bool bPopsAllowed = !bNoPop;
	const bool bUrgent = event->GetBool("crit") || event->GetBool("minicrit") || percentHealth < HEALTH_LIMIT;

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

	if (bPopsAllowed && CanPop(medigun, m_GoalResType))
	{
		const bool bWorth = bUrgent || bLethalStream;
		if (m_GoalResType == MEDIGUN_BULLET_RESIST && !PlayerHasResUber(MEDIGUN_BULLET_RESIST, victimEnt) && (bSelfDamage || bWorth))
			m_ShouldPop = true;

		if (!m_ShouldPop && m_GoalResType == MEDIGUN_FIRE_RESIST && !PlayerHasResUber(MEDIGUN_FIRE_RESIST, victimEnt) && bWorth)
			m_ShouldPop = true;

		if (!m_ShouldPop && m_GoalResType == MEDIGUN_BLAST_RESIST && !PlayerHasResUber(MEDIGUN_BLAST_RESIST, victimEnt) && (bSelfDamage || bWorth))
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
