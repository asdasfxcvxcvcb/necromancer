#pragma once

#include "EntityChecks.h"
#include <deque>
#include <unordered_map>

enum class EntityEnum
{
	Invalid = -1,
	PlayerAll,
	PlayerEnemy,
	PlayerTeam,
	BuildingAll,
	BuildingEnemy,
	BuildingTeam,
	WorldProjectile,
	WorldNPC
};

class CEntityMotionExt
{
public:
	struct VelFixOriginRecord { Vec3 m_vecOrigin; float m_flSimulationTime; };

private:
	std::unordered_map<int, Vec3> m_mAvgVelocity;
	std::unordered_map<int, bool> m_mLagCompensation;
	std::unordered_map<int, std::deque<VelFixOriginRecord>> m_mOrigins;

public:
	C_TFPlayer* GetLocal() { return H::Entities ? H::Entities->GetLocal() : nullptr; }
	C_TFWeaponBase* GetWeapon() { return H::Entities ? H::Entities->GetWeapon() : nullptr; }

	void Clear()
	{
		m_mAvgVelocity.clear();
		m_mLagCompensation.clear();
		m_mOrigins.clear();
	}

	const std::vector<C_BaseEntity*>& GetGroup(EntityEnum eGroup)
	{
		static std::vector<C_BaseEntity*> empty;

		if (!H::Entities)
			return empty;

		switch (eGroup)
		{
		case EntityEnum::PlayerAll:
			return H::Entities->GetGroup(EEntGroup::PLAYERS_ALL);
		case EntityEnum::PlayerEnemy:
			return H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES);
		case EntityEnum::PlayerTeam:
			return H::Entities->GetGroup(EEntGroup::PLAYERS_TEAMMATES);
		case EntityEnum::BuildingAll:
			return H::Entities->GetGroup(EEntGroup::BUILDINGS_ALL);
		case EntityEnum::BuildingEnemy:
			return H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES);
		case EntityEnum::BuildingTeam:
			return H::Entities->GetGroup(EEntGroup::BUILDINGS_TEAMMATES);
		case EntityEnum::WorldProjectile:
			return H::Entities->GetGroup(EEntGroup::PROJECTILES_ALL);
		case EntityEnum::WorldNPC:
			return empty;
		default:
			return empty;
		}
	}

	bool IsFriend(int iIndex)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
		if (pEntity)
		{
			auto pBaseEntity = pEntity->As<C_BaseEntity>();
			if (pBaseEntity && IsPlayer(pBaseEntity))
				return static_cast<C_TFPlayer*>(pBaseEntity)->IsPlayerOnSteamFriendsList();
		}
		return false;
	}

	Vec3* GetAvgVelocity(int iIndex)
	{
		if (!I::EngineClient || iIndex == I::EngineClient->GetLocalPlayer())
			return nullptr;

		if (m_mAvgVelocity.contains(iIndex))
			return &m_mAvgVelocity[iIndex];

		return nullptr;
	}

	void SetAvgVelocity(int iIndex, const Vec3& vVel)
	{
		m_mAvgVelocity[iIndex] = vVel;
	}

	bool GetLagCompensation(int iIndex)
	{
		return m_mLagCompensation.contains(iIndex) ? m_mLagCompensation[iIndex] : false;
	}

	void SetLagCompensation(int iIndex, bool bState)
	{
		m_mLagCompensation[iIndex] = bState;
	}

	Vec3 GetEyeAngles(int iIndex)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
		if (pEntity)
		{
			auto pBaseEntity = pEntity->As<C_BaseEntity>();
			if (pBaseEntity && IsPlayer(pBaseEntity))
				return static_cast<C_TFPlayer*>(pBaseEntity)->GetEyeAngles();
		}
		return {};
	}

	std::deque<VelFixOriginRecord>* GetOrigins(int iIndex)
	{
		if (!m_mOrigins.contains(iIndex))
			return nullptr;
		return &m_mOrigins[iIndex];
	}

	void PushOrigin(int iIndex, const Vec3& vOrigin, float flSimTime, int iMaxCount = 20)
	{
		auto& deq = m_mOrigins[iIndex];
		deq.emplace_front(VelFixOriginRecord{ vOrigin, flSimTime });
		while (deq.size() > static_cast<size_t>(iMaxCount))
			deq.pop_back();
	}

	void ClearOrigins(int iIndex)
	{
		m_mOrigins.erase(iIndex);
	}

	void ClearPlayer(int iIndex)
	{
		m_mAvgVelocity.erase(iIndex);
		m_mLagCompensation.erase(iIndex);
		m_mOrigins.erase(iIndex);
	}
};

inline CEntityMotionExt g_EntityMotionExt;
