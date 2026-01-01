#include "SeedPred.h"

#include "../CFG.h"
#include <vector>
#include <algorithm>
#include <ranges>
#include <limits>
#include <cmath>
#include <deque>
#include <numeric>

// Pre-compute spread offsets for all 256 seeds
// Server uses: RandomSeed(iSeed); x = RandomFloat(-0.5, 0.5) + RandomFloat(-0.5, 0.5)
void CSeedPred::InitSpreadLUT()
{
	if (m_SpreadInit)
		return;

	for (int n = 0; n <= 255; n++)
	{
		SDKUtils::RandomSeed(n);
		const float x = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
		const float y = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
		m_SpreadOffsets[n] = { x, y };
	}

	m_SpreadInit = true;
}

// Calculate mantissa step - determines seed prediction reliability
// IEEE 754: float = sign(1) | exponent(8) | mantissa(23)
// As server uptime increases, float precision decreases, making seed prediction more reliable
// Server seed: *(int*)&(Plat_FloatTime() * 1000.0f) & 255
float CSeedPred::CalcMantissaStep(float val) const
{
	const float t = val * 1000.0f;
	const int i = std::bit_cast<int>(t);
	const int e = (i >> 23) & 0xFF; // Extract exponent
	// 2^(exponent - 127 - 23) = step between consecutive float values
	return std::powf(2.0f, static_cast<float>(e) - 150.0f);
}

void CSeedPred::AskForPlayerPerf()
{
	if (!CFG::Exploits_SeedPred_Active)
	{
		Reset();
		return;
	}

	// Initialize spread LUT on first call
	InitSpreadLUT();

	// Does the current weapon have spread?
	const auto weapon = H::Entities->GetWeapon();
	if (!weapon || H::AimUtils->GetWeaponType(weapon) != EWeaponType::HITSCAN || weapon->GetWeaponSpread() <= 0.0f)
	{
		Reset();
		return;
	}

	// Are we dead?
	if (const auto local = H::Entities->GetLocal())
	{
		if (local->deadflag())
		{
			Reset();
			return;
		}
	}

	// Already waiting for response?
	if (m_WaitingForPP)
		return;

	// Adaptive resync based on mantissa step
	// Lower step = less reliable = resync more often
	if (m_ServerTime > 0.0f)
	{
		const float timeSinceRecv = static_cast<float>(Plat_FloatTime()) - m_RecvTime;
		
		// Adaptive interval: resync more frequently when precision is low
		float interval = RESYNC_INTERVAL;
		if (m_MantissaStep < 1.0f)
			interval = 1.0f; // Very unreliable, resync constantly
		else if (m_MantissaStep < 4.0f)
			interval = 2.0f; // Somewhat reliable
		else if (m_MantissaStep < 16.0f)
			interval = 5.0f; // Reliable
		else
			interval = 10.0f; // Very reliable, server has been up for hours

		if (timeSinceRecv < interval)
			return;
	}

	// Request perf data
	I::ClientState->SendStringCmd("playerperf");
	m_AskTime = static_cast<float>(Plat_FloatTime());
	m_WaitingForPP = true;
}

bool CSeedPred::ParsePlayerPerf(bf_read& msgData)
{
	if (!CFG::Exploits_SeedPred_Active)
		return false;

	char rawMsg[256]{};
	msgData.ReadString(rawMsg, sizeof(rawMsg), true);
	msgData.Seek(0);

	std::string msg(rawMsg);
	msg.erase(msg.begin()); // STX

	// playerperf format: "servertime ticks ents simtime frametime vel velocity"
	// Example: "12345.67 1000 50 0.015 0.015 vel 320.00"
	std::smatch matches{};
	std::regex_match(msg, matches, std::regex(R"((\d+.\d+)\s\d+\s\d+\s\d+.\d+\s\d+.\d+\svel\s\d+.\d+)"));

	if (matches.size() == 2)
	{
		m_WaitingForPP = false;
		m_RecvTime = static_cast<float>(Plat_FloatTime());

		const float newServerTime = std::stof(matches[1].str());

		if (newServerTime > m_ServerTime)
		{
			m_PrevServerTime = m_ServerTime;
			m_ServerTime = newServerTime;

			// Calculate round-trip time and estimate one-way latency
			const float rtt = m_RecvTime - m_AskTime;
			
			// Server time was recorded when processing our command, so we need to account for:
			// 1. Time for our request to reach server (~rtt/2)
			// 2. Server processing time (~1 tick)
			// Delta = ServerTime - ClientTimeWhenServerProcessed
			// ClientTimeWhenServerProcessed ≈ AskTime + rtt/2
			// So: Delta ≈ ServerTime - (AskTime + rtt/2) = ServerTime - AskTime - rtt/2
			// But we want: PredictedServerTime = ClientTime + Delta
			// Which means: Delta = ServerTime - AskTime + rtt/2 (to predict future server time)
			
			const double measuredDelta = static_cast<double>(m_ServerTime - m_AskTime) + 
			                             static_cast<double>(rtt * 0.5f) + 
			                             static_cast<double>(TICK_INTERVAL);
			
			// Average deltas to reduce jitter from network variance
			// Use weighted average - recent samples matter more
			m_TimeDeltas.push_back(measuredDelta);
			while (m_TimeDeltas.size() > 30)
				m_TimeDeltas.pop_front();

			// Weighted average: newer samples have more weight
			double weightedSum = 0.0;
			double totalWeight = 0.0;
			size_t idx = 0;
			for (const auto& delta : m_TimeDeltas)
			{
				const double weight = static_cast<double>(idx + 1); // 1, 2, 3, ... n
				weightedSum += delta * weight;
				totalWeight += weight;
				idx++;
			}
			const double avgDelta = totalWeight > 0.0 ? (weightedSum / totalWeight) : measuredDelta;
			m_SyncOffset = static_cast<float>(avgDelta);

			// Calculate mantissa step - higher = more reliable prediction
			// Step >= 1.0 means seed changes at most once per millisecond
			// Step >= 4.0 means seed changes at most once per 4ms (very reliable)
			m_MantissaStep = CalcMantissaStep(m_ServerTime);
			m_Synced = (m_MantissaStep >= 1.0f);
		}

		return true;
	}

	return std::regex_match(msg, std::regex(R"(\d+.\d+\s\d+\s\d+)"));
}

int CSeedPred::GetSeed() const
{
	// Server seed calculation (from TF2 source):
	// When sv_usercmd_custom_random_seed is enabled (default on Valve servers):
	// float flTime = Plat_FloatTime() * 1000.0f;
	// int iSeed = *(int*)&flTime & 255;
	
	const double dTime = static_cast<double>(Plat_FloatTime()) + static_cast<double>(m_SyncOffset);
	const float flTime = static_cast<float>(dTime * 1000.0);
	return std::bit_cast<int32_t>(flTime) & 255;
}

int CSeedPred::GetSeedForCmd(const CUserCmd* cmd)
{
	static ConVar* sv_usercmd_custom_random_seed = I::CVar->FindVar("sv_usercmd_custom_random_seed");
	if (sv_usercmd_custom_random_seed && sv_usercmd_custom_random_seed->GetBool())
	{
		// Server uses time-based seed
		return GetSeed();
	}
	// Fallback: engine MD5-based per-command seed
	return cmd ? (cmd->random_seed & 255) : GetSeed();
}

void CSeedPred::Reset()
{
	m_Synced = false;
	m_ServerTime = 0.0f;
	m_PrevServerTime = 0.0f;
	m_AskTime = 0.0f;
	m_RecvTime = 0.0f;
	m_SyncOffset = 0.0f;
	m_WaitingForPP = false;
	m_MantissaStep = 0.0f;
	m_TimeDeltas.clear();
}

// Weapon-specific spread calculation helpers
namespace
{
	// TF2 constants from source
	constexpr float TF_MINIGUN_SPINUP_TIME = 0.75f;
	constexpr float TF_MINIGUN_PENALTY_PERIOD = 1.0f;

	// Weapon categories for spread handling
	enum class EWeaponSpreadType
	{
		NONE,           // No spread (sniper rifle scoped, etc.)
		SINGLE_BULLET,  // Single bullet with spread (pistol, SMG, revolver, minigun)
		MULTI_PELLET,   // Shotgun-style (scattergun, shotguns, soda popper)
		SPECIAL         // Special handling (ambassador, etc.)
	};

	// Get weapon spread type for proper handling
	EWeaponSpreadType GetWeaponSpreadType(C_TFWeaponBase* weapon)
	{
		const int weaponID = weapon->GetWeaponID();

		switch (weaponID)
		{
		// === SNIPER RIFLES (no spread when scoped) ===
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:     // Bazaar Bargain
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:   // Classic
			return EWeaponSpreadType::NONE; // Handled by GetWeaponSpread() returning 0

		// === SHOTGUNS (multi-pellet) ===
		case TF_WEAPON_SHOTGUN_PRIMARY:       // Engineer primary
		case TF_WEAPON_SHOTGUN_SOLDIER:       // Soldier secondary
		case TF_WEAPON_SHOTGUN_HWG:           // Heavy secondary
		case TF_WEAPON_SHOTGUN_PYRO:          // Pyro secondary
		case TF_WEAPON_SHOTGUN_BUILDING_RESCUE: // Rescue Ranger
		case TF_WEAPON_SCATTERGUN:            // Scout primary
		case TF_WEAPON_SODA_POPPER:           // Scout primary
		case TF_WEAPON_PEP_BRAWLER_BLASTER:   // Baby Face's Blaster
		case TF_WEAPON_HANDGUN_SCOUT_PRIMARY: // Shortstop (4 pellets)
			return EWeaponSpreadType::MULTI_PELLET;

		// === SINGLE BULLET WEAPONS ===
		case TF_WEAPON_PISTOL:                // Engineer/Scout pistol
		case TF_WEAPON_PISTOL_SCOUT:          // Scout pistol
		case TF_WEAPON_SMG:                   // Sniper SMG
		case TF_WEAPON_CHARGED_SMG:           // Cleaner's Carbine
		case TF_WEAPON_MINIGUN:               // Heavy primary
		case TF_WEAPON_HANDGUN_SCOUT_SECONDARY: // Winger, Pretty Boy's, etc.
		case TF_WEAPON_SENTRY_BULLET:         // Sentry gun bullets
		case TF_WEAPON_SENTRY_REVENGE:        // Frontier Justice (crits from sentry)
			return EWeaponSpreadType::SINGLE_BULLET;

		// === REVOLVERS (special handling for Ambassador) ===
		case TF_WEAPON_REVOLVER:
			return EWeaponSpreadType::SPECIAL;

		default:
			// Check bullets per shot for unknown weapons
			if (const auto pInfo = weapon->GetWeaponInfo())
			{
				int nBullets = pInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_nBulletsPerShot;
				nBullets = static_cast<int>(SDKUtils::AttribHookValue(static_cast<float>(nBullets), "mult_bullets_per_shot", weapon));
				if (nBullets > 1)
					return EWeaponSpreadType::MULTI_PELLET;
			}
			return EWeaponSpreadType::SINGLE_BULLET;
		}
	}

	// Get minigun spread multiplier based on firing duration
	// From tf_weapon_minigun.cpp: spread *= RemapValClamped(spinTime, 0, 1, 1.5, 1.0)
	float GetMinigunSpreadMult(C_TFWeaponBase* weapon, C_TFPlayer* local)
	{
		const auto pMinigun = weapon->As<C_TFMinigun>();
		if (!pMinigun)
			return 1.5f;

		const int nState = pMinigun->m_iWeaponState();
		
		// Not firing yet = worst spread
		if (nState < AC_STATE_FIRING)
			return 1.5f;

		// Calculate firing duration from last fire time
		const float flCurrentTime = local->m_nTickBase() * TICK_INTERVAL;
		const float flFiringDuration = flCurrentTime - weapon->m_flLastFireTime();

		// After 1 second of firing, spread is normal
		if (flFiringDuration >= TF_MINIGUN_PENALTY_PERIOD)
			return 1.0f;

		// Ramp from 1.5x to 1.0x over 1 second
		return Math::RemapValClamped(flFiringDuration, 0.0f, TF_MINIGUN_PENALTY_PERIOD, 1.5f, 1.0f);
	}

	// Get Ambassador spread multiplier based on time since last shot
	// From tf_weapon_revolver.cpp: spread = RemapValClamped(timeSince, 1.0, 0.5, 0.0, baseSpread)
	float GetAmbassadorSpreadMult(float timeSinceLastShot)
	{
		// After 1 second, perfect accuracy (0 spread)
		if (timeSinceLastShot >= 1.0f)
			return 0.0f;
		
		// Before 0.5 seconds, full spread
		if (timeSinceLastShot <= 0.5f)
			return 1.0f;

		// Ramp from full spread to 0 between 0.5s and 1.0s
		return Math::RemapValClamped(timeSinceLastShot, 0.5f, 1.0f, 1.0f, 0.0f);
	}

	// Check if weapon is Ambassador (revolver with set_weapon_mode = 1)
	bool IsAmbassador(C_TFWeaponBase* weapon)
	{
		if (weapon->GetWeaponID() != TF_WEAPON_REVOLVER)
			return false;
		const int nWeaponMode = static_cast<int>(SDKUtils::AttribHookValue(0.0f, "set_weapon_mode", weapon));
		return nWeaponMode == 1;
	}

	// Check if weapon is Diamondback (revolver with set_weapon_mode = 2)
	bool IsDiamondback(C_TFWeaponBase* weapon)
	{
		if (weapon->GetWeaponID() != TF_WEAPON_REVOLVER)
			return false;
		const int nWeaponMode = static_cast<int>(SDKUtils::AttribHookValue(0.0f, "set_weapon_mode", weapon));
		return nWeaponMode == 2;
	}

	// Check if weapon is L'Etranger (revolver with set_weapon_mode = 0 but has cloak_on_hit)
	bool IsLetranger(C_TFWeaponBase* weapon)
	{
		if (weapon->GetWeaponID() != TF_WEAPON_REVOLVER)
			return false;
		const float fCloakOnHit = SDKUtils::AttribHookValue(0.0f, "add_cloak_on_hit", weapon);
		return fCloakOnHit > 0.0f;
	}

	// Check if weapon is Enforcer (revolver with damage bonus while disguised)
	bool IsEnforcer(C_TFWeaponBase* weapon)
	{
		if (weapon->GetWeaponID() != TF_WEAPON_REVOLVER)
			return false;
		const float fDisguiseDmg = SDKUtils::AttribHookValue(0.0f, "disguise_damage_bonus", weapon);
		return fDisguiseDmg > 0.0f;
	}

	// Get perfect shot threshold for weapon type
	// From tf_fx_shared.cpp: Multi-pellet = 0.25s, Single-pellet = 1.25s
	float GetPerfectShotThreshold(C_TFWeaponBase* weapon, int bulletsPerShot)
	{
		const int weaponID = weapon->GetWeaponID();

		// Multi-pellet weapons always use 0.25s
		if (bulletsPerShot > 1)
			return 0.25f;

		// Ambassador has its own spread recovery system (handled separately)
		if (IsAmbassador(weapon))
			return 999.0f; // Never use standard perfect shot

		// Weapon-specific thresholds for single-bullet weapons
		switch (weaponID)
		{
		// === PISTOLS (faster recovery) ===
		case TF_WEAPON_PISTOL:
		case TF_WEAPON_PISTOL_SCOUT:
		case TF_WEAPON_HANDGUN_SCOUT_SECONDARY: // Winger, Pretty Boy's Pocket Pistol
			return 1.25f; // Standard single-bullet threshold

		// === SMG (very fast recovery) ===
		case TF_WEAPON_SMG:
		case TF_WEAPON_CHARGED_SMG:             // Cleaner's Carbine
			return 1.25f;

		// === MINIGUN (never has perfect first shot) ===
		case TF_WEAPON_MINIGUN:
			return 999.0f;

		// === REVOLVERS ===
		case TF_WEAPON_REVOLVER:
			// Standard revolver, L'Etranger, Enforcer, Diamondback
			return 1.25f;

		// === SNIPER RIFLES (no spread when scoped, full spread unscoped) ===
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			return 1.25f;

		// === SENTRY GUNS ===
		case TF_WEAPON_SENTRY_BULLET:
		case TF_WEAPON_SENTRY_REVENGE:
			return 999.0f; // Sentries don't have perfect shot

		default:
			return 1.25f; // Default single-bullet threshold
		}
	}

	// Check if weapon has fixed spread pattern (competitive mode or attribute)
	bool HasFixedSpreadPattern(C_TFWeaponBase* weapon)
	{
		static ConVar* tf_use_fixed_weaponspreads = I::CVar->FindVar("tf_use_fixed_weaponspreads");
		if (tf_use_fixed_weaponspreads && tf_use_fixed_weaponspreads->GetBool())
			return true;

		// Some weapons have fixed spread via attribute
		const int nFixedSpread = static_cast<int>(SDKUtils::AttribHookValue(0.0f, "fixed_shot_pattern", weapon));
		return nFixedSpread != 0;
	}

	// Get Panic Attack spread multiplier based on health
	// Lower health = tighter spread (inverse of normal panic attack behavior for damage)
	float GetPanicAttackSpreadMult(C_TFWeaponBase* weapon, C_TFPlayer* local)
	{
		const float flReducedHealthBonus = SDKUtils::AttribHookValue(1.0f, "panic_attack_negative", weapon);
		if (flReducedHealthBonus == 1.0f)
			return 1.0f;

		const float flHealthFrac = static_cast<float>(local->m_iHealth()) / static_cast<float>(local->GetMaxHealth());
		return Math::RemapValClamped(flHealthFrac, 0.2f, 0.9f, flReducedHealthBonus, 1.0f);
	}

	// Get spread multiplier for weapons with consecutive shot scaling
	// (e.g., some weapons get more spread the more you fire)
	float GetConsecutiveShotSpreadMult(C_TFWeaponBase* weapon, float timeSinceLastShot)
	{
		const float flScaler = SDKUtils::AttribHookValue(0.0f, "mult_spread_scales_consecutive", weapon);
		if (flScaler == 0.0f)
			return 1.0f;

		// If we haven't fired recently, no penalty
		if (timeSinceLastShot > 0.5f)
			return 1.0f;

		// Estimate consecutive shots based on time since last shot
		// This is approximate - ideally we'd track actual consecutive shots
		const float flConsecutive = Math::RemapValClamped(timeSinceLastShot, 0.0f, 0.5f, 5.0f, 1.0f);
		return Math::RemapValClamped(flConsecutive, 1.0f, 5.0f, 1.125f, 1.5f);
	}

	// Get weapon-specific spread multiplier (combines all modifiers)
	float GetWeaponSpreadMultiplier(C_TFWeaponBase* weapon, C_TFPlayer* local, float timeSinceLastShot)
	{
		float flMult = 1.0f;
		const int weaponID = weapon->GetWeaponID();

		// Minigun: 1.5x spread during first second of firing
		if (weaponID == TF_WEAPON_MINIGUN)
		{
			flMult *= GetMinigunSpreadMult(weapon, local);
		}

		// Panic Attack: spread based on health
		flMult *= GetPanicAttackSpreadMult(weapon, local);

		// Consecutive shot scaling
		flMult *= GetConsecutiveShotSpreadMult(weapon, timeSinceLastShot);

		return flMult;
	}
}

void CSeedPred::AdjustAngles(CUserCmd* cmd)
{
	static ConVar* sv_usercmd_custom_random_seed = I::CVar->FindVar("sv_usercmd_custom_random_seed");
	const bool bTimeBased = sv_usercmd_custom_random_seed && sv_usercmd_custom_random_seed->GetBool();

	// For time-based seeding we need sync; for MD5-per-cmd we don't
	if (!CFG::Exploits_SeedPred_Active || !cmd || !G::bFiring || (bTimeBased && !m_Synced))
		return;

	const auto local = H::Entities->GetLocal();
	if (!local)
		return;

	const auto weapon = H::Entities->GetWeapon();
	if (!weapon || H::AimUtils->GetWeaponType(weapon) != EWeaponType::HITSCAN)
		return;

	float spread = weapon->GetWeaponSpread();
	if (spread <= 0.0f)
		return;

	auto bulletsPerShot = weapon->GetWeaponInfo()->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_nBulletsPerShot;
	bulletsPerShot = static_cast<int>(SDKUtils::AttribHookValue(static_cast<float>(bulletsPerShot), "mult_bullets_per_shot", weapon));

	const int seed = GetSeedForCmd(cmd);
	m_CachedSeed = seed;

	const float timeSinceLastShot = (local->m_nTickBase() * TICK_INTERVAL) - weapon->m_flLastFireTime();
	const int weaponID = weapon->GetWeaponID();
	const EWeaponSpreadType spreadType = GetWeaponSpreadType(weapon);

	// === WEAPON-SPECIFIC SPREAD MODIFIERS ===

	// Get combined spread multiplier for this weapon
	const float spreadMult = GetWeaponSpreadMultiplier(weapon, local, timeSinceLastShot);

	// Ambassador: Special spread recovery (0.5s → 1.0s = full → zero spread)
	const bool bIsAmbassador = IsAmbassador(weapon);
	if (bIsAmbassador)
	{
		const float ambSpreadMult = GetAmbassadorSpreadMult(timeSinceLastShot);
		
		// Perfect accuracy after 1 second - no correction needed
		if (ambSpreadMult <= 0.0f)
			return;

		// Scale spread by Ambassador's recovery multiplier
		spread *= ambSpreadMult;
	}

	// Perfect shot logic from TF2 source (tf_fx_shared.cpp)
	const float perfectShotThreshold = GetPerfectShotThreshold(weapon, bulletsPerShot);
	const bool perfectShot = (timeSinceLastShot > perfectShotThreshold);

	// Fixed spread patterns (competitive mode or weapon attribute) - no seed prediction needed!
	if (HasFixedSpreadPattern(weapon) && bulletsPerShot > 1)
	{
		// In fixed spread mode, first pellet always goes center
		return;
	}

	// === SINGLE-BULLET WEAPONS ===
	if (bulletsPerShot == 1)
	{
		// Perfect first shot = no spread (except Ambassador which uses its own system)
		if (perfectShot)
			return;

		// Use pre-computed LUT
		Vec2 multiplier;
		if (seed <= 255 && m_SpreadInit)
		{
			multiplier = m_SpreadOffsets[seed];
		}
		else
		{
			SDKUtils::RandomSeed(seed);
			multiplier.x = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
			multiplier.y = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
		}

		Vec3 forward{}, right{}, up{};
		Math::AngleVectors(cmd->viewangles, &forward, &right, &up);

		// Calculate where the bullet will actually go with this spread
		const Vec3 spreadDir = forward + (right * multiplier.x * spread) + (up * multiplier.y * spread);
		Vec3 spreadAngles{};
		Math::VectorAngles(spreadDir, spreadAngles);

		// Reverse the spread: aim where we need to so spread lands on target
		Vec3 correctedAngles = (cmd->viewangles * 2.0f) - spreadAngles;
		Math::ClampAngles(correctedAngles);
		
		H::AimUtils->FixMovement(cmd, correctedAngles);
		cmd->viewangles = correctedAngles;
		G::bSilentAngles = true;
		return;
	}

	// === MULTI-BULLET WEAPONS (SHOTGUNS, SCATTERGUNS, ETC.) ===
	// Find bullet closest to center for maximum damage potential
	// Also consider neighboring seeds (±1) to account for timing jitter
	std::vector<Vec3> bulletDirections{};
	Vec3 averageDir{};

	// Try current seed and neighbors if mantissa step is low (unreliable timing)
	const int seedOffset = (m_MantissaStep < 4.0f) ? 1 : 0;
	int bestSeedOffset = 0;
	float bestSpreadScore = std::numeric_limits<float>::max();

	for (int tryOffset = -seedOffset; tryOffset <= seedOffset; tryOffset++)
	{
		std::vector<Vec3> tryDirections{};
		Vec3 tryAverage{};
		const int trySeed = (seed + tryOffset + 256) % 256;

		// TF2 uses sequential random calls for each pellet
		SDKUtils::RandomSeed(trySeed);

		for (int bullet = 0; bullet < bulletsPerShot; bullet++)
		{
			Vec2 multiplier;
			
			// First pellet of perfect shot goes center
			if (bullet == 0 && perfectShot)
			{
				multiplier = {0.0f, 0.0f};
			}
			else
			{
				// TF2 calls RandomFloat 4 times per pellet (x1, x2, y1, y2)
				multiplier.x = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
				multiplier.y = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
			}

			Vec3 forward{}, right{}, up{};
			Math::AngleVectors(cmd->viewangles, &forward, &right, &up);

			Vec3 bulletDir = forward + (right * multiplier.x * spread) + (up * multiplier.y * spread);
			bulletDir.NormalizeInPlace();
			tryAverage += bulletDir;
			tryDirections.push_back(bulletDir);
		}

		tryAverage /= static_cast<float>(bulletsPerShot);

		// Score this seed by how tight the spread is (lower = better)
		float spreadScore = 0.0f;
		for (const auto& dir : tryDirections)
			spreadScore += dir.DistTo(tryAverage);

		if (spreadScore < bestSpreadScore)
		{
			bestSpreadScore = spreadScore;
			bestSeedOffset = tryOffset;
			bulletDirections = std::move(tryDirections);
			averageDir = tryAverage;
		}
	}

	// Update cached seed if we picked a neighbor
	if (bestSeedOffset != 0)
		m_CachedSeed = (seed + bestSeedOffset + 256) % 256;

	// Find the bullet closest to center - this maximizes damage potential
	const auto bestBullet = std::ranges::min_element(bulletDirections,
		[&](const Vec3& lhs, const Vec3& rhs)
		{
			return lhs.DistTo(averageDir) < rhs.DistTo(averageDir);
		});

	if (bestBullet == bulletDirections.end())
		return;

	Vec3 bestAngles{};
	Math::VectorAngles(*bestBullet, bestAngles);

	// Apply correction to aim where the best bullet will land on target
	const Vec3 correction = cmd->viewangles - bestAngles;
	Vec3 correctedAngles = cmd->viewangles + correction;
	Math::ClampAngles(correctedAngles);

	H::AimUtils->FixMovement(cmd, correctedAngles);
	cmd->viewangles = correctedAngles;
	G::bSilentAngles = true;
}


void CSeedPred::Paint()
{
	if (!CFG::Exploits_SeedPred_Active || I::EngineVGui->IsGameUIVisible() || SDKUtils::BInEndOfMatch() || m_ServerTime <= 0.0f)
		return;

	// Anti-Screenshot
	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
		return;

	// Indicator
	if (CFG::Exploits_SeedPred_DrawIndicator)
	{
		const std::chrono::hh_mm_ss time{std::chrono::seconds(static_cast<int>(m_ServerTime))};

		int x = 2;
		int y = 2;

		H::Draw->String(
			H::Fonts->Get(EFonts::ESP_SMALL),
			x, y,
			{200, 200, 200, 255}, POS_DEFAULT,
			std::format("{}h {}m {}s (step {:.0f})", time.hours().count(), time.minutes().count(), time.seconds().count(), m_MantissaStep).c_str()
		);

		y += 10;

		H::Draw->String(
			H::Fonts->Get(EFonts::ESP_SMALL),
			x, y,
			!m_Synced ? Color_t{250, 130, 49, 255} : Color_t{32, 191, 107, 255}, POS_DEFAULT,
			!m_Synced ? "syncing.." : std::format("synced ({:.3f})", m_SyncOffset).c_str()
		);

		y += 10;

		H::Draw->String(
			H::Fonts->Get(EFonts::ESP_SMALL),
			x, y,
			{200, 200, 200, 255}, POS_DEFAULT,
			std::format("seed: {}", GetSeed()).c_str()
		);
	}
}
