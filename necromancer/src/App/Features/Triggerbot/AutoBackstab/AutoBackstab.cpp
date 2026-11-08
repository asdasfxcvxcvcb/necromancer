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

// Check if player has an active Razorback equipped
// The Razorback blocks one backstab, then becomes inactive (not drawn)
bool HasActiveRazorback(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return false;
	
	// Check if player has the "set_blockbackstab_once" attribute (Razorback)
	// The attribute value will be > 0 if they have a Razorback
	int iBackstabShield = static_cast<int>(SDKUtils::AttribHookValue(0.f, "set_blockbackstab_once", pPlayer));
	if (iBackstabShield <= 0)
		return false;
	
	// The Razorback is only active if it's visible (ShouldDraw returns true)
	// After blocking a backstab, it becomes invisible until recharged
	// We need to find the actual wearable entity to check this
	for (int i = 0; i < I::ClientEntityList->GetHighestEntityIndex(); i++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pClientEntity)
			continue;
		
		// Check if it's a Razorback wearable
		if (pClientEntity->GetClassId() != ETFClassIds::CTFWearableRazorback)
			continue;
		
		// Cast to C_BaseEntity to access m_hOwnerEntity
		auto pEntity = pClientEntity->As<C_BaseEntity>();
		if (!pEntity)
			continue;
		
		// Check if it belongs to this player
		auto pOwner = pEntity->m_hOwnerEntity().Get();
		if (!pOwner || pOwner != pPlayer)
			continue;
		
		// Check if the Razorback is visible (active)
		if (pEntity->ShouldDraw())
			return true;
	}
	
	return false;
}

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

	// Iterate entity list directly for safety (cached group pointers may be stale after class change)
	// This prevents crashes when entities are destroyed and recreated during class switches
	const int nMaxClients = I::EngineClient->GetMaxClients();
	
	for (int n = 1; n <= nMaxClients; n++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(n);
		if (!pClientEntity || pClientEntity->IsDormant())
			continue;

		// Validate it's a player before casting
		auto pNetworkable = pClientEntity->GetClientNetworkable();
		if (!pNetworkable || !pNetworkable->GetClientClass())
			continue;

		if (pNetworkable->GetClientClass()->m_ClassID != static_cast<int>(ETFClassIds::CTFPlayer))
			continue;

		const auto pPlayer = pClientEntity->As<C_TFPlayer>();
		if (!pPlayer)
			continue;

		// Skip invalid or dead players
		if (!H::Entities->IsEntityValid(pPlayer) || pPlayer->deadflag() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
		{
			continue;
		}

		// Skip teammates (can't backstab your own team)
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

		// Skip snipers with active Razorback (blocks backstab)
		if (CFG::Triggerbot_AutoBackstab_Ignore_Razorback && HasActiveRazorback(pPlayer))
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
		// With fake latency, the server backtracks the target — we should only aim at backtrack records
		const bool bFakeLatencyActive = F::LagRecords->GetFakeLatency() > 0.0f;
		const float flSafetyMargin = GetBackstabSafetyMargin();
		
		if (!bFakeLatencyActive)
		{
			if (canKnife || IsBehindAndFacingTarget(pLocal->GetCenter(), angleTo, pPlayer->GetCenter(), pPlayer->GetEyeAngles(), 0.0f))
			{
				Vec3 forward{};
				Math::AngleVectors(angleTo, &forward);

				auto to = pLocal->GetShootPos() + (forward * 47.0f);

				if (H::AimUtils->TraceEntityMeleeDirect(pPlayer, pLocal->GetShootPos(), to))
				{
					if (CFG::Triggerbot_AutoBacktab_Mode == 1)
					{
						pCmd->viewangles = angleTo;
						// this triggerbot runs after CreateMove's Attacking recompute,
						// so its stab cmds always report not-attacking and anti-aim
						// would overwrite the stab angles - flag the cmd like the
						// aimbots do so FakeAngle::ShouldRun skips it
						G::bSilentAngles = true;

						if (CFG::Triggerbot_AutoBacktab_Aim_Mode == 1)
						{
							G::bPSilentAngles = true;  // Fire tick: pSilent choke
						}
					}

					pCmd->buttons |= IN_ATTACK;
					m_flLastStab = I::EngineClient->Time();

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

		for (int r = nStartRecord; r < nEndRecord; r++)
		{
			const auto record = F::LagRecords->GetRecord(pPlayer, r, true);

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

				if (!H::AimUtils->TraceEntityMeleeDirect(pPlayer, pLocal->GetShootPos(), to))
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
						G::bPSilentAngles = true;  // Fire tick: pSilent choke
					}
				}

				pCmd->buttons |= IN_ATTACK;
				m_flLastStab = I::EngineClient->Time();

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
