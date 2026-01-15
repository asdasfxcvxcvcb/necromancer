#include "Resolver.h"
#include "../CFG.h"
#include "../LagRecords/LagRecords.h"

void CResolver::Reset()
{
	m_mResolverData.clear();
	m_mSniperDots.clear();
	
	m_iWaitingForTarget = -1;
	m_flWaitingForDamage = 0.0f;
	m_bWaitingForHeadshot = false;
}

void CResolver::StoreSniperDots()
{
	m_mSniperDots.clear();
	
	// Find all sniper dots and store their positions
	// Iterate through all entities since there's no WORLD group
	const int nHighestIndex = I::ClientEntityList->GetHighestEntityIndex();
	for (int n = 1; n < nHighestIndex; n++)
	{
		const auto pEntity = I::ClientEntityList->GetClientEntity(n);
		if (!pEntity || pEntity->IsDormant())
			continue;
			
		if (pEntity->GetClassId() != ETFClassIds::CSniperDot)
			continue;
			
		const auto pDot = pEntity->As<C_BaseEntity>();
		const auto pOwner = pDot->m_hOwnerEntity().Get();
		
		if (!pOwner || pOwner->GetClassId() != ETFClassIds::CTFPlayer)
			continue;
			
		m_mSniperDots[pOwner->entindex()] = pDot->m_vecOrigin();
	}
}

std::optional<float> CResolver::GetPitchForSniperDot(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return std::nullopt;
		
	const int nIndex = pPlayer->entindex();
	if (!m_mSniperDots.contains(nIndex))
		return std::nullopt;
		
	const Vec3 vDotPos = m_mSniperDots[nIndex];
	const Vec3 vEyePos = pPlayer->m_vecOrigin() + pPlayer->GetViewOffset();
	const Vec3 vAngles = Math::CalcAngle(vEyePos, vDotPos);
	
	return vAngles.x;
}

void CResolver::FrameStageNotify()
{
	if (!CFG::Aimbot_Hitscan_Resolver || !I::EngineClient->IsInGame())
		return;
		
	StoreSniperDots();
	
	// Update resolver data for all players
	const auto& enemyPlayers = H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES);
	for (const auto pEntity : enemyPlayers)
	{
		if (!pEntity)
			continue;
			
		const auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer->IsAlive() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
			continue;
			
		const int nIndex = pPlayer->entindex();
		auto& data = m_mResolverData[nIndex];
		
		// Detect when enemy shoots (they expose real angles)
		DetectEnemyShoot(pPlayer);
		
		// Update yaw resolver state
		data.m_bYaw = (data.m_flYaw != 0.0f);
		
		// Handle out-of-bounds pitch (fake angles)
		if (fabsf(pPlayer->m_angEyeAnglesX()) == 90.0f)
		{
			// Try to get real pitch from sniper dot
			if (auto flPitch = GetPitchForSniperDot(pPlayer))
			{
				data.m_flPitch = flPitch.value();
			}
			// Auto-resolve: use inverse pitch by default
			else if (!data.m_bFirstOOBPitch && data.m_bAutoSetPitch || data.m_bInversePitch)
			{
				data.m_flPitch = -pPlayer->m_angEyeAnglesX();
			}
			// Clamp to valid values
			else
			{
				data.m_flPitch = fabsf(data.m_flPitch) > 45.0f ? (data.m_flPitch < 0.0f ? -90.0f : 90.0f) : 0.0f;
			}
			
			data.m_bPitch = true;
			data.m_bFirstOOBPitch = true;
		}
		else
		{
			data.m_bPitch = false;
		}
	}
}

void CResolver::CreateMove(C_TFPlayer* pLocal)
{
	if (!CFG::Aimbot_Hitscan_Resolver)
		return;
		
	// Auto-resolve on miss
	if (m_iWaitingForTarget != -1 && m_flWaitingForDamage < I::GlobalVars->curtime)
	{
		const auto pTarget = I::ClientEntityList->GetClientEntity(m_iWaitingForTarget)->As<C_TFPlayer>();
		if (pTarget && pTarget->IsAlive())
		{
			const int nIndex = pTarget->entindex();
			auto& data = m_mResolverData[nIndex];
			
			// Increment miss count
			data.m_nMissCount++;
			
			// Auto-resolve yaw - cycle through common fake angle offsets
			if (data.m_bAutoSetYaw)
			{
				// Cycle: 0° -> 90° -> -90° -> 180° -> repeat
				switch (data.m_nMissCount % 4)
				{
				case 0: data.m_flYaw = 0.0f; break;
				case 1: data.m_flYaw = 90.0f; break;
				case 2: data.m_flYaw = -90.0f; break;
				case 3: data.m_flYaw = 180.0f; break;
				}
				
				data.m_bYaw = (data.m_flYaw != 0.0f);
			}
			
			// Auto-resolve pitch
			if (data.m_bAutoSetPitch && fabsf(pTarget->m_angEyeAnglesX()) == 90.0f)
			{
				// Flip pitch on miss
				data.m_flPitch = -data.m_flPitch;
			}
			
			// Update lag records with new resolver angles
			F::LagRecords->ResolverUpdate(pTarget);
		}
		
		m_iWaitingForTarget = -1;
		m_flWaitingForDamage = 0.0f;
		m_bWaitingForHeadshot = false;
	}
}

void CResolver::HitscanRan(C_TFPlayer* pLocal, C_TFPlayer* pTarget, C_TFWeaponBase* pWeapon, int nHitbox)
{
	if (!CFG::Aimbot_Hitscan_Resolver || !pLocal || !pTarget || !pWeapon)
		return;
		
	// Don't resolve teammates
	if (pLocal->m_iTeamNum() == pTarget->m_iTeamNum())
		return;
		
	// Don't resolve on smooth aim (not accurate enough)
	if (CFG::Aimbot_Hitscan_Aim_Type == 2)
		return;
		
	// For shotguns, wait for perfect bullet (first shot after delay)
	if (pWeapon->GetWeaponSpread())
	{
		const float flTimeSinceLastShot = I::GlobalVars->curtime - pWeapon->m_flLastFireTime();
		const bool bIsShotgun = pWeapon->GetBulletsPerShot() > 1;
		const float flPerfectBulletTime = bIsShotgun ? 0.25f : 1.25f;
		
		if (flTimeSinceLastShot <= flPerfectBulletTime)
			return;
	}
	
	// Set up wait for damage event
	m_iWaitingForTarget = pTarget->entindex();
	
	// Calculate max latency for damage event
	const float flMaxLatency = F::LagRecords->GetMaxUnlag();
	m_flWaitingForDamage = I::GlobalVars->curtime + flMaxLatency * 1.5f + 0.1f;
	
	// Track if we're waiting for headshot (for crit detection)
	m_bWaitingForHeadshot = (nHitbox == HITBOX_HEAD && G::bCanHeadshot);
}

void CResolver::OnPlayerHurt(C_TFPlayer* pAttacker, C_TFPlayer* pVictim, bool bCrit)
{
	if (!CFG::Aimbot_Hitscan_Resolver)
		return;
		
	// Check if this is the shot we're waiting for
	if (m_iWaitingForTarget == -1 || !pAttacker || !pVictim)
		return;
		
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pAttacker != pLocal)
		return;
		
	if (pVictim->entindex() != m_iWaitingForTarget)
		return;
		
	// If we were waiting for headshot, verify it was a crit
	if (m_bWaitingForHeadshot && !bCrit)
		return;
		
	// We hit! Cancel the auto-resolve
	m_iWaitingForTarget = -1;
	m_flWaitingForDamage = 0.0f;
	m_bWaitingForHeadshot = false;
	
	// Reset miss count since we hit
	const int nIndex = pVictim->entindex();
	m_mResolverData[nIndex].m_nMissCount = 0;
}

bool CResolver::GetAngles(C_TFPlayer* pPlayer, float* pYaw, float* pPitch, bool* pMinwalk)
{
	if (!CFG::Aimbot_Hitscan_Resolver || !pPlayer)
		return false;
		
	if (!pPlayer->IsAlive() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
		return false;
		
	const int nIndex = pPlayer->entindex();
	auto& data = m_mResolverData[nIndex];
	
	const bool bYaw = data.m_bYaw;
	const bool bPitch = data.m_bPitch;
	
	// Calculate resolved yaw
	if (pYaw)
	{
		*pYaw = pPlayer->m_angEyeAnglesY();
		
		if (bYaw)
		{
			// View mode: aim towards local player
			if (data.m_bView)
			{
				const auto pLocal = H::Entities->GetLocal();
				if (pLocal)
				{
					const Vec3 vAngle = Math::CalcAngle(pPlayer->m_vecOrigin(), pLocal->m_vecOrigin());
					*pYaw = vAngle.y;
				}
			}
			
			// Add resolver offset
			*pYaw += data.m_flYaw;
			*pYaw = Math::NormalizeAngle(*pYaw);
		}
	}
	
	// Calculate resolved pitch
	if (pPitch)
	{
		*pPitch = pPlayer->m_angEyeAnglesX();
		
		if (bPitch)
		{
			*pPitch = data.m_flPitch;
		}
	}
	
	// Minwalk detection
	if (pMinwalk)
	{
		*pMinwalk = data.m_bMinwalk;
	}
	
	return bYaw || bPitch;
}

void CResolver::SetYaw(int nEntIndex, float flValue, bool bAuto)
{
	auto& data = m_mResolverData[nEntIndex];
	
	if (bAuto)
	{
		data.m_bAutoSetYaw = true;
		data.m_flYaw = 0.0f;
	}
	else
	{
		data.m_flYaw = flValue;
		data.m_bAutoSetYaw = false;
	}
	
	data.m_bYaw = (data.m_flYaw != 0.0f) || bAuto;
	
	// Update lag records
	const auto pPlayer = I::ClientEntityList->GetClientEntity(nEntIndex)->As<C_TFPlayer>();
	if (pPlayer)
	{
		F::LagRecords->ResolverUpdate(pPlayer);
	}
}

void CResolver::SetPitch(int nEntIndex, float flValue, bool bInverse, bool bAuto)
{
	auto& data = m_mResolverData[nEntIndex];
	
	if (bAuto)
	{
		data.m_bAutoSetPitch = true;
		data.m_bInversePitch = false;
	}
	else if (bInverse)
	{
		data.m_bInversePitch = true;
		data.m_bAutoSetPitch = false;
		
		const auto pPlayer = I::ClientEntityList->GetClientEntity(nEntIndex)->As<C_TFPlayer>();
		if (pPlayer)
		{
			data.m_flPitch = -pPlayer->m_angEyeAnglesX();
		}
	}
	else
	{
		data.m_flPitch = flValue;
		data.m_bInversePitch = false;
		data.m_bAutoSetPitch = false;
	}
	
	// Update lag records
	const auto pPlayer = I::ClientEntityList->GetClientEntity(nEntIndex)->As<C_TFPlayer>();
	if (pPlayer)
	{
		F::LagRecords->ResolverUpdate(pPlayer);
	}
}


// Detect when an enemy shoots - they must expose real angles to fire
void CResolver::DetectEnemyShoot(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return;
		
	const int nIndex = pPlayer->entindex();
	auto& data = m_mResolverData[nIndex];
	
	// Get player's weapon
	const auto pWeapon = pPlayer->m_hActiveWeapon().Get();
	if (!pWeapon)
		return;
		
	const auto pTFWeapon = pWeapon->As<C_TFWeaponBase>();
	if (!pTFWeapon)
		return;
		
	// Check if they just fired (last fire time changed)
	const float flLastFireTime = pTFWeapon->m_flLastFireTime();
	
	// If last fire time is recent and different from what we tracked
	if (flLastFireTime > data.m_flLastShootTime && flLastFireTime > 0.0f)
	{
		// Enemy just shot! Save this tick - they exposed real angles
		data.m_flLastShootTime = flLastFireTime;
		data.m_flShootSimTime = pPlayer->m_flSimulationTime();
		data.m_bHasShootRecord = true;
	}
	
	// Expire old shoot records after 1 second
	if (data.m_bHasShootRecord && I::GlobalVars->curtime - data.m_flLastShootTime > 1.0f)
	{
		data.m_bHasShootRecord = false;
	}
}

// Check if we should prioritize the shoot record (only for scoped sniper)
bool CResolver::ShouldUseShootRecord(C_TFPlayer* pPlayer, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (!pPlayer || !pLocal || !pWeapon)
		return false;
		
	// Only use shoot record when we're scoped as sniper
	if (pLocal->m_iClass() != TF_CLASS_SNIPER)
		return false;
		
	if (!pLocal->IsZoomed())
		return false;
		
	const int nWeaponID = pWeapon->GetWeaponID();
	if (nWeaponID != TF_WEAPON_SNIPERRIFLE && 
		nWeaponID != TF_WEAPON_SNIPERRIFLE_CLASSIC && 
		nWeaponID != TF_WEAPON_SNIPERRIFLE_DECAP)
		return false;
		
	// Check if we have a valid shoot record for this player
	const int nIndex = pPlayer->entindex();
	const auto& data = m_mResolverData[nIndex];
	
	return data.m_bHasShootRecord;
}

// Get the simulation time of the shoot record
float CResolver::GetShootRecordSimTime(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return 0.0f;
		
	const int nIndex = pPlayer->entindex();
	const auto& data = m_mResolverData[nIndex];
	
	if (!data.m_bHasShootRecord)
		return 0.0f;
		
	return data.m_flShootSimTime;
}
