#pragma once

#include "../../TF2/c_tf_player.h"
#include <unordered_map>

enum class EEntGroup
{
	PLAYERS_ALL,
	PLAYERS_ENEMIES,
	PLAYERS_TEAMMATES,
	PLAYERS_OBSERVER,

	BUILDINGS_ALL,
	BUILDINGS_ENEMIES,
	BUILDINGS_TEAMMATES,

	PROJECTILES_ALL,
	PROJECTILES_ENEMIES,
	PROJECTILES_TEAMMATES,
	PROJECTILES_LOCAL_STICKIES,

	HEALTHPACKS,
	AMMOPACKS,
	HALLOWEEN_GIFT,
	MVM_MONEY
};

// Hash specialization for unordered_map
template<>
struct std::hash<EEntGroup>
{
	std::size_t operator()(const EEntGroup& e) const noexcept
	{
		return static_cast<std::size_t>(e);
	}
};

// Maximum number of parties to track (each gets a unique color)
constexpr int MAX_PARTY_COLORS = 12;

// Player info cache for F2P and party detection
struct PlayerInfoCache
{
	bool bIsF2P = false;
	int nPartyIndex = 0; // 0 = no party, 1-12 = party index
};

class CEntityHelper
{
public:
	C_TFPlayer* GetLocal();
	C_TFWeaponBase* GetWeapon();

private:
	std::unordered_map<EEntGroup, std::vector<C_BaseEntity*>> m_mapGroups = {};
	std::unordered_map<int, bool> m_mapHealthPacks = {};
	std::unordered_map<int, bool> m_mapAmmoPacks = {};

	// F2P and Party detection cache (by player index)
	std::unordered_map<int, PlayerInfoCache> m_mapPlayerInfo = {};
	// Party tracking: maps party ID to party index (1-12)
	std::unordered_map<uint64_t, int> m_mapPartyToIndex = {};
	int m_nNextPartyIndex = 1;

	bool IsHealthPack(C_BaseEntity* pEntity)
	{
		return m_mapHealthPacks.contains(pEntity->m_nModelIndex());
	}

	bool IsAmmoPack(C_BaseEntity* pEntity)
	{
		return m_mapAmmoPacks.contains(pEntity->m_nModelIndex());
	}

public:
	void UpdateCache();
	void UpdateModelIndexes();
	void ClearCache();

	void ClearModelIndexes()
	{
		m_mapHealthPacks.clear();
		m_mapAmmoPacks.clear();
	}

	const std::vector<C_BaseEntity*>& GetGroup(const EEntGroup group) { return m_mapGroups[group]; }

	// F2P and Party detection methods
	bool IsF2P(int nPlayerIndex);
	int GetPartyIndex(int nPlayerIndex); // Returns 0 if not in party, 1-12 for party index
	int GetPartyCount(); // Returns number of unique parties detected
	void ClearPlayerInfoCache();
	void UpdatePlayerInfoFromGC(); // Update F2P and party info from GC system
	void ForceRefreshPlayerInfo(); // Force immediate refresh (call on level init)
};

MAKE_SINGLETON_SCOPED(CEntityHelper, Entities, H);
