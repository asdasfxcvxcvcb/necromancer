#include "Hitchance.h"
#include "../CFG.h"
#include "../SeedPred/SeedPred.h"
#include "../RapidFire/RapidFire.h"

// Fixed spread patterns from tf_fx_shared.cpp (competitive mode)
// 10 pellets - Square pattern
static const Vec3 g_vecFixedWpnSpreadPellets[] = {
	Vec3(0.0f, 0.0f, 0.0f),      // First pellet center
	Vec3(1.0f, 0.0f, 0.0f),
	Vec3(-1.0f, 0.0f, 0.0f),
	Vec3(0.0f, -1.0f, 0.0f),
	Vec3(0.0f, 1.0f, 0.0f),
	Vec3(0.85f, -0.85f, 0.0f),
	Vec3(0.85f, 0.85f, 0.0f),
	Vec3(-0.85f, -0.85f, 0.0f),
	Vec3(-0.85f, 0.85f, 0.0f),
	Vec3(0.0f, 0.0f, 0.0f)       // Last pellet center
};

// 15 pellets - Rectangle pattern
static const Vec3 g_vecFixedWpnSpreadPelletsWideLarge[] = {
	Vec3(0.0f, 0.0f, 0.0f),
	Vec3(-0.5f, 0.0f, 0.0f),
	Vec3(-1.0f, 0.0f, 0.0f),
	Vec3(0.5f, 0.0f, 0.0f),
	Vec3(1.0f, 0.0f, 0.0f),
	Vec3(0.0f, 0.5f, 0.0f),
	Vec3(-0.5f, 0.5f, 0.0f),
	Vec3(-1.0f, 0.5f, 0.0f),
	Vec3(0.5f, 0.5f, 0.0f),
	Vec3(1.0f, 0.5f, 0.0f),
	Vec3(0.0f, -0.5f, 0.0f),
	Vec3(-0.5f, -0.5f, 0.0f),
	Vec3(-1.0f, -0.5f, 0.0f),
	Vec3(0.5f, -0.5f, 0.0f),
	Vec3(1.0f, -0.5f, 0.0f)
};

// TF2 constants from source
#define TF_MINIGUN_SPINUP_TIME 0.75f
#define TF_MINIGUN_PENALTY_PERIOD 1.0f

void CHitchance::InitSpreadLUT()
{
	if (m_bInitialized) return;

	// Pre-compute spread offsets for all 256 seeds
	// TF2 uses: x = RandomFloat(-0.5, 0.5) + RandomFloat(-0.5, 0.5) (triangular distribution)
	for (int seed = 0; seed < 256; seed++)
	{
		SDKUtils::RandomSeed(seed);
		m_SpreadLUT[seed].x = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
		m_SpreadLUT[seed].y = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
	}
	m_bInitialized = true;
}

Vec2 CHitchance::GetSpreadForSeed(int nSeed)
{
	InitSpreadLUT();
	return m_SpreadLUT[nSeed & 255];
}

Vec2 CHitchance::GetFixedSpreadForPellet(int nPellet, int nTotalPellets)
{
	if (nTotalPellets >= 15)
	{
		const int idx = nPellet % 15;
		return Vec2(g_vecFixedWpnSpreadPelletsWideLarge[idx].x, g_vecFixedWpnSpreadPelletsWideLarge[idx].y);
	}
	else
	{
		const int idx = nPellet % 10;
		return Vec2(g_vecFixedWpnSpreadPellets[idx].x * 0.5f, g_vecFixedWpnSpreadPellets[idx].y * 0.5f);
	}
}

// Minigun spread multiplier based on spin-up time
// From tf_weapon_minigun.cpp: spread *= RemapValClamped(spinTime, 0, 1, 1.5, 1.0)
float CHitchance::GetMinigunSpreadMult(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal)
{
	if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_MINIGUN)
		return 1.0f;

	const auto pMinigun = pWeapon->As<C_TFMinigun>();
	if (!pMinigun)
		return 1.0f;

	const int nState = pMinigun->m_iWeaponState();
	
	// Not firing yet = worst spread
	if (nState < AC_STATE_FIRING)
		return 1.5f;

	// Calculate firing duration
	const float flCurrentTime = pLocal->m_nTickBase() * TICK_INTERVAL;
	const float flFiringDuration = flCurrentTime - pWeapon->m_flLastFireTime();

	// After 1 second of firing, spread is normal
	if (flFiringDuration >= TF_MINIGUN_PENALTY_PERIOD)
		return 1.0f;

	// Ramp from 1.5x to 1.0x over 1 second
	return Math::RemapValClamped(flFiringDuration, 0.0f, TF_MINIGUN_PENALTY_PERIOD, 1.5f, 1.0f);
}

// Ambassador spread multiplier based on time since last shot
// From tf_weapon_revolver.cpp: spread = RemapValClamped(timeSince, 1.0, 0.5, 0.0, baseSpread)
float CHitchance::GetAmbassadorSpreadMult(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal)
{
	if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_REVOLVER)
		return 1.0f;

	// Check if this is Ambassador (set_weapon_mode = 1)
	const int nWeaponMode = static_cast<int>(SDKUtils::AttribHookValue(0.0f, "set_weapon_mode", pWeapon));
	if (nWeaponMode != 1)
		return 1.0f;

	const float flCurrentTime = pLocal->m_nTickBase() * TICK_INTERVAL;
	const float flTimeSince = flCurrentTime - pWeapon->m_flLastFireTime();

	// After 1 second, perfect accuracy (0 spread)
	if (flTimeSince >= 1.0f)
		return 0.0f;
	
	// Before 0.5 seconds, full spread
	if (flTimeSince <= 0.5f)
		return 1.0f;

	// Ramp from full spread to 0 between 0.5s and 1.0s
	return Math::RemapValClamped(flTimeSince, 0.5f, 1.0f, 1.0f, 0.0f);
}

// Returns the variance multiplier for first shot accuracy
// From tf_fx_shared.cpp:
// - Multi-pellet: perfect accuracy if timeSince > 0.25s
// - Single-pellet: perfect accuracy if timeSince > 1.25s
// Returns 0.0 = perfect accuracy, 0.5 = full spread
float CHitchance::GetFirstShotVariance(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal)
{
	if (!pWeapon || !pLocal)
		return 0.5f;

	const float flCurrentTime = pLocal->m_nTickBase() * TICK_INTERVAL;
	const float flTimeSince = flCurrentTime - pWeapon->m_flLastFireTime();
	const int nBullets = GetBulletsPerShot(pWeapon);
	const int nWeaponID = pWeapon->GetWeaponID();

	// Ambassador has special handling - uses its own spread recovery
	if (nWeaponID == TF_WEAPON_REVOLVER)
	{
		const int nWeaponMode = static_cast<int>(SDKUtils::AttribHookValue(0.0f, "set_weapon_mode", pWeapon));
		if (nWeaponMode == 1)
		{
			// Ambassador: 0 spread after 1s, full spread before 0.5s
			if (flTimeSince >= 1.0f)
				return 0.0f;
			if (flTimeSince <= 0.5f)
				return 0.5f;
			return Math::RemapValClamped(flTimeSince, 0.5f, 1.0f, 0.5f, 0.0f);
		}
	}

	// Minigun doesn't have first shot accuracy
	if (nWeaponID == TF_WEAPON_MINIGUN)
		return 0.5f;

	// Standard first shot accuracy check
	bool bAccuracyBonus = false;
	if (nBullets > 1 && flTimeSince > 0.25f)
		bAccuracyBonus = true;
	else if (nBullets == 1 && flTimeSince > 1.25f)
		bAccuracyBonus = true;

	if (bAccuracyBonus)
	{
		// By default, perfect accuracy (0.0) unless attribute modifies it
		float flMult = SDKUtils::AttribHookValue(0.0f, "mult_spread_scale_first_shot", pWeapon);
		return flMult;
	}

	return 0.5f;
}

bool CHitchance::HasFirstShotAccuracy(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal)
{
	return GetFirstShotVariance(pWeapon, pLocal) < 0.5f;
}

bool CHitchance::IsFixedSpreadEnabled(C_TFWeaponBase* pWeapon)
{
	static ConVar* tf_use_fixed_weaponspreads = I::CVar->FindVar("tf_use_fixed_weaponspreads");
	if (tf_use_fixed_weaponspreads && tf_use_fixed_weaponspreads->GetBool())
		return true;

	if (pWeapon)
	{
		const int nFixedSpread = static_cast<int>(SDKUtils::AttribHookValue(0.0f, "fixed_shot_pattern", pWeapon));
		if (nFixedSpread != 0)
			return true;
	}

	return false;
}

int CHitchance::GetBulletsPerShot(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon) return 1;

	const auto pInfo = pWeapon->GetWeaponInfo();
	if (!pInfo) return 1;

	int nBullets = pInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_nBulletsPerShot;
	nBullets = static_cast<int>(SDKUtils::AttribHookValue(static_cast<float>(nBullets), "mult_bullets_per_shot", pWeapon));

	return std::max(1, nBullets);
}

int CHitchance::GetConsecutiveShots(C_TFWeaponBase* pWeapon)
{
	// TODO: Track consecutive shots for panic attack spread scaling
	return 0;
}

// Get weapon-specific spread multiplier
float CHitchance::GetWeaponSpreadMult(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal)
{
	if (!pWeapon || !pLocal)
		return 1.0f;

	const int nWeaponID = pWeapon->GetWeaponID();
	float flMult = 1.0f;

	// Minigun: 1.5x spread during first second of firing
	if (nWeaponID == TF_WEAPON_MINIGUN)
	{
		flMult *= GetMinigunSpreadMult(pWeapon, pLocal);
	}

	// Panic Attack: spread increases with lower health
	float flReducedHealthBonus = 1.0f;
	flReducedHealthBonus = SDKUtils::AttribHookValue(1.0f, "panic_attack_negative", pWeapon);
	if (flReducedHealthBonus != 1.0f)
	{
		const float flHealthFrac = static_cast<float>(pLocal->m_iHealth()) / static_cast<float>(pLocal->GetMaxHealth());
		flReducedHealthBonus = Math::RemapValClamped(flHealthFrac, 0.2f, 0.9f, flReducedHealthBonus, 1.0f);
		flMult *= flReducedHealthBonus;
	}

	// Consecutive shot spread scaling (some weapons)
	float flScaler = SDKUtils::AttribHookValue(0.0f, "mult_spread_scales_consecutive", pWeapon);
	if (flScaler != 0.0f)
	{
		const int nConsecutive = GetConsecutiveShots(pWeapon);
		if (nConsecutive > 0)
		{
			flScaler = Math::RemapValClamped(static_cast<float>(nConsecutive), 1.0f, 5.0f, 1.125f, 1.5f);
			flMult *= flScaler;
		}
	}

	return flMult;
}


float CHitchance::Calculate(
	C_TFPlayer* pLocal,
	C_TFWeaponBase* pWeapon,
	const Vec3& vShootPos,
	const Vec3& vTargetPos,
	float flHitboxRadius,
	bool bIsRapidFire,
	int nRapidFireShots)
{
	if (!pLocal || !pWeapon)
		return 0.0f;

	InitSpreadLUT();

	// Get weapon spread value (already includes weapon-specific modifiers from GetWeaponSpread)
	float flSpread = pWeapon->GetWeaponSpread();

	// No spread = always hit
	if (flSpread <= 0.0f)
		return 100.0f;

	const int nBullets = GetBulletsPerShot(pWeapon);
	const int nWeaponID = pWeapon->GetWeaponID();
	const bool bFixedSpread = IsFixedSpreadEnabled(pWeapon) && nBullets > 1;

	// Get first shot variance (0.0 = perfect, 0.5 = full spread)
	float flFirstShotVariance = GetFirstShotVariance(pWeapon, pLocal);

	// Single bullet weapon with perfect first shot accuracy = 100%
	if (nBullets == 1 && flFirstShotVariance <= 0.0f)
		return 100.0f;

	// Calculate distance to target
	Vec3 vDelta = vTargetPos - vShootPos;
	const float flDist = vDelta.Length();
	if (flDist <= 0.0f)
		return 100.0f;

	// The spread in TF2 works like this:
	// dir = forward + (x * spread * right) + (y * spread * up)
	// where x,y are random values from triangular distribution (-1 to 1)
	// 
	// At distance D, the bullet offset is approximately:
	// offset = D * spread * sqrt(x² + y²)
	//
	// To hit a target with radius R, we need:
	// D * spread * sqrt(x² + y²) <= R
	// sqrt(x² + y²) <= R / (D * spread)
	const float flMaxSpreadMagnitude = flHitboxRadius / (flDist * flSpread);

	// Fixed spread (competitive mode shotguns)
	if (bFixedSpread)
	{
		int nHits = 0;

		for (int pellet = 0; pellet < nBullets; pellet++)
		{
			Vec2 spreadPos = GetFixedSpreadForPellet(pellet, nBullets);
			float flMagnitude = sqrtf(spreadPos.x * spreadPos.x + spreadPos.y * spreadPos.y);

			if (flMagnitude <= flMaxSpreadMagnitude)
				nHits++;
		}

		return nHits > 0 ? 100.0f : 0.0f;
	}

	// Random spread simulation
	int nShotsToSimulate = 1;
	if (bIsRapidFire && nRapidFireShots > 1)
		nShotsToSimulate = nRapidFireShots;

	int nHits = 0;
	const int nSamples = 256;

	if (nBullets > 1)
	{
		// Shotgun-style weapons: check if ANY pellet hits
		for (int seed = 0; seed < nSamples; seed++)
		{
			bool bAnyHit = false;

			for (int shot = 0; shot < nShotsToSimulate && !bAnyHit; shot++)
			{
				for (int pellet = 0; pellet < nBullets && !bAnyHit; pellet++)
				{
					const int pelletSeed = (seed + shot + pellet) & 255;
					Vec2 spread = m_SpreadLUT[pelletSeed];

					// First pellet of first shot uses first shot variance
					float variance = 0.5f;
					if (pellet == 0 && shot == 0)
						variance = flFirstShotVariance;

					// Scale spread by variance (LUT is computed with variance=0.5)
					float x = spread.x * (variance / 0.5f);
					float y = spread.y * (variance / 0.5f);
					float flMagnitude = sqrtf(x * x + y * y);

					if (flMagnitude <= flMaxSpreadMagnitude)
						bAnyHit = true;
				}
			}

			if (bAnyHit)
				nHits++;
		}
	}
	else
	{
		// Single bullet weapons (pistol, SMG, minigun, revolver, etc.)
		for (int seed = 0; seed < nSamples; seed++)
		{
			bool bHit = false;

			for (int shot = 0; shot < nShotsToSimulate && !bHit; shot++)
			{
				const int shotSeed = (seed + shot) & 255;
				Vec2 spread = m_SpreadLUT[shotSeed];

				// First shot uses first shot variance, subsequent shots use full spread
				float variance = (shot == 0) ? flFirstShotVariance : 0.5f;

				float x = spread.x * (variance / 0.5f);
				float y = spread.y * (variance / 0.5f);
				float flMagnitude = sqrtf(x * x + y * y);

				if (flMagnitude <= flMaxSpreadMagnitude)
					bHit = true;
			}

			if (bHit)
				nHits++;
		}
	}

	return (static_cast<float>(nHits) / static_cast<float>(nSamples)) * 100.0f;
}
