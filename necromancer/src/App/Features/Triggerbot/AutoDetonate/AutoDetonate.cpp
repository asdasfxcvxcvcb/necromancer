#include "AutoDetonate.h"

#include "../../CFG.h"
#include "../../amalgam_port/AmalgamCompat.h"
#include <algorithm>

// Update or create tracking entry for enemy-sticky pair
void CAutoDetonate::UpdateEnemyTracking(int stickyIndex, C_BaseEntity* pEnemy, const Vec3& vStickyPos, float flCurTime)
{
	if (!pEnemy)
		return;

	const int enemyIndex = pEnemy->entindex();
	auto& trackingList = m_mStickyEnemyTracking[stickyIndex];

	// Find existing tracking entry for this enemy
	for (auto& tracking : trackingList)
	{
		if (tracking.entityIndex == enemyIndex)
		{
			// Update existing entry
			const Vec3 vEnemyPos = pEnemy->GetCenter();
			tracking.lastDistance = vStickyPos.DistTo(vEnemyPos);
			tracking.lastPosition = vEnemyPos;
			return;
		}
	}

	// Create new tracking entry
	EnemyTrackingData newTracking;
	newTracking.entityIndex = enemyIndex;
	newTracking.firstSeenTime = flCurTime;
	const Vec3 vEnemyPos = pEnemy->GetCenter();
	newTracking.lastDistance = vStickyPos.DistTo(vEnemyPos);
	newTracking.lastPosition = vEnemyPos;
	trackingList.push_back(newTracking);
}

// Check if enemy is moving away from sticky
bool CAutoDetonate::IsEnemyMovingAway(const EnemyTrackingData& tracking, const Vec3& vCurrentPos, const Vec3& vStickyPos)
{
	const float flCurrentDist = vStickyPos.DistTo(vCurrentPos);
	const float flPreviousDist = tracking.lastDistance;

	// Enemy is moving away if distance is increasing beyond threshold
	const float MOVEMENT_THRESHOLD = 5.0f;
	return (flCurrentDist - flPreviousDist) > MOVEMENT_THRESHOLD;
}

// Clear all tracking data for a specific sticky
void CAutoDetonate::ClearStickyTracking(int stickyIndex)
{
	m_mStickyEnemyTracking.erase(stickyIndex);
}

// Check if enemy is in danger zone (50% of splash radius)
bool CAutoDetonate::ShouldDetonateImmediate(C_BaseEntity* pEnemy, const Vec3& vStickyPos, float flRadius)
{
	if (!pEnemy)
		return false;

	// Calculate danger zone radius (50% of splash radius)
	const float flDangerZoneRadius = flRadius * 0.5f;
	const float flDangerZoneRadiusSqr = flDangerZoneRadius * flDangerZoneRadius;

	// Check if enemy is within danger zone using squared distance
	const Vec3 vEnemyPos = pEnemy->GetCenter();
	return vStickyPos.DistToSqr(vEnemyPos) < flDangerZoneRadiusSqr;
}

// Sticky blast radius: full splash once touched, the ramped airdet radius while
// airborne (the server only grants full radius after tf_grenadelauncher_livetime
// plus the ramp), scaled by the launcher's explosion radius attribute
float CAutoDetonate::GetEffectiveRadius(C_TFGrenadePipebombProjectile* pSticky, C_TFPlayer* pLocal)
{
	float flRadius = 146.0f;

	auto pLauncher = pSticky->m_hOriginalLauncher().Get();
	auto pWeapon = pLauncher && pLauncher->As<C_TFWeaponBase>() ? pLauncher->As<C_TFWeaponBase>() : H::Entities->GetWeapon();
	if (pWeapon)
		flRadius = SDKUtils::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);

	if (!pSticky->m_bTouched())
	{
		static auto tf_grenadelauncher_livetime = I::CVar->FindVar("tf_grenadelauncher_livetime");
		static auto tf_sticky_radius_ramp_time = I::CVar->FindVar("tf_sticky_radius_ramp_time");
		static auto tf_sticky_airdet_radius = I::CVar->FindVar("tf_sticky_airdet_radius");
		const float flLiveTime = tf_grenadelauncher_livetime ? tf_grenadelauncher_livetime->GetFloat() : 5.0f;
		const float flRampTime = tf_sticky_radius_ramp_time ? tf_sticky_radius_ramp_time->GetFloat() : 0.5f;
		const float flAirdetRadius = tf_sticky_airdet_radius ? tf_sticky_airdet_radius->GetFloat() : 0.3f;
		flRadius *= Math::RemapVal(I::GlobalVars->curtime - pSticky->m_flCreationTime(), flLiveTime, flLiveTime + flRampTime, flAirdetRadius, 1.0f);
	}

	return flRadius;
}

// Closest point of the entity's collision to the sticky, at its current position
static Vec3 NearestToSticky(C_BaseEntity* pEntity, const Vec3& vStickyPos)
{
	Vec3 vNearest = pEntity->GetCenter();
	if (auto pCollideable = pEntity->GetCollideable())
		reinterpret_cast<CCollisionProperty*>(pCollideable)->CalcNearestPoint(vStickyPos, &vNearest);
	return vNearest;
}

// The entity is caught both now and where latency puts it when the det command
// lands - Amalgam detonates only when the target is in range on both clocks
bool CAutoDetonate::EntityCaught(C_BaseEntity* pEntity, const Vec3& vStickyPos, float flRadius, float flLatency)
{
	const float flRadiusSqr = flRadius * flRadius;
	if (vStickyPos.DistToSqr(NearestToSticky(pEntity, vStickyPos)) >= flRadiusSqr)
		return false;

	if (flLatency <= 0.0f)
		return true;

	const Vec3 vMins = pEntity->m_vecMins(), vMaxs = pEntity->m_vecMaxs();
	const Vec3 vPredOrigin = pEntity->GetAbsOrigin() + pEntity->GetAbsVelocity() * flLatency;
	const Vec3 vClosest = { std::clamp(vStickyPos.x, vPredOrigin.x + vMins.x, vPredOrigin.x + vMaxs.x),
		std::clamp(vStickyPos.y, vPredOrigin.y + vMins.y, vPredOrigin.y + vMaxs.y),
		std::clamp(vStickyPos.z, vPredOrigin.z + vMins.z, vPredOrigin.z + vMaxs.z) };
	return vStickyPos.DistToSqr(vClosest) < flRadiusSqr;
}

// Self-damage guard: skip the det if we would be caught by the same blast on
// both the current and the latency-forward clock
bool CAutoDetonate::SelfCaught(C_TFPlayer* pLocal, const Vec3& vStickyPos, float flRadius, float flLatency)
{
	return EntityCaught(pLocal, vStickyPos, flRadius, flLatency);
}

void CAutoDetonate::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (pLocal->m_iClass() != TF_CLASS_DEMOMAN)
		return;

	// Check if enabled via always-on option OR keybind
	// Works if: Always_On is true OR Active (keybind) is true
	if (!CFG::Triggerbot_AutoDetonate_Always_On && !CFG::Triggerbot_AutoDetonate_Active)
		return;

	// Cache arm time (expensive attribute lookup)
	const float flArmTime = SDKUtils::AttribHookValue(0.8f, "sticky_arm_time", pLocal);
	const float flCurTime = I::GlobalVars->curtime;
	
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_LOCAL_STICKIES))
	{
		if (!pEntity)
			continue;

		const auto pSticky = pEntity->As<C_TFGrenadePipebombProjectile>();

		if (!pSticky || pSticky->m_iType() == TF_GL_MODE_REMOTE_DETONATE_PRACTICE)
			continue;

		// Check arm time
		if (flCurTime < pSticky->m_flCreationTime() + flArmTime)
			continue;

		// Cache sticky position, index, and radius — used multiple times below
		// Radius follows the server: full splash when touched, ramped airdet while airborne
		const Vec3 vStickyPos = pSticky->GetCenter();
		const int nStickyIndex = pSticky->entindex();
		const float flRadius = GetEffectiveRadius(pSticky, pLocal);
		const float flRadiusSqr = flRadius * flRadius;
		const float flLatency = SDKUtils::GetRealLatency();

		// Auto detonate players
		if (CFG::Triggerbot_AutoDetonate_Target_Players)
		{
			for (const auto pPlayerEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
			{
				if (!pPlayerEntity)
					continue;

				const auto pPlayer = pPlayerEntity->As<C_TFPlayer>();

				if (pPlayer->deadflag() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
					continue;

				if (CFG::Triggerbot_AutoDetonate_Ignore_Friends && pPlayer->IsPlayerOnSteamFriendsList())
					continue;

				if (CFG::Triggerbot_AutoDetonate_Ignore_Invisible && pPlayer->IsInvisible())
					continue;

				if (CFG::Triggerbot_AutoDetonate_Ignore_Invulnerable && pPlayer->IsInvulnerable())
					continue;

				// Fast squared distance check (avoids sqrt)
				const Vec3 vPlayerPos = pPlayer->GetCenter();
				if (EntityCaught(pPlayer, vStickyPos, flRadius, flLatency)
					&& !(CFG::Triggerbot_AutoDetonate_Prevent_Self && SelfCaught(pLocal, vStickyPos, flRadius, flLatency)))
				{
					if (H::AimUtils->TraceEntityAutoDet(pPlayer, vStickyPos, vPlayerPos))
					{
						bool bShouldDetonate = false;

						// 4.1: Timer disabled path - immediate detonation (original behavior)
						if (!CFG::Triggerbot_AutoDetonate_Timer_Enabled)
						{
							bShouldDetonate = true;
						}
						else
						{
							// Timer enabled path
							// 4.2: Check danger zone first (if enabled)
							if (CFG::Triggerbot_AutoDetonate_DangerZone_Enabled)
							{
								if (ShouldDetonateImmediate(pPlayer, vStickyPos, flRadius))
								{
									bShouldDetonate = true;
								}
							}

							if (!bShouldDetonate)
							{
								// Find tracking data for this enemy
							auto& trackingList = m_mStickyEnemyTracking[nStickyIndex];
								bool bFoundTracking = false;
								
								for (const auto& tracking : trackingList)
								{
									if (tracking.entityIndex == pPlayer->entindex())
									{
										bFoundTracking = true;
										
										// 4.3: Check if enemy is moving away (BEFORE updating tracking)
										if (IsEnemyMovingAway(tracking, vPlayerPos, vStickyPos))
										{
											bShouldDetonate = true;
											break;
										}

										// 4.4: Check dwell time
										const float flDwellTime = flCurTime - tracking.firstSeenTime;
										if (flDwellTime >= CFG::Triggerbot_AutoDetonate_Timer_Value)
										{
											bShouldDetonate = true;
											break;
										}
									}
								}
								
								// 4.3: Update enemy tracking data each frame (AFTER checking movement)
								UpdateEnemyTracking(nStickyIndex, pPlayer, vStickyPos, flCurTime);
							}
						}

						if (bShouldDetonate)
						{
							if (pSticky->m_bDefensiveBomb())
							{
								const Vec3 vAngle = Math::CalcAngle(pLocal->GetShootPos(), vStickyPos);
								H::AimUtils->FixMovement(pCmd, vAngle);
								pCmd->viewangles = vAngle;
								G::bSilentAngles = true;
							}

							pCmd->buttons |= IN_ATTACK2;
							m_flLastDetonate = I::EngineClient->Time();

							// 4.5: Clear tracking data on detonation
							if (CFG::Triggerbot_AutoDetonate_Timer_Enabled)
							{
								ClearStickyTracking(nStickyIndex);
							}

							return;
						}
					}
				}
				else
				{
					// 4.5: Clear tracking for enemies that exit radius
					if (CFG::Triggerbot_AutoDetonate_Timer_Enabled)
					{
						auto& trackingList = m_mStickyEnemyTracking[nStickyIndex];
						trackingList.erase(
							std::remove_if(trackingList.begin(), trackingList.end(),
								[pPlayer](const EnemyTrackingData& tracking) {
									return tracking.entityIndex == pPlayer->entindex();
								}),
							trackingList.end()
						);
					}
				}
			}
		}

		// Auto detonate buildings
		if (CFG::Triggerbot_AutoDetonate_Target_Buildings)
		{
			for (const auto pBuildingEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
			{
				if (!pBuildingEntity)
					continue;

				// Fast squared distance check
				const Vec3 vBuildingPos = pBuildingEntity->GetCenter();
				if (vStickyPos.DistToSqr(vBuildingPos) < flRadiusSqr)
				{
					if (H::AimUtils->TraceEntityAutoDet(pBuildingEntity, vStickyPos, vBuildingPos))
					{
						bool bShouldDetonate = false;

						// 4.1: Timer disabled path - immediate detonation (original behavior)
						if (!CFG::Triggerbot_AutoDetonate_Timer_Enabled)
						{
							bShouldDetonate = true;
						}
						else
						{
							// Timer enabled path

							// 4.2: Check danger zone first (if enabled)
							if (CFG::Triggerbot_AutoDetonate_DangerZone_Enabled)
							{
								if (ShouldDetonateImmediate(pBuildingEntity, vStickyPos, flRadius))
								{
									bShouldDetonate = true;
								}
							}

							if (!bShouldDetonate)
							{
								// Find tracking data for this building
							auto& trackingList = m_mStickyEnemyTracking[nStickyIndex];
								bool bFoundTracking = false;
								
								for (const auto& tracking : trackingList)
								{
									if (tracking.entityIndex == pBuildingEntity->entindex())
									{
										bFoundTracking = true;
										
										// 4.3: Check if building is moving away (BEFORE updating tracking)
										if (IsEnemyMovingAway(tracking, vBuildingPos, vStickyPos))
										{
											bShouldDetonate = true;
											break;
										}

										// 4.4: Check dwell time
										const float flDwellTime = flCurTime - tracking.firstSeenTime;
										if (flDwellTime >= CFG::Triggerbot_AutoDetonate_Timer_Value)
										{
											bShouldDetonate = true;
											break;
										}
									}
								}
								
								// 4.3: Update enemy tracking data each frame (AFTER checking movement)
								UpdateEnemyTracking(nStickyIndex, pBuildingEntity, vStickyPos, flCurTime);
							}
						}

						if (bShouldDetonate)
						{
							if (pSticky->m_bDefensiveBomb())
							{
								const Vec3 vAngle = Math::CalcAngle(pLocal->GetShootPos(), vStickyPos);
								H::AimUtils->FixMovement(pCmd, vAngle);
								pCmd->viewangles = vAngle;
								G::bSilentAngles = true;
							}

							pCmd->buttons |= IN_ATTACK2;
							m_flLastDetonate = I::EngineClient->Time();

							// 4.5: Clear tracking data on detonation
							if (CFG::Triggerbot_AutoDetonate_Timer_Enabled)
							{
								ClearStickyTracking(nStickyIndex);
							}

							return;
						}
					}
				}
				else
				{
					// 4.5: Clear tracking for buildings that exit radius
					if (CFG::Triggerbot_AutoDetonate_Timer_Enabled)
					{
						auto& trackingList = m_mStickyEnemyTracking[nStickyIndex];
						trackingList.erase(
							std::remove_if(trackingList.begin(), trackingList.end(),
								[pBuildingEntity](const EnemyTrackingData& tracking) {
									return tracking.entityIndex == pBuildingEntity->entindex();
								}),
							trackingList.end()
						);
					}
				}
			}
		}
	}
}
