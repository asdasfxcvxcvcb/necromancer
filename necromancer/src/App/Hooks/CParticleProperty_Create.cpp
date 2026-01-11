#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

MAKE_SIGNATURE(CParticleProperty_Create_Point, "client.dll", "44 89 4C 24 ? 44 89 44 24 ? 53", 0x0);

// Helper to get particle name for projectile trail
static const char* GetProjectileTrailParticle(int nType, bool bBlue)
{
	switch (nType)
	{
		case 1: return nullptr; // None
		case 2: return "rockettrail"; // Rocket
		case 3: return bBlue ? "critical_rocket_blue" : "critical_rocket_red"; // Critical
		case 4: return bBlue ? "drg_cow_rockettrail_normal_blue" : "drg_cow_rockettrail_normal"; // Energy
		case 5: return bBlue ? "drg_cow_rockettrail_charged_blue" : "drg_cow_rockettrail_charged"; // Charged
		case 6: return "drg_manmelter_projectile"; // Ray
		case 7: return bBlue ? "spell_fireball_small_trail_blue" : "spell_fireball_small_trail_red"; // Fireball
		case 8: return bBlue ? "spell_teleport_blue" : "spell_teleport_red"; // Teleport
		case 9: return "flamethrower"; // Fire
		case 10: return "flying_flaming_arrow"; // Flame
		case 11: return bBlue ? "critical_rocket_bluesparks" : "critical_rocket_redsparks"; // Sparks
		case 12: return bBlue ? "flaregun_trail_blue" : "flaregun_trail_red"; // Flare
		case 13: return bBlue ? "stickybombtrail_blue" : "stickybombtrail_red"; // Trail
		case 14: return bBlue ? "healshot_trail_blue" : "healshot_trail_red"; // Health
		case 15: return "rockettrail_airstrike_line"; // Smoke
		case 16: return bBlue ? "pyrovision_scorchshot_trail_blue" : "pyrovision_scorchshot_trail_red"; // Bubbles
		case 17: return "halloween_rockettrail"; // Halloween
		case 18: return "eyeboss_projectile"; // Monoculus
		case 19: return bBlue ? "burningplayer_rainbow_blue" : "burningplayer_rainbow_red"; // Sparkles
		case 20: return "flamethrower_rainbow"; // Rainbow
		default: return nullptr;
	}
}

// Check if particle name is a projectile trail we want to replace
static bool IsProjectileTrail(const char* pszParticleName)
{
	if (!pszParticleName)
		return false;

	// Check for common projectile trail substrings
	if (strstr(pszParticleName, "rockettrail") ||
		strstr(pszParticleName, "pipebombtrail") ||
		strstr(pszParticleName, "stickybombtrail") ||
		strstr(pszParticleName, "flaregun_trail") ||
		strstr(pszParticleName, "scorchshot_trail") ||
		strstr(pszParticleName, "peejar_trail") ||
		strstr(pszParticleName, "stunballtrail") ||
		strstr(pszParticleName, "healshot_trail") ||
		strstr(pszParticleName, "drg_cow_rockettrail") ||
		strstr(pszParticleName, "drg_bison_projectile") ||
		strstr(pszParticleName, "drg_manmelter_projectile") ||
		strstr(pszParticleName, "eyeboss_projectile") ||
		strstr(pszParticleName, "flaming_arrow") ||
		strstr(pszParticleName, "spell_fireball_small_trail"))
	{
		return true;
	}

	return false;
}

MAKE_HOOK(CParticleProperty_Create_Point, Signatures::CParticleProperty_Create_Point.Get(), void*, __fastcall,
	void* rcx, const char* pszParticleName, int iAttachType, int iAttachmentPoint, Vec3 vecOriginOffset)
{
	const int nTrailType = CFG::Visuals_Projectile_Trail;
	
	// Only process if we have a trail type set and it's a projectile trail
	if (nTrailType > 0 && IsProjectileTrail(pszParticleName))
	{
		const auto pLocal = H::Entities->GetLocal();
		if (pLocal)
		{
			const bool bBlue = pLocal->m_iTeamNum() == TF_TEAM_BLUE;
			
			if (nTrailType == 1) // None
				return nullptr;
			
			const char* pszNewParticle = GetProjectileTrailParticle(nTrailType, bBlue);
			if (pszNewParticle)
				return CALL_ORIGINAL(rcx, pszNewParticle, iAttachType, iAttachmentPoint, vecOriginOffset);
		}
	}

	// Hide the airstrike smoke line if we're replacing trails
	if (nTrailType > 0 && pszParticleName && strcmp(pszParticleName, "rockettrail_airstrike_line") == 0)
		return nullptr;

	return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);
}
