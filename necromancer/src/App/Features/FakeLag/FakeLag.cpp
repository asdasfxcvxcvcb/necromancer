#include "FakeLag.h"
#include "../CFG.h"
#include "../Misc/Misc.h"
#include "../Players/Players.h"
#include "../FakeAngle/FakeAngle.h"
#include "../amalgam_port/AmalgamCompat.h"

void CFakeLag::PreserveBlastJump(C_TFPlayer* pLocal)
{
	m_bPreservingBlast = false;
	
	// Skip if auto rocket jump is active
	if (F::Misc->m_bRJDisableFakeLag)
		return;
	
	if (!pLocal->IsAlive())
		return;
	
	static bool bStaticGround = true;
	const bool bLastGround = bStaticGround;
	const bool bCurrGround = bStaticGround = pLocal->m_hGroundEntity() != nullptr;
	
	if (!pLocal->InCond(TF_COND_BLASTJUMPING) || bLastGround || !bCurrGround)
		return;
	
	m_bPreservingBlast = true;
}

void CFakeLag::Unduck(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	m_bUnducking = false;
	
	if (!pLocal->IsAlive())
		return;
	
	if (!(pLocal->m_hGroundEntity() && IsDucking(pLocal) && !(pCmd->buttons & IN_DUCK)))
		return;
	
	m_bUnducking = true;
}

void CFakeLag::Prediction(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	PreserveBlastJump(pLocal);
	Unduck(pLocal, pCmd);
}

bool CFakeLag::IsAllowed(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	// Calculate max allowed fakelag ticks
	static auto sv_maxusrcmdprocessticks = I::CVar->FindVar("sv_maxusrcmdprocessticks");
	int nMaxTicks = sv_maxusrcmdprocessticks ? sv_maxusrcmdprocessticks->GetInt() : 24;
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
		nMaxTicks = std::min(nMaxTicks, 8);
	
	// Reserve ticks for anti-aim if it's active
	const bool bAntiAimActive = F::FakeAngle->YawOn() && F::FakeAngle->ShouldRun(pLocal, pWeapon, pCmd);
	const int nAntiAimTicks = bAntiAimActive ? F::FakeAngle->AntiAimTicks() : 0;
	
	// Calculate max choke based on shifted ticks and anti-aim reservation
	int nMaxChoke = std::min(24 - Shifting::nAvailableTicks - nAntiAimTicks, std::min(21 - nAntiAimTicks, nMaxTicks - nAntiAimTicks));
	nMaxChoke = std::min(nMaxChoke, CFG::Exploits_FakeLag_Max_Ticks);
	
	// Check basic conditions
	if (!(CFG::Exploits_FakeLag_Enabled || m_bPreservingBlast || m_bUnducking)
		|| I::ClientState->chokedcommands >= nMaxChoke
		|| Shifting::bShifting || Shifting::bRecharging
		|| !pLocal->IsAlive())
		return false;
	
	// Preserve blast jump takes priority
	if (m_bPreservingBlast)
	{
		G::bPSilentAngles = true; // Prevent unchoking while grounded
		return true;
	}
	
	// ONLY unchoke when actually firing a shot (G::Attacking == 1 or G::bFiring)
	if (G::Attacking == 1 || G::bFiring)
		return false;
	
	// Don't fakelag during auto rocket jump or AutoFaN
	if (F::Misc->m_bRJDisableFakeLag || F::Misc->IsAutoFaNRunning())
		return false;
	
	// Don't fakelag with Beggar's Bazooka while charging
	if (pWeapon && pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka 
		&& (pCmd->buttons & IN_ATTACK) && !(G::LastUserCmd ? G::LastUserCmd->buttons & IN_ATTACK : false))
		return false;
	
	// Unduck handling
	if (m_bUnducking)
		return true;
	
	// Only fakelag when moving (if option enabled)
	if (CFG::Exploits_FakeLag_Only_Moving && pLocal->m_vecVelocity().Length2D() <= 10.0f)
		return false;
	
	// Adaptive mode: check teleport distance
	static auto sv_lagcompensation_teleport_dist = I::CVar->FindVar("sv_lagcompensation_teleport_dist");
	const float flMaxDist = sv_lagcompensation_teleport_dist ? sv_lagcompensation_teleport_dist->GetFloat() : 64.0f;
	const float flDistSqr = (pLocal->m_vecOrigin() - m_vLastPosition).Length2DSqr();
	
	// If we've moved too far, unchoke
	if (flDistSqr >= (flMaxDist * flMaxDist))
		return false;
	
	return true;
}

bool CFakeLag::IsSniperThreat(C_TFPlayer* pLocal, int& outMinTicks, int& outMaxTicks)
{
	// Legacy wrapper - returns true if any threat type is detected
	return CheckSniperThreat(pLocal, outMinTicks, outMaxTicks) != EFakeLagThreatType::None;
}

int CFakeLag::CalculateMaxAllowedTicks(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	// Get server max ticks
	static auto sv_maxusrcmdprocessticks = I::CVar->FindVar("sv_maxusrcmdprocessticks");
	int nMaxTicks = sv_maxusrcmdprocessticks ? sv_maxusrcmdprocessticks->GetInt() : 24;
	
	// Anti-cheat compatibility limits to 8 ticks
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
		nMaxTicks = std::min(nMaxTicks, 8);
	
	// Reserve ticks for anti-aim if active (max 22 ticks when anti-aiming)
	int nAntiAimTicks = 0;
	bool bAntiAiming = F::FakeAngle->YawOn() && F::FakeAngle->ShouldRun(pLocal, pWeapon, pCmd);
	if (bAntiAiming)
	{
		nAntiAimTicks = F::FakeAngle->AntiAimTicks();
		// When anti-aiming, absolute max is 22 ticks for fakelag
		nMaxTicks = std::min(nMaxTicks, 22);
	}
	
	// Account for stored ticks (rapid fire/doubletap)
	int nStoredTicks = Shifting::nAvailableTicks;
	
	// Calculate available ticks: max - stored - anti-aim reservation
	int nAvailable = nMaxTicks - nStoredTicks - nAntiAimTicks;
	
	// Also respect the 21 tick limit minus anti-aim
	nAvailable = std::min(nAvailable, 21 - nAntiAimTicks);
	
	// Respect user's max ticks slider
	nAvailable = std::min(nAvailable, CFG::Exploits_FakeLag_Max_Ticks);
	
	// Ensure at least 1 tick available
	return std::max(nAvailable, 1);
}

EFakeLagThreatType CFakeLag::CheckSniperThreat(C_TFPlayer* pLocal, int& outMinTicks, int& outMaxTicks)
{
	if (!CFG::Exploits_FakeLag_Activate_On_Sightline || !CFG::Exploits_FakeLag_Enabled)
		return EFakeLagThreatType::None;

	// Default values for no tag (will be overridden based on threat type)
	outMinTicks = 4;
	outMaxTicks = 7;
	
	// Base sightline distance multiplier for OBB check (no tag baseline)
	constexpr float flBaseDistMultX = 6.0f;
	constexpr float flBaseDistMultY = 6.0f;
	constexpr float flBaseDistMultZ = 3.0f;
	
	EFakeLagThreatType eHighestThreat = EFakeLagThreatType::None;
	int nHighestMinTicks = 0;
	int nHighestMaxTicks = 0;

	// Cache local player data once
	const Vec3 vLocalOrigin = pLocal->m_vecOrigin();
	const Vec3 vLocalMins = pLocal->m_vecMins();
	const Vec3 vLocalMaxs = pLocal->m_vecMaxs();
	const auto& localTransform = pLocal->RenderableToWorldTransform();

	// Get enemy players once
	const auto& vEnemies = H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES);
	
	for (const auto pEntity : vEnemies)
	{
		if (!pEntity)
			continue;

		const auto pEnemy = pEntity->As<C_TFPlayer>();
		if (!pEnemy || pEnemy->deadflag())
			continue;
		
		// Early class check before expensive operations
		if (pEnemy->m_iClass() != TF_CLASS_SNIPER)
			continue;

		const auto pWeapon = pEnemy->m_hActiveWeapon().Get();
		if (!pWeapon)
			continue;
			
		const auto pTFWeapon = pWeapon->As<C_TFWeaponBase>();
		if (!pTFWeapon)
			continue;
		
		// Early slot check
		if (pTFWeapon->GetSlot() != WEAPON_SLOT_PRIMARY)
			continue;
			
		const int nWeaponID = pTFWeapon->GetWeaponID();
		if (nWeaponID == TF_WEAPON_COMPOUND_BOW)
			continue;

		// Check if enemy is scoped (required for all threat types)
		bool bZoomed = pEnemy->InCond(TF_COND_ZOOMED);
		if (nWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC)
			bZoomed = pTFWeapon->As<C_TFSniperRifleClassic>()->m_bCharging();

		if (!bZoomed)
			continue;

		// Get player tag info
		PlayerPriority pInfo{};
		const bool bHasTag = F::Players->GetInfo(pEnemy->entindex(), pInfo);

		// Determine threat type based on tag
		// Targeted and Nigger have same priority as Cheater
		if (bHasTag && (pInfo.Cheater || pInfo.Targeted || pInfo.Nigger))
		{
			// CHEATER/TARGETED/NIGGER TAG: 19-24 ticks, no sightline distance check needed
			if (eHighestThreat != EFakeLagThreatType::Cheater)
			{
				eHighestThreat = EFakeLagThreatType::Cheater;
				nHighestMinTicks = 19;
				nHighestMaxTicks = 24;
			}
			continue;
		}

		// Calculate OBB bounds based on tag type
		// Streamer has same priority as RetardLegit
		const float flDistMult = (bHasTag && (pInfo.RetardLegit || pInfo.Streamer)) ? 2.0f : 1.0f;
		
		Vec3 vMins = vLocalMins;
		Vec3 vMaxs = vLocalMaxs;
		vMins.x *= flBaseDistMultX * flDistMult;
		vMins.y *= flBaseDistMultY * flDistMult;
		vMins.z *= flBaseDistMultZ * flDistMult;
		vMaxs.x *= flBaseDistMultX * flDistMult;
		vMaxs.y *= flBaseDistMultY * flDistMult;
		vMaxs.z *= flBaseDistMultZ * flDistMult;

		Vec3 vForward{};
		Math::AngleVectors(pEnemy->GetEyeAngles(), &vForward);

		if (!Math::RayToOBB(pEnemy->GetShootPos(), vForward, vLocalOrigin, vMins, vMaxs, localTransform))
			continue;

		// Streamer has same priority as RetardLegit
		if (bHasTag && (pInfo.RetardLegit || pInfo.Streamer))
		{
			// RETARD LEGIT/STREAMER TAG: 6-12 ticks
			if (eHighestThreat != EFakeLagThreatType::Cheater)
			{
				eHighestThreat = EFakeLagThreatType::RetardLegit;
				nHighestMinTicks = 6;
				nHighestMaxTicks = 12;
			}
		}
		else if (eHighestThreat == EFakeLagThreatType::None)
		{
			// NO TAG: 4-7 ticks
			eHighestThreat = EFakeLagThreatType::NoTag;
			nHighestMinTicks = 4;
			nHighestMaxTicks = 7;
		}
	}

	// Set output values based on highest threat found
	if (eHighestThreat != EFakeLagThreatType::None)
	{
		outMinTicks = nHighestMinTicks;
		outMaxTicks = nHighestMaxTicks;
	}

	return eHighestThreat;
}

void CFakeLag::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd, bool* pSendPacket)
{
	// Default to sending packet
	*pSendPacket = true;
	m_eCurrentThreat = EFakeLagThreatType::None;
	
	// Set goal to 22 for adaptive mode
	if (!m_iGoal)
		m_iGoal = 22;
	
	// Check for new entities and force unchoke
	static int nLastPlayerCount = 0;
	int nCurrentPlayerCount = 0;
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		if (auto pPlayer = pEntity->As<C_TFPlayer>())
		{
			if (!pPlayer->deadflag())
				nCurrentPlayerCount++;
		}
	}
	
	if (nCurrentPlayerCount > nLastPlayerCount)
		m_iNewEntityUnchokeTicks = 5;
	nLastPlayerCount = nCurrentPlayerCount;
	
	if (m_iNewEntityUnchokeTicks > 0)
	{
		m_iNewEntityUnchokeTicks--;
		m_iGoal = 0;
		m_vLastPosition = pLocal->m_vecOrigin();
		m_bEnabled = false;
		return;
	}
	
	// Run prediction (blast jump preservation, unduck)
	Prediction(pLocal, pCmd);
	
	// Sightline-based logic
	if (CFG::Exploits_FakeLag_Activate_On_Sightline)
	{
		if (!CFG::Exploits_FakeLag_Enabled || pLocal->deadflag())
		{
			m_iGoal = 0;
			m_vLastPosition = pLocal->m_vecOrigin();
			m_bEnabled = false;
			return;
		}
		
		// Calculate max allowed ticks considering stored ticks, anti-aim, and menu settings
		int nMaxAllowedTicks = CalculateMaxAllowedTicks(pLocal, pWeapon, pCmd);
		
		if (I::ClientState->chokedcommands >= nMaxAllowedTicks)
		{
			m_iGoal = 0;
			m_vLastPosition = pLocal->m_vecOrigin();
			m_bEnabled = false;
			return;
		}
		
		if (Shifting::bShifting || Shifting::bRecharging)
		{
			m_iGoal = 0;
			m_vLastPosition = pLocal->m_vecOrigin();
			m_bEnabled = false;
			return;
		}
		
		if (F::Misc->m_bRJDisableFakeLag || F::Misc->IsAutoFaNRunning())
		{
			m_iGoal = 0;
			m_vLastPosition = pLocal->m_vecOrigin();
			m_bEnabled = false;
			return;
		}
		
		int nMinTicks = 4;
		int nMaxTicks = 7;
		EFakeLagThreatType eThreat = CheckSniperThreat(pLocal, nMinTicks, nMaxTicks);
		m_eCurrentThreat = eThreat;
		
		if (eThreat == EFakeLagThreatType::None)
		{
			m_iGoal = 0;
			m_iCurrentChokeTicks = 0;
			m_iTargetChokeTicks = 0;
			m_vLastPosition = pLocal->m_vecOrigin();
			m_bEnabled = false;
			return;
		}
		
		// ONLY unchoke when actually firing a shot - not just holding attack
		// This ensures we stay fakelagged until the moment we shoot
		if (G::Attacking == 1 || G::bFiring)
		{
			m_iGoal = 0;
			m_iCurrentChokeTicks = 0;
			m_iTargetChokeTicks = 0;
			m_vLastPosition = pLocal->m_vecOrigin();
			m_bEnabled = false;
			return;
		}
		
		// Clamp max ticks to what's actually allowed
		nMaxTicks = std::min(nMaxTicks, nMaxAllowedTicks);
		
		// Ensure min doesn't exceed max
		nMinTicks = std::min(nMinTicks, nMaxTicks);
		
		// Generate new target if needed
		if (m_iCurrentChokeTicks >= m_iTargetChokeTicks || m_iTargetChokeTicks == 0)
		{
			// Random ticks between min and max
			int nRange = nMaxTicks - nMinTicks;
			m_iTargetChokeTicks = nMinTicks + (nRange > 0 ? (rand() % (nRange + 1)) : 0);
			m_iCurrentChokeTicks = 0;
		}
		
		if (m_iCurrentChokeTicks < m_iTargetChokeTicks)
		{
			*pSendPacket = false;
			m_iCurrentChokeTicks++;
		}
		else
		{
			m_iCurrentChokeTicks = 0;
		}
		
		m_bEnabled = true;
		return;
	}
	
	if (!IsAllowed(pLocal, pWeapon, pCmd))
	{
		m_iGoal = 0;
		m_vLastPosition = pLocal->m_vecOrigin();
		m_bEnabled = false;
		return;
	}

	*pSendPacket = false;
	m_bEnabled = true;
}

// Called after Run() to set the draw chams flag based on actual fakelag state
void CFakeLag::UpdateDrawChams()
{
	// Show fake model when fakelagging or anti-aim is active
	F::FakeAngle->m_bDrawChams = m_bEnabled || F::FakeAngle->AntiAimOn();
}
