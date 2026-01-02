#include "Crits.h"

#include "../CFG.h"
#include "../Menu/Menu.h"
#include "../amalgam_port/AmalgamCompat.h"

// Convert command number to seed with entity/player mask (Amalgam's method)
int CCritHack::CommandToSeed(int iCommandNumber)
{
	int iSeed = MD5_PseudoRandom(iCommandNumber) & std::numeric_limits<int>::max();
	int iMask = m_bMelee
		? m_iEntIndex << 16 | I::EngineClient->GetLocalPlayer() << 8
		: m_iEntIndex << 8 | I::EngineClient->GetLocalPlayer();
	return iSeed ^ iMask;
}

// Check if a specific seed will produce a crit
bool CCritHack::IsCritSeed(int iSeed, C_TFWeaponBase* pWeapon, bool bCrit, bool bSafe)
{
	if (iSeed == pWeapon->m_iCurrentSeed())
		return false;

	SDKUtils::RandomSeed(iSeed);
	int iRandom = SDKUtils::RandomInt(0, WEAPON_RANDOM_RANGE - 1);

	if (bSafe)
	{
		int iLower, iUpper;
		if (m_bMelee)
			iLower = 1500, iUpper = 6000;
		else
			iLower = 100, iUpper = 800;
		iLower = static_cast<int>(iLower * m_flMultCritChance);
		iUpper = static_cast<int>(iUpper * m_flMultCritChance);

		if (bCrit ? iLower >= 0 : iUpper < WEAPON_RANDOM_RANGE)
			return bCrit ? iRandom < iLower : !(iRandom < iUpper);
	}

	int iRange = static_cast<int>(m_flCritChance * WEAPON_RANDOM_RANGE);
	return bCrit ? iRandom < iRange : !(iRandom < iRange);
}

// Check if a command number will produce a crit
bool CCritHack::IsCritCommand(int iCommandNumber, C_TFWeaponBase* pWeapon, bool bCrit, bool bSafe)
{
	int iSeed = CommandToSeed(iCommandNumber);
	return IsCritSeed(iSeed, pWeapon, bCrit, bSafe);
}

// Find a command number that will produce a crit (or non-crit)
int CCritHack::GetCritCommand(C_TFWeaponBase* pWeapon, int iCommandNumber, bool bCrit, bool bSafe)
{
	for (int i = iCommandNumber; i < iCommandNumber + SEED_ATTEMPTS; i++)
	{
		if (IsCritCommand(i, pWeapon, bCrit, bSafe))
			return i;
	}
	return 0;
}


// Update weapon-specific info for crit calculations
void CCritHack::UpdateWeaponInfo(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	m_iEntIndex = pWeapon->entindex();
	m_bMelee = pWeapon->GetSlot() == 2; // SLOT_MELEE
	
	// Calculate base crit chance
	if (m_bMelee)
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE_MELEE * pLocal->GetCritMult();
	else if (pWeapon->IsRapidFire())
	{
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE_RAPID * pLocal->GetCritMult();
		float flNonCritDuration = (TF_DAMAGE_CRIT_DURATION_RAPID / m_flCritChance) - TF_DAMAGE_CRIT_DURATION_RAPID;
		m_flCritChance = 1.f / flNonCritDuration;
	}
	else
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE * pLocal->GetCritMult();
	
	m_flMultCritChance = SDKUtils::AttribHookValue(1.f, "mult_crit_chance", pWeapon);
	m_flCritChance *= m_flMultCritChance;

	// Track weapon state changes
	static C_TFWeaponBase* pStaticWeapon = nullptr;
	const C_TFWeaponBase* pOldWeapon = pStaticWeapon;
	pStaticWeapon = pWeapon;

	static float flStaticBucket = 0.f;
	const float flLastBucket = flStaticBucket;
	const float flBucket = flStaticBucket = pWeapon->m_flCritTokenBucket();

	static int iStaticCritChecks = 0;
	const int iLastCritChecks = iStaticCritChecks;
	const int iCritChecks = iStaticCritChecks = pWeapon->m_nCritChecks();

	static int iStaticCritSeedRequests = 0;
	const int iLastCritSeedRequests = iStaticCritSeedRequests;
	const int iCritSeedRequests = iStaticCritSeedRequests = pWeapon->m_nCritSeedRequests();

	if (pWeapon == pOldWeapon && flBucket == flLastBucket && iCritChecks == iLastCritChecks && iCritSeedRequests == iLastCritSeedRequests)
		return;

	// Get bucket cap from convar
	static auto tf_weapon_criticals_bucket_cap = I::CVar->FindVar("tf_weapon_criticals_bucket_cap");
	const float flBucketCap = tf_weapon_criticals_bucket_cap ? tf_weapon_criticals_bucket_cap->GetFloat() : 1000.f;
	bool bRapidFire = pWeapon->IsRapidFire();
	float flFireRate = pWeapon->GetFireRate();

	// Calculate damage per shot
	float flDamage = pWeapon->GetDamage();
	int nProjectilesPerShot = pWeapon->GetBulletsPerShot();
	if (!m_bMelee && nProjectilesPerShot > 0)
		nProjectilesPerShot = static_cast<int>(SDKUtils::AttribHookValue(static_cast<float>(nProjectilesPerShot), "mult_bullets_per_shot", pWeapon));
	else
		nProjectilesPerShot = 1;
	float flBaseDamage = flDamage *= nProjectilesPerShot;
	
	if (bRapidFire)
	{
		flDamage *= TF_DAMAGE_CRIT_DURATION_RAPID / flFireRate;
		if (flDamage * TF_DAMAGE_CRIT_MULTIPLIER > flBucketCap)
			flDamage = flBucketCap / TF_DAMAGE_CRIT_MULTIPLIER;
	}

	float flMult = m_bMelee ? 0.5f : Math::RemapValClamped(float(iCritSeedRequests + 1) / (iCritChecks + 1), 0.1f, 1.f, 1.f, 3.f);
	float flCost = flDamage * TF_DAMAGE_CRIT_MULTIPLIER;

	// Calculate potential crits (max possible)
	int iPotentialCrits = static_cast<int>((std::max(flBucketCap, flBucket) - flBaseDamage) / (TF_DAMAGE_CRIT_MULTIPLIER * flDamage / (m_bMelee ? 2 : 1) - flBaseDamage));
	
	// Calculate available crits (currently available)
	int iAvailableCrits = 0;
	{
		int iTestShots = iCritChecks, iTestCrits = iCritSeedRequests;
		float flTestBucket = flBucket;
		for (int i = 0; i < BUCKET_ATTEMPTS; i++)
		{
			iTestShots++; iTestCrits++;

			float flTestMult = m_bMelee ? 0.5f : Math::RemapValClamped(float(iTestCrits) / iTestShots, 0.1f, 1.f, 1.f, 3.f);
			if (flTestBucket < flBucketCap)
				flTestBucket = std::min(flTestBucket + flBaseDamage, flBucketCap);
			flTestBucket -= flCost * flTestMult;
			if (flTestBucket < 0.f)
				break;

			iAvailableCrits++;
		}
	}

	// Calculate next crit (shots until next crit available)
	int iNextCrit = 0;
	if (iAvailableCrits != iPotentialCrits)
	{
		int iTestShots = iCritChecks, iTestCrits = iCritSeedRequests;
		float flTestBucket = flBucket;
		float flTickBase = I::GlobalVars->curtime;
		float flLastRapidFireCritCheckTime = pWeapon->m_flLastRapidFireCritCheckTime();
		for (int i = 0; i < BUCKET_ATTEMPTS; i++)
		{
			int iCrits = 0;
			{
				int iTestShots2 = iTestShots, iTestCrits2 = iTestCrits;
				float flTestBucket2 = flTestBucket;
				for (int j = 0; j < BUCKET_ATTEMPTS; j++)
				{
					iTestShots2++; iTestCrits2++;

					float flTestMult = m_bMelee ? 0.5f : Math::RemapValClamped(float(iTestCrits2) / iTestShots2, 0.1f, 1.f, 1.f, 3.f);
					if (flTestBucket2 < flBucketCap)
						flTestBucket2 = std::min(flTestBucket2 + flBaseDamage, flBucketCap);
					flTestBucket2 -= flCost * flTestMult;
					if (flTestBucket2 < 0.f)
						break;

					iCrits++;
				}
			}
			if (iAvailableCrits < iCrits)
				break;

			if (!bRapidFire)
				iTestShots++;
			else 
			{
				flTickBase += std::ceilf(flFireRate / TICK_INTERVAL) * TICK_INTERVAL;
				if (flTickBase >= flLastRapidFireCritCheckTime + 1.f || !i && flTestBucket == flBucketCap)
				{
					iTestShots++;
					flLastRapidFireCritCheckTime = flTickBase;
				}
			}

			if (flTestBucket < flBucketCap)
				flTestBucket = std::min(flTestBucket + flBaseDamage, flBucketCap);

			iNextCrit++;
		}
	}

	m_flDamage = flBaseDamage;
	m_flCost = flCost * flMult;
	m_iPotentialCrits = iPotentialCrits;
	m_iAvailableCrits = iAvailableCrits;
	m_iNextCrit = iNextCrit;
}


// Update all crit info including ban status
void CCritHack::UpdateInfo(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	UpdateWeaponInfo(pLocal, pWeapon);

	m_bCritBanned = false;
	m_flDamageTilFlip = 0;
	if (!m_bMelee)
	{
		const float flNormalizedDamage = m_iCritDamage / TF_DAMAGE_CRIT_MULTIPLIER;
		float flCritChance = m_flCritChance + 0.1f;
		if (m_iRangedDamage && m_iCritDamage)
		{
			const float flObservedCritChance = flNormalizedDamage / (flNormalizedDamage + m_iRangedDamage - m_iCritDamage);
			m_bCritBanned = flObservedCritChance > flCritChance;
		}

		if (m_bCritBanned)
			m_flDamageTilFlip = flNormalizedDamage / flCritChance + flNormalizedDamage * 2 - m_iRangedDamage;
		else
			m_flDamageTilFlip = TF_DAMAGE_CRIT_MULTIPLIER * (flNormalizedDamage - flCritChance * (flNormalizedDamage + m_iRangedDamage - m_iCritDamage)) / (flCritChance - 1);
	}

	// Check player resource for damage desync
	if (auto pResource = GetTFPlayerResource())
	{
		// Access m_iDamage array by player index
		static int nDamageOffset = NetVars::GetNetVar("CTFPlayerResource", "m_iDamage");
		int iLocalPlayer = I::EngineClient->GetLocalPlayer();
		m_iResourceDamage = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(pResource) + nDamageOffset + iLocalPlayer * sizeof(int));
		m_iDesyncDamage = m_iRangedDamage + m_iMeleeDamage - m_iResourceDamage;
	}
}

// Check if weapon can random crit
bool CCritHack::WeaponCanCrit(C_TFWeaponBase* pWeapon, bool bWeaponOnly)
{
	// Check crit chance attribute
	if (SDKUtils::AttribHookValue(1.f, "mult_crit_chance", pWeapon) <= 0.f)
		return false;

	// Check tf_weapon_criticals convar
	if (!bWeaponOnly)
	{
		static auto tf_weapon_criticals = I::CVar->FindVar("tf_weapon_criticals");
		if (tf_weapon_criticals && !tf_weapon_criticals->GetInt())
			return false;
	}

	// Weapons that can't random crit
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PDA:
	case TF_WEAPON_PDA_ENGINEER_BUILD:
	case TF_WEAPON_PDA_ENGINEER_DESTROY:
	case TF_WEAPON_PDA_SPY:
	case TF_WEAPON_BUILDER:
	case TF_WEAPON_INVIS:
	case TF_WEAPON_LUNCHBOX:
	case TF_WEAPON_BUFF_ITEM:
	case TF_WEAPON_LASER_POINTER:
	case TF_WEAPON_MEDIGUN:
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_KNIFE:
	case TF_WEAPON_PASSTIME_GUN:
	case TF_WEAPON_FLAME_BALL:
	case TF_WEAPON_ROCKETPACK:
		return false;
	}

	return true;
}

void CCritHack::Reset()
{
	m_iCritDamage = 0;
	m_iRangedDamage = 0;
	m_iMeleeDamage = 0;
	m_iResourceDamage = 0;
	m_iDesyncDamage = 0;

	m_bCritBanned = false;
	m_flDamageTilFlip = 0;

	m_mHealthHistory.clear();
}

// Determine what crit action to take
int CCritHack::GetCritRequest(CUserCmd* pCmd, C_TFWeaponBase* pWeapon)
{
	// If "Ignore Crit Ban" is enabled, ignore both ban and available crits check
	bool bCanCrit = CFG::Exploits_Crits_Ignore_Ban || (m_iAvailableCrits > 0 && !m_bCritBanned);
	bool bPressed = H::Input->IsDown(CFG::Exploits_Crits_Force_Crit_Key);
	
	// Always melee crit when aimbot has target
	if (m_bMelee && H::Input->IsDown(CFG::Exploits_Crits_Force_Crit_Key_Melee) && G::nTargetIndex)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(G::nTargetIndex);
		if (pEntity && pEntity->GetClassId() == ETFClassIds::CTFPlayer)
			bPressed = true;
	}
	
	bool bSkip = CFG::Exploits_Crits_Skip_Random_Crits;
	bool bDesync = CommandToSeed(pCmd->command_number) == pWeapon->m_iCurrentSeed();

	return bCanCrit && bPressed ? CritRequest_Crit : bSkip || bDesync ? CritRequest_Skip : CritRequest_Any;
}

// Main run function - called from CreateMove
void CCritHack::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	// During shifting (doubletap), don't reset forced state - keep using the same forced command
	// This ensures all shifted ticks use the same crit seed
	if (!Shifting::bShiftingRapidFire && !Shifting::bRapidFireWantShift)
	{
		m_bForcingCrit = false;
		m_bForcingSkip = false;
		m_iForcedCommandNumber = 0;
		m_iForcedSeed = 0;
	}

	if (!pWeapon || !pLocal || pLocal->deadflag() || !I::EngineClient->IsInGame())
		return;

	UpdateInfo(pLocal, pWeapon);
	
	// Skip if crit boosted or streaming crits or weapon can't crit
	if (pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime || !WeaponCanCrit(pWeapon))
		return;

	// Attack detection - check for doubletap/shifting first
	bool bDoubletap = Shifting::bShiftingRapidFire || Shifting::bRapidFireWantShift;
	bool bAttacking = bDoubletap;
	
	if (!bAttacking)
	{
		if (m_bMelee)
		{
			bAttacking = G::bCanPrimaryAttack && (pCmd->buttons & IN_ATTACK);
			if (!bAttacking && pWeapon->GetWeaponID() == TF_WEAPON_FISTS)
				bAttacking = G::bCanPrimaryAttack && (pCmd->buttons & IN_ATTACK2);
		}
		else if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
		{
			// Minigun: check current command for IN_ATTACK (aimbot may have added it)
			bAttacking = (pCmd->buttons & IN_ATTACK);
		}
		else
		{
			// For other weapons, use G::Attacking or check buttons directly
			bAttacking = G::Attacking || (G::bCanPrimaryAttack && (pCmd->buttons & IN_ATTACK));
			
			// Beggar's Bazooka special case
			if (!bAttacking)
			{
				int nWeaponID = pWeapon->GetWeaponID();
				if (nWeaponID == TF_WEAPON_ROCKETLAUNCHER || nWeaponID == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)
				{
					if (pWeapon->IsInReload() && G::bCanPrimaryAttack && SDKUtils::AttribHookValue(0.f, "can_overload", pWeapon))
					{
						int iClip1 = pWeapon->m_iClip1();
						if (iClip1 > 0)
							bAttacking = true;
					}
				}
			}
		}
	}
	
	if (!bAttacking)
		return;
	
	// Rapid fire weapons have a 1 second cooldown on crit checks
	if (pWeapon->IsRapidFire() && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
		return;

	int iRequest = GetCritRequest(pCmd, pWeapon);
	if (iRequest == CritRequest_Any)
		return;

	// Anti-cheat compatibility mode (skip if crit detection bypass is enabled)
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_SkipCritDetection)
	{
		// Safe mode: only fire when current command matches our request
		if (!IsCritCommand(pCmd->command_number, pWeapon, iRequest == CritRequest_Crit, false))
		{
			// Block the attack - but preserve minigun rev state
			pCmd->buttons &= ~IN_ATTACK;
			
			// For minigun: restore IN_ATTACK2 so it stays revved while waiting
			if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
			{
				if (G::OriginalCmd.buttons & IN_ATTACK2)
					pCmd->buttons |= IN_ATTACK2;
			}
			
			G::bPSilentAngles = false;
			return;
		}
	}
	else
	{
		// During shifting, reuse the already-found forced command for all ticks
		// This ensures all doubletap shots use the same crit seed
		if (bDoubletap && m_iForcedCommandNumber != 0)
		{
			pCmd->command_number = m_iForcedCommandNumber;
			pCmd->random_seed = MD5_PseudoRandom(m_iForcedCommandNumber) & std::numeric_limits<int>::max();
		}
		else
		{
			// Normal mode: find a command number that will produce crit/non-crit
			int iCommand = GetCritCommand(pWeapon, pCmd->command_number, iRequest == CritRequest_Crit);
			if (iCommand)
			{
				pCmd->command_number = iCommand;
				pCmd->random_seed = MD5_PseudoRandom(iCommand) & std::numeric_limits<int>::max();
				
				// Store forced state so CalcIsAttackCritical hook can use it
				m_iForcedCommandNumber = iCommand;
				m_iForcedSeed = CommandToSeed(iCommand);
				m_bForcingCrit = (iRequest == CritRequest_Crit);
				m_bForcingSkip = (iRequest == CritRequest_Skip);
			}
		}
	}
	
	// Handle minigun special case - only remove IN_ATTACK2 when we're actually firing
	// This prevents both buttons being pressed at once which causes issues
	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && (pCmd->buttons & IN_ATTACK))
		pCmd->buttons &= ~IN_ATTACK2;
}

// Check if aimbot should fire - returns false if safe mode would block the shot
// Aimbots should call this BEFORE adding IN_ATTACK
bool CCritHack::ShouldAllowFire(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	// If safe mode is not enabled, always allow
	if (!CFG::Misc_AntiCheat_Enabled || CFG::Misc_AntiCheat_SkipCritDetection)
		return true;
	
	// If no weapon or player, allow (let other code handle it)
	if (!pWeapon || !pLocal || pLocal->deadflag())
		return true;
	
	// If weapon can't crit, allow
	if (!WeaponCanCrit(pWeapon))
		return true;
	
	// If crit boosted or streaming, allow
	if (pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime)
		return true;
	
	// Rapid fire cooldown check
	if (pWeapon->IsRapidFire() && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
		return true;
	
	// Update info for accurate crit checking
	UpdateInfo(pLocal, pWeapon);
	
	// Get what we want (crit or skip)
	int iRequest = GetCritRequest(pCmd, pWeapon);
	
	// If we don't care about crits, allow
	if (iRequest == CritRequest_Any)
		return true;
	
	// Check if current command matches our request
	return IsCritCommand(pCmd->command_number, pWeapon, iRequest == CritRequest_Crit, false);
}

// Predict command number for projectile simulation
int CCritHack::PredictCmdNum(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	auto getCmdNum = [&](int iCommandNumber)
	{
		if (!pWeapon || !pLocal || pLocal->deadflag() || !I::EngineClient->IsInGame() || CFG::Misc_AntiCheat_Enabled
			|| pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime || !WeaponCanCrit(pWeapon))
			return iCommandNumber;

		UpdateInfo(pLocal, pWeapon);
		if (pWeapon->IsRapidFire() && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
			return iCommandNumber;

		int iRequest = GetCritRequest(pCmd, pWeapon);
		if (iRequest == CritRequest_Any)
			return iCommandNumber;

		if (int iCommand = GetCritCommand(pWeapon, iCommandNumber, iRequest == CritRequest_Crit))
			return iCommand;
		return iCommandNumber;
	};

	static int iCommandNumber = 0;
	static int iStaticCommand = 0;
	if (pCmd->command_number != iStaticCommand)
	{
		iCommandNumber = getCmdNum(pCmd->command_number);
		iStaticCommand = pCmd->command_number;
	}

	return iCommandNumber;
}


// Store health history for damage tracking
void CCritHack::StoreHealthHistory(int iIndex, int iHealth, bool bDamage)
{
	bool bContains = m_mHealthHistory.contains(iIndex);
	auto& tHistory = m_mHealthHistory[iIndex];

	if (!bContains)
		tHistory = { iHealth, iHealth };
	else if (iHealth != tHistory.m_iNewHealth)
	{
		tHistory.m_iOldHealth = std::max(bDamage && tHistory.m_mHistory.contains(iHealth % 32768) ? tHistory.m_mHistory[iHealth % 32768].m_iOldHealth : tHistory.m_iNewHealth, iHealth);
		tHistory.m_iNewHealth = iHealth;
	}

	tHistory.m_mHistory[iHealth % 32768] = { tHistory.m_iOldHealth, I::GlobalVars->curtime };
	while (tHistory.m_mHistory.size() > 3)
	{
		int iIndex2 = 0; 
		float flMin = std::numeric_limits<float>::max();
		for (auto& [i, tStorage] : tHistory.m_mHistory)
		{
			if (tStorage.m_flTime < flMin)
				flMin = tStorage.m_flTime, iIndex2 = i;
		}
		tHistory.m_mHistory.erase(iIndex2);
	}
}

// Store current health of all players
void CCritHack::Store()
{
	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		auto pPlayer = I::ClientEntityList->GetClientEntity(n);
		if (pPlayer && pPlayer->GetClassId() == ETFClassIds::CTFPlayer)
		{
			auto pTFPlayer = pPlayer->As<C_TFPlayer>();
			if (pTFPlayer && !pTFPlayer->deadflag())
				StoreHealthHistory(pTFPlayer->entindex(), pTFPlayer->m_iHealth());
		}
	}
}

// Event handler for damage tracking
void CCritHack::Event(IGameEvent* pEvent, uint32_t uHash)
{
	static constexpr auto player_hurt = HASH_CT("player_hurt");
	static constexpr auto scorestats_accumulated_update = HASH_CT("scorestats_accumulated_update");
	static constexpr auto mvm_reset_stats = HASH_CT("mvm_reset_stats");
	static constexpr auto client_beginconnect = HASH_CT("client_beginconnect");
	static constexpr auto client_disconnect = HASH_CT("client_disconnect");
	static constexpr auto game_newmap = HASH_CT("game_newmap");
	
	if (uHash == player_hurt)
	{
		auto pLocal = H::Entities->GetLocal();
		if (!pLocal)
			return;
		
		int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
		int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
		bool bCrit = pEvent->GetBool("crit") || pEvent->GetBool("minicrit");
		int iDamage = pEvent->GetInt("damageamount");
		int iHealth = pEvent->GetInt("health");
		int iWeaponID = pEvent->GetInt("weaponid");

		// Correct damage for Dead Ringer spies
		if (m_mHealthHistory.contains(iVictim))
		{
			auto& tHistory = m_mHealthHistory[iVictim];
			auto pVictim = I::ClientEntityList->GetClientEntity(iVictim);

			if (!iHealth)
				iDamage = std::clamp(iDamage, 0, tHistory.m_iNewHealth);
			else if (pVictim && pVictim->GetClassId() == ETFClassIds::CTFPlayer)
			{
				auto pTFVictim = pVictim->As<C_TFPlayer>();
				if (pTFVictim && (pTFVictim->m_bFeignDeathReady() || pTFVictim->InCond(TF_COND_FEIGN_DEATH)))
				{
					int iOldHealth = (tHistory.m_mHistory.contains(iHealth) ? tHistory.m_mHistory[iHealth].m_iOldHealth : tHistory.m_iNewHealth) % 32768;
					if (iHealth > iOldHealth)
					{
						for (auto& [_, tOldHealth] : tHistory.m_mHistory)
						{
							int iOldHealth2 = tOldHealth.m_iOldHealth % 32768;
							if (iOldHealth2 > iHealth)
								iOldHealth = iHealth > iOldHealth ? iOldHealth2 : std::min(iOldHealth, iOldHealth2);
						}
					}
					iDamage = std::clamp(iOldHealth - iHealth, 0, iDamage);
				}
			}
		}
		if (iHealth)
			StoreHealthHistory(iVictim, iHealth);

		if (iVictim == iAttacker || iAttacker != I::EngineClient->GetLocalPlayer())
			return;

		// Don't track damage when crit boosted
		if (pLocal->IsCritBoosted())
			return;

		// Find which weapon dealt the damage
		C_TFWeaponBase* pWeapon = nullptr;
		for (int i = 0; i < 8; i++)
		{
			auto pSlotWeapon = pLocal->GetWeaponFromSlot(i);
			if (!pSlotWeapon || pSlotWeapon->GetWeaponID() != iWeaponID)
				continue;
			
			pWeapon = pSlotWeapon;
			break;
		}

		// Track damage based on weapon type
		if (!pWeapon || pWeapon->GetSlot() != 2) // Not melee
		{
			m_iRangedDamage += iDamage;
			if (bCrit && !pLocal->IsCritBoosted())
				m_iCritDamage += iDamage;
		}
		else
		{
			m_iMeleeDamage += iDamage;
		}
	}
	else if (uHash == scorestats_accumulated_update || uHash == mvm_reset_stats)
	{
		m_iRangedDamage = m_iCritDamage = m_iMeleeDamage = 0;
	}
	else if (uHash == client_beginconnect || uHash == client_disconnect || uHash == game_newmap)
	{
		Reset();
	}
}


// Draggable indicator logic
void CCritHack::Drag()
{
	const int nMouseX = H::Input->GetMouseX();
	const int nMouseY = H::Input->GetMouseY();
	
	static bool bDragging = false;
	static int nDeltaX = 0;
	static int nDeltaY = 0;
	
	if (!bDragging && F::Menu->IsMenuWindowHovered())
		return;
	
	const int x = CFG::Visuals_Crit_Indicator_Pos_X;
	const int y = CFG::Visuals_Crit_Indicator_Pos_Y;
	
	// Hitbox matches the indicator size (uses config values)
	const int nWidth = CFG::Visuals_Crit_Indicator_Width;
	const int nHeight = CFG::Visuals_Crit_Indicator_Height + 40;  // Bar height + text rows
	const bool bHovered = nMouseX >= x && nMouseX <= x + nWidth && nMouseY >= y && nMouseY <= y + nHeight;
	
	if (bHovered && H::Input->IsPressed(VK_LBUTTON))
	{
		nDeltaX = nMouseX - x;
		nDeltaY = nMouseY - y;
		bDragging = true;
	}
	
	if (!H::Input->IsPressed(VK_LBUTTON) && !H::Input->IsHeld(VK_LBUTTON))
		bDragging = false;
	
	if (bDragging)
	{
		CFG::Visuals_Crit_Indicator_Pos_X = nMouseX - nDeltaX;
		CFG::Visuals_Crit_Indicator_Pos_Y = nMouseY - nDeltaY;
	}
}

// Draw crit indicator - Two column flat dashboard style
void CCritHack::Draw()
{
	if (!CFG::Visuals_Crit_Indicator || I::EngineClient->IsTakingScreenshot())
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
		return;

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon)
		return;

	// Handle dragging when menu is open
	if (F::Menu->IsOpen())
		Drag();

	const int x = CFG::Visuals_Crit_Indicator_Pos_X;
	const int y = CFG::Visuals_Crit_Indicator_Pos_Y;
	
	// Text Mode - simple text-based indicator
	if (CFG::Visuals_Crit_Indicator_TextMode)
	{
		// Update the dynamic font size based on slider
		H::Fonts->UpdateCritIndicatorFont(CFG::Visuals_Crit_Indicator_TextSize);
		
		const auto& font = H::Fonts->Get(EFonts::CritIndicator);
		const int nBarW = 110 * CFG::Visuals_Crit_Indicator_TextSize / 100;
		const int lineSpacing = font.m_nTall;
		int yOffset = y + 2;
		
		Color_t textColor = CFG::Menu_Text;
		float flTickBase = TICKS_TO_TIME(pLocal->m_nTickBase());
		int iSlot = pWeapon->GetSlot();
		bool bHoldingMelee = (iSlot == WEAPON_SLOT_MELEE);
		bool bRapidFire = pWeapon->IsRapidFire();
		
		if (!WeaponCanCrit(pWeapon, false))
		{
			H::Draw->String(font, x + (nBarW / 2), y + 2, Color_t(255, 150, 150, 255), POS_CENTERXY, "Random crits disabled");
			return;
		}
		
		// Show calculating if we don't have data yet
		if (m_iAvailableCrits == 0 && m_iPotentialCrits == 0 && !m_bCritBanned)
		{
			H::Draw->String(font, x + (nBarW / 2), yOffset, { 255, 255, 0, 255 }, POS_CENTERXY, "Calculating...");
			return;
		}
		
		if (!m_bCritBanned || !bHoldingMelee)
		{
			H::Draw->String(font, x + (nBarW / 2), yOffset, textColor, POS_CENTERXY, "%d%s / %d crits", 
				m_iAvailableCrits, m_iAvailableCrits >= BUCKET_ATTEMPTS ? "+" : "", m_iPotentialCrits);
			yOffset += lineSpacing;
		}
		
		if (bHoldingMelee)
		{
			H::Draw->String(font, x + (nBarW / 2), yOffset, Color_t(100, 255, 100, 255), POS_CENTERXY, "Crits Always Available");
			yOffset += lineSpacing;
			if (pLocal->IsCritBoosted())
			{
				H::Draw->String(font, x + (nBarW / 2), yOffset, Color_t(100, 255, 255, 255), POS_CENTERXY, "Crit Boosted");
				yOffset += lineSpacing;
			}
			return;
		}
		
		if (m_bCritBanned)
		{
			H::Draw->String(font, x + (nBarW / 2), yOffset, Color_t(255, 100, 100, 255), POS_CENTERXY, "Crit Banned");
			yOffset += lineSpacing;
			H::Draw->String(font, x + (nBarW / 2), yOffset, Color_t(255, 150, 150, 255), POS_CENTERXY, "%.0f damage required", m_flDamageTilFlip);
			yOffset += lineSpacing;
		}
		else
		{
			if (pLocal->IsCritBoosted())
			{
				H::Draw->String(font, x + (nBarW / 2), yOffset, Color_t(100, 255, 255, 255), POS_CENTERXY, "Crit Boosted");
				yOffset += lineSpacing;
			}
			else if (pWeapon->m_flCritTime() > flTickBase)
			{
				float flTime = pWeapon->m_flCritTime() - flTickBase;
				H::Draw->String(font, x + (nBarW / 2), yOffset, Color_t(100, 255, 255, 255), POS_CENTERXY, "Streaming Crits %.1fs", flTime);
				yOffset += lineSpacing;
			}
			else if (m_iAvailableCrits > 0)
			{
				if (bRapidFire && flTickBase < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
				{
					float flTime = pWeapon->m_flLastRapidFireCritCheckTime() + 1.f - flTickBase;
					H::Draw->String(font, x + (nBarW / 2), yOffset, Color_t(255, 255, 255, 255), POS_CENTERXY, "Wait %.1fs", flTime);
				}
				else
				{
					H::Draw->String(font, x + (nBarW / 2), yOffset, Color_t(100, 255, 100, 255), POS_CENTERXY, "Crits Available");
				}
				yOffset += lineSpacing;
			}
			else
			{
				int iShots = m_iNextCrit;
				H::Draw->String(font, x + (nBarW / 2), yOffset, Color_t(255, 150, 150, 255), POS_CENTERXY, "Crit in %d%s Shot%s", 
					iShots, iShots >= BUCKET_ATTEMPTS ? "+" : "", iShots == 1 ? "" : "s");
				yOffset += lineSpacing;
			}
		}
		return;
	}
	
	// Bar Mode - original dashboard style indicator
	// Layout dimensions - configurable via sliders
	const int nWidth = CFG::Visuals_Crit_Indicator_Width;
	const int nBarHeight = CFG::Visuals_Crit_Indicator_Height;
	const int nPadding = 6;
	const int nRowHeight = 12;  // Smaller row height for smaller text
	
	const auto& font = H::Fonts->Get(EFonts::ESP_SMALL);  // Use smaller font
	
	// Colors - no background, just outline
	const Color_t clrOutline = {60, 60, 60, 255};
	const Color_t clrBarBg = {40, 40, 40, 180};  // Semi-transparent background
	const Color_t clrText = {220, 220, 220, 255};
	const Color_t clrTextGreen = {80, 255, 120, 255};
	const Color_t clrTextRed = {255, 80, 80, 255};
	const Color_t clrTextYellow = {255, 180, 60, 255};
	const Color_t clrTextCyan = {100, 255, 255, 255};
	
	// Get accent secondary color (with RGB support)
	Color_t clrAccent = CFG::Menu_Accent_Secondary;
	if (CFG::Menu_Accent_Secondary_RGB)
	{
		const float rate = CFG::Menu_Accent_Secondary_RGB_Rate;
		clrAccent.r = static_cast<byte>(std::lround(std::cosf(I::GlobalVars->realtime * rate + 0.0f) * 127.5f + 127.5f));
		clrAccent.g = static_cast<byte>(std::lround(std::cosf(I::GlobalVars->realtime * rate + 2.0f) * 127.5f + 127.5f));
		clrAccent.b = static_cast<byte>(std::lround(std::cosf(I::GlobalVars->realtime * rate + 4.0f) * 127.5f + 127.5f));
	}
	
	float flTickBase = TICKS_TO_TIME(pLocal->m_nTickBase());
	
	// Calculate box position (x,y is top-left)
	int nBoxX = x;
	int nBoxY = y;
	
	// Special states - compact single-line display (no background/outline)
	if (!WeaponCanCrit(pWeapon, true))
	{
		H::Draw->String(font, nBoxX + nWidth / 2, nBoxY + nPadding + nRowHeight / 2, {150, 150, 150, 255}, POS_CENTERXY, "NO CRITS");
		return;
	}
	
	if (pLocal->IsCritBoosted())
	{
		H::Draw->String(font, nBoxX + nWidth / 2, nBoxY + nPadding + nRowHeight / 2, clrTextCyan, POS_CENTERXY, "CRIT BOOSTED");
		return;
	}
	
	if (pWeapon->m_flCritTime() > flTickBase)
	{
		float flTime = pWeapon->m_flCritTime() - flTickBase;
		H::Draw->String(font, nBoxX + nWidth / 2, nBoxY + nPadding + nRowHeight / 2, clrTextCyan, POS_CENTERXY, 
			std::format("STREAMING {:.1f}s", flTime).c_str());
		return;
	}
	
	int nLeftX = nBoxX + nPadding;
	int nBarEndX = nBoxX + nWidth - nPadding;  // Right edge of the bar
	int nTextRightX = nBarEndX - 20;  // Offset for right-aligned text (text center point)
	int nDrawY = nBoxY;
	
	// === ROW 0: SAFE MODE (centered, only when anticheat is on and skip crit detection is off) ===
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_SkipCritDetection)
	{
		H::Draw->String(font, nBoxX + nWidth / 2, nDrawY, clrTextYellow, POS_CENTERX, "SAFE MODE");
		nDrawY += nRowHeight;
	}
	
	// === ROW 1: CRITS (left) and STATUS (right-aligned to bar end) ===
	int iCrits = m_iAvailableCrits;
	int iPotential = m_iPotentialCrits;
	
	// Left: CRITS: X/Y
	H::Draw->String(font, nLeftX, nDrawY, clrText, POS_DEFAULT,
		std::format("CRITS: {}{}/{}", iCrits, iCrits >= BUCKET_ATTEMPTS ? "+" : "", iPotential).c_str());
	
	// Right: Status (READY / BANNED / WAIT) - right-aligned to bar end
	if (m_bCritBanned)
	{
		H::Draw->String(font, nTextRightX, nDrawY, clrTextRed, POS_CENTERX, "BANNED");
	}
	else if (iCrits > 0)
	{
		if (pWeapon->IsRapidFire() && flTickBase < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
		{
			float flTime = pWeapon->m_flLastRapidFireCritCheckTime() + 1.f - flTickBase;
			H::Draw->String(font, nTextRightX, nDrawY, clrTextYellow, POS_CENTERX, std::format("{:.1f}s", flTime).c_str());
		}
		else
		{
			H::Draw->String(font, nTextRightX, nDrawY, clrTextGreen, POS_CENTERX, "READY");
		}
	}
	else if (iPotential > 0)
	{
		H::Draw->String(font, nTextRightX, nDrawY, clrTextYellow, POS_CENTERX, 
			std::format("{} SHOTS", m_iNextCrit).c_str());
	}
	else
	{
		H::Draw->String(font, nTextRightX, nDrawY, clrTextRed, POS_CENTERX, "EMPTY");
	}
	nDrawY += nRowHeight + 2;
	
	// === ROW 2: Progress bar ===
	int nBarX = nLeftX;
	int nBarY = nDrawY;
	int nActualBarWidth = nWidth - nPadding * 2;
	
	// Bar background + outline
	H::Draw->Rect(nBarX, nBarY, nActualBarWidth, nBarHeight, clrBarBg);
	H::Draw->OutlinedRect(nBarX, nBarY, nActualBarWidth, nBarHeight, clrOutline);
	
	// Calculate target fill ratio for available crits
	float flTargetRatio = 0.f;
	if (iPotential > 0)
		flTargetRatio = static_cast<float>(iCrits) / static_cast<float>(iPotential);
	
	// Track shot progress toward next crit
	// When m_iNextCrit decreases, we've made progress
	if (m_iNextCrit > 0 && m_iLastNextCrit > 0)
	{
		if (m_iNextCrit < m_iLastNextCrit)
		{
			// Shot was fired, increase progress
			m_iShotProgress += (m_iLastNextCrit - m_iNextCrit);
		}
		else if (m_iNextCrit > m_iLastNextCrit)
		{
			// Next crit requirement increased (maybe weapon switch), reset progress
			m_iShotProgress = 0;
		}
	}
	if (m_iNextCrit == 0)
	{
		// We have a crit available, reset progress
		m_iShotProgress = 0;
	}
	m_iLastNextCrit = m_iNextCrit;
	
	// Calculate ghost fill ratio (progress toward next crit)
	float flGhostTargetRatio = 0.f;
	if (iPotential > 0 && m_iNextCrit > 0)
	{
		// Ghost fill shows the "next crit" slot being filled
		int iTotalShotsNeeded = m_iNextCrit + m_iShotProgress;
		if (iTotalShotsNeeded > 0)
		{
			float flProgress = static_cast<float>(m_iShotProgress) / static_cast<float>(iTotalShotsNeeded);
			// Ghost fill is one crit slot ahead of current
			float flOneSlot = 1.0f / static_cast<float>(iPotential);
			flGhostTargetRatio = flTargetRatio + flOneSlot * flProgress;
		}
	}
	
	// Smooth animation - lerp toward target
	float flDeltaTime = I::GlobalVars->frametime;
	float flLerpSpeed = 8.0f;
	m_flDisplayedFillRatio += (flTargetRatio - m_flDisplayedFillRatio) * std::min(flDeltaTime * flLerpSpeed, 1.0f);
	m_flDisplayedNextCritRatio += (flGhostTargetRatio - m_flDisplayedNextCritRatio) * std::min(flDeltaTime * flLerpSpeed, 1.0f);
	
	// Snap to target when very close
	if (std::fabsf(flTargetRatio - m_flDisplayedFillRatio) < 0.01f)
		m_flDisplayedFillRatio = flTargetRatio;
	if (std::fabsf(flGhostTargetRatio - m_flDisplayedNextCritRatio) < 0.01f)
		m_flDisplayedNextCritRatio = flGhostTargetRatio;
	
	// Clamp to valid range
	m_flDisplayedFillRatio = std::clamp(m_flDisplayedFillRatio, 0.0f, 1.0f);
	m_flDisplayedNextCritRatio = std::clamp(m_flDisplayedNextCritRatio, 0.0f, 1.0f);
	
	int nInnerWidth = nActualBarWidth - 2;
	int nFillX = nBarX + 1;
	int nFillY = nBarY + 1;
	int nFillHeight = nBarHeight - 2;
	
	// Draw ghost fill first (transparent, shows progress toward next crit)
	int nGhostWidth = static_cast<int>(m_flDisplayedNextCritRatio * nInnerWidth);
	if (nGhostWidth > 0 && m_iNextCrit > 0)
	{
		Color_t clrGhost = {clrAccent.r, clrAccent.g, clrAccent.b, 80};  // Transparent
		H::Draw->Rect(nFillX, nFillY, nGhostWidth, nFillHeight, clrGhost);
	}
	
	// Draw solid fill on top (available crits)
	int nFillWidth = (m_flDisplayedFillRatio >= 1.0f) ? nInnerWidth : static_cast<int>(m_flDisplayedFillRatio * nInnerWidth);
	if (nFillWidth > 0)
	{
		// 3D-like gradient: draw multiple strips for smooth vertical gradient
		for (int row = 0; row < nFillHeight; row++)
		{
			float flVertRatio = static_cast<float>(row) / static_cast<float>(nFillHeight - 1);
			
			// Create 3D effect: bright highlight at top, base color in middle, darker at bottom
			float flBrightness;
			if (flVertRatio < 0.3f)
				flBrightness = 1.4f - (flVertRatio / 0.3f) * 0.4f;
			else if (flVertRatio < 0.7f)
				flBrightness = 1.0f;
			else
				flBrightness = 1.0f - ((flVertRatio - 0.7f) / 0.3f) * 0.35f;
			
			Color_t clrLeft = {
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.r * flBrightness * 0.9f), 0, 255)),
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.g * flBrightness * 0.9f), 0, 255)),
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.b * flBrightness * 0.9f), 0, 255)),
				255
			};
			
			Color_t clrRight = {
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.r * flBrightness), 0, 255)),
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.g * flBrightness), 0, 255)),
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.b * flBrightness), 0, 255)),
				255
			};
			
			H::Draw->GradientRect(nFillX, nFillY + row, nFillWidth, 1, clrLeft, clrRight, true);
		}
	}
	
	nDrawY += nBarHeight + 2;
	
	// === ROW 3: Damage til ban/unban (right-aligned to bar end) ===
	if (m_bCritBanned)
	{
		H::Draw->String(font, nTextRightX, nDrawY, clrTextRed, POS_CENTERX,
			std::format("DMG: {}", static_cast<int>(std::ceilf(m_flDamageTilFlip))).c_str());
	}
	else if (m_flDamageTilFlip > 0.f)
	{
		H::Draw->String(font, nTextRightX, nDrawY, clrTextGreen, POS_CENTERX,
			std::format("DMG: {}", static_cast<int>(std::floorf(m_flDamageTilFlip))).c_str());
	}
	
	// === Optional: Desync warning (on same row, left side) ===
	if (m_iDesyncDamage < 0)
	{
		H::Draw->String(font, nLeftX, nDrawY, clrTextYellow, POS_DEFAULT,
			std::format("DS: {}", m_iDesyncDamage).c_str());
	}
	nDrawY += nRowHeight;
	
	// === Optional: Debug info ===
	if (CFG::Visuals_Crit_Indicator_Debug)
	{
		H::Draw->String(font, nLeftX, nDrawY, {150, 150, 150, 255}, POS_DEFAULT,
			std::format("R:{} C:{} M:{}", m_iRangedDamage, m_iCritDamage, m_iMeleeDamage).c_str());
		nDrawY += nRowHeight;
		H::Draw->String(font, nLeftX, nDrawY, {150, 150, 150, 255}, POS_DEFAULT,
			std::format("B:{:.0f}", pWeapon->m_flCritTokenBucket()).c_str());
	}
}
