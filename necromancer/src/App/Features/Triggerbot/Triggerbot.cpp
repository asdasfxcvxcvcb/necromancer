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

	// Cache "Always On" states to avoid repeated config reads
	const bool bAutoDetAlwaysOn = CFG::Triggerbot_AutoDetonate_Always_On;
	const bool bAutoBackstabAlwaysOn = CFG::Triggerbot_AutoBackstab_Always_On;
	const bool bAutoSapperAlwaysOn = CFG::Triggerbot_AutoSapper_Always_On;
	const bool bAutoVaccAlwaysOn = CFG::Triggerbot_AutoVaccinator_Always_On;

	// Auto Detonate can run independently if "Always On" is enabled
	if (bAutoDetAlwaysOn)
		F::AutoDetonate->Run(pLocal, pWeapon, pCmd);

	// Auto Backstab can run independently if "Always On" is enabled
	if (bAutoBackstabAlwaysOn)
		F::AutoBackstab->Run(pLocal, pWeapon, pCmd);

	// Auto Sapper can run independently if "Always On" is enabled
	if (bAutoSapperAlwaysOn)
		F::AutoSapper->Run(pLocal, pWeapon, pCmd);

	// Other triggerbot features require master switch
	if (!CFG::Triggerbot_Active || (CFG::Triggerbot_Key && !H::Input->IsDown(CFG::Triggerbot_Key)))
		return;

	F::AutoAirblast->Run(pLocal, pWeapon, pCmd);
	
	// Auto Backstab also runs with master triggerbot (if not already run above)
	if (!bAutoBackstabAlwaysOn)
		F::AutoBackstab->Run(pLocal, pWeapon, pCmd);
	
	// Auto Detonate also runs with master triggerbot (if not already run above)
	if (!bAutoDetAlwaysOn)
		F::AutoDetonate->Run(pLocal, pWeapon, pCmd);

	// Auto Sapper also runs with master triggerbot (if not already run above)
	if (!bAutoSapperAlwaysOn)
		F::AutoSapper->Run(pLocal, pWeapon, pCmd);

	// AutoVaccinator runs with triggerbot if not Always On
	if (!bAutoVaccAlwaysOn)
		F::AutoVaccinator->Run(pLocal, pWeapon, pCmd);
}
