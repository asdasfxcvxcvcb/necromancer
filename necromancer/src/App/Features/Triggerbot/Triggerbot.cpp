#include "Triggerbot.h"

#include "../CFG.h"

#include "AutoAirblast/AutoAirblast.h"
#include "AutoBackstab/AutoBackstab.h"
#include "AutoDetonate/AutoDetonate.h"
#include "AutoVaccinator/AutoVaccinator.h"
#include "AutoSapper/AutoSapper.h"

void CTriggerbot::Run(CUserCmd* pCmd)
{
	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal || pLocal->deadflag()
		|| pLocal->InCond(TF_COND_HALLOWEEN_GHOST_MODE)
		|| pLocal->InCond(TF_COND_HALLOWEEN_BOMB_HEAD)
		|| pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;

	const auto pWeapon = H::Entities->GetWeapon();

	if (!pWeapon)
		return;

	// Auto Detonate can run independently if "Always On" is enabled
	if (CFG::Triggerbot_AutoDetonate_Always_On)
		F::AutoDetonate->Run(pLocal, pWeapon, pCmd);

	// Auto Backstab can run independently if "Always On" is enabled
	if (CFG::Triggerbot_AutoBackstab_Always_On)
		F::AutoBackstab->Run(pLocal, pWeapon, pCmd);

	// Auto Sapper can run independently if "Always On" is enabled
	if (CFG::Triggerbot_AutoSapper_Always_On)
		F::AutoSapper->Run(pLocal, pWeapon, pCmd);

	// AutoVaccinator runs early in CreateMove if Always On, otherwise runs with triggerbot

	// Other triggerbot features require master switch
	if (!CFG::Triggerbot_Active || (CFG::Triggerbot_Key && !H::Input->IsDown(CFG::Triggerbot_Key)))
		return;

	F::AutoAirblast->Run(pLocal, pWeapon, pCmd);
	
	// Auto Backstab also runs with master triggerbot (if not already run above)
	if (!CFG::Triggerbot_AutoBackstab_Always_On)
		F::AutoBackstab->Run(pLocal, pWeapon, pCmd);
	
	// Auto Detonate also runs with master triggerbot (if not already run above)
	if (!CFG::Triggerbot_AutoDetonate_Always_On)
		F::AutoDetonate->Run(pLocal, pWeapon, pCmd);

	// Auto Sapper also runs with master triggerbot (if not already run above)
	if (!CFG::Triggerbot_AutoSapper_Always_On)
		F::AutoSapper->Run(pLocal, pWeapon, pCmd);

	// AutoVaccinator runs with triggerbot if not Always On
	if (!CFG::Triggerbot_AutoVaccinator_Always_On)
		F::AutoVaccinator->Run(pLocal, pWeapon, pCmd);
}
