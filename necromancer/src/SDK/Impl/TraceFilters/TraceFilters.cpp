#include "TraceFilters.h"
#include "../../SDK.h"

bool CTraceFilterHitscan::ShouldHitEntity(IHandleEntity *pServerEntity, int contentsMask)
{
	auto pLocal = H::Entities->GetLocal();
	auto pWeapon = H::Entities->GetWeapon();

	if (!pLocal || !pWeapon)
		return false;

	if (!pServerEntity || pServerEntity == m_pIgnore || pServerEntity == pLocal)
		return false;

	if (auto pEntity = static_cast<IClientEntity *>(pServerEntity)->As<C_BaseEntity>())
	{
		switch (pEntity->GetClassId())
		{
			case ETFClassIds::CFuncAreaPortalWindow:
			case ETFClassIds::CFuncRespawnRoomVisualizer:
			case ETFClassIds::CSniperDot:
			case ETFClassIds::CTFAmmoPack: return false;

			case ETFClassIds::CTFMedigunShield:
			{
				if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
					return false;

				break;
			}

			case ETFClassIds::CTFPlayer:
			case ETFClassIds::CObjectSentrygun:
			case ETFClassIds::CObjectDispenser:
			case ETFClassIds::CObjectTeleporter:
			{
				switch (pWeapon->GetWeaponID())
				{
					case TF_WEAPON_SNIPERRIFLE:
					case TF_WEAPON_SNIPERRIFLE_CLASSIC:
					case TF_WEAPON_SNIPERRIFLE_DECAP:
					{
						if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
							return false;

						break;
					}

					default: break;
				}

				break;
			}

			default: break;
		}

	}

	return true;
}

bool CTraceFilterWorldCustom::ShouldHitEntity(IHandleEntity *pServerEntity, int contentsMask)
{
	if (!pServerEntity)
		return false;

	// Skip the entity we're told to skip (usually local player)
	if (pServerEntity == m_pSkip)
		return false;

	auto pEntity = static_cast<IClientEntity *>(pServerEntity)->As<C_BaseEntity>();
	if (!pEntity)
		return false;

	// If this is our target, hit it
	if (pEntity == m_pTarget)
		return true;

	switch (pEntity->GetClassId())
	{
		// Target entities - only hit if it's our target (handled above)
		case ETFClassIds::CTFPlayer:
		case ETFClassIds::CObjectSentrygun:
		case ETFClassIds::CObjectDispenser:
		case ETFClassIds::CObjectTeleporter: 
			return pEntity == m_pTarget;

		// World props that can block projectiles (matching Amalgam's CTraceFilterCollideable)
		case ETFClassIds::CBaseEntity:
		case ETFClassIds::CBaseDoor:
		case ETFClassIds::CDynamicProp:
		case ETFClassIds::CPhysicsProp:
		case ETFClassIds::CPhysicsPropMultiplayer:
		case ETFClassIds::CFunc_LOD:
		case ETFClassIds::CObjectCartDispenser:
		case ETFClassIds::CFuncTrackTrain:
		case ETFClassIds::CFuncConveyor:
		case ETFClassIds::CTFGenericBomb:
		case ETFClassIds::CTFPumpkinBomb:
			return true;

		// Skip these - they don't block projectiles
		case ETFClassIds::CFuncAreaPortalWindow:
		case ETFClassIds::CTFAmmoPack:
		case ETFClassIds::CSniperDot:
		case ETFClassIds::CFuncRespawnRoomVisualizer:
		case ETFClassIds::CTFReviveMarker:
			return false;

		default: 
			return false;
	}
}

bool CTraceFilterArc::ShouldHitEntity(IHandleEntity* pServerEntity, int contentsMask)
{
	if (const auto pEntity = static_cast<IClientEntity*>(pServerEntity)->As<C_BaseEntity>())
	{
		// Skip the entity we're told to skip (usually local player)
		if (pEntity == m_pSkip)
			return false;

		switch (pEntity->GetClassId())
		{
			case ETFClassIds::CTFPlayer:
			case ETFClassIds::CObjectSentrygun:
			case ETFClassIds::CObjectDispenser:
			case ETFClassIds::CObjectTeleporter:
			case ETFClassIds::CObjectCartDispenser:
			case ETFClassIds::CBaseDoor:
			case ETFClassIds::CPhysicsProp:
			case ETFClassIds::CDynamicProp:
			case ETFClassIds::CBaseEntity:
			case ETFClassIds::CFuncTrackTrain:
			{
				return true;
			}

			default:
			{
				return false;
			}
		}
	}

	return false;
}

// Amalgam-style CTraceFilterCollideable implementation
bool CTraceFilterCollideable::ShouldHitEntity(IHandleEntity* pServerEntity, int contentsMask)
{
	if (!pServerEntity || pServerEntity == pSkip)
		return false;

	auto pEntity = static_cast<IClientEntity*>(pServerEntity)->As<C_BaseEntity>();
	if (!pEntity)
		return false;

	// Get team from skip entity if not set
	int nTeam = iTeam;
	if (nTeam == -1 && pSkip)
		nTeam = pSkip->m_iTeamNum();

	switch (pEntity->GetClassId())
	{
	// World props that always block projectiles
	case ETFClassIds::CBaseEntity:
	case ETFClassIds::CBaseDoor:
	case ETFClassIds::CDynamicProp:
	case ETFClassIds::CPhysicsProp:
	case ETFClassIds::CPhysicsPropMultiplayer:
	case ETFClassIds::CFunc_LOD:
	case ETFClassIds::CObjectCartDispenser:
	case ETFClassIds::CFuncTrackTrain:
	case ETFClassIds::CFuncConveyor:
	case ETFClassIds::CTFGenericBomb:
	case ETFClassIds::CTFPumpkinBomb:
		return true;

	// Respawn room visualizer - check team
	case ETFClassIds::CFuncRespawnRoomVisualizer:
		if (contentsMask & CONTENTS_PLAYERCLIP)
			return pEntity->m_iTeamNum() != nTeam;
		break;

	// Medigun shield - check team
	case ETFClassIds::CTFMedigunShield:
		if (!(contentsMask & CONTENTS_PLAYERCLIP))
			return pEntity->m_iTeamNum() != nTeam;
		break;

	// Players - skip check if iType == SKIP_CHECK (for GetProjectileFireSetup)
	case ETFClassIds::CTFPlayer:
		if (iType == SKIP_CHECK)
			return false;  // Skip player collision when doing fire setup trace
		if (iPlayer == PLAYER_ALL)
			return true;
		if (iPlayer == PLAYER_NONE)
			return false;
		return pEntity->m_iTeamNum() != nTeam;

	// Buildings - skip check if iType == SKIP_CHECK
	case ETFClassIds::CBaseObject:
	case ETFClassIds::CObjectSentrygun:
	case ETFClassIds::CObjectDispenser:
		if (iType == SKIP_CHECK)
			return false;  // Skip building collision when doing fire setup trace
		if (iObject == OBJECT_ALL)
			return true;
		if (iObject == OBJECT_NONE)
			return false;
		return pEntity->m_iTeamNum() != nTeam;

	// Teleporters - skip check if iType == SKIP_CHECK
	case ETFClassIds::CObjectTeleporter:
		if (iType == SKIP_CHECK)
			return false;
		return true;

	// Stickies - only if bMisc is true
	case ETFClassIds::CTFGrenadePipebombProjectile:
		return bMisc;
	}

	return false;
}

// Amalgam-style CTraceFilterWorldAndPropsOnlyAmalgam implementation
bool CTraceFilterWorldAndPropsOnlyAmalgam::ShouldHitEntity(IHandleEntity* pServerEntity, int contentsMask)
{
	if (!pServerEntity || pServerEntity == pSkip)
		return false;
	
	// Check for world entity (serial number check from Amalgam)
	if (pServerEntity->GetRefEHandle().GetSerialNumber() == (1 << 15))
		return I::ClientEntityList->GetClientEntity(0) != pSkip;

	auto pEntity = static_cast<IClientEntity*>(pServerEntity)->As<C_BaseEntity>();
	if (!pEntity)
		return false;

	// Get team from skip entity if not set
	int nTeam = iTeam;
	if (nTeam == -1 && pSkip)
		nTeam = pSkip->m_iTeamNum();

	switch (pEntity->GetClassId())
	{
	// World props that block projectiles
	case ETFClassIds::CBaseEntity:
	case ETFClassIds::CBaseDoor:
	case ETFClassIds::CDynamicProp:
	case ETFClassIds::CPhysicsProp:
	case ETFClassIds::CPhysicsPropMultiplayer:
	case ETFClassIds::CFunc_LOD:
	case ETFClassIds::CObjectCartDispenser:
	case ETFClassIds::CFuncTrackTrain:
	case ETFClassIds::CFuncConveyor:
		return true;
	
	// Respawn room visualizer - check team
	case ETFClassIds::CFuncRespawnRoomVisualizer:
		if (contentsMask & CONTENTS_PLAYERCLIP)
			return pEntity->m_iTeamNum() != nTeam;
		break;
	}

	return false;
}
