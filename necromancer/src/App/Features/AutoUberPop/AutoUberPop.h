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

// projectile danger engine shared by the AutoVaccinator and the uber pop:
// real blast params off each projectile (netvars + launcher attrs), gravity
// arc flight with wall-impact capping against the MoveSim path of whoever we
// protect, and the server blast model (falloff toward the radius edge, world
// geometry blocks, players never do)
namespace ProjectileDanger
{
	constexpr float SIM_TICKS = 24;
	constexpr float PROJ_TICKS = 33.f;
	constexpr float REACT_BUFFER = 0.02f;
	constexpr float GRAVITY_SCALE = 0.4f;
	constexpr float ROCKET_RADIUS = 146.f;
	constexpr float ROCKET_DAMAGE = 90.f;
	constexpr float ROCKET_DH_DAMAGE = 112.5f;
	constexpr float SENTRY_ROCKET_DAMAGE = 80.f;
	constexpr float ENERGY_DAMAGE = 90.f;
	constexpr float ENERGY_CHARGED_DAMAGE = 150.f;
	constexpr float CRIT_MULT = 3.f;
	constexpr float STICKY_VEL_MAX = 30.f;

	struct Approach_t
	{
		float flMinDist = FLT_MAX;
		Vec3 vBlastPos{};
		Vec3 vEnd{};
	};

	struct BlastParams_t
	{
		bool bValid = false;
		bool bPlacedSticky = false;
		bool bHalfFalloff = false;
		bool bCrit = false;
		bool bCharged = false;
		float flDamage = 0.f;
		float flRadius = 0.f;
	};

	bool IsWorldVisible(const Vec3& vFrom, const Vec3& vTo);
	void GetTargetPath(C_TFPlayer* pPlayer, std::vector<Vec3>& vecPath);
	Approach_t ProjectileClosestApproach(C_BaseEntity* pEntity, const std::vector<Vec3>& vecPath, int nTicks);
	bool GetBlastParams(C_BaseEntity* pEntity, BlastParams_t& tOut);
	bool BlastReachable(const Vec3& vBlast, const std::vector<Vec3>& vecPath, float flCenterZ);
	float WorstBlastDamage(const Vec3& vBlast, float flDamage, float flRadius, bool bHalfFalloff, float flMult, const std::vector<Vec3>& vecPath, float flCenterZ, float& flPathOnly);
}
