#include "AutoUberPop.h"

#include "../CFG.h"
#include "../Players/Players.h"
#include "../Crits/Crits.h"
#include "../amalgam_port/AmalgamCompat.h"
#include "../amalgam_port/ProjectileSimulation/ProjectileSimulation.h"
#include "../amalgam_port/AimbotProjectile/AimbotProjectileAmalgam.h"

// Port of Amalgam's CAutoHeal::AutoVaccinator/GetDangers, retargeted to the
// stock medigun uber. Differences from the original:
// - the vaccinator's resist-type swapping is replaced by a single pop decision
//   on the worst danger bucket (the stock uber has one all-purpose charge)
// - Necromancer doesn't cache sniper dots, so the sniper branch scores zoomed
//   snipers at full headshot potential and unzoomed ones at bodyshot damage
//   instead of reading their charge dot
// - no vaccinator scale debug vars and no player_hurt DPS feed (vaccinator-only)

namespace
{
	// Amalgam Math::RemapVal (clamped)
	float RemapVal(float flVal, float a, float b, float c, float d)
	{
		if (a == b)
			return flVal >= b ? d : c;

		const float t = std::clamp((flVal - a) / (b - a), 0.f, 1.f);
		return c + (d - c) * t;
	}

	// Amalgam SDK::PredictOrigin
	Vec3 PredictOrigin(const Vec3& vOrigin, const Vec3& vVelocity, float flLatency, bool bTrace = true,
		const Vec3& vMins = {}, const Vec3& vMaxs = {}, unsigned int nMask = MASK_SOLID)
	{
		if (vVelocity.IsZero() || !flLatency)
			return vOrigin;

		Vec3 vOut = vOrigin + vVelocity * flLatency;
		if (!bTrace)
			return vOut;

		CGameTrace trace = {};
		CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
		SDK::TraceHull(vOrigin, vOut, vMins, vMaxs, nMask, &filter, &trace);
		return trace.endpos;
	}

	// Amalgam CTFPlayer::CanAttack (enemy context: bCloak = true, bLocal = false)
	bool CanAttack(C_TFPlayer* pPlayer)
	{
		if (!pPlayer || pPlayer->deadflag() || pPlayer->IsDormant()
			|| pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE) || pPlayer->InCond(TF_COND_TAUNTING)
			|| pPlayer->m_bViewingCYOAPDA() || pPlayer->InCond(TF_COND_PHASE)
			|| pPlayer->InCond(TF_COND_HALLOWEEN_KART)
			|| pPlayer->InCond(TF_COND_STUNNED) && pPlayer->m_iStunFlags() & (TF_STUN_CONTROLS | TF_STUN_LOSER_STATE))
			return false;

		// cloaked (dead ringer style) - only the grapple hook lets cloaked players attack
		if (pPlayer->m_flInvisibility() && (pPlayer->InCond(TF_COND_STEALTHED)
			|| !pPlayer->InCond(TF_COND_STEALTHED_USER_BUFF) && !pPlayer->InCond(TF_COND_STEALTHED_USER_BUFF_FADING)))
		{
			const auto pWpnEnt = pPlayer->m_hActiveWeapon().Get();
			const auto pWeapon = pWpnEnt ? pWpnEnt->As<C_TFWeaponBase>() : nullptr;
			if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_GRAPPLINGHOOK)
				return false;
		}

		if (pPlayer->m_bFeignDeathReady())
			return false;

		return true;
	}

	bool IsIgnoredIndex(int iIndex)
	{
		PlayerPriority priority{};
		return F::Players->GetInfo(iIndex, priority) && priority.Ignored;
	}

	bool IsCheaterIndex(int iIndex)
	{
		PlayerPriority priority{};
		return F::Players->GetInfo(iIndex, priority) && priority.Cheater;
	}

	bool TraceToEntity(C_TFPlayer* pPlayer, C_BaseEntity* pEntity, const Vec3& vPlayerOrigin, const Vec3& vEntityOrigin,
		bool bEntityIsPlayer, unsigned int nMask = MASK_SHOT | CONTENTS_GRATE)
	{
		CGameTrace trace = {};
		CTraceFilterWorldAndPropsOnlyAmalgam filter = {};

		if (bEntityIsPlayer)
		{
			Vec3 vMins = pPlayer->m_vecMins() + 1;
			Vec3 vMaxs = pPlayer->m_vecMaxs() - 1;
			Vec3 vMinsS = (vMins - vMaxs) / 2;
			Vec3 vMaxsS = (vMaxs - vMins) / 2;

			std::vector<Vec3> vPoints = {
				Vec3(),
				Vec3(vMinsS.x, vMinsS.y, vMaxsS.z),
				Vec3(vMaxsS.x, vMinsS.y, vMaxsS.z),
				Vec3(vMinsS.x, vMaxsS.y, vMaxsS.z),
				Vec3(vMaxsS.x, vMaxsS.y, vMaxsS.z),
				Vec3(vMinsS.x, vMinsS.y, vMinsS.z),
				Vec3(vMaxsS.x, vMinsS.y, vMinsS.z),
				Vec3(vMinsS.x, vMaxsS.y, vMinsS.z),
				Vec3(vMaxsS.x, vMaxsS.y, vMinsS.z)
			};
			for (auto& vPoint : vPoints)
			{
				SDK::Trace(vPlayerOrigin + vPoint, vEntityOrigin, nMask, &filter, &trace);
				if (trace.fraction == 1.f)
					return true;
			}
		}
		else
		{
			SDK::Trace(vPlayerOrigin, vEntityOrigin, nMask, &filter, &trace);
			return trace.fraction == 1.f;
		}

		return false;
	}

	int GetShotsWithinTime(int iWeaponID, float flFireRate, float flTime)
	{
		int iTicks = TIME_TO_TICKS(flTime);

		int iDelay = 1;
		switch (iWeaponID)
		{
		case TF_WEAPON_MINIGUN:
		case TF_WEAPON_PIPEBOMBLAUNCHER:
		case TF_WEAPON_CANNON:
			iDelay = 2;
		}

		return 1 + (iTicks - iDelay) / std::ceilf(flFireRate / TICK_INTERVAL);
	}

	// Amalgam CAutoHeal::GetDamage
	float GetDamage(C_BaseEntity* pProjectile, C_TFPlayer* pOwner, C_TFWeaponBase* pWeapon, C_TFPlayer* pTarget,
		float flMult, Vec3 vProjectileOrigin, int* pType = nullptr)
	{
		float flReturn = 100.f;
		switch (pProjectile->GetClassId())
		{
		case ETFClassIds::CTFGrenadePipebombProjectile:
			flReturn = 100.f;
			break;
		case ETFClassIds::CTFProjectile_Arrow:
		{
			const auto pArrow = pProjectile->As<C_TFProjectile_Arrow>();
			if (pArrow && (pArrow->m_iProjectileType() == TF_PROJECTILE_ARROW || pArrow->m_iProjectileType() == TF_PROJECTILE_FESTIVE_ARROW))
			{
				flReturn = RemapVal(pArrow->m_vInitialVelocity().Length(), 1800.f, 2600.f, 50.f, 120.f);
				if (pType)
					*pType = MEDIGUN_BULLET_RESIST;
			}
			else
			{
				flReturn = 40.f;
				if (pType)
					*pType = MEDIGUN_BULLET_RESIST;
			}
			break;
		}
		case ETFClassIds::CTFProjectile_HealingBolt:
			flReturn = RemapVal(pOwner->m_vecOrigin().DistTo(vProjectileOrigin), 200.f, 1600.f, 38.f, 75.f);
			if (pType)
				*pType = MEDIGUN_BULLET_RESIST;
			break;
		case ETFClassIds::CTFProjectile_Rocket:
		case ETFClassIds::CTFProjectile_EnergyBall:
			flReturn = flMult ? 90.f : RemapVal(pOwner->m_vecOrigin().DistTo(vProjectileOrigin), 500.f, 920.f, 90.f, 48.f);
			break;
		case ETFClassIds::CTFProjectile_SentryRocket:
			flReturn = flMult ? 100.f : RemapVal(pOwner->m_vecOrigin().DistTo(vProjectileOrigin), 100.f, 920.f, 100.f, 50.f);
			break;
		case ETFClassIds::CTFProjectile_BallOfFire:
			flReturn = pTarget->InCond(TF_COND_BURNING) || pTarget->InCond(TF_COND_BURNING_PYRO) ? 90.f : 30.f;
			if (pType)
				*pType = MEDIGUN_FIRE_RESIST;
			break;
		case ETFClassIds::CTFProjectile_Flare:
			flReturn = 30.f;
			if (pType)
				*pType = MEDIGUN_FIRE_RESIST;
			break;
		case ETFClassIds::CTFProjectile_EnergyRing:
			flReturn = 60.f;
			if (pType)
				*pType = MEDIGUN_BULLET_RESIST;
		}
		if (pWeapon)
			flReturn *= SDK::AttribHookValue(1, "mult_dmg", pWeapon);
		return flReturn;
	}

	// Amalgam CAutoHeal::GetMult
	float GetMult(C_BaseEntity* pProjectile, C_TFWeaponBase* pWeapon, C_TFPlayer* pTarget)
	{
		float flReturn = pTarget->IsMarked() ? 1.36f : 1.f;
		switch (pProjectile->GetClassId())
		{
		case ETFClassIds::CTFGrenadePipebombProjectile:
			if (pProjectile->As<C_TFWeaponBaseGrenadeProj>()->m_bCritical())
				flReturn = 3.f;
			else if (pProjectile->As<C_TFWeaponBaseGrenadeProj>()->m_iDeflected() && !pProjectile->GetAbsVelocity().IsZero())
				flReturn = 1.36f;
			break;
		case ETFClassIds::CTFProjectile_Arrow:
			if (pProjectile->As<C_TFProjectile_Arrow>()->m_iProjectileType() == TF_PROJECTILE_ARROW
				|| pProjectile->As<C_TFProjectile_Arrow>()->m_iProjectileType() == TF_PROJECTILE_FESTIVE_ARROW)
			{
				flReturn = 3.f;
				break;
			}
			[[fallthrough]];
		case ETFClassIds::CTFProjectile_HealingBolt:
			if (pProjectile->As<C_TFProjectile_Arrow>()->m_bCritical())
				flReturn = 3.f;
			else if (pProjectile->As<C_TFBaseRocket>()->m_iDeflected())
				flReturn = 1.36f;
			break;
		case ETFClassIds::CTFProjectile_Rocket:
		case ETFClassIds::CTFProjectile_BallOfFire:
		case ETFClassIds::CTFProjectile_SentryRocket:
			if (pProjectile->As<C_TFProjectile_Rocket>()->m_bCritical())
				flReturn = 3.f;
			else if (pProjectile->As<C_TFBaseRocket>()->m_iDeflected()
				|| pTarget->InCond(TF_COND_BLASTJUMPING) && pWeapon && SDK::AttribHookValue(0, "mini_crit_airborne", pWeapon) == 1)
				flReturn = 1.36f;
			break;
		case ETFClassIds::CTFProjectile_EnergyBall:
			if (pProjectile->As<C_TFProjectile_EnergyBall>()->m_bChargedShot())
				flReturn = 2.f; // whatever
			else if (pProjectile->As<C_TFBaseRocket>()->m_iDeflected())
				flReturn = 1.36f;
			break;
		case ETFClassIds::CTFProjectile_Flare:
			if (pProjectile->As<C_TFProjectile_Flare>()->m_bCritical() || pTarget->InCond(TF_COND_BURNING) || pTarget->InCond(TF_COND_BURNING_PYRO))
				flReturn = 3.f;
			else if (pProjectile->As<C_TFBaseRocket>()->m_iDeflected())
				flReturn = 1.36f;
		}
		return flReturn;
	}
}

void CAutoUberPop::UserMessage(bf_read& msgData)
{
	// VoiceSubtitle: [byte entity, byte voicemenu, byte command]
	const int iEntityID = msgData.ReadByte();
	const int iVoiceMenu = msgData.ReadByte();
	const int iCommandID = msgData.ReadByte();
	msgData.Seek(0);

	if (iVoiceMenu == 1 && iCommandID == 6) // "MEDIC!"
		m_mMedicCallers.insert(iEntityID);
}

void CAutoUberPop::Reset()
{
	m_mMedicCallers.clear();
	m_pLocal = nullptr;
	m_pWeapon = nullptr;
}

void CAutoUberPop::Run(CUserCmd* pCmd)
{
	if (!CFG::AutoUber_Active)
		return;

	if (!CFG::AutoUber_Always_On && !H::Input->IsDown(CFG::Triggerbot_Key))
		return;

	if (I::EngineVGui->IsGameUIVisible() || I::MatSystemSurface->IsCursorVisible() || SDKUtils::BInEndOfMatch())
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag() || pLocal->m_iClass() != TF_CLASS_MEDIC)
	{
		Reset();
		return;
	}

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_MEDIGUN)
	{
		Reset();
		return;
	}

	m_pLocal = pLocal;
	m_pWeapon = pWeapon->As<C_WeaponMedigun>();
	if (!m_pWeapon || m_pWeapon->GetChargeType() != MEDIGUN_CHARGE_INVULN)
	{
		m_pWeapon = nullptr;
		return;
	}

	// Amalgam ActivateOnVoice: release the charge when our heal target calls for medic
	const auto pHealEnt = m_pWeapon->m_hHealingTarget().Get();
	const auto pHealTarget = pHealEnt ? pHealEnt->As<C_TFPlayer>() : nullptr;
	const bool bHealTargetAllowed = !CFG::AutoUber_Friends_Only || !pHealTarget || pHealTarget->IsPlayerOnSteamFriendsList();

	if (CFG::AutoUber_Pop_On_Voice)
	{
		if (pHealTarget && bHealTargetAllowed && m_mMedicCallers.contains(pHealTarget->entindex()))
			pCmd->buttons |= IN_ATTACK2;
	}
	m_mMedicCallers.clear();

	// Amalgam: score the local medic and whoever we are healing
	float flBullet = 0.f, flBlast = 0.f, flFire = 0.f;
	GetDangers(pLocal, false, flBullet, flBlast, flFire);
	if (pHealTarget && bHealTargetAllowed)
		GetDangers(pHealTarget, false, flBullet, flBlast, flFire);

	// pop when the worst damage class expects to deal at least the target's
	// current health within our latency window (Amalgam pops at >= 1.f)
	if (std::max(flBullet, std::max(flBlast, flFire)) >= 1.f
		&& m_pWeapon->m_flChargeLevel() >= 1.f && !m_pWeapon->m_bChargeRelease())
		pCmd->buttons |= IN_ATTACK2;
}

void CAutoUberPop::ScoreDangers(C_TFPlayer* pLocal, C_WeaponMedigun* pWeapon, C_TFPlayer* pTarget, float& flBulletOut, float& flBlastOut, float& flFireOut)
{
	m_pLocal = pLocal;
	m_pWeapon = pWeapon;
	GetDangers(pTarget, true, flBulletOut, flBlastOut, flFireOut);
}

void CAutoUberPop::GetDangers(C_TFPlayer* pTarget, bool bVaccinator, float& flBulletOut, float& flBlastOut, float& flFireOut)
{
	if (!pTarget || !m_pLocal || !m_pWeapon || pTarget->IsInvulnerable())
		return;

	float flBulletDanger = 0.f, flBlastDanger = 0.f, flFireDanger = 0.f;
	float flBulletResist = pTarget->InCond(TF_COND_BULLET_IMMUNE) ? 1.f : pTarget->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) ? 0.75f : pTarget->InCond(TF_COND_MEDIGUN_SMALL_BULLET_RESIST) ? -0.1f : 0.f;
	float flBlastResist = pTarget->InCond(TF_COND_BLAST_IMMUNE) ? 1.f : pTarget->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST) ? 0.75f : pTarget->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST) ? -0.1f : 0.f;
	float flFireResist = pTarget->InCond(TF_COND_FIRE_IMMUNE) ? 1.f : pTarget->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) ? 0.75f : pTarget->InCond(TF_COND_MEDIGUN_SMALL_FIRE_RESIST) ? -0.1f : 0.f;

	float flLatency = SDKUtils::GetRealLatency();
	int iPlayerHealth = pTarget->m_iHealth();
	Vec3 vTargetOrigin = PredictOrigin(pTarget->m_vecOrigin(), pTarget->m_vecVelocity(),
		pTarget->entindex() != I::EngineClient->GetLocalPlayer() ? flLatency : 0.f, true, pTarget->m_vecMins(), pTarget->m_vecMaxs(), SolidMask(pTarget));
	Vec3 vTargetCenter = vTargetOrigin + (pTarget->m_vecMins() + pTarget->m_vecMaxs()) / 2;
	Vec3 vTargetEye = vTargetOrigin + pTarget->m_vecViewOffset();

	// enemy players with lethal-capable weapons
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		const auto pPlayer = pEntity ? pEntity->As<C_TFPlayer>() : nullptr;
		if (!pPlayer || pPlayer->IsDormant() || !CanAttack(pPlayer) || IsIgnoredIndex(pPlayer->entindex()))
			continue;

		const auto pActiveWpn = pPlayer->m_hActiveWeapon().Get();
		const auto pWeapon = pActiveWpn ? pActiveWpn->As<C_TFWeaponBase>() : nullptr;
		const int nWeaponID = pWeapon ? pWeapon->GetWeaponID() : 0;
		const auto eWeaponType = H::AimUtils->GetWeaponType(pWeapon);
		if (eWeaponType == EWeaponType::OTHER || eWeaponType == EWeaponType::MELEE // if we ever want melee, handle it
			|| nWeaponID == TF_WEAPON_DRG_POMSON || nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER
			|| nWeaponID == TF_WEAPON_MINIGUN && !pPlayer->InCond(TF_COND_AIMING))
			continue;

		Vec3 vPlayerOrigin = PredictOrigin(pPlayer->m_vecOrigin(), pPlayer->m_vecVelocity(), flLatency, true, pPlayer->m_vecMins(), pPlayer->m_vecMaxs(), SolidMask(pPlayer));
		Vec3 vPlayerEye = vPlayerOrigin + pPlayer->m_vecViewOffset();

		const bool bCheater = IsCheaterIndex(pPlayer->entindex());
		const bool bZoom = pPlayer->InCond(TF_COND_ZOOMED);
		const float flFOV = Math::CalcFov(pPlayer->GetEyeAngles(), Math::CalcAngle(vPlayerEye, bZoom ? vTargetEye : vTargetCenter));
		const bool bFOV = bCheater || flFOV < (bZoom ? 10.f : eWeaponType == EWeaponType::HITSCAN ? 30.f : 90.f);
		if (!bFOV || !TraceToEntity(pTarget, pPlayer, vTargetCenter, vPlayerEye, true))
			continue;

		const bool bCrits = pPlayer->IsCritBoosted() || F::CritHack->WeaponCanCrit(pWeapon);
		const bool bMinicrits = pPlayer->IsMiniCritBoosted() || pTarget->IsMarked();
		const float flDistance = vPlayerEye.DistTo(vTargetCenter);
		float flDamage, flMult = bCrits ? 3.f : bMinicrits ? 1.36f : 1.f;
		const float flFireRate = std::min(pWeapon->GetFireRate(), 1.f);
		const int iBulletCount = pWeapon->GetBulletsPerShot();
		const int iShotsWithinTime = GetShotsWithinTime(nWeaponID, flFireRate, flLatency + TICKS_TO_TIME(bCheater ? 22 : 0));
		switch (nWeaponID)
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		{
			// no sniper dots in Necromancer's entity cache: zoomed snipers are
			// scored at full headshot potential, unzoomed at bodyshot damage
			const bool bPiss = SDK::AttribHookValue(0, "jarate_duration", pWeapon) > 0;
			if (SDK::AttribHookValue(0, "sniper_only_fire_zoomed", pWeapon) && !bZoom)
				flDamage = 0.f;
			else if (nWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC || !bZoom)
				flDamage = SDK::AttribHookValue(50.f, "bodyshot_damage_modify", pWeapon);
			else
			{
				flDamage = 150.f;
				flMult = std::max(bPiss ? 1.36f : 3.f, flMult);
			}
			break;
		}
		case TF_WEAPON_COMPOUND_BOW:
			flDamage = 120.f, flMult = 3.f;
			break;
		case TF_WEAPON_FLAMETHROWER:
			flDamage = 80.f * flFireRate;
			break;
		default:
			flDamage = pWeapon->GetDamage();
		}

		switch (eWeaponType)
		{
		case EWeaponType::HITSCAN:
		{
			flMult = flBulletResist > 0.f ? 1.f - flBulletResist : flMult + flBulletResist;
			flDamage *= flMult * SDK::AttribHookValue(1, "mult_dmg", pWeapon);

			const float flSpread = std::clamp(pWeapon->GetWeaponSpread(), 0.001f, 1.f);
			const float flMappedCount = RemapVal(flDistance, 20 / flSpread, 100 / flSpread, iBulletCount, 1);
			const float flDamageInLatency = flDamage * flMappedCount * iShotsWithinTime;
			const float flDamageDanger = flDamageInLatency / iPlayerHealth;
			float flDistanceDanger = RemapVal(flDistance, 100, 100 / flSpread, 1.f, 0.001f);
			if (bCheater) // may use seed pred +/ crits
				flDistanceDanger = std::max(flDistanceDanger, bCrits ? 1.f : 0.34f);

			flBulletDanger += flDamageDanger * flDistanceDanger;
			break;
		}
		case EWeaponType::PROJECTILE:
		{
			ProjectileInfo tProjInfo = {};
			if (!F::AmalgamProjSim->GetInfo(pPlayer, pWeapon, {}, tProjInfo, ProjSimEnum::NoRandomAngles | ProjSimEnum::MaxSpeed))
				continue;

			int iType = MEDIGUN_BLAST_RESIST;
			switch (nWeaponID)
			{
			case TF_WEAPON_COMPOUND_BOW:
			case TF_WEAPON_CROSSBOW:
			case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
			case TF_WEAPON_SYRINGEGUN_MEDIC:
			case TF_WEAPON_RAYGUN: iType = MEDIGUN_BULLET_RESIST; break;
			case TF_WEAPON_FLAMETHROWER: // maybe just test full range?
			case TF_WEAPON_FLAME_BALL:
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE: iType = MEDIGUN_FIRE_RESIST; break;
			}

			switch (iType)
			{
			case MEDIGUN_BULLET_RESIST: flMult = flBulletResist > 0.f ? 1.f - flBulletResist : flMult + flBulletResist; break;
			case MEDIGUN_BLAST_RESIST: flMult = flBlastResist > 0.f ? 1.f - flBlastResist : flMult + flBlastResist; break;
			case MEDIGUN_FIRE_RESIST: flMult = flFireResist > 0.f ? 1.f - flFireResist : flMult + flFireResist; break;
			}
			flDamage *= flMult * SDK::AttribHookValue(1, "mult_dmg", pWeapon);

			const float flRadius = tProjInfo.m_flVelocity * flLatency + F::AmalgamAimbotProjectile->GetSplashRadius(pWeapon, pPlayer) + (pTarget->m_vecMaxs() - pTarget->m_vecMins()).Length() / 2;
			const float flDamageInLatency = flDamage * iBulletCount * iShotsWithinTime;
			const float flDamageDanger = flDamageInLatency / iPlayerHealth;
			const float flDistanceDanger = RemapVal(flDistance, flRadius, flRadius, 1.f, 0.001f);

			switch (iType)
			{
			case MEDIGUN_BULLET_RESIST: flBulletDanger += flDamageDanger * flDistanceDanger; break;
			case MEDIGUN_BLAST_RESIST: flBlastDanger += flDamageDanger * flDistanceDanger; break;
			case MEDIGUN_FIRE_RESIST: flFireDanger += flDamageDanger * flDistanceDanger; break;
			}
		}
		}
	}

	// sentries currently shooting at the target
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
	{
		const auto pSentry = pEntity ? pEntity->As<C_ObjectSentrygun>() : nullptr;
		if (!pSentry || pSentry->GetClassId() != ETFClassIds::CObjectSentrygun)
			continue;

		if (pSentry->m_hEnemy().Get() != pTarget && pSentry->m_hAutoAimTarget().Get() != pTarget || !pSentry->m_iAmmoShells())
			continue;

		const float flDistance = pSentry->GetCenter().DistTo(vTargetCenter);
		float flDamage = 19.f, flMult = pTarget->IsMarked() ? 1.36f : 1.f;
		float flFireRate;
		switch (pSentry->m_bMiniBuilding() ? 0 : pSentry->m_iUpgradeLevel())
		{
		case 0:
			flDamage /= 2;
			flFireRate = pSentry->m_bPlayerControlled() ? 0.09f : 0.18f;
			break;
		case 1:
			flFireRate = pSentry->m_bPlayerControlled() ? 0.135f : 0.225f;
			break;
		default:
			flFireRate = pSentry->m_bPlayerControlled() ? 0.09f : 0.135f;
		}
		const int iShotsWithinTime = GetShotsWithinTime(0, flFireRate, 1.f /*flLatency*/);
		flMult = flBulletResist > 0.f ? 1.f - flBulletResist : flMult + flBulletResist;
		flDamage *= flMult;

		const float flDamageInLatency = flDamage * iShotsWithinTime;
		const float flDamageDanger = flDamageInLatency / iPlayerHealth;
		const float flDistanceDanger = RemapVal(flDistance, 1100.f, 3200.f, 1.f, 0.001f);

		flBulletDanger += flDamageDanger * flDistanceDanger;
	}

	// enemy projectiles in flight
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
	{
		C_TFPlayer* pOwner = nullptr;
		C_TFWeaponBase* pWeapon = nullptr;

		switch (pEntity->GetClassId())
		{
		case ETFClassIds::CTFGrenadePipebombProjectile:
		{
			const auto pPipe = pEntity->As<C_TFGrenadePipebombProjectile>();
			const auto pLauncher = pPipe->m_hOriginalLauncher().Get();
			const auto pThrower = pPipe->m_hThrower().Get();
			pWeapon = pLauncher ? pLauncher->As<C_TFWeaponBase>() : nullptr;
			pOwner = pThrower ? pThrower->As<C_TFPlayer>() : nullptr;
			break;
		}
		case ETFClassIds::CTFProjectile_Arrow:
		case ETFClassIds::CTFProjectile_HealingBolt:
		case ETFClassIds::CTFProjectile_Rocket:
		case ETFClassIds::CTFProjectile_SentryRocket:
		case ETFClassIds::CTFProjectile_BallOfFire:
		case ETFClassIds::CTFProjectile_EnergyBall:
		case ETFClassIds::CTFProjectile_Flare:
		{
			const auto pRocket = pEntity->As<C_TFBaseRocket>();
			const auto pLauncher = pRocket ? pRocket->m_hLauncher().Get() : nullptr;
			pWeapon = pLauncher ? pLauncher->As<C_TFWeaponBase>() : nullptr;
			const auto pWpnOwner = pWeapon && pWeapon->m_hOwnerEntity().Get() ? pWeapon->m_hOwnerEntity().Get()->As<C_TFPlayer>() : nullptr;
			pOwner = pWpnOwner;
			break;
		}
		case ETFClassIds::CTFProjectile_EnergyRing:
		{
			const auto pBase = pEntity->As<C_TFBaseProjectile>();
			const auto pLauncher = pBase ? pBase->m_hLauncher().Get() : nullptr;
			pWeapon = pLauncher ? pLauncher->As<C_TFWeaponBase>() : nullptr;
			const auto pWpnOwner = pWeapon && pWeapon->m_hOwnerEntity().Get() ? pWeapon->m_hOwnerEntity().Get()->As<C_TFPlayer>() : nullptr;
			pOwner = pWpnOwner;
			break;
		}
		}
		if (!pOwner || IsIgnoredIndex(pOwner->entindex())
			|| pWeapon && !pWeapon->GetDamage())
			continue;

		const Vec3 vVelocity = F::AmalgamProjSim->GetVelocity(pEntity);
		float flRadius = F::AmalgamAimbotProjectile->GetSplashRadius(pEntity, pWeapon, pOwner);

		const Vec3 vProjectileOrigin = PredictOrigin(pEntity->m_vecOrigin(), vVelocity, flLatency, true, pEntity->m_vecMins(), pEntity->m_vecMaxs());
		if (!TraceToEntity(pTarget, pEntity, vTargetEye, vProjectileOrigin, false, MASK_SHOT))
			continue;

		int iType = MEDIGUN_BLAST_RESIST;
		float flMult = GetMult(pEntity, pWeapon, pTarget);
		float flDamage = GetDamage(pEntity, pOwner, pWeapon, pTarget, flMult, vProjectileOrigin, &iType);
		switch (iType)
		{
		case MEDIGUN_BULLET_RESIST: flMult = flBulletResist > 0.f ? 1.f - flBulletResist : flMult + flBulletResist; break;
		case MEDIGUN_BLAST_RESIST: flMult = flBlastResist > 0.f ? 1.f - flBlastResist : flMult + flBlastResist; break;
		case MEDIGUN_FIRE_RESIST: flMult = flFireResist > 0.f ? 1.f - flFireResist : flMult + flFireResist; break;
		}
		flDamage *= flMult;

		flRadius += vVelocity.Length() * flLatency + (pTarget->m_vecMaxs() - pTarget->m_vecMins()).Length() / 2;
		const float flDamageDanger = flDamage / iPlayerHealth;
		const float flDistanceDanger = RemapVal(pTarget->m_vecOrigin().DistTo(vProjectileOrigin), flRadius, flRadius, 1.f, 0.001f);

		switch (iType)
		{
		case MEDIGUN_BULLET_RESIST:
			flBulletDanger += flDamageDanger * flDistanceDanger;
			break;
		case MEDIGUN_BLAST_RESIST:
			flBlastDanger += flDamageDanger * flDistanceDanger;
			break;
		case MEDIGUN_FIRE_RESIST:
			flFireDanger += flDamageDanger * flDistanceDanger;
		}
	}

	// ignore damage classes the target is already immune to; the vaccinator's
	// own resist uber already covers its type while it is active
	if (flBulletResist >= (bVaccinator ? 0.75f : 1.f))
		flBulletDanger = 0.f;
	if (flBlastResist >= (bVaccinator ? 0.75f : 1.f))
		flBlastDanger = 0.f;
	if (flFireResist >= (bVaccinator ? 0.75f : 1.f))
		flFireDanger = 0.f;

	flBulletOut = std::max(flBulletOut, flBulletDanger);
	flBlastOut = std::max(flBlastOut, flBlastDanger);
	flFireOut = std::max(flFireOut, flFireDanger);
}
