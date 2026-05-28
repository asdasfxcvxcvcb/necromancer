#include "Aimbot.h"

#include "AimbotHitscan/AimbotHitscan.h"
#include "AimbotMelee/AimbotMelee.h"
#include "AimbotProjectile/AimbotProjectile.h"
#include "AimbotWrangler/AimbotWrangler.h"
#include "../RapidFire/RapidFire.h"

#include "../CFG.h"
#include "../Misc/Misc.h"

void CAimbot::RunMain(CUserCmd* pCmd)
{
	G::nTargetIndex = -1;
	G::flAimbotFOV = 0.0f;
	G::nTargetIndexEarly = -1;

	// Handle switch key for head/body toggle
	F::AimbotHitscan->HandleSwitchKey();

	if (!CFG::Aimbot_Active || SDKUtils::BInEndOfMatch())
		return;

	if (Shifting::bRecharging)
		return;

	// Skip aimbot during doubletap shifts - we use the saved command angles
	// This is critical for DT to work properly, the reference project does the same
	if (Shifting::bShifting && !Shifting::bShiftingWarp)
		return;

	// Skip aimbot when AutoFaN is running (it needs to control viewangles for the jump boost)
	if (F::Misc->IsAutoFaNRunning())
		return;

	const auto pLocal = H::Entities->GetLocal();
	const auto pWeapon = H::Entities->GetWeapon();

	if (!pLocal || !pWeapon
		|| !H::Entities->SafeIsEntityValid(pLocal, I::EngineClient->GetLocalPlayer())
		|| pLocal->deadflag()
		|| pLocal->InCond(TF_COND_TAUNTING) || pLocal->InCond(TF_COND_PHASE)
		|| pLocal->InCond(TF_COND_HALLOWEEN_GHOST_MODE)
		|| pLocal->InCond(TF_COND_HALLOWEEN_BOMB_HEAD)
		|| pLocal->InCond(TF_COND_HALLOWEEN_KART)
		|| pLocal->m_bFeignDeathReady() || pLocal->m_flInvisibility() > 0.0f
		|| !H::Entities->IsEntityValid(pWeapon)
		|| pWeapon->m_iItemDefinitionIndex() == Soldier_m_RocketJumper || pWeapon->m_iItemDefinitionIndex() == Demoman_s_StickyJumper)
		return;

	// Check for Wrangler first (it returns EWeaponType::OTHER)
	if (F::AimbotWrangler->IsWrangler(pWeapon))
	{
		F::AimbotWrangler->Run(pCmd, pLocal, pWeapon);
		return;
	}

	switch (H::AimUtils->GetWeaponType(pWeapon))
	{
		case EWeaponType::HITSCAN:
		{
			F::AimbotHitscan->Run(pCmd, pLocal, pWeapon);
			break;
		}

		case EWeaponType::PROJECTILE:
		{
			F::AimbotProjectile->Run(pCmd, pLocal, pWeapon);
			break;
		}

		case EWeaponType::MELEE:
		{
			F::AimbotMelee->Run(pCmd, pLocal, pWeapon);
			break;
		}

		default: break;
	}
}

void CAimbot::Run(CUserCmd* pCmd)
{
	RunMain(pCmd);

	// Re-fetch from engine instead of using cached values.
	// Entities can be destroyed during RunMain() (class change, death),
	// making cached pointers dangling. Fresh fetches guarantee validity.
	const auto pLocal = H::Entities->GetLocal();
	const auto pWeapon = H::Entities->GetWeapon();

	if (!pLocal || !pWeapon
		|| !H::Entities->SafeIsEntityValid(pLocal, I::EngineClient->GetLocalPlayer())
		|| !H::Entities->IsEntityValid(pWeapon)
		|| pLocal->deadflag()
		|| pLocal->InCond(TF_COND_TAUNTING) || pLocal->InCond(TF_COND_PHASE)
		|| pLocal->m_bFeignDeathReady() || pLocal->m_flInvisibility() > 0.0f)
		return;

	const auto nWeaponType = H::AimUtils->GetWeaponType(pWeapon);

	if (!G::bFiring)
	{
		switch (nWeaponType)
		{
			case EWeaponType::HITSCAN:
			{
				G::bFiring = F::AimbotHitscan->IsFiring(pCmd, pWeapon);
				break;
			}

			case EWeaponType::PROJECTILE:
			{
				G::bFiring = F::AimbotProjectile->IsFiring(pCmd, pLocal, pWeapon);
				break;
			}

			case EWeaponType::MELEE:
			{
				G::bFiring = F::AimbotMelee->IsFiring(pCmd, pWeapon);
				break;
			}

			default: break;
		}
	}

}
