#include "AutoBackstab.h"

#include "../../CFG.h"

#include "../../EnginePrediction/EnginePrediction.h"
#include "../../LagRecords/LagRecords.h"

#include <algorithm>
#include <vector>


bool HasActiveRazorback(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return false;
	
	int iBackstabShield = static_cast<int>(SDKUtils::AttribHookValue(0.f, "set_blockbackstab_once", pPlayer));
	if (iBackstabShield <= 0)
		return false;
	
	for (int i = 0; i < I::ClientEntityList->GetHighestEntityIndex(); i++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pClientEntity)
			continue;
		
		if (pClientEntity->GetClassId() != ETFClassIds::CTFWearableRazorback)
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

bool IsBehindAndFacingTarget(const Vec3& ownerCenter, const Vec3& ownerViewangles, const Vec3& targetCenter, const Vec3& targetEyeAngles, float flSafetyMargin = 0.0f, float* flOutBehind = nullptr, float* flOutFace = nullptr, float* flOutSame = nullptr)
{
	Vec3 toTarget = targetCenter - ownerCenter;
	toTarget.z = 0.0f;
	toTarget.NormalizeInPlace();

	Vec3 ownerForward{};
	Math::AngleVectors(ownerViewangles, &ownerForward);
	ownerForward.z = 0.0f;
	ownerForward.NormalizeInPlace();

	Vec3 targetForward{};
	Math::AngleVectors(targetEyeAngles, &targetForward);
	targetForward.z = 0.0f;
	targetForward.NormalizeInPlace();

	const float flBehindThreshold = 0.0f + 0.03125f + flSafetyMargin;
	const float flFacingThreshold = 0.5f + 0.03125f;
	const float flSameDirectionThreshold = -0.3f + 0.03125f + flSafetyMargin;

	const float flBehindDot = toTarget.Dot(targetForward);
	const float flFaceDot = toTarget.Dot(ownerForward);
	const float flSameDot = targetForward.Dot(ownerForward);

	if (flOutBehind)
		*flOutBehind = flBehindDot;
	if (flOutFace)
		*flOutFace = flFaceDot;
	if (flOutSame)
		*flOutSame = flSameDot;

	return flBehindDot > flBehindThreshold
		&& flFaceDot > flFacingThreshold
		&& flSameDot > flSameDirectionThreshold;
}

bool CanKnifeOneShot(C_TFPlayer* target, bool crit, bool miniCrit)
{
	if (!target || target->IsInvulnerable())
	{
		return false;
	}

	const auto pWeapon{ target->m_hActiveWeapon().Get() };

	if (!pWeapon)
	{
		return false;
	}

	int dmgMult = 1;

	if (miniCrit || target->IsMarked())
	{
		dmgMult = 2;
	}

	if (crit)
	{
		dmgMult = 3;
	}

	if (pWeapon->As<C_TFWeaponBase>()->m_iItemDefinitionIndex() == Heavy_t_FistsofSteel)
	{
		return target->m_iHealth() <= 80 * dmgMult;
	}

	return target->m_iHealth() <= 40 * dmgMult;
}

void CAutoBackstab::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!CFG::Triggerbot_AutoBackstab_Always_On && !CFG::Triggerbot_AutoBackstab_Active)
	{
		return;
	}

	if (pLocal->m_bFeignDeathReady() || pLocal->m_flInvisibility() > 0.0f || pWeapon->GetWeaponID() != TF_WEAPON_KNIFE
		|| I::GlobalVars->curtime < pLocal->m_flNextAttack()
		|| I::GlobalVars->curtime < pWeapon->m_flNextPrimaryAttack())
	{
		return;
	}

	const int nMaxClients = I::EngineClient->GetMaxClients();

	const float flIdealAge = F::LagRecords->GetServerAcceptedAge();
	const float flAgeRange = F::LagRecords->GetServerAcceptedAgeRange();
	float flSnapshotInterval = 0.05f;
	if (const auto pUpdaterate = I::CVar->FindVar("cl_updaterate"))
		flSnapshotInterval = 1.0f / std::max(pUpdaterate->GetFloat(), 1.0f);
	const float flExecDelay = std::min(F::LagRecords->GetOutgoingLatency() * 0.5f + I::GlobalVars->interval_per_tick + flSnapshotInterval * 0.5f, 0.1f);
	std::vector<C_TFPlayer*> vecCrowdPlayers;
	for (int n = 1; n <= nMaxClients; n++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(n);
		if (!pClientEntity || pClientEntity->IsDormant())
			continue;

		auto pNetworkable = pClientEntity->GetClientNetworkable();
		if (!pNetworkable || !pNetworkable->GetClientClass() || pNetworkable->GetClientClass()->m_ClassID != static_cast<int>(ETFClassIds::CTFPlayer))
			continue;

		const auto pCrowdPlayer = pClientEntity->As<C_TFPlayer>();
		if (!pCrowdPlayer || !H::Entities->IsEntityValid(pCrowdPlayer) || pCrowdPlayer->deadflag() || pCrowdPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;

		vecCrowdPlayers.push_back(pCrowdPlayer);
	}

	for (int n = 1; n <= nMaxClients; n++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(n);
		if (!pClientEntity || pClientEntity->IsDormant())
			continue;

		auto pNetworkable = pClientEntity->GetClientNetworkable();
		if (!pNetworkable || !pNetworkable->GetClientClass())
			continue;

		if (pNetworkable->GetClientClass()->m_ClassID != static_cast<int>(ETFClassIds::CTFPlayer))
			continue;

		const auto pPlayer = pClientEntity->As<C_TFPlayer>();
		if (!pPlayer)
			continue;

		if (!H::Entities->IsEntityValid(pPlayer) || pPlayer->deadflag() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
		{
			continue;
		}

		if (pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;

		if (CFG::Triggerbot_AutoBackstab_Ignore_Friends && pPlayer->IsPlayerOnSteamFriendsList())
		{
			continue;
		}

		if (CFG::Triggerbot_AutoBackstab_Ignore_Invisible && pPlayer->IsInvisible())
		{
			continue;
		}

		if (CFG::Triggerbot_AutoBackstab_Ignore_Invulnerable && pPlayer->IsInvulnerable())
		{
			continue;
		}

		if (CFG::Triggerbot_AutoBackstab_Ignore_Razorback && HasActiveRazorback(pPlayer))
		{
			continue;
		}

		auto canKnife = false;
		if (CFG::Triggerbot_AutoBackstab_Knife_If_Lethal)
		{
			canKnife = CanKnifeOneShot(pPlayer, pLocal->IsCritBoosted(), pLocal->IsMiniCritBoosted());
		}

		auto angleTo{ I::EngineClient->GetViewAngles() };

		if (CFG::Triggerbot_AutoBacktab_Mode == 1)
		{
			angleTo = Math::CalcAngle(pLocal->GetShootPos(), pPlayer->GetCenter());
		}

		const bool bFakeLatencyActive = F::LagRecords->GetFakeLatency() > 0.0f
			|| F::LagRecords->GetFakeInterp() > SDKUtils::GetLerp() + 0.05f;


		Vec3 vTargetEyeAngles = pPlayer->GetEyeAngles();
		float flYawRate = 0.0f;
		float flRecentMaxRate = 0.0f;
		{
			const LagRecord_t* pNewest = nullptr;
			const LagRecord_t* pOldest = nullptr;
			int nRecs = 0;
			if (F::LagRecords->HasRecords(pPlayer, &nRecs))
			{
				for (int r = 1; r <= nRecs; r++)
				{
					const auto record = F::LagRecords->GetRecord(pPlayer, r, true);
					if (!record || !record->bIsAlive)
						continue;

					const float flAge = I::GlobalVars->curtime - record->SimulationTime;
					if (fabsf(flAge - flIdealAge) > flAgeRange || flAge > F::LagRecords->GetMaxUnlag())
						continue;

					if (!pNewest)
						pNewest = record;
					pOldest = record;
				}
			}

			if (pNewest && pOldest && pNewest->SimulationTime - pOldest->SimulationTime > 0.001f)
			{
				float flTotalYaw = 0.0f;
				float flRecentYaw = 0.0f;
				const LagRecord_t* pStep = pOldest;
				const LagRecord_t* pRecentStep = nullptr;
				const LagRecord_t* pRecentFirst = nullptr;
				const float flRecentBase = pNewest->SimulationTime - 0.15f;
				for (int r = nRecs; r >= 1; r--)
				{
					const auto record = F::LagRecords->GetRecord(pPlayer, r, true);
					if (!record || !record->bIsAlive)
						continue;
					if (record->SimulationTime < pOldest->SimulationTime || record->SimulationTime > pNewest->SimulationTime)
						continue;

					flTotalYaw += Math::NormalizeAngle(record->EyeAngles.y - pStep->EyeAngles.y);
					pStep = record;

					if (record->SimulationTime >= flRecentBase)
					{
						if (!pRecentFirst)
							pRecentFirst = record;
						if (pRecentStep)
						{
							const float flStepTime = record->SimulationTime - pRecentStep->SimulationTime;
							if (flStepTime > 0.001f)
							{
								const float flStepRate = fabsf(Math::NormalizeAngle(record->EyeAngles.y - pRecentStep->EyeAngles.y)) / flStepTime;
								if (flStepRate > flRecentMaxRate)
									flRecentMaxRate = flStepRate;
							}
							flRecentYaw += Math::NormalizeAngle(record->EyeAngles.y - pRecentStep->EyeAngles.y);
						}
						pRecentStep = record;
					}
				}

				const float flSpan = pNewest->SimulationTime - pOldest->SimulationTime;
				if (flSpan > 0.001f)
				{
					if (pRecentFirst && pNewest->SimulationTime - pRecentFirst->SimulationTime > 0.001f)
						flYawRate = flRecentYaw / (pNewest->SimulationTime - pRecentFirst->SimulationTime);
					else
						flYawRate = flTotalYaw / flSpan;

					vTargetEyeAngles.y = Math::NormalizeAngle(vTargetEyeAngles.y + flYawRate * flExecDelay);
				}
			}
		}
		
		if (!bFakeLatencyActive)
		{
			const Vec3 vEye = pLocal->GetShootPos();
			if (vEye.DistTo(pPlayer->GetCenter()) <= 82.0f && (vEye - pPlayer->GetCenter()).Length2D() >= 35.0f)
			{
				if (canKnife || IsBehindAndFacingTarget(pLocal->GetCenter(), angleTo, pPlayer->GetCenter(), vTargetEyeAngles, 0.0f))
				{
						Vec3 forward{};
						Math::AngleVectors(angleTo, &forward);

						const Vec3 vTo = vEye + (forward * 48.0f);

						trace_t trace = {};
						CTraceFilterHitscan filter = {};
						Ray_t rayLine;
						rayLine.Init(vEye, vTo);
						I::EngineTrace->TraceRay(rayLine, MASK_SOLID, &filter, &trace);
						if (trace.fraction >= 1.0f)
							H::AimUtils->TraceHull(vEye, vTo, { -18.0f, -18.0f, -18.0f }, { 18.0f, 18.0f, 18.0f }, MASK_SOLID, &filter, &trace);

						if (trace.m_pEnt == pPlayer && !trace.startsolid && !trace.allsolid)
					{
						if (CFG::Triggerbot_AutoBacktab_Mode == 1)
						{
							pCmd->viewangles = angleTo;

							if (CFG::Triggerbot_AutoBacktab_Aim_Mode == 1)
							{
								G::bPSilentAngles = true;
							}
						}

						G::bSilentAngles = true;

						pCmd->buttons |= IN_ATTACK;
						G::Attacking = 1;
						G::bFiring = true;
						m_flLastStab = I::EngineClient->Time();

						if (CFG::Misc_Accuracy_Improvements)
						{
							pCmd->tick_count = TIME_TO_TICKS(pPlayer->m_flSimulationTime() + F::LagRecords->GetServerLerp());
						}

						return;
					}
				}
			}
		}

		int numRecords = 0;

		if (!F::LagRecords->HasRecords(pPlayer, &numRecords))
		{
			continue;
		}

		std::vector<const LagRecord_t*> vecRecords;
		for (int r = 1; r <= numRecords; r++)
		{
			const auto record = F::LagRecords->GetRecord(pPlayer, r, true);
			if (!record)
				continue;

			if (!record->bIsAlive || record->bInvalid)
				continue;

			const float flRecordAge = I::GlobalVars->curtime - record->SimulationTime;
			if (flRecordAge < flIdealAge - 0.1f || flRecordAge - flIdealAge > flAgeRange)
				continue;

			if (flRecordAge > F::LagRecords->GetMaxUnlag())
				continue;

			vecRecords.push_back(record);
		}

		std::sort(vecRecords.begin(), vecRecords.end(), [flIdealAge](const LagRecord_t* a, const LagRecord_t* b)
		{
			return fabsf((I::GlobalVars->curtime - a->SimulationTime) - flIdealAge)
				< fabsf((I::GlobalVars->curtime - b->SimulationTime) - flIdealAge);
		});

		auto GetNeighborExecRecord = [flExecDelay](C_TFPlayer* pNeighbor, float flClaimSimTime) -> LagRecord_t
		{
			int nRecs = 0;
			if (F::LagRecords->HasRecords(pNeighbor, &nRecs))
			{
				for (int r = nRecs; r >= 1; r--)
				{
					const auto record = F::LagRecords->GetRecord(pNeighbor, r, true);
					if (!record || !record->bIsAlive || record->bInvalid)
						continue;

					if (flClaimSimTime >= record->SimulationTime)
						return F::LagRecords->GetInterpolatedRecord(pNeighbor, flClaimSimTime);

					break;
				}
			}

			LagRecord_t tCurrent = F::LagRecords->GetInterpolatedRecord(pNeighbor, I::GlobalVars->curtime);
			const Vec3 vShift = tCurrent.Velocity * flExecDelay;
			tCurrent.AbsOrigin += vShift;
			tCurrent.VecOrigin += vShift;
			tCurrent.Center += vShift;
			return tCurrent;
		};

		auto CrowdTrace = [&](const LagRecord_t* pCandidate, const Vec3& vFrom, const Vec3& vTo, trace_t& outTrace)
		{
			std::vector<CLagRecordMatrixHelper> vecHelpers;
			for (const auto pCrowdPlayer : vecCrowdPlayers)
			{
				if (pCrowdPlayer == pPlayer)
					continue;

				const LagRecord_t tNeighborRecord = GetNeighborExecRecord(pCrowdPlayer, pCandidate->SimulationTime);
				if (!tNeighborRecord.bIsAlive)
					continue;

				vecHelpers.emplace_back();
				vecHelpers.back().Set(&tNeighborRecord);
			}

			F::LagRecordMatrixHelper->Set(pCandidate);
			CTraceFilterHitscan filter = {};
			Ray_t rayLine;
			rayLine.Init(vFrom, vTo);
			I::EngineTrace->TraceRay(rayLine, MASK_SOLID, &filter, &outTrace);
			if (outTrace.fraction >= 1.0f)
				H::AimUtils->TraceHull(vFrom, vTo, { -18.0f, -18.0f, -18.0f }, { 18.0f, 18.0f, 18.0f }, MASK_SOLID, &filter, &outTrace);
			F::LagRecordMatrixHelper->Restore();

			for (auto& helper : vecHelpers)
				helper.Restore();
		};

		for (const auto record : vecRecords)
		{
			const Vec3 vEye = pLocal->GetShootPos();

			if (vEye.DistTo(record->Center) >= 82.0f || (vEye - record->Center).Length2D() < 35.0f)
				continue;

			if (CFG::Triggerbot_AutoBacktab_Mode == 1)
			{
				angleTo = Math::CalcAngle(vEye, record->Center);
			}

			float flBehindDot = 0.f, flFaceDot = 0.f, flSameDot = 0.f;
			if (canKnife || IsBehindAndFacingTarget(pLocal->GetCenter(), angleTo, record->Center, vTargetEyeAngles, 0.0f, &flBehindDot, &flFaceDot, &flSameDot))
			{
				if (!canKnife && acosf(flBehindDot) * 57.29578f + std::max(fabsf(flYawRate), flRecentMaxRate) * flExecDelay > 90.0f)
					continue;
				const CUserCmd backupCmd = *pCmd;
				bool bVerified = false;

				if (CFG::Triggerbot_AutoBacktab_Mode == 1)
				{
					H::AimUtils->FixMovement(pCmd, angleTo);
					pCmd->viewangles = angleTo;
				}

				F::EnginePrediction->End(pLocal, pCmd);
				F::EnginePrediction->Start(pLocal, pCmd);
				{
					const Vec3 vEye2 = pLocal->GetShootPos();

					float flBehindDot2 = 0.f, flFaceDot2 = 0.f, flSameDot2 = 0.f;
					bVerified = vEye2.DistTo(record->Center) < 82.0f
						&& (vEye2 - record->Center).Length2D() >= 35.0f
						&& (canKnife || (IsBehindAndFacingTarget(pLocal->GetCenter(), angleTo, record->Center, vTargetEyeAngles, 0.0f, &flBehindDot2, &flFaceDot2, &flSameDot2)
							&& acosf(flBehindDot2) * 57.29578f + std::max(fabsf(flYawRate), flRecentMaxRate) * flExecDelay <= 90.0f));

					if (bVerified)
					{
						Vec3 forward2{};
						Math::AngleVectors(angleTo, &forward2);

						const Vec3 vTo2 = vEye2 + (forward2 * 48.0f);

						trace_t trace2 = {};
						CrowdTrace(record, vEye2, vTo2, trace2);

						bVerified = trace2.m_pEnt == pPlayer && !trace2.startsolid && !trace2.allsolid;
					}
				}

				*pCmd = backupCmd;
				F::EnginePrediction->End(pLocal, pCmd);
				F::EnginePrediction->Start(pLocal, pCmd);

				if (!bVerified)
					continue;

				if (CFG::Triggerbot_AutoBacktab_Mode == 1)
				{
					H::AimUtils->FixMovement(pCmd, angleTo);
					pCmd->viewangles = angleTo;

					if (CFG::Triggerbot_AutoBacktab_Aim_Mode == 1)
					{
						G::bPSilentAngles = true;
					}
				}

				G::bSilentAngles = true;

				pCmd->buttons |= IN_ATTACK;
				G::Attacking = 1;
				G::bFiring = true;
				m_flLastStab = I::EngineClient->Time();

				pCmd->tick_count = TIME_TO_TICKS(record->SimulationTime + F::LagRecords->GetServerLerp());

				return;
			}
		}
	}
}
