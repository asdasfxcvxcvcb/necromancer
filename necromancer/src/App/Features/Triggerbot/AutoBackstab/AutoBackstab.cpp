#include "AutoBackstab.h"

#include "../../CFG.h"

#include "../../LagRecords/LagRecords.h"

// TF2 default thresholds from CTFKnife::IsBehindAndFacingTarget:
// - Behind check: toTarget.Dot(targetForward) > 0.0
// - Facing check: toTarget.Dot(ownerForward) > 0.5
// - Same direction: targetForward.Dot(ownerForward) > -0.3
//
// With high fake latency, the target's eye angles on the server may differ from what we see
// because EyeAngles are NOT lag compensated (only position is). We add a safety margin
// to account for potential angle changes during the latency window.

bool IsBehindAndFacingTarget(const Vec3& ownerCenter, const Vec3& ownerViewangles, const Vec3& targetCenter, const Vec3& targetEyeAngles, float flSafetyMargin = 0.0f)
{
	Vec3 toTarget = targetCenter - ownerCenter;
	toTarget.z = 0.0f;
	toTarget.NormalizeInPlace();

	Vec3 ownerForward{};
	Math::AngleVectors(ownerViewangles, &ownerForward, nullptr, nullptr);
	ownerForward.z = 0.0f;
	ownerForward.NormalizeInPlace();

	Vec3 targetForward{};
	Math::AngleVectors(targetEyeAngles, &targetForward, nullptr, nullptr);
	targetForward.z = 0.0f;
	targetForward.NormalizeInPlace();

	// Base thresholds + small buffer for floating point precision
	const float flBehindThreshold = 0.0f + 0.03125f + flSafetyMargin;
	const float flFacingThreshold = 0.5f + 0.03125f;
	const float flSameDirectionThreshold = -0.3f + 0.03125f + flSafetyMargin;

	return toTarget.Dot(targetForward) > flBehindThreshold
		&& toTarget.Dot(ownerForward) > flFacingThreshold
		&& targetForward.Dot(ownerForward) > flSameDirectionThreshold;
}

// Calculate safety margin based on fake latency
// Higher latency = more time for target to turn = need stricter thresholds
float GetBackstabSafetyMargin()
{
	const float flFakeLatency = F::LagRecords->GetFakeLatency();
	
	if (flFakeLatency <= 0.0f)
		return 0.0f;
	
	// Scale margin based on latency:
	// 100ms = 0.1 margin, 200ms = 0.2 margin, 400ms = 0.4 margin
	// Clamped to max 0.5 (very strict, requires being almost directly behind)
	return std::min(flFakeLatency, 0.5f);
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
	// Check if enabled via always-on option OR keybind
	// Works if: Always_On is true OR Active (keybind) is true
	if (!CFG::Triggerbot_AutoBackstab_Always_On && !CFG::Triggerbot_AutoBackstab_Active)
	{
		return;
	}

	if (!G::bCanPrimaryAttack || pLocal->m_bFeignDeathReady() || pLocal->m_flInvisibility() > 0.0f || pWeapon->GetWeaponID() != TF_WEAPON_KNIFE)
	{
		return;
	}

	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		if (!pEntity)
		{
			continue;
		}

		const auto pPlayer = pEntity->As<C_TFPlayer>();

		if (!pPlayer || pPlayer->deadflag() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
		{
			continue;
		}

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

		// Knife if lethal
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

		// Skip current model position when fake latency is active
		const bool bFakeLatencyActive = F::LagRecords->GetFakeLatency() > 0.0f;
		const float flSafetyMargin = GetBackstabSafetyMargin();
		
		if (!bFakeLatencyActive)
		{
			if (canKnife || IsBehindAndFacingTarget(pLocal->GetCenter(), angleTo, pPlayer->GetCenter(), pPlayer->GetEyeAngles(), 0.0f))
			{
				Vec3 forward{};
				Math::AngleVectors(angleTo, &forward);

				auto to = pLocal->GetShootPos() + (forward * 47.0f);

				if (H::AimUtils->TraceEntityMelee(pPlayer, pLocal->GetShootPos(), to))
				{
					if (CFG::Triggerbot_AutoBacktab_Mode == 1)
					{
						pCmd->viewangles = angleTo;

						if (CFG::Triggerbot_AutoBacktab_Aim_Mode == 1)
						{
							G::bPSilentAngles = true;
						}
					}

					pCmd->buttons |= IN_ATTACK;

					if (CFG::Misc_Accuracy_Improvements)
					{
						pCmd->tick_count = TIME_TO_TICKS(pPlayer->m_flSimulationTime() + SDKUtils::GetLerp());
					}

					return;
				}
			}
		}

		int numRecords = 0;

		if (!F::LagRecords->HasRecords(pPlayer, &numRecords))
		{
			continue;
		}

		// When fake latency is active, only target the last 5 backtrack records
		const int nStartRecord = bFakeLatencyActive ? std::max(1, numRecords - 5) : 1;
		const int nEndRecord = numRecords;

		for (int n = nStartRecord; n < nEndRecord; n++)
		{
			const auto record = F::LagRecords->GetRecord(pPlayer, n, true);

			if (!record)
			{
				continue;
			}

			// Rage mode
			if (CFG::Triggerbot_AutoBacktab_Mode == 1)
			{
				angleTo = Math::CalcAngle(pLocal->GetShootPos(), record->Center);
			}

			// Use safety margin when fake latency is active to account for angle desync
			// The server uses CURRENT eye angles (not lag compensated), so with high latency
			// the target may have turned since we last received their angles
			if (canKnife || IsBehindAndFacingTarget(pLocal->GetCenter(), angleTo, record->Center, pPlayer->GetEyeAngles(), flSafetyMargin))
			{
				F::LagRecordMatrixHelper->Set(record);

				Vec3 forward{};
				Math::AngleVectors(angleTo, &forward);

				auto to = pLocal->GetShootPos() + (forward * 47.0f);

				if (!H::AimUtils->TraceEntityMelee(pPlayer, pLocal->GetShootPos(), to))
				{
					F::LagRecordMatrixHelper->Restore();

					continue;
				}

				F::LagRecordMatrixHelper->Restore();

				if (CFG::Triggerbot_AutoBacktab_Mode == 1)
				{
					pCmd->viewangles = angleTo;

					if (CFG::Triggerbot_AutoBacktab_Aim_Mode == 1)
					{
						G::bPSilentAngles = true;
					}
				}

				pCmd->buttons |= IN_ATTACK;

				if (CFG::Misc_Accuracy_Improvements)
				{
					pCmd->tick_count = TIME_TO_TICKS(record->SimulationTime + SDKUtils::GetLerp());
				}
				else
				{
					pCmd->tick_count = TIME_TO_TICKS(record->SimulationTime + GetClientInterpAmount());
				}

				return;
			}
		}
	}
}
