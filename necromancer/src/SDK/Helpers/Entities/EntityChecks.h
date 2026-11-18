#pragma once

#include "../../TF2/c_tf_player.h"
#include "../../TF2/c_baseobject.h"

inline bool IsPlayer(C_BaseEntity* pEntity)
{
	if (!pEntity)
		return false;
	return pEntity->GetClassId() == ETFClassIds::CTFPlayer;
}

inline bool IsWorld(C_BaseEntity* pEntity)
{
	if (!pEntity)
		return false;
	return pEntity->GetClassId() == ETFClassIds::CWorld;
}

inline bool IsAlive(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return false;
	return pPlayer->m_lifeState() == LIFE_ALIVE;
}

inline bool IsAGhost(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return false;
	return pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE);
}

inline bool IsOnGround(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return false;
	return (pPlayer->m_fFlags() & FL_ONGROUND) != 0;
}

inline bool IsSwimming(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return false;
	return pPlayer->m_nWaterLevel() >= 2;
}

inline bool IsDucking(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return false;
	return pPlayer->m_bDucked() || (pPlayer->m_fFlags() & FL_DUCKING) != 0;
}

inline unsigned int SolidMask(C_TFPlayer* pPlayer)
{
	return MASK_PLAYERSOLID;
}

inline bool IsSentrygun(C_BaseEntity* pEntity)
{
	if (!pEntity)
		return false;
	auto pObj = pEntity->As<C_BaseObject>();
	return pObj && pObj->m_iObjectType() == OBJ_SENTRYGUN;
}

inline bool IsDispenser(C_BaseEntity* pEntity)
{
	if (!pEntity)
		return false;
	auto pObj = pEntity->As<C_BaseObject>();
	return pObj && pObj->m_iObjectType() == OBJ_DISPENSER;
}

inline bool IsTeleporter(C_BaseEntity* pEntity)
{
	if (!pEntity)
		return false;
	auto pObj = pEntity->As<C_BaseObject>();
	return pObj && pObj->m_iObjectType() == OBJ_TELEPORTER;
}
