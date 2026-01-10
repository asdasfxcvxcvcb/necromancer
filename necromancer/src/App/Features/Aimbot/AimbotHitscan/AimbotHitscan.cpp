#include "AimbotHitscan.h"

#include "../../CFG.h"
#include "../../RapidFire/RapidFire.h"
#include "../../ShotgunDamagePrediction/SmartShotgunAimbot.h"
#include "../../FakeLagFix/FakeLagFix.h"
#include "../../Menu/Menu.h"
#include "../../Crits/Crits.h"
#include "../../Hitchance/Hitchance.h"
#include <algorithm>

// Get the predicted shoot position accounting for duck state in the command
// This is critical for CrouchWhileAirborne - the command may have IN_DUCK set
// but the entity's current state doesn't reflect that yet
static Vec3 GetPredictedShootPos(C_TFPlayer* pLocal, const CUserCmd* pCmd)
{
	Vec3 vShootPos = pLocal->GetShootPos();
	
	const bool bCurrentlyDucking = (pLocal->m_fFlags() & FL_DUCKING) != 0;
	const bool bWantsToDuck = (pCmd->buttons & IN_DUCK) != 0;
	const bool bOnGround = (pLocal->m_fFlags() & FL_ONGROUND) != 0;
	
	// When airborne, ducking is instant (no transition time)
	// If CrouchWhileAirborne set IN_DUCK but FL_DUCKING isn't set yet, we need to predict
	// TF2 view heights: standing = 68, ducking = 45, difference = 23 units
	if (bWantsToDuck && !bCurrentlyDucking && !bOnGround)
	{
		vShootPos.z -= 10.0f;
	}
	
	return vShootPos;
}

// Check if weapon is a sniper rifle or ambassador (can headshot)
bool CAimbotHitscan::IsHeadshotCapableWeapon(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	const int nWeaponID = pWeapon->GetWeaponID();
	
	// Sniper rifles
	if (nWeaponID == TF_WEAPON_SNIPERRIFLE || 
		nWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC || 
		nWeaponID == TF_WEAPON_SNIPERRIFLE_DECAP)
		return true;
	
	// Ambassador (check for set_weapon_mode attribute)
	if (nWeaponID == TF_WEAPON_REVOLVER)
	{
		const int nWeaponMode = static_cast<int>(SDKUtils::AttribHookValue(0.0f, "set_weapon_mode", pWeapon));
		return nWeaponMode == 1; // Ambassador has weapon mode 1
	}
	
	return false;
}

int CAimbotHitscan::GetAimHitbox(C_TFWeaponBase* pWeapon)
{
	switch (CFG::Aimbot_Hitscan_Hitbox)
	{
		case 0: return HITBOX_HEAD;
		case 1: return HITBOX_PELVIS;
		case 2:
		{
			if (pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC)
				return (pWeapon->As<C_TFSniperRifle>()->m_flChargedDamage() >= 150.0f) ? HITBOX_HEAD : HITBOX_PELVIS;

			return H::AimUtils->IsWeaponCapableOfHeadshot(pWeapon) ? HITBOX_HEAD : HITBOX_PELVIS;
		}
		case 3: // Switch mode
		{
			// Only apply switch for headshot-capable weapons
			if (IsHeadshotCapableWeapon(pWeapon))
			{
				// For classic, still check charge
				if (pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC)
				{
					if (pWeapon->As<C_TFSniperRifle>()->m_flChargedDamage() < 150.0f)
						return HITBOX_PELVIS;
				}
				
				// Return based on switch state (false = Head, true = Body)
				return CFG::Aimbot_Hitscan_Switch_State ? HITBOX_PELVIS : HITBOX_HEAD;
			}
			
			// Non-headshot weapons fall back to auto behavior
			return H::AimUtils->IsWeaponCapableOfHeadshot(pWeapon) ? HITBOX_HEAD : HITBOX_PELVIS;
		}
		default: return HITBOX_PELVIS;
	}
}

bool CAimbotHitscan::ScanHead(C_TFPlayer* pLocal, const CUserCmd* pCmd, HitscanTarget_t& target)
{
	if (!CFG::Aimbot_Hitscan_Scan_Head)
		return false;

	const auto pPlayer = target.Entity->As<C_TFPlayer>();
	if (!pPlayer)
		return false;

	const auto pModel = pPlayer->GetModel();
	if (!pModel)
		return false;

	const auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR)
		return false;

	const auto pSet = pHDR->pHitboxSet(pPlayer->m_nHitboxSet());
	if (!pSet)
		return false;

	const auto pBox = pSet->pHitbox(HITBOX_HEAD);
	if (!pBox)
		return false;

	matrix3x4_t boneMatrix[128] = {};
	if (!pPlayer->SetupBones(boneMatrix, 128, 0x100, I::GlobalVars->curtime))
		return false;

	const Vec3 vMins = pBox->bbmin;
	const Vec3 vMaxs = pBox->bbmax;

	const std::array vPoints = {
		Vec3((vMins.x + vMaxs.x) * 0.5f, vMins.y * 0.7f, (vMins.z + vMaxs.z) * 0.5f)
	};

	const Vec3 vLocalPos = GetPredictedShootPos(pLocal, pCmd);
	for (const auto& vPoint : vPoints)
	{
		Vec3 vTransformed = {};
		Math::VectorTransform(vPoint, boneMatrix[pBox->bone], vTransformed);

		int nHitHitbox = -1;

		if (!H::AimUtils->TraceEntityBullet(pPlayer, vLocalPos, vTransformed, &nHitHitbox))
			continue;

		if (nHitHitbox != HITBOX_HEAD)
			continue;

		target.Position = vTransformed;
		target.AngleTo = Math::CalcAngle(vLocalPos, vTransformed);
		target.WasMultiPointed = true;

		return true;
	}

	return false;
}

bool CAimbotHitscan::ScanBody(C_TFPlayer* pLocal, const CUserCmd* pCmd, HitscanTarget_t& target)
{
	const bool bScanningBody = CFG::Aimbot_Hitscan_Scan_Body;
	const bool bScaningArms = CFG::Aimbot_Hitscan_Scan_Arms;
	const bool bScanningLegs = CFG::Aimbot_Hitscan_Scan_Legs;

	if (!bScanningBody && !bScaningArms && !bScanningLegs)
		return false;

	const auto pPlayer = target.Entity->As<C_TFPlayer>();
	if (!pPlayer)
		return false;

	const Vec3 vLocalPos = GetPredictedShootPos(pLocal, pCmd);
	for (int n = 1; n < pPlayer->GetNumOfHitboxes(); n++)
	{
		if (n == target.AimedHitbox)
			continue;

		const int nHitboxGroup = pPlayer->GetHitboxGroup(n);

		if (!bScanningBody && (nHitboxGroup == HITGROUP_CHEST || nHitboxGroup == HITGROUP_STOMACH))
			continue;

		if (!bScaningArms && (nHitboxGroup == HITGROUP_LEFTARM || nHitboxGroup == HITGROUP_RIGHTARM))
			continue;

		if (!bScanningLegs && (nHitboxGroup == HITGROUP_LEFTLEG || nHitboxGroup == HITGROUP_RIGHTLEG))
			continue;

		Vec3 vHitbox = pPlayer->GetHitboxPos(n);

		if (!H::AimUtils->TraceEntityBullet(pPlayer, vLocalPos, vHitbox))
			continue;

		target.Position = vHitbox;
		target.AngleTo = Math::CalcAngle(vLocalPos, vHitbox);

		return true;
	}

	return false;
}

bool CAimbotHitscan::ScanBuilding(C_TFPlayer* pLocal, const CUserCmd* pCmd, HitscanTarget_t& target)
{
	if (!CFG::Aimbot_Hitscan_Scan_Buildings)
		return false;

	const auto pObject = target.Entity->As<C_BaseObject>();
	if (!pObject)
		return false;

	const Vec3 vLocalPos = GetPredictedShootPos(pLocal, pCmd);

	if (pObject->GetClassId() == ETFClassIds::CObjectSentrygun)
	{
		for (int n = 0; n < pObject->GetNumOfHitboxes(); n++)
		{
			Vec3 vHitbox = pObject->GetHitboxPos(n);

			if (!H::AimUtils->TraceEntityBullet(pObject, vLocalPos, vHitbox))
				continue;

			target.Position = vHitbox;
			target.AngleTo = Math::CalcAngle(vLocalPos, vHitbox);

			return true;
		}
	}

	else
	{
		const Vec3 vMins = pObject->m_vecMins();
		const Vec3 vMaxs = pObject->m_vecMaxs();

		const std::array vPoints = {
			Vec3(vMins.x * 0.9f, ((vMins.y + vMaxs.y) * 0.5f), ((vMins.z + vMaxs.z) * 0.5f)),
			Vec3(vMaxs.x * 0.9f, ((vMins.y + vMaxs.y) * 0.5f), ((vMins.z + vMaxs.z) * 0.5f)),
			Vec3(((vMins.x + vMaxs.x) * 0.5f), vMins.y * 0.9f, ((vMins.z + vMaxs.z) * 0.5f)),
			Vec3(((vMins.x + vMaxs.x) * 0.5f), vMaxs.y * 0.9f, ((vMins.z + vMaxs.z) * 0.5f)),
			Vec3(((vMins.x + vMaxs.x) * 0.5f), ((vMins.y + vMaxs.y) * 0.5f), vMins.z * 0.9f),
			Vec3(((vMins.x + vMaxs.x) * 0.5f), ((vMins.y + vMaxs.y) * 0.5f), vMaxs.z * 0.9f)
		};

		const matrix3x4_t& transform = pObject->RenderableToWorldTransform();
		for (const auto& vPoint : vPoints)
		{
			Vec3 vTransformed = {};
			Math::VectorTransform(vPoint, transform, vTransformed);

			if (!H::AimUtils->TraceEntityBullet(pObject, vLocalPos, vTransformed))
				continue;

			target.Position = vTransformed;
			target.AngleTo = Math::CalcAngle(vLocalPos, vTransformed);

			return true;
		}
	}

	return false;
}

bool CAimbotHitscan::GetTarget(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const CUserCmd* pCmd, HitscanTarget_t& outTarget)
{
	const Vec3 vLocalPos = GetPredictedShootPos(pLocal, pCmd);
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	m_vecTargets.clear();

	// Find player targets
	if (CFG::Aimbot_Target_Players)
	{
		const int nAimHitbox = GetAimHitbox(pWeapon);

		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
		{
			if (!pEntity)
				continue;

			const auto pPlayer = pEntity->As<C_TFPlayer>();
			if (pPlayer->deadflag() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
				continue;

			if (CFG::Aimbot_Ignore_Friends && pPlayer->IsPlayerOnSteamFriendsList())
				continue;

			if (CFG::Aimbot_Ignore_Invisible && pPlayer->IsInvisible())
				continue;

			if (CFG::Aimbot_Ignore_Invulnerable && pPlayer->IsInvulnerable())
				continue;

			if (CFG::Aimbot_Ignore_Taunting && pPlayer->InCond(TF_COND_TAUNTING))
				continue;

			if (CFG::Aimbot_Hitscan_Target_LagRecords)
			{
				int nRecords = 0;
				bool bHasValidLagRecords = false;

				if (F::LagRecords->HasRecords(pPlayer, &nRecords))
				{
					// Determine backtrack range based on weapon and fake latency
					const bool bFakeLatencyActive = F::LagRecords->GetFakeLatency() > 0.0f;
					const int nWeaponID = pWeapon->GetWeaponID();
					const bool bIsSniperRifle = (nWeaponID == TF_WEAPON_SNIPERRIFLE || 
												 nWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC || 
												 nWeaponID == TF_WEAPON_SNIPERRIFLE_DECAP);
					const bool bIsSniper = pLocal->m_iClass() == TF_CLASS_SNIPER;
					const bool bIsAmbassador = (pWeapon->m_iItemDefinitionIndex() == Spy_m_TheAmbassador || 
												pWeapon->m_iItemDefinitionIndex() == Spy_m_FestiveAmbassador);
					
					int nStartRecord = 1;
					int nEndRecord = nRecords;
					const bool bDoubletap = Shifting::bRapidFireWantShift;
					
					// When fake latency is 0 and using sniper rifle: add original model as valid target alongside backtrack
					if (!bFakeLatencyActive && bIsSniper && bIsSniperRifle)
					{
						Vec3 vPos = pPlayer->GetHitboxPos(nAimHitbox);
						Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
						const float flFOVTo = CFG::Aimbot_Hitscan_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
						const float flDistTo = vLocalPos.DistTo(vPos);

						if (CFG::Aimbot_Hitscan_Sort == 0 && flFOVTo <= CFG::Aimbot_Hitscan_FOV)
						{
							m_vecTargets.emplace_back(AimTarget_t { pPlayer, vPos, vAngleTo, flFOVTo, flDistTo}, nAimHitbox, pPlayer->m_flSimulationTime());
						}
					}
					
					// Sniper rifles, Ambassador, OR fake latency: prioritize 3rd-from-last backtrack
					if ((bIsSniper && bIsSniperRifle) || bIsAmbassador || bFakeLatencyActive)
					{
						nStartRecord = std::max(1, nRecords - 5);
						// Preserve DT last tick avoidance when resetting end record
						nEndRecord = bDoubletap && nRecords > 1 ? nRecords - 1 : nRecords;
						
						// Prioritize the 3rd-from-last backtrack first (best balance, avoids blocking)
						const int nPriorityRecord = nRecords - 3;
						if (nPriorityRecord >= 1 && nPriorityRecord < nRecords)
						{
							const auto pRecord = F::LagRecords->GetRecord(pPlayer, nPriorityRecord, true);
							if (pRecord && F::LagRecords->DiffersFromCurrent(pRecord))
							{
								bHasValidLagRecords = true;
								Vec3 vPos = SDKUtils::GetHitboxPosFromMatrix(pPlayer, nAimHitbox, const_cast<matrix3x4_t*>(pRecord->BoneMatrix));
								Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
								const float flFOVTo = CFG::Aimbot_Hitscan_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
								const float flDistTo = vLocalPos.DistTo(vPos);

								if (CFG::Aimbot_Hitscan_Sort == 0 && flFOVTo <= CFG::Aimbot_Hitscan_FOV)
								{
									m_vecTargets.emplace_back(AimTarget_t {
										pPlayer, vPos, vAngleTo, flFOVTo, flDistTo
									}, nAimHitbox, pRecord->SimulationTime, pRecord);
								}
							}
						}
					}

					for (int n = nStartRecord; n < nEndRecord; n++)
					{
						// Skip the priority record if we already added it
						if (((bIsSniper && bIsSniperRifle) || bIsAmbassador || bFakeLatencyActive) && n == nRecords - 3)
							continue;
						
						const auto pRecord = F::LagRecords->GetRecord(pPlayer, n, true);

						if (!pRecord || !F::LagRecords->DiffersFromCurrent(pRecord))
							continue;

						bHasValidLagRecords = true;
						Vec3 vPos = SDKUtils::GetHitboxPosFromMatrix(pPlayer, nAimHitbox, const_cast<matrix3x4_t*>(pRecord->BoneMatrix));
						Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
						const float flFOVTo = CFG::Aimbot_Hitscan_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
						const float flDistTo = vLocalPos.DistTo(vPos);

						if (CFG::Aimbot_Hitscan_Sort == 0 && flFOVTo > CFG::Aimbot_Hitscan_FOV)
							continue;

						m_vecTargets.emplace_back(AimTarget_t {
							pPlayer, vPos, vAngleTo, flFOVTo, flDistTo
						}, nAimHitbox, pRecord->SimulationTime, pRecord);
					}
				}

				// Fallback: if no valid lag records exist that differ from current (enemy standing still), target the real model position
				// Skip this for sniper rifles with no fake latency since we already added the original model above
				const bool bFakeLatencyActive = F::LagRecords->GetFakeLatency() > 0.0f;
				const int nWeaponID = pWeapon->GetWeaponID();
				const bool bIsSniperRifle = (nWeaponID == TF_WEAPON_SNIPERRIFLE || 
											 nWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC || 
											 nWeaponID == TF_WEAPON_SNIPERRIFLE_DECAP);
				const bool bIsSniper = pLocal->m_iClass() == TF_CLASS_SNIPER;
				const bool bAlreadyAddedOriginal = !bFakeLatencyActive && bIsSniper && bIsSniperRifle;
				
				if (!bHasValidLagRecords && !bAlreadyAddedOriginal)
				{
					Vec3 vPos = pPlayer->GetHitboxPos(nAimHitbox);
					Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
					const float flFOVTo = CFG::Aimbot_Hitscan_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
					const float flDistTo = vLocalPos.DistTo(vPos);

					if (CFG::Aimbot_Hitscan_Sort == 0 && flFOVTo <= CFG::Aimbot_Hitscan_FOV)
					{
						m_vecTargets.emplace_back(AimTarget_t { pPlayer, vPos, vAngleTo, flFOVTo, flDistTo}, nAimHitbox, pPlayer->m_flSimulationTime());
					}
				}
			}

			else
			{
				// Not using lag records, just target current position
				Vec3 vPos = pPlayer->GetHitboxPos(nAimHitbox);
				Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
				const float flFOVTo = CFG::Aimbot_Hitscan_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
				const float flDistTo = vLocalPos.DistTo(vPos);

				if (CFG::Aimbot_Hitscan_Sort == 0 && flFOVTo > CFG::Aimbot_Hitscan_FOV)
					continue;

				m_vecTargets.emplace_back(AimTarget_t { pPlayer, vPos, vAngleTo, flFOVTo, flDistTo}, nAimHitbox, pPlayer->m_flSimulationTime());
			}
		}
	}

	// Find Building targets
	if (CFG::Aimbot_Target_Buildings)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
		{
			if (!pEntity)
				continue;

			const auto pBuilding = pEntity->As<C_BaseObject>();
			if (pBuilding->m_bPlacing())
				continue;

			Vec3 vPos = pBuilding->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = CFG::Aimbot_Hitscan_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
			const float flDistTo = vLocalPos.DistTo(vPos);

			if (CFG::Aimbot_Hitscan_Sort == 0 && flFOVTo > CFG::Aimbot_Hitscan_FOV)
				continue;

			m_vecTargets.emplace_back(AimTarget_t { pBuilding, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	// Find stickybomb targets
	if (CFG::Aimbot_Hitscan_Target_Stickies)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ENEMIES))
		{
			if (!pEntity || pEntity->GetClassId() != ETFClassIds::CTFGrenadePipebombProjectile)
			{
				continue;
			}

			const auto pipe = pEntity->As<C_TFGrenadePipebombProjectile>();
			if (!pipe || !pipe->m_bTouched() || !pipe->HasStickyEffects() || pipe->m_iType() == TF_GL_MODE_REMOTE_DETONATE_PRACTICE)
			{
				continue;
			}

			Vec3 vPos = pipe->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = CFG::Aimbot_Hitscan_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
			const float flDistTo = vLocalPos.DistTo(vPos);

			if (CFG::Aimbot_Hitscan_Sort == 0 && flFOVTo > CFG::Aimbot_Hitscan_FOV)
				continue;

			m_vecTargets.emplace_back(AimTarget_t {pipe, vPos, vAngleTo, flFOVTo, flDistTo});
		}
	}

	if (m_vecTargets.empty())
		return false;

	// Sort by target priority
	F::AimbotCommon->Sort(m_vecTargets, CFG::Aimbot_Hitscan_Sort);

	// Find and return the first valid target
	for (auto& target : m_vecTargets)
	{
		switch (target.Entity->GetClassId())
		{
			case ETFClassIds::CTFPlayer:
			{
				if (!target.LagRecord)
				{
					int nHitHitbox = -1;

					if (!H::AimUtils->TraceEntityBullet(target.Entity, vLocalPos, target.Position, &nHitHitbox))
					{
						if (target.AimedHitbox == HITBOX_HEAD)
						{
							if (!ScanHead(pLocal, pCmd, target))
								continue;
						}

						else if (target.AimedHitbox == HITBOX_PELVIS)
						{
							if (!ScanBody(pLocal, pCmd, target))
								continue;
						}

						else
						{
							continue;
						}
					}

					else
					{
						if (nHitHitbox != target.AimedHitbox && target.AimedHitbox == HITBOX_HEAD)
							ScanHead(pLocal, pCmd, target);
					}
				}

				else
				{
					F::LagRecordMatrixHelper->Set(target.LagRecord);

					int nHitHitbox = -1;
					const bool bTraceResult = H::AimUtils->TraceEntityBullet(target.Entity, vLocalPos, target.Position, &nHitHitbox);

					F::LagRecordMatrixHelper->Restore();

					if (!bTraceResult)
						continue;

					// For lag records aiming at head, verify we actually hit the head hitbox
					if (target.AimedHitbox == HITBOX_HEAD && nHitHitbox != HITBOX_HEAD)
						continue;
				}

				break;
			}

			case ETFClassIds::CObjectSentrygun:
			case ETFClassIds::CObjectDispenser:
			case ETFClassIds::CObjectTeleporter:
			{
				if (!H::AimUtils->TraceEntityBullet(target.Entity, vLocalPos, target.Position))
				{
					if (!ScanBuilding(pLocal, pCmd, target))
						continue;
				}

				break;
			}

			case ETFClassIds::CTFGrenadePipebombProjectile:
			{
				if (!H::AimUtils->TraceEntityBullet(target.Entity, vLocalPos, target.Position))
				{
					continue;
				}

				break;
			}

			default: continue;
		}

		outTarget = target;
		return true;
	}

	return false;
}

bool CAimbotHitscan::ShouldAim(const CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (CFG::Aimbot_Hitscan_Aim_Type == 1 && (!IsFiring(pCmd, pWeapon) || !pWeapon->HasPrimaryAmmoForShot()))
		return false;

	// Smooth and Triggerbot share the same ShouldAim behavior
	if (CFG::Aimbot_Hitscan_Aim_Type == 2 || CFG::Aimbot_Hitscan_Aim_Type == 3)
	{
		const int nWeaponID = pWeapon->GetWeaponID();
		if (nWeaponID == TF_WEAPON_SNIPERRIFLE || nWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC || nWeaponID == TF_WEAPON_SNIPERRIFLE_DECAP)
		{
			if (!G::bCanPrimaryAttack)
				return false;
		}
	}

	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && pWeapon->As<C_TFMinigun>()->m_iWeaponState() == AC_STATE_DRYFIRE)
		return false;

	return true;
}
//
void CAimbotHitscan::Aim(CUserCmd* pCmd, C_TFPlayer* pLocal, const Vec3& vAngles)
{
	Vec3 vAngleTo = vAngles - pLocal->m_vecPunchAngle();
	

	Math::ClampAngles(vAngleTo);

	switch (CFG::Aimbot_Hitscan_Aim_Type)
	{
		// Plain
		case 0:
		{
			pCmd->viewangles = vAngleTo;
			break;
		}
		
		// Silent (only set angles on the EXACT tick when firing)
		case 1:
		{
			// Check if we're actually going to fire THIS tick
			// G::Attacking is set BEFORE aimbot runs, so we need to check directly
			// We fire when: can attack AND pressing attack (or aimbot added IN_ATTACK)
			const bool bWillFire = G::bCanPrimaryAttack && (pCmd->buttons & IN_ATTACK);
			
			if (bWillFire)
			{
				H::AimUtils->FixMovement(pCmd, vAngleTo);
				pCmd->viewangles = vAngleTo;
				G::bSilentAngles = true;
			}

			break;
		}

		// Smooth
		case 2:
		{
			// Use ENGINE view angles, not cmd angles
			// This is critical because anti-aim modifies pCmd->viewangles AFTER aimbot
			// We need to smooth from what the player actually sees (engine angles)
			Vec3 vCurrentAngles = I::EngineClient->GetViewAngles();
			Vec3 vDelta = vAngleTo - vCurrentAngles;
			Math::ClampAngles(vDelta);

			// Apply smoothing
			if (vDelta.Length() > 0.0f && CFG::Aimbot_Hitscan_Smoothing > 0.f)
			{
				Vec3 vNewAngles = vCurrentAngles + vDelta / CFG::Aimbot_Hitscan_Smoothing;
				Math::ClampAngles(vNewAngles);
				pCmd->viewangles = vNewAngles;
				
				// Store smooth aim angles for restoration at end of CreateMove
				// This prevents AA from overwriting our view with the original angles
				G::vSmoothAimAngles = vNewAngles;
				G::bUseSmoothAimAngles = true;
			}

			break;
		}

		// Triggerbot - same as smooth with smoothing at 0 (no angle adjustment)
		case 3:
		{
			// Smooth at 0 does nothing because the condition (Smoothing > 0) is false
			// So triggerbot just doesn't modify angles at all
			break;
		}

		default: break;
	}
}

bool CAimbotHitscan::ShouldFire(const CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const HitscanTarget_t& target)
{
	if (!CFG::Aimbot_AutoShoot)
		return false;

	// Smart Shotgun Damage Prediction - CHECK FIRST before hitchance
	// This ensures RapidFire won't start if smart shotgun would block the shot
	if (CFG::Aimbot_Hitscan_Smart_Shotgun && target.Entity && target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
	{
		const int wid = pWeapon->GetWeaponID();
		const bool bIsShotgun = (
			wid == TF_WEAPON_SCATTERGUN ||
			wid == TF_WEAPON_SODA_POPPER ||
			wid == TF_WEAPON_PEP_BRAWLER_BLASTER ||
			wid == TF_WEAPON_SHOTGUN_PRIMARY ||
			wid == TF_WEAPON_SHOTGUN_SOLDIER ||
			wid == TF_WEAPON_SHOTGUN_HWG ||
			wid == TF_WEAPON_SHOTGUN_PYRO
		);

		if (bIsShotgun)
		{
			// Check if double-tap is available
			const bool bDoubleTapKeyHeld = CFG::Exploits_RapidFire_Key != 0 && H::Input->IsDown(CFG::Exploits_RapidFire_Key);
			const int nStoredTicks = F::RapidFire->GetTicks(pWeapon);
			const bool bCanDoubleTap = bDoubleTapKeyHeld && nStoredTicks >= CFG::Exploits_RapidFire_Ticks;

			// Only apply smart shotgun logic when double-tap is ready
			if (bCanDoubleTap)
			{
				const auto pPlayer = target.Entity->As<C_TFPlayer>();

				// Build config for double-tap mode
				SmartShotgunConfig config = {};
				config.bDoubleTapEnabled = true;
				config.nDoubleTapTicksRequired = CFG::Exploits_RapidFire_Ticks;
				config.nDoubleTapTicksStored = nStoredTicks;
				config.flDoubleTapMinPelletPercent = 0.50f;  // 50% for double tap
				config.flNormalMinPelletPercent = 0.10f;
				config.bCanCrit = pLocal->IsCritBoosted();
				config.bHoldingCritKey = CFG::Exploits_Crits_Force_Crit_Key != 0 && H::Input->IsDown(CFG::Exploits_Crits_Force_Crit_Key);
				config.bRapidFireReady = true;
				
				// Get smart shot decision
				SmartShotDecision decision = g_SmartShotgun.ShouldShoot(pLocal, pWeapon, pPlayer, config);
				
				if (!decision.bShouldShoot)
					return false;
			}
			// When not double-tapping, use normal aimbot logic (no additional checks)
		}
	}

	// Hitchance check - calculate if we meet the required hit probability
	// NOTE: Minigun uses tapfire delay system instead of hitchance blocking
	// The tapfire delay is checked later and scales with distance + hitchance slider
	if (CFG::Aimbot_Hitscan_Hitchance > 0 && target.Entity && pWeapon->GetWeaponID() != TF_WEAPON_MINIGUN)
	{
		// Get hitbox radius based on target type
		float flHitboxRadius = 12.0f; // Default for players (head ~24 units diameter)
		
		if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
		{
			const auto pPlayer = target.Entity->As<C_TFPlayer>();
			if (pPlayer)
			{
				// Use different radius based on aimed hitbox
				// Head is smaller (~12 radius), body is larger (~18 radius)
				if (target.AimedHitbox == HITBOX_HEAD)
					flHitboxRadius = 10.0f;
				else
					flHitboxRadius = 16.0f;
			}
		}
		else if (target.Entity->GetClassId() == ETFClassIds::CObjectSentrygun ||
				 target.Entity->GetClassId() == ETFClassIds::CObjectDispenser ||
				 target.Entity->GetClassId() == ETFClassIds::CObjectTeleporter)
		{
			// Buildings are larger targets
			flHitboxRadius = 24.0f;
		}
		else if (target.Entity->GetClassId() == ETFClassIds::CTFGrenadePipebombProjectile)
		{
			// Stickies are small
			flHitboxRadius = 8.0f;
		}

		// Check if RapidFire/DoubleTap is active
		const bool bRapidFireKey = CFG::Exploits_RapidFire_Key != 0 && H::Input->IsDown(CFG::Exploits_RapidFire_Key);
		const int nStoredTicks = F::RapidFire->GetTicks(pWeapon);
		const bool bRapidFireActive = bRapidFireKey && nStoredTicks >= CFG::Exploits_RapidFire_Ticks;
		const int nRapidFireShots = bRapidFireActive ? 2 : 1; // DoubleTap fires 2 shots

		const Vec3 vShootPos = pLocal->GetShootPos();
		const float flHitchance = F::Hitchance->Calculate(pLocal, pWeapon, vShootPos, target.Position, flHitboxRadius, bRapidFireActive, nRapidFireShots);
		
		if (flHitchance < static_cast<float>(CFG::Aimbot_Hitscan_Hitchance))
		{
			return false;
		}
	}

	// FakeLag Fix - only shoot when target unchokes (their position is accurate)
	if (CFG::Aimbot_Hitscan_FakeLagFix && target.Entity && target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
	{
		const auto pTarget = target.Entity->As<C_TFPlayer>();
		if (pTarget && !F::FakeLagFix->ShouldShoot(pTarget))
			return false;
	}

	const bool bIsMachina = pWeapon->m_iItemDefinitionIndex() == Sniper_m_TheMachina || pWeapon->m_iItemDefinitionIndex() == Sniper_m_ShootingStar;
	const bool bCapableOfHeadshot = H::AimUtils->IsWeaponCapableOfHeadshot(pWeapon);
	const bool bIsSydneySleeper = pWeapon->m_iItemDefinitionIndex() == Sniper_m_TheSydneySleeper;
	const bool bIsSniper = pLocal->m_iClass() == TF_CLASS_SNIPER;

	if (bIsMachina && !pLocal->IsZoomed())
		return false;

	if (CFG::Aimbot_Hitscan_Wait_For_Headshot)
	{
		if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer && bCapableOfHeadshot && !G::bCanHeadshot)
			return false;
	}

	if (CFG::Aimbot_Hitscan_Wait_For_Charge)
	{
		if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer && bIsSniper && (bCapableOfHeadshot || bIsSydneySleeper))
		{
			const auto pPlayer = target.Entity->As<C_TFPlayer>();
			const auto pSniperRifle = pWeapon->As<C_TFSniperRifle>();

			const int nHealth = pPlayer->m_iHealth();
			const bool bIsCritBoosted = pLocal->IsCritBoosted();

			if (target.AimedHitbox == HITBOX_HEAD && !bIsSydneySleeper)
			{
				if (nHealth > 150)
				{
					const float flDamage = Math::RemapValClamped(pSniperRifle->m_flChargedDamage(), 0.0f, 150.0f, 0.0f, 450.0f);
					const int nDamage = static_cast<int>(flDamage);

					if (nDamage < nHealth && nDamage != 450)
						return false;
				}

				else
				{
					if (!bIsCritBoosted && !G::bCanHeadshot)
						return false;
				}
			}

			else
			{
				if (nHealth > (bIsCritBoosted ? 150 : 50))
				{
					float flMult = pPlayer->IsMarked() ? 1.36f : 1.0f;

					if (bIsCritBoosted)
						flMult = 3.0f;

					const float flMax = 150.0f * flMult;
					const int nDamage = static_cast<int>(pSniperRifle->m_flChargedDamage() * flMult);

					if (nDamage < pPlayer->m_iHealth() && nDamage != static_cast<int>(flMax))
						return false;
				}
			}
		}
	}

	// Minigun tapfire - uses hitchance slider to control delay based on distance
	// This replaces the hitchance check for minigun (minigun is excluded from hitchance above)
	// Returns false if tapfire delay not met - the Run function will handle removing IN_ATTACK
	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && CFG::Aimbot_Hitscan_Hitchance > 0)
	{
		const float flDistance = pLocal->GetAbsOrigin().DistTo(target.Position);
		
		// Get hitbox radius for the calculation
		float flHitboxRadius = 16.0f; // Default body
		if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
			flHitboxRadius = (target.AimedHitbox == HITBOX_HEAD) ? 10.0f : 16.0f;
		
		// Get optimal delay from hitchance system (uses hitchance slider for max delay)
		// Close range = no delay, far range = up to 0.3s delay at 100% hitchance
		const float flRequiredDelay = F::Hitchance->GetMinigunOptimalTapfireDelay(pWeapon, pLocal, flDistance, flHitboxRadius);
		
		if (flRequiredDelay > 0.0f)
		{
			const float flTimeSinceFire = (pLocal->m_nTickBase() * TICK_INTERVAL) - pWeapon->m_flLastFireTime();
			if (flTimeSinceFire < flRequiredDelay)
				return false;
		}
	}

	// Smooth autoshoot check OR Triggerbot - only fire when crosshair is on target
	// Triggerbot is essentially smooth with smoothing=0, so it always needs this check
	if ((CFG::Aimbot_Hitscan_Advanced_Smooth_AutoShoot && CFG::Aimbot_Hitscan_Aim_Type == 2) || CFG::Aimbot_Hitscan_Aim_Type == 3)
	{
		Vec3 vForward = {};
		Math::AngleVectors(pCmd->viewangles, &vForward);
		const Vec3 vTraceStart = pLocal->GetShootPos();
		const Vec3 vTraceEnd = vTraceStart + (vForward * 8192.0f);

		if (target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
		{
			const auto pPlayer = target.Entity->As<C_TFPlayer>();

			const bool bTriggerbotMode = CFG::Aimbot_Hitscan_Aim_Type == 3;

			// Triggerbot: use stricter center-of-hitbox check to avoid edge shots
			// This ensures we're not just grazing the edge of the hitbox
			if (bTriggerbotMode && target.AimedHitbox == HITBOX_HEAD)
			{
				int nHitHitbox = -1;

				if (!H::AimUtils->TraceEntityBullet(pPlayer, vTraceStart, vTraceEnd, &nHitHitbox))
					return false;

				if (nHitHitbox != HITBOX_HEAD)
					return false;

				// Additional check: make sure we're hitting closer to the center of the hitbox
				// Use the same RayToOBB check that smooth autoshoot uses, but with smaller bounds
				Vec3 vMins = {}, vMaxs = {}, vCenter = {};
				matrix3x4_t matrix = {};
				pPlayer->GetHitboxInfo(nHitHitbox, &vCenter, &vMins, &vMaxs, &matrix);

				// Hardcoded per-class head hitbox scale (height and width)
				float flScaleH = 0.94f, flScaleW = 0.94f;
				switch (pPlayer->m_iClass())
				{
					case TF_CLASS_SCOUT:
						flScaleH = 0.95f;
						flScaleW = 0.95f;
						break;
					case TF_CLASS_SOLDIER:
						flScaleH = 0.22f;
						flScaleW = 0.70f;
						break;
					case TF_CLASS_PYRO:
						flScaleH = 0.69f;
						flScaleW = 0.69f;
						break;
					case TF_CLASS_DEMOMAN:
						flScaleH = 0.86f;
						flScaleW = 0.81f;
						break;
					case TF_CLASS_HEAVYWEAPONS:
						flScaleH = 0.60f;
						flScaleW = 0.25f;
						break;
					case TF_CLASS_ENGINEER:
						flScaleH = 0.91f;
						flScaleW = 0.88f;
						break;
					case TF_CLASS_MEDIC:
						flScaleH = 0.95f;
						flScaleW = 0.95f;
						break;
					case TF_CLASS_SNIPER:
						flScaleH = 0.92f;
						flScaleW = 0.69f;
						break;
					case TF_CLASS_SPY:
						flScaleH = 0.64f;
						flScaleW = 0.50f;
						break;
				}

				// Apply height scale to Z axis, width scale to X/Y axes
				vMins.x *= flScaleW;
				vMins.y *= flScaleW;
				vMins.z *= flScaleH;
				vMaxs.x *= flScaleW;
				vMaxs.y *= flScaleW;
				vMaxs.z *= flScaleH;

				if (!Math::RayToOBB(vTraceStart, vForward, vCenter, vMins, vMaxs, matrix))
					return false;
			}
			else if (!target.LagRecord)
			{
				int nHitHitbox = -1;

				if (!H::AimUtils->TraceEntityBullet(pPlayer, vTraceStart, vTraceEnd, &nHitHitbox))
					return false;

				// For both triggerbot and smooth autoshoot: if aiming for head, require head hit
				if (target.AimedHitbox == HITBOX_HEAD)
				{
					if (nHitHitbox != HITBOX_HEAD)
						return false;

					if (!bTriggerbotMode && !target.WasMultiPointed)
					{
						Vec3 vMins = {}, vMaxs = {}, vCenter = {};
						matrix3x4_t matrix = {};
						pPlayer->GetHitboxInfo(nHitHitbox, &vCenter, &vMins, &vMaxs, &matrix);

						vMins *= 0.5f;
						vMaxs *= 0.5f;

						if (!Math::RayToOBB(vTraceStart, vForward, vCenter, vMins, vMaxs, matrix))
							return false;
					}
				}
			}

			else
			{
				F::LagRecordMatrixHelper->Set(target.LagRecord);

				int nHitHitbox = -1;

				if (!H::AimUtils->TraceEntityBullet(pPlayer, vTraceStart, vTraceEnd, &nHitHitbox))
				{
					F::LagRecordMatrixHelper->Restore();
					return false;
				}

				// For both triggerbot and smooth autoshoot: if aiming for head, require head hit
				if (target.AimedHitbox == HITBOX_HEAD)
				{
					if (nHitHitbox != HITBOX_HEAD)
					{
						F::LagRecordMatrixHelper->Restore();
						return false;
					}

					if (!bTriggerbotMode && !target.WasMultiPointed)
					{
						Vec3 vMins = {}, vMaxs = {}, vCenter = {};
						SDKUtils::GetHitboxInfoFromMatrix(pPlayer, nHitHitbox, const_cast<matrix3x4_t*>(target.LagRecord->BoneMatrix), &vCenter, &vMins, &vMaxs);

						vMins *= 0.5f;
						vMaxs *= 0.5f;

						if (!Math::RayToOBB(vTraceStart, vForward, vCenter, vMins, vMaxs, *target.LagRecord->BoneMatrix))
						{
							F::LagRecordMatrixHelper->Restore();
							return false;
						}
					}
				}

				F::LagRecordMatrixHelper->Restore();
			}
		}

		else
		{
			if (!H::AimUtils->TraceEntityBullet(target.Entity, vTraceStart, vTraceEnd, nullptr))
			{
				return false;
			}
		}
	}

	return true;
}

// Handles and updated the IN_ATTACK state
void CAimbotHitscan::HandleFire(CUserCmd* pCmd, C_TFWeaponBase* pWeapon)
{
	if (!pWeapon->HasPrimaryAmmoForShot())
		return;

	if (pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC)
	{
		if (G::nOldButtons & IN_ATTACK)
		{
			pCmd->buttons &= ~IN_ATTACK;
		}
		else
		{
			pCmd->buttons |= IN_ATTACK;
		}
	}

	else
	{
		pCmd->buttons |= IN_ATTACK;
	}
}

bool CAimbotHitscan::IsFiring(const CUserCmd* pCmd, C_TFWeaponBase* pWeapon)
{
	if (!pWeapon->HasPrimaryAmmoForShot())
		return false;

	if (pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC)
		return !(pCmd->buttons & IN_ATTACK) && (G::nOldButtons & IN_ATTACK);

	// Normal case: attacking and can attack
	if ((pCmd->buttons & IN_ATTACK) && G::bCanPrimaryAttack)
		return true;
	
	// Reload interrupt case: attacking during reload with single-reload weapon that has ammo
	// For these weapons, pressing attack will abort reload and fire immediately
	if ((pCmd->buttons & IN_ATTACK) && pWeapon->IsInReload() && 
		pWeapon->m_bReloadsSingly() && pWeapon->m_iClip1() > 0)
	{
		return true;
	}
	
	return false;
}

void CAimbotHitscan::Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (!CFG::Aimbot_Hitscan_Active)
		return;

	// Set FOV for circle drawing - but not for triggerbot (no FOV circle needed)
	if (CFG::Aimbot_Hitscan_Sort == 0 && CFG::Aimbot_Hitscan_Aim_Type != 3)
		G::flAimbotFOV = CFG::Aimbot_Hitscan_FOV;
	else if (CFG::Aimbot_Hitscan_Aim_Type == 3)
		G::flAimbotFOV = 0.0f; // Disable FOV circle for triggerbot

	// Skip during doubletap shifting (but not warp)
	if (Shifting::bShifting && !Shifting::bShiftingWarp)
		return;

	const bool isFiring = IsFiring(pCmd, pWeapon);

	HitscanTarget_t target = {};
	if (GetTarget(pLocal, pWeapon, pCmd, target) && target.Entity)
	{
		G::nTargetIndexEarly = target.Entity->entindex();

		const auto aimKeyDown = H::Input->IsDown(CFG::Aimbot_Key);
		if (aimKeyDown || isFiring)
		{
			G::nTargetIndex = target.Entity->entindex();

			// Auto Scope
			if (CFG::Aimbot_Hitscan_Auto_Scope
				&& !pLocal->IsZoomed() && pLocal->m_iClass() == TF_CLASS_SNIPER && pWeapon->GetSlot() == WEAPON_SLOT_PRIMARY && G::bCanPrimaryAttack)
			{
				pCmd->buttons |= IN_ATTACK2;
				return;
			}

			// Auto Shoot
			if (CFG::Aimbot_AutoShoot && pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC)
				pCmd->buttons |= IN_ATTACK;

			// Spin up minigun
			if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
			{
				const int nState = pWeapon->As<C_TFMinigun>()->m_iWeaponState();
				if (nState == AC_STATE_IDLE || nState == AC_STATE_STARTFIRING)
					G::bCanPrimaryAttack = false; // TODO: hack

				// Always spin up (IN_ATTACK2) when targeting
				pCmd->buttons |= IN_ATTACK2;
				
				// For minigun, add IN_ATTACK when in firing/spinning state
				// The tapfire check in ShouldFire will remove it if needed based on distance
				// This also ensures G::LastUserCmd has IN_ATTACK for the crithack
				if (nState == AC_STATE_FIRING || nState == AC_STATE_SPINNING)
				{
					if (pWeapon->HasPrimaryAmmoForShot())
						pCmd->buttons |= IN_ATTACK;
				}
			}

			bool bShouldFire = ShouldFire(pCmd, pLocal, pWeapon, target);

			if (bShouldFire)
			{
				HandleFire(pCmd, pWeapon);
			}
			else
			{
				// If ShouldFire returned false for minigun, remove IN_ATTACK but keep revved
				// This ensures the minigun stays spun up while waiting for tapfire delay
				if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
					pCmd->buttons &= ~IN_ATTACK;
			}

			const bool bIsFiring = IsFiring(pCmd, pWeapon);
			G::bFiring = bIsFiring;

			// Are we ready to aim?
			if (ShouldAim(pCmd, pLocal, pWeapon) || bIsFiring)
			{
				if (aimKeyDown)
				{
					Aim(pCmd, pLocal, target.AngleTo);
				}

				// Set tick_count for lag compensation
				if (bIsFiring && target.Entity->GetClassId() == ETFClassIds::CTFPlayer)
				{
					if (CFG::Misc_AntiCheat_Enabled)
					{
						// Anti-cheat compatibility: match Amalgam's behavior
						// In Amalgam, CreateMove() returns early when anti-cheat is enabled,
						// which means tick_count is NOT adjusted for fake interp.
						// We still need to set tick_count for the backtrack record though.
						// Use just the lerp value, don't add fake interp (matching Amalgam's skip)
						pCmd->tick_count = TIME_TO_TICKS(target.SimulationTime + SDKUtils::GetLerp());
					}
					else if (CFG::Misc_Accuracy_Improvements)
					{
						pCmd->tick_count = TIME_TO_TICKS(target.SimulationTime + SDKUtils::GetLerp());
					}
					else if (target.LagRecord)
					{
						pCmd->tick_count = TIME_TO_TICKS(target.SimulationTime + GetClientInterpAmount());
					}
				}
			}
		}
	}
}

void CAimbotHitscan::HandleSwitchKey()
{
	if (CFG::Aimbot_Hitscan_Hitbox != 3 || CFG::Aimbot_Hitscan_Switch_Key == 0)
		return;

	// Don't process when menu/cursor is active
	if (I::MatSystemSurface->IsCursorVisible() || I::EngineVGui->IsGameUIVisible())
		return;

	// Use static to track previous key state for reliable edge detection
	static bool bWasPressed = false;
	const bool bIsPressed = H::Input->IsDown(CFG::Aimbot_Hitscan_Switch_Key);

	// Toggle on key press (rising edge)
	if (bIsPressed && !bWasPressed)
	{
		CFG::Aimbot_Hitscan_Switch_State = !CFG::Aimbot_Hitscan_Switch_State;
	}

	bWasPressed = bIsPressed;
}

void CAimbotHitscan::DragIndicator()
{
	const int nMouseX = H::Input->GetMouseX();
	const int nMouseY = H::Input->GetMouseY();
	
	static bool bDragging = false;
	static int nDeltaX = 0;
	static int nDeltaY = 0;
	
	if (!bDragging && F::Menu->IsMenuWindowHovered())
		return;
	
	const int x = CFG::Aimbot_Hitscan_Switch_Indicator_X;
	const int y = CFG::Aimbot_Hitscan_Switch_Indicator_Y;
	
	// Hover area around icon (40x40 area)
	const bool bHovered = nMouseX > x - 20 && nMouseX < x + 20 && nMouseY > y - 20 && nMouseY < y + 20;
	
	if (bHovered && H::Input->IsPressed(VK_LBUTTON))
	{
		nDeltaX = nMouseX - x;
		nDeltaY = nMouseY - y;
		bDragging = true;
	}
	
	if (!H::Input->IsPressed(VK_LBUTTON) && !H::Input->IsHeld(VK_LBUTTON))
		bDragging = false;
	
	if (bDragging)
	{
		CFG::Aimbot_Hitscan_Switch_Indicator_X = nMouseX - nDeltaX;
		CFG::Aimbot_Hitscan_Switch_Indicator_Y = nMouseY - nDeltaY;
	}
}

void CAimbotHitscan::DrawSwitchIndicator()
{
	// Only show when switch mode is active
	if (CFG::Aimbot_Hitscan_Hitbox != 3)
		return;

	if (I::EngineClient->IsTakingScreenshot())
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
		return;

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon)
		return;

	// Only show for headshot-capable weapons
	if (!IsHeadshotCapableWeapon(pWeapon))
		return;

	// Handle dragging when menu is open
	if (F::Menu->IsOpen())
		DragIndicator();

	const int x = CFG::Aimbot_Hitscan_Switch_Indicator_X;
	const int y = CFG::Aimbot_Hitscan_Switch_Indicator_Y;

	// Use secondary accent color
	Color_t clr = CFG::Menu_Accent_Secondary;
	
	// Apply RGB if enabled
	if (CFG::Menu_Accent_Secondary_RGB)
	{
		float flTime = I::GlobalVars->curtime * CFG::Menu_Accent_Secondary_RGB_Rate;
		clr.r = static_cast<byte>(127.5f + 127.5f * sinf(flTime));
		clr.g = static_cast<byte>(127.5f + 127.5f * sinf(flTime + 2.094f));
		clr.b = static_cast<byte>(127.5f + 127.5f * sinf(flTime + 4.188f));
	}

	const bool bIsBody = CFG::Aimbot_Hitscan_Switch_State;
	
	// Draw a simple head/body icon
	if (bIsBody)
	{
		// Body icon - rectangle representing torso
		H::Draw->OutlinedRect(x - 8, y - 12, 16, 24, clr);
		H::Draw->Rect(x - 6, y - 10, 12, 20, { clr.r, clr.g, clr.b, 100 });
		
		// Small head circle on top (dimmed)
		H::Draw->OutlinedRect(x - 4, y - 20, 8, 8, { clr.r, clr.g, clr.b, 80 });
		
		// Text label
		H::Draw->String(H::Fonts->Get(EFonts::ESP), x, y + 20, clr, POS_CENTERX, "BODY");
	}
	else
	{
		// Head icon - circle representing head
		H::Draw->OutlinedRect(x - 8, y - 8, 16, 16, clr);
		H::Draw->Rect(x - 6, y - 6, 12, 12, { clr.r, clr.g, clr.b, 100 });
		
		// Body below (dimmed)
		H::Draw->OutlinedRect(x - 6, y + 10, 12, 16, { clr.r, clr.g, clr.b, 80 });
		
		// Text label
		H::Draw->String(H::Fonts->Get(EFonts::ESP), x, y + 32, clr, POS_CENTERX, "HEAD");
	}
}
