#pragma once

#include "../../../SDK/SDK.h"
#include <unordered_set>

// Port of Amalgam's medic danger engine (CAutoHeal::AutoVaccinator/GetDangers),
// retargeted to the stock medigun uber. Enemy players, sentries and enemy
// projectiles are scored into bullet/blast/fire danger (expected damage within
// our latency window over the target's current health, scaled by how close the
// threat really is); the uber is popped when the worst bucket reaches 1.0.
// Amalgam's ActivateOnVoice (pop when the heal target calls "Medic!") is here
// too, driven by the VoiceSubtitle user message.
class CAutoUberPop
{
public:
	void Run(CUserCmd* pCmd);
	void UserMessage(bf_read& msgData); // VoiceSubtitle - track who called for medic
	void Reset();
	void ScoreDangers(C_TFPlayer* pLocal, C_WeaponMedigun* pWeapon, C_TFPlayer* pTarget, float& flBulletOut, float& flBlastOut, float& flFireOut);

private:
	void GetDangers(C_TFPlayer* pTarget, bool bVaccinator, float& flBulletOut, float& flBlastOut, float& flFireOut);

	std::unordered_set<int> m_mMedicCallers = {};
	C_TFPlayer* m_pLocal = nullptr;
	C_WeaponMedigun* m_pWeapon = nullptr;
};

MAKE_SINGLETON_SCOPED(CAutoUberPop, AutoUberPop, F);
