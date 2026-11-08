#include "AimbotMeleeAmalgam.h"

#include "../Ticks/Ticks.h"
#include "../../FakeLag/FakeLag.h"

#include <algorithm>
#include <cmath>

namespace
{
	// Amalgam PLAYER_ORIGIN_COMPRESSION (CTFPlayer.h): networked origins are
	// quantized to 1/8 units, so a record origin can sit up to half that off
	// the server's real one. Amalgam shrinks record hulls by the full value
	// and compensates the backstab dot thresholds by half of it.
	const Vec3 PLAYER_ORIGIN_COMPRESSION = { 0.125f, 0.125f, 0.125f };

	// Amalgam's GetSwingTime: the knife has no swing delay; otherwise the
	// swing prediction horizon (Amalgam's NOSAVE SwingTicks debug var, 13
	// default) or the weapon's real smack delay in ticks.
	int GetSwingTime(C_TFWeaponBase* pWeapon, bool bVar = true)
	{
		return pWeapon->GetWeaponID() == TF_WEAPON_KNIFE ? 0
			: bVar ? Vars::Aimbot::Melee::SwingTicks.Value
			: static_cast<int>(ceilf(pWeapon->GetSmackDelay() / I::GlobalVars->interval_per_tick));
	}

	Vec3 VecClamp(const Vec3& v, const Vec3& vMin, const Vec3& vMax)
	{
		return { std::clamp(v.x, vMin.x, vMax.x), std::clamp(v.y, vMin.y, vMax.y), std::clamp(v.z, vMin.z, vMax.z) };
	}

	// Amalgam AimFriendlyBuilding: a friendly building is worth a swing when it
	// can be upgraded, needs repair, is sapped, or is a sentry missing ammo.
	bool AimFriendlyBuilding(C_BaseObject* pBuilding)
	{
		if (!pBuilding->m_bMiniBuilding() && pBuilding->m_iUpgradeLevel() != 3 || pBuilding->m_iHealth() < pBuilding->m_iMaxHealth() || pBuilding->m_bHasSapper())
			return true;

		if (pBuilding->GetClassId() == ETFClassIds::CObjectSentrygun)
		{
			auto pSentry = pBuilding->As<C_ObjectSentrygun>();
			// Necromancer has no GetAmmoCount; the native melee hardcodes the
			// same ammo limits (150/200 shells, 20 rockets on level 3).
			const int iMaxShells = pSentry->m_iUpgradeLevel() == 1 ? 150 : 200;
			if (pSentry->m_iAmmoShells() < iMaxShells || pSentry->m_iUpgradeLevel() == 3 && pSentry->m_iAmmoRockets() < 20)
				return true;
		}

		return false;
	}

	// Amalgam PlayerBoneInFOV scans every hitbox for the smallest FOV; the body
	// center is close enough for this eligibility gate (CanHit re-aims at every
	// record anyway).
	bool PlayerInFOV(C_TFPlayer* pTarget, const Vec3& vLocalPos, const Vec3& vLocalAngles, float flAimFOV, float& flFOVTo, Vec3& vPos, Vec3& vAngleTo)
	{
		vPos = pTarget->GetCenter();
		vAngleTo = Math::CalcAngle(vLocalPos, vPos);
		flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
		return flFOVTo < flAimFOV;
	}

	// Amalgam EntityCenterInFOV.
	bool EntityCenterInFOV(C_BaseEntity* pTarget, const Vec3& vLocalPos, const Vec3& vLocalAngles, float flAimFOV, float& flFOVTo, Vec3& vPos, Vec3& vAngleTo)
	{
		vPos = pTarget->GetCenter();
		vAngleTo = Math::CalcAngle(vLocalPos, vPos);
		flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
		return flFOVTo < flAimFOV;
	}

	Target_t MakeTarget(C_BaseEntity* pEntity, int iType, const Vec3& vPos, const Vec3& vAngleTo, float flFOVTo, float flDistTo, int iPriority = 0)
	{
		Target_t tTarget = {};
		tTarget.m_pEntity = pEntity;
		tTarget.m_iTargetType = iType;
		tTarget.m_vPos = vPos;
		tTarget.m_vAngleTo = vAngleTo;
		tTarget.m_flFOVTo = flFOVTo;
		tTarget.m_flDistTo = flDistTo;
		tTarget.m_nPriority = iPriority;
		return tTarget;
	}

	// Check if player has an active Razorback equipped (same logic as the
	// AutoBackstab triggerbot: the wearable blocks one backstab and goes
	// invisible until it recharges).
	bool HasActiveRazorback(C_TFPlayer* pPlayer)
	{
		if (!pPlayer)
			return false;

		const int iBackstabShield = static_cast<int>(SDKUtils::AttribHookValue(0.f, "set_blockbackstab_once", pPlayer));
		if (iBackstabShield <= 0)
			return false;

		for (int i = 0; i < I::ClientEntityList->GetHighestEntityIndex(); i++)
		{
			auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pClientEntity || pClientEntity->GetClassId() != ETFClassIds::CTFWearableRazorback)
				continue;

			auto pEntity = pClientEntity->As<C_BaseEntity>();
			if (!pEntity)
				continue;

			auto pOwner = pEntity->m_hOwnerEntity().Get();
			if (!pOwner || pOwner != pPlayer)
				continue;

			if (pEntity->ShouldDraw())
				return true;
		}

	return false;
}

	// Amalgam GetTargets.
	std::vector<Target_t> GetTargets(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
	{
		std::vector<Target_t> vTargets = {};

		const Vec3 vLocalPos = F::AmalgamTicks->GetShootPos();
		const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
		const float flAimFOV = CFG::Aimbot_Melee_FOV;
		// A target within the swing's physical reach must never be vetoed by the
		// view cone: silent aim writes the swing direction, so an enemy at face
		// range connects just as well directly behind the player as in front.
		// The FOV setting still gates targets beyond reach, where the swing
		// cannot connect anyway (same reach math as CanHit).
		float flReach = SDK::AttribHookValue(pWeapon->GetSwingRange(), "melee_range_multiplier", pWeapon)
			+ SDK::AttribHookValue(18.f, "melee_bounds_multiplier", pWeapon);
		if (pLocal->m_flModelScale() > 1.0f)
			flReach *= pLocal->m_flModelScale();
		const float flReachSqr = flReach * flReach;
		const bool bFriendlyFire = F::AimbotGlobal->FriendlyFire();

		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
		{
			// Amalgam: enemies unless friendly fire; the whip speeds up allies
			// so it always targets the whole team when enabled.
			auto eGroup = !bFriendlyFire ? EEntGroup::PLAYERS_ENEMIES : EEntGroup::PLAYERS_ALL;
			if (Vars::Aimbot::Melee::WhipTeam.Value
				&& !bFriendlyFire && SDK::AttribHookValue(0.f, "speed_buff_ally", pWeapon) > 0.f)
				eGroup = EEntGroup::PLAYERS_ALL;

			for (auto pEntity : H::Entities->GetGroup(eGroup))
			{
				if (!pEntity)
					continue;

				if (F::AimbotGlobal->ShouldIgnore(pEntity, pLocal, pWeapon))
					continue;

				float flFOVTo = 0.f; Vec3 vPos = {}, vAngleTo = {};
				const bool bInFOV = PlayerInFOV(pEntity->As<C_TFPlayer>(), vLocalPos, vLocalAngles, flAimFOV, flFOVTo, vPos, vAngleTo);
				const float flDistTo = vLocalPos.DistToSqr(vPos);
				if (!bInFOV && flDistTo > flReachSqr)
					continue;

				bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
				int iPriority = F::AimbotGlobal->GetPriority(pEntity->entindex());
				if (bTeam && !bFriendlyFire)
					iPriority = 0;
				vTargets.push_back(MakeTarget(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, iPriority));
			}
		}

		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Building)
		{
			auto eGroup = EEntGroup::BUILDINGS_ENEMIES;
			bool bWrench = pWeapon->GetWeaponID() == TF_WEAPON_WRENCH;
			bool bSapper = SDK::AttribHookValue(0.f, "set_dmg_apply_to_sapper", pWeapon) > 0.f;
			// Amalgam: AutoRepair with a wrench/sapper widens the group to all
			// buildings (enemies + team).
			if (CFG::Aimbot_Melee_Auto_Repair && (bWrench || bSapper))
				eGroup = EEntGroup::BUILDINGS_ALL;

			for (auto pEntity : H::Entities->GetGroup(eGroup))
			{
				if (!pEntity || F::AimbotGlobal->ShouldIgnore(pEntity, pLocal, pWeapon))
					continue;

				auto pBuilding = pEntity->As<C_BaseObject>();
				if (pBuilding->m_bPlacing() || pBuilding->m_bCarried())
					continue;

				bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
				if (bTeam && (bWrench && !AimFriendlyBuilding(pBuilding) || bSapper && !pBuilding->m_bHasSapper()))
					continue;

				float flFOVTo = 0.f; Vec3 vPos = {}, vAngleTo = {};
				const bool bInFOV = EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flAimFOV, flFOVTo, vPos, vAngleTo);
				const float flDistTo = vLocalPos.DistToSqr(vPos);
				if (!bInFOV && flDistTo > flReachSqr)
					continue;

				vTargets.push_back(MakeTarget(pEntity,
					pEntity->GetClassId() == ETFClassIds::CObjectSentrygun ? TargetEnum::Sentry
					: pEntity->GetClassId() == ETFClassIds::CObjectDispenser ? TargetEnum::Dispenser
					: TargetEnum::Teleporter,
					vPos, vAngleTo, flFOVTo, flDistTo));
			}
		}

	// Amalgam also collects WorldNPC targets here; Necromancer has no
	// matching entity group, so that target class is skipped.

	return vTargets;
}
}

// Amalgam's Aim overloads default to the general AimType var; this port maps
// Necromancer's melee-specific CFG (0 Normal, 1 Silent, 2 Smooth) instead.
int CAmalgamAimbotMelee::GetMeleeAimMethod()
{
	switch (CFG::Aimbot_Melee_Aim_Type)
	{
	case 2: return Vars::Aimbot::General::AimTypeEnum::Smooth;
	case 1: return Vars::Aimbot::General::AimTypeEnum::Silent;
	default: return Vars::Aimbot::General::AimTypeEnum::Plain;
	}
}

// The native melee aimbot's IsFiring, kept for the engagement gate: the knife
// is "firing" while the user holds attack, other melee while the smack is
// imminent (Necromancer's smack-time semantics).
bool CAmalgamAimbotMelee::IsFiring(const CUserCmd* pCmd, C_TFWeaponBase* pWeapon)
{
	if (Shifting::bShifting && Shifting::bShiftingWarp)
		return true;

	if (pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
		return (pCmd->buttons & IN_ATTACK) && G::bCanPrimaryAttack;

	return fabsf(pWeapon->m_flSmackTime() - I::GlobalVars->curtime) < I::GlobalVars->interval_per_tick * 2.0f;
}

void CAmalgamAimbotMelee::UpdateInfo(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd, const std::vector<Target_t>& vTargets)
{
	m_mRecordMap.clear();
	m_vEyePos = F::AmalgamTicks->GetShootPos();
	m_flRange = pWeapon->GetSwingRange();
	m_iSimulatedTicks = GetSwingTime(pWeapon), m_iSwingTicks = GetSwingTime(pWeapon, false);
	m_iDoubletapTicks = F::AmalgamTicks->GetTicks(); // 0 - melee is excluded from Necromancer's RapidFire
	m_bShouldSwing = m_iDoubletapTicks <= m_iSwingTicks; // || AntiWarp && grounded (doubletap only)
	m_bSimulatedLocal = false;
	m_tHitRecord = {};

	if ((!Vars::Aimbot::Melee::SwingPrediction.Value || !m_iSimulatedTicks) && !m_iDoubletapTicks || !G::bCanPrimaryAttack || pWeapon->m_flSmackTime() > 0.f)
	{
		m_iSimulatedTicks = m_iSwingTicks = 0;
		return;
	}

	std::unordered_map<int, AmalgamMoveStorage> mMoveStorage;

	F::MoveSim->Initialize(pLocal, mMoveStorage[pLocal->entindex()], false, !m_iDoubletapTicks, false);
	for (auto& tTarget : vTargets)
		F::MoveSim->Initialize(tTarget.m_pEntity, mMoveStorage[tTarget.m_pEntity->entindex()], false, true, false);

	int iMax = std::max(m_iSimulatedTicks, m_iDoubletapTicks), iTicks = iMax; bool bSwung = false;
	Vec3 vLocalOrigin = mMoveStorage[pLocal->entindex()].m_MoveData.m_vecAbsOrigin;
	for (int i = 0; i < iTicks; i++) // intended for plocal to collide with targets
	{
		{
			auto& tMoveStorage = mMoveStorage[pLocal->entindex()];
			if (!bSwung && (!m_iDoubletapTicks || iMax - i <= m_iSwingTicks))
			{
				iTicks = std::min(i + m_iSwingTicks, iMax), bSwung = true;
				if (!m_iSwingTicks)
					break;

				if (pLocal->InCond(TF_COND_SHIELD_CHARGE))
				{	// demo charge fix for swing pred. Amalgam removes the cond
					// and resets both speeds here; Necromancer has no
					// RemoveCond and the MoveSim port tracks the charge expiry
					// itself, so only the simulated speed is reset.
					tMoveStorage.m_MoveData.m_flMaxSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed = SDK::MaxSpeed(pLocal);
				}
			}
			// Amalgam feeds Ticks::AntiWarp here for doubletap swings; melee
			// has no doubletap in Necromancer.

			F::MoveSim->RunTick(tMoveStorage);

			vLocalOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
			m_bSimulatedLocal = true;
		}

			if (i < m_iSimulatedTicks - m_iDoubletapTicks)
			{
				for (auto& tTarget : vTargets)
				{
					auto& tMoveStorage = mMoveStorage[tTarget.m_pEntity->entindex()];
					if (tMoveStorage.m_bFailed)
						continue;

					F::MoveSim->RunTick(tMoveStorage);
					if (Vars::Aimbot::Melee::SwingPredictLag.Value && !tMoveStorage.m_bPredictNetworked)
						continue;

					Vec3 vOrigin = Vars::Aimbot::Melee::SwingPredictLag.Value ? tMoveStorage.m_vPredictedOrigin : tMoveStorage.m_MoveData.m_vecAbsOrigin;

						MeleeRecord_t tRecord = {};
						tRecord.m_flSimTime = tTarget.m_pEntity->m_flSimulationTime() + TICKS_TO_TIME((i + 1));
					tRecord.m_vOrigin = vOrigin;
					tRecord.m_vMins = tTarget.m_pEntity->m_vecMins();
					tRecord.m_vMaxs = tTarget.m_pEntity->m_vecMaxs();
					m_mRecordMap[tTarget.m_pEntity->entindex()].emplace_front(tRecord);
				}
			}
	}
	m_vEyePos = vLocalOrigin + pLocal->m_vecViewOffset();
	m_flRange = pWeapon->GetSwingRange();

	// Amalgam draws swing paths here (Visuals::Prediction::SwingLines);
	// Necromancer's draw storage differs, so that is skipped.

	for (auto& tMoveStorage : mMoveStorage)
		F::MoveSim->Restore(tMoveStorage.second);
}

bool CAmalgamAimbotMelee::CanBackstab(C_BaseEntity* pTarget, C_TFPlayer* pLocal, Vec3 vEyeAngles)
{
	if (!IsPlayer(pTarget) || pTarget->m_iTeamNum() == pLocal->m_iTeamNum())
		return false;

	if (CFG::Aimbot_Melee_Ignore_Razorback && HasActiveRazorback(pTarget->As<C_TFPlayer>()))
		return false;

	Vec3 vEyePos = m_vEyePos;
	const float flCompDist = 0.5f * PLAYER_ORIGIN_COMPRESSION.x;
	const float flSqCompDist = 0.0884f;

	// Amalgam re-predicts with the new angles when the local player wasn't
	// simulated (!m_bSimulatedLocal) to keep prediction errors from missing
	// the stab. The Necromancer aimbot already runs inside EnginePrediction
	// and m_vEyePos here is the same position the trace above used, so the
	// re-predict block reduces to a no-op in this port.

	// GetAbsOrigin (not m_vecOrigin) here: CanBackstab runs while the entity
	// is still moved onto the record origin, so the geometry is tested at the
	// backtracked position the stab will actually land on.
	Vec3 vToTarget = (pTarget->GetAbsOrigin() - vEyePos).To2D();
	const float flDist = vToTarget.Normalize2D();
	if (flDist < flSqCompDist)
		return false;

	const float flExtra = 2.f * flCompDist / flDist; // account for origin compression
	float flPosVsTargetViewMinDot = 0.f + 0.0031f + flExtra;
	float flPosVsOwnerViewMinDot = 0.5f + flExtra;
	float flViewAnglesMinDot = -0.3f + 0.0031f; // 0.00306795676297 ?

	auto fTestDots = [&](Vec3 vTargetAngles)
	{
		Vec3 vOwnerForward; Math::AngleVectors(vEyeAngles, &vOwnerForward);
		vOwnerForward.Normalize2D();

		Vec3 vTargetForward; Math::AngleVectors(vTargetAngles, &vTargetForward);
		vTargetForward.Normalize2D();

		const float flPosVsTargetViewDot = vToTarget.Dot(vTargetForward); // Behind?
		const float flPosVsOwnerViewDot = vToTarget.Dot(vOwnerForward); // Facing?
		const float flViewAnglesDot = vTargetForward.Dot(vOwnerForward); // Facestab?

		return flPosVsTargetViewDot > flPosVsTargetViewMinDot && flPosVsOwnerViewDot > flPosVsOwnerViewMinDot && flViewAnglesDot > flViewAnglesMinDot;
	};

	Vec3 vTargetAngles = { 0.f, pTarget->As<C_TFPlayer>()->GetEyeAngles().y, 0.f };
	// Amalgam's BackstabFlags::AccountPing rotates the target angles by the
	// eye-angle delta over the choke window; Necromancer keeps no eye-angle
	// history, so the plain test runs (DoubleTest without a delta is the same
	// test twice).
	if (!fTestDots(vTargetAngles))
		return false;

	return true;
}

int CAmalgamAimbotMelee::CanHit(Target_t& tTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	float flRange = SDK::AttribHookValue(m_flRange, "melee_range_multiplier", pWeapon);
	float flHull = SDK::AttribHookValue(18.f, "melee_bounds_multiplier", pWeapon);
	if (pLocal->m_flModelScale() > 1.0f)
	{
		flRange *= pLocal->m_flModelScale();
		flHull *= pLocal->m_flModelScale();
	}
	if (pWeapon->GetWeaponID() == TF_WEAPON_WRENCH && tTarget.m_pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
	{
		flRange = 70.f;
		flHull = 18.f;
	}
	Vec3 vSwingMins = { -flHull, -flHull, -flHull };
	Vec3 vSwingMaxs = { flHull, flHull, flHull };
	auto& vSimRecords = m_mRecordMap[tTarget.m_pEntity->entindex()];

	std::vector<MeleeRecord_t*> vRecords = {};

	// Amalgam's F::Backtrack.GetRecords - bridged onto Necromancer's
	// LagRecords as lightweight views (the melee trace is hull based and
	// never touches bones).
	m_dBacktrackScratch.clear();
	if (IsPlayer(tTarget.m_pEntity))
	{
		auto pPlayer = tTarget.m_pEntity->As<C_TFPlayer>();
		int nRecords = 0;
		if (F::LagRecords->HasRecords(pPlayer, &nRecords))
		{
			for (int n = 0; n <= nRecords; n++)
			{
				const auto pLagRecord = F::LagRecords->GetRecord(pPlayer, n, true);
				if (!pLagRecord || !pLagRecord->bIsAlive || pLagRecord->bInvalid)
					continue;

				MeleeRecord_t tRecord = {};
				tRecord.m_flSimTime = pLagRecord->SimulationTime;
				tRecord.m_vOrigin = pLagRecord->AbsOrigin;
				tRecord.m_vMins = pLagRecord->VecMinsPreScaled;
				tRecord.m_vMaxs = pLagRecord->VecMaxsPreScaled;
				tRecord.m_bBacktrack = true;
				tRecord.m_pLagRecord = pLagRecord;
				m_dBacktrackScratch.push_back(tRecord);
			}
		}

		// Amalgam's compat mode keeps the tick_count rewind but caps its interp
		// at 0.1s (GetFakeInterp/GetWishLerp) - the largest cl_interp a real
		// client would run, and what Necromancer's own wire clamp reports. Match
		// that, and bound the reach on top of the interp by Amalgam's Backtrack
		// window (0.185s default): the same envelope its non-compat mode grants
		// around the expected rewind, and one a real client's cmd timing plus
		// interp produces, so chasing a runner keeps the closer older records
		// the swing actually lands on instead of only the newest one that is
		// stepping out of range. Anchored to the newest record because simTime
		// runs ahead of the client's tickcount by the incoming delay. If nothing
		// survives, the current-position fallback below takes over.
		if (CFG::Misc_AntiCheat_Enabled && !m_dBacktrackScratch.empty())
		{
			float flNewest = 0.f;
			for (auto& tRecord : m_dBacktrackScratch)
				flNewest = std::max(flNewest, tRecord.m_flSimTime);
			const float flCutoff = flNewest - (std::min(SDKUtils::GetLerp(), 0.1f) + 0.185f);
			m_dBacktrackScratch.erase(std::remove_if(m_dBacktrackScratch.begin(), m_dBacktrackScratch.end(),
				[flCutoff](const MeleeRecord_t& tRecord) { return tRecord.m_flSimTime < flCutoff; }),
				m_dBacktrackScratch.end());
			// Amalgam sorts the survivors by distance to the local player for
			// melee (GetValidRecords bDistance) - for a runner the closest
			// position is the oldest record, so trace that first
			std::sort(m_dBacktrackScratch.begin(), m_dBacktrackScratch.end(),
				[pLocal](const MeleeRecord_t& tA, const MeleeRecord_t& tB)
				{ return pLocal->m_vecOrigin().DistToSqr(tA.m_vOrigin) < pLocal->m_vecOrigin().DistToSqr(tB.m_vOrigin); });
		}
		// melee's lag comp runs at SMACK time (tf_weaponbase_melee.cpp:707, Smack
		// -> StartLagCompensation), so the server's +/-0.2 acceptance delta
		// applies to a record's age AT SMACK: |fake+lerp - (age_now + smack)|.
		// The honored depth follows the fake the sequences actually apply (the
		// wish, effective immediately), not the smoothed m_flFakeLatency model.
		// Records outside the window are never hard-rejected - the server CLAMPS
		// their rewind to the window edge, so the closest-to-window record is
		// the position the swing can actually land on. Sort by that distance and
		// let the trace loop take the best one that connects.
		if (m_dBacktrackScratch.size() > 1)
		{
			const float flAnchor = CFG::Aimbot_Hitscan_Fake_Latency / 1000.f + SDKUtils::GetLerp();
			const float flSmack = TICKS_TO_TIME(m_iSwingTicks);
			std::stable_sort(m_dBacktrackScratch.begin(), m_dBacktrackScratch.end(),
				[flAnchor, flSmack](const MeleeRecord_t& tA, const MeleeRecord_t& tB)
				{
					const float flDeltaA = fabsf(I::GlobalVars->curtime - tA.m_flSimTime + flSmack - flAnchor);
					const float flDeltaB = fabsf(I::GlobalVars->curtime - tB.m_flSimTime + flSmack - flAnchor);
					return flDeltaA < flDeltaB;
				});
		}
	}

	if (!m_dBacktrackScratch.empty())
	{
		for (auto& tRecord : m_dBacktrackScratch)
			vRecords.push_back(&tRecord);

		// the real-model prediction is only rewindable when we run no fake
		// latency (the server's honored depth = out + fake + lerp before smack);
		// with fake latency above 0 the server clamps every rewind into the
		// past, so aiming at the real model whiffs - records only
		if (CFG::Aimbot_Hitscan_Fake_Latency <= 0.f)
		{
			for (auto& tRecord : vSimRecords)
				vRecords.push_back(&tRecord);
		}
	}
	else
	{	// buildings / record-less entities: current position (Amalgam's
		// F::Backtrack.m_tRecord fallback; bones skipped - unused here).
		// For players this counts as backtrack so Run still aims the
		// tick_count at the entity's simtime (the native melee aimbot's
		// behavior for current-position targets); sim records never do.
		MeleeRecord_t tRecord = {};
		tRecord.m_flSimTime = tTarget.m_pEntity->m_flSimulationTime();
		tRecord.m_vOrigin = tTarget.m_pEntity->m_vecOrigin();
		tRecord.m_vMins = tTarget.m_pEntity->m_vecMins();
		tRecord.m_vMaxs = tTarget.m_pEntity->m_vecMaxs();
		tRecord.m_bBacktrack = IsPlayer(tTarget.m_pEntity);
		m_dBacktrackScratch.push_back(tRecord);
		vRecords = { &m_dBacktrackScratch.front() };
	}

	CGameTrace trace = {};
	CTraceFilterHitscan filter = {};
	filter.m_pIgnore = pLocal;

	for (auto pRecord : vRecords)
	{
		// RijiN-style point scan: the clamped point is the closest point of
		// the record's hull to the (simulated) eye position; if its angle is
		// blocked, points across the hull give the swing other angles to
		// connect through. The clamped point connecting is unbeatable, so it
		// returns immediately; otherwise the first connecting scan point wins.
		Vec3 vClampPos = VecClamp(m_vEyePos, pRecord->m_vOrigin + pRecord->m_vMins, pRecord->m_vOrigin + pRecord->m_vMaxs);
		if (CFG::Aimbot_Melee_Auto_Backstab && pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
			vClampPos.x = pRecord->m_vOrigin.x, vClampPos.y = pRecord->m_vOrigin.y;

		Vec3 vRestoreOrigin = tTarget.m_pEntity->GetAbsOrigin(); // reflects the SetAbsOrigin move below, like Amalgam
		Vec3 vRestoreMins = tTarget.m_pEntity->m_vecMins();
		Vec3 vRestoreMaxs = tTarget.m_pEntity->m_vecMaxs();

		// move the entity onto the record (Amalgam's SetAbsOrigin with origin
		// compression compensation) so the hull trace hits the record pose
		tTarget.m_pEntity->SetAbsOrigin(pRecord->m_vOrigin);
		tTarget.m_pEntity->m_vecMins() = pRecord->m_vMins + PLAYER_ORIGIN_COMPRESSION;
		tTarget.m_pEntity->m_vecMaxs() = pRecord->m_vMaxs - PLAYER_ORIGIN_COMPRESSION;

		bool bReturn = false;
		for (int nPoint = 0; nPoint <= 10 && !bReturn; nPoint += 2)
		{
			Vec3 vPoint = vClampPos;
			if (nPoint > 0)
			{
				vPoint = pRecord->m_vOrigin + (pRecord->m_vMins + (pRecord->m_vMaxs - pRecord->m_vMins) * (nPoint / 10.f));
				if (CFG::Aimbot_Melee_Auto_Backstab && pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
					vPoint.x = pRecord->m_vOrigin.x, vPoint.y = pRecord->m_vOrigin.y;
			}

			Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, vPoint), tTarget.m_vAngleTo);
			// Amalgam's ShouldAimAtAngle only restricts when LeadAndRestrict is
			// on (default off), so it always passes here.

			Vec3 vForward; Math::AngleVectors(tTarget.m_vAngleTo, &vForward);
			Vec3 vTraceEnd = m_vEyePos + vForward * flRange;

			SDK::TraceHull(m_vEyePos, vTraceEnd, {}, {}, MASK_SOLID, &filter, &trace);
			bReturn = trace.m_pEnt == tTarget.m_pEntity;
			if (!bReturn)
			{
				SDK::TraceHull(m_vEyePos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
				bReturn = trace.m_pEnt == tTarget.m_pEntity;
			}

			if (bReturn)
				tTarget.m_vPos = vPoint;
		}

		if (bReturn && CFG::Aimbot_Melee_Auto_Backstab && pWeapon->GetWeaponID() == TF_WEAPON_KNIFE && !CanBackstab(tTarget.m_pEntity, pLocal, tTarget.m_vAngleTo))
		{
			// Auto Backstab vetoes the swing so the knife only stabs from
			// behind - with the fallback on, a failed backstab check still
			// swings as a normal slash instead of doing nothing face-to-face
			bReturn = CFG::Aimbot_Melee_Auto_Backstab_Fallback;
		}

		tTarget.m_pEntity->SetAbsOrigin(vRestoreOrigin);
		tTarget.m_pEntity->m_vecMins() = vRestoreMins;
		tTarget.m_pEntity->m_vecMaxs() = vRestoreMaxs;

		if (bReturn)
		{
			m_tHitRecord = *pRecord;
			tTarget.m_bBacktrack = tTarget.m_iTargetType == TargetEnum::Player;

			return true;
		}
		else if (GetMeleeAimMethod() == Vars::Aimbot::General::AimTypeEnum::Smooth)
		{	// Amalgam's smooth/assistive fallback: the hull at the aim angle
			// (identical to the recalculated angle Amalgam uses) still hits
			Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, vClampPos), tTarget.m_vAngleTo);
			Vec3 vSmoothForward; Math::AngleVectors(tTarget.m_vAngleTo, &vSmoothForward);
			Vec3 vSmoothEnd = m_vEyePos + vSmoothForward * flRange;
			SDK::TraceHull(m_vEyePos, vSmoothEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
			if (trace.m_pEnt == tTarget.m_pEntity)
				return 2;
		}
	}

	return false;
}

bool CAmalgamAimbotMelee::Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod)
{
	bool bReturn = false;
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		vOut = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
		vOut = vCurAngle.LerpAngle(vToAngle, Vars::Aimbot::General::AssistStrength.Value / 100.f);
		bReturn = true;
		break;
	}

	if (iMethod != Vars::Aimbot::General::AimTypeEnum::Silent || CFG::Misc_AntiCheat_Enabled)
		Math::ClampAngles(vOut);
	return bReturn;
}

// assume angle calculated outside with other overload
void CAmalgamAimbotMelee::Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod)
{
	bool bUnsure = F::AmalgamTicks->IsTimingUnsure();
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
		if (G::Attacking != 1 && !bUnsure)
			break;
		[[fallthrough]];
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
		pCmd->viewangles = vAngles;
		// same guard as the projectile aimbot's SetViewAngles
		if (Memory::IsInModule("engine.dll", I::EngineClient, true))
			I::EngineClient->SetViewAngles(vAngles);
		// a swing cmd must keep these angles: CreateMove recomputes G::Attacking
		// after the aimbot and prediction can report the weapon not ready on the
		// very tick IN_ATTACK goes out, and FakeAngle::ShouldRun only skips on
		// bSilentAngles - without this flag anti-aim overwrites the aimed swing
		if (G::Attacking == 1 || pCmd->buttons & IN_ATTACK)
		{
			G::bSilentAngles = true;
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (G::Attacking == 1 || bUnsure)
		{
			SDK::FixMovement(pCmd, vAngles);
			pCmd->viewangles = vAngles;
			G::bPSilentAngles = true;
			// every native aimbot sets this alongside bPSilentAngles: it is what
			// FakeAngle::ShouldRun checks, and the pSilent flag alone leaves the
			// swing cmd open to anti-aim overwrite whenever CreateMove's Attacking
			// recompute reports 0 (prediction consumed bCanPrimaryAttack)
			G::bSilentAngles = true;
		}
		break;
	}
}

bool CAmalgamAimbotMelee::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!CFG::Aimbot_Melee_Active)
		return false;

	// Necromancer handles the sapper through its AutoSapper triggerbot (the
	// native melee aimbot skips the builder as well).
	if (pWeapon->GetWeaponID() == TF_WEAPON_BUILDER)
		return false;

	if (CFG::Aimbot_Melee_Sort == 0)
		G::flAimbotFOV = CFG::Aimbot_Melee_FOV;

	if (F::AimbotGlobal->ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;

	// Amalgam's Run gate: ShouldAim() (an attackable tick for Plain/Silent)
	// unless a swing is already in progress. Engagement is the aim key (or
	// Always_Active) ONLY - the native melee's aimKeyDown || isFiring also
	// engaged on an in-flight manual swing, which aimed at whatever was in FOV
	// the moment the user clicked M1. bFiring still keeps an aimbot-started
	// swing aiming through the cooldown gate below.
	const bool bKeyAim = H::Input->IsDown(CFG::Aimbot_Key) || CFG::Aimbot_Melee_Always_Active;
	const bool bFiring = IsFiring(pCmd, pWeapon);
	bool bShouldAim = true;
	if (GetMeleeAimMethod() == Vars::Aimbot::General::AimTypeEnum::Plain
		|| GetMeleeAimMethod() == Vars::Aimbot::General::AimTypeEnum::Silent)
	{
		if (!G::bCanPrimaryAttack && !G::bReloading && !F::AmalgamTicks->IsTimingUnsure())
			bShouldAim = false;
	}
	const bool bEarlyOut = !bFiring && !bShouldAim && pWeapon->m_flSmackTime() < 0.f;
	if (!bKeyAim)
		return bFiring;
	// an attacking cmd swings wherever its angles point, so it must never
	// early-out of being aimed - the idle-tick gate below is only for ticks
	// with no attack input, otherwise a held M1 during the attack cooldown
	// fires on raw/anti-aim angles
	if (bEarlyOut && !(pCmd->buttons & IN_ATTACK))
		return false;

	auto vTargets = GetTargets(pLocal, pWeapon);
	if (vTargets.empty())
		return pWeapon->m_flSmackTime() > 0.f;

	// Amalgam's ManageTargets: sort by the selection method, cap the count,
	// then sort priority on top. In-reach targets survive the cap: the FOV
	// sort puts a target standing behind us last, so a small cap drops the
	// only target the swing can physically connect with while keeping
	// distant view-cone fills that always fail the trace (GetTargets exempts
	// in-reach targets from the FOV cone for the same reason).
	float flReach = SDK::AttribHookValue(pWeapon->GetSwingRange(), "melee_range_multiplier", pWeapon)
		+ SDK::AttribHookValue(18.f, "melee_bounds_multiplier", pWeapon);
	if (pLocal->m_flModelScale() > 1.0f)
		flReach *= pLocal->m_flModelScale();
	const float flReachSqr = flReach * flReach;
	F::AimbotGlobal->SortTargets(vTargets, CFG::Aimbot_Melee_Sort);
	const auto tPartition = std::stable_partition(vTargets.begin(), vTargets.end(),
		[flReachSqr](const Target_t& tTarget) { return tTarget.m_flDistTo <= flReachSqr; });
	const size_t nInReach = size_t(tPartition - vTargets.begin());
	vTargets.resize(std::min(vTargets.size(),
		nInReach + std::max(size_t(Vars::Aimbot::General::MaxTargets.Value), size_t(1))));
	F::AimbotGlobal->SortPriority(vTargets);

	UpdateInfo(pLocal, pWeapon, pCmd, vTargets);
	bool bHit = false;
	for (auto& tTarget : vTargets)
	{
		const auto iResult = CanHit(tTarget, pLocal, pWeapon);
		if (!iResult) continue;
		bHit = true;
		if (iResult == 2)
		{
			G::nTargetIndex = tTarget.m_pEntity->entindex();
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

		if (CFG::Aimbot_AutoShoot && pWeapon->m_flSmackTime() < 0.f)
		{
			if (m_bShouldSwing)
				pCmd->buttons |= IN_ATTACK;
			// Amalgam flags F::Ticks.m_bDoubletap here; melee has no
			// doubletap in Necromancer.
		}

		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		// IsAttacking requires bCanPrimaryAttack, which engine prediction can
		// have already consumed on the very tick we add IN_ATTACK - treat a cmd
		// that actually attacks as attacking so the swing keeps its angles and
		// tick_count rewind below, and anti-aim/anti-backstab don't overwrite it
		if (!G::Attacking && pCmd->buttons & IN_ATTACK)
			G::Attacking = 1;
		if (G::Attacking == 1 && m_tHitRecord.m_bBacktrack)
		{
			// the rewind must land on the record the aim and the trace validated:
			// tick_count = simTime + lerp asks the server for exactly that record's
			// position (the native melee aimbot's formula). Adding the fake latency
			// here shifted the rewind fake-forward of the aimed record, so the
			// damage checked a different spot than the crosshair - the misses.
			// The acceptance sort's anchor (fake + lerp) is what makes the server
			// honor this depth, not the tick_count itself.
			if (CFG::Misc_AntiCheat_Enabled)
				pCmd->tick_count = TIME_TO_TICKS(m_tHitRecord.m_flSimTime + std::min(SDKUtils::GetLerp(), 0.1f));
			else
				pCmd->tick_count = TIME_TO_TICKS(m_tHitRecord.m_flSimTime + SDKUtils::GetLerp());
		}
		else
		{
			m_vEyePos = pLocal->GetShootPos();
			Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo);
		}

		G::nTargetIndex = tTarget.m_pEntity->entindex();
		Aim(pCmd, tTarget.m_vAngleTo);

		if (CFG::Aimbot_Melee_Walk_To_Target && (pLocal->m_fFlags() & FL_ONGROUND))
			SDKUtils::WalkTo(pCmd, pLocal->m_vecOrigin(), tTarget.m_vPos, 1.f);
		break;
	}
	// A cmd that already carries IN_ATTACK swings wherever its angles point,
	// even when this tick's CanHit failed (record window blips, a teammate in
	// the swing hull) - aim it at the closest target rather than letting it fly
	// on raw view angles. Auto Backstab with the fallback off still vetoes:
	// those swings are meant to be held back, not redirected.
	if (!bHit && pCmd->buttons & IN_ATTACK
		&& !(pWeapon->GetWeaponID() == TF_WEAPON_KNIFE && CFG::Aimbot_Melee_Auto_Backstab && !CFG::Aimbot_Melee_Auto_Backstab_Fallback))
	{
		auto& tNearest = *std::min_element(vTargets.begin(), vTargets.end(),
			[](const Target_t& tA, const Target_t& tB) { return tA.m_flDistTo < tB.m_flDistTo; });
		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		if (!G::Attacking && pCmd->buttons & IN_ATTACK)
			G::Attacking = 1;
		G::nTargetIndex = tNearest.m_pEntity->entindex();
		Aim(pCmd, tNearest.m_vAngleTo);
	}
	// Compat-mode chase assist: the rewind this mode grants is capped at
	// interp + Amalgam's window (~0.28s of record age, roughly a hundred
	// units against a runner), so a target chased beyond that extended
	// reach stays unhittable until the gap closes. Steer into range while
	// nothing is hittable (same opt-in var as the post-hit walk above).
	if (!bHit && CFG::Aimbot_Melee_Walk_To_Target && (pLocal->m_fFlags() & FL_ONGROUND) && !vTargets.empty())
	{
		const Target_t& tNearest = *std::min_element(vTargets.begin(), vTargets.end(),
			[](const Target_t& tA, const Target_t& tB) { return tA.m_flDistTo < tB.m_flDistTo; });
		SDKUtils::WalkTo(pCmd, pLocal->m_vecOrigin(), tNearest.m_vPos, 1.f);
	}
	// dead tick: nothing connected this pass. Claim only while a swing is in
	// flight (its angles/tick_count must survive); on fully idle ticks hand
	// the cmd back so the native melee aimbot gets its shot at the target
	// instead of the port silently eating every tick.
	return pWeapon->m_flSmackTime() > 0.f;
}
