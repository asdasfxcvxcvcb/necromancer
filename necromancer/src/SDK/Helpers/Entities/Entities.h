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
	MVM_MONEY,
	WEAPONS_DROPPED,

	COUNT // Must be last - used for array sizing
};

// Hash specialization for unordered_map (kept for backward compat, but m_mapGroups is now a flat array)
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

	// Validate a cached entity pointer is still alive and matches the entity list
	// This prevents dangling pointer access after entity destruction (class change, death, level transition)
	template<typename T>
	static bool IsEntityValid(T* pEntity)
	{
		if (!pEntity || !I::ClientEntityList || !I::EngineClient)
			return false;

		const int nIndex = pEntity->entindex();
		if (nIndex <= 0)
			return false;

		const auto pCheck = I::ClientEntityList->GetClientEntity(nIndex);
		return pCheck == pEntity;
	}

	// Safe validation that doesn't dereference the cached pointer.
	// Use when the pointer might be dangling (freed memory) — calling entindex()
	// on a dangling pointer can crash. This version uses a known index instead.
	template<typename T>
	static bool SafeIsEntityValid(T* pEntity, int nKnownIndex)
	{
		if (!pEntity || !I::ClientEntityList || nKnownIndex <= 0)
			return false;

		const auto pCheck = I::ClientEntityList->GetClientEntity(nKnownIndex);
		return pCheck == pEntity;
	}

private:
	// Flat array indexed by EEntGroup - O(1) lookup, no hash overhead
	// Replaces unordered_map<EEntGroup, vector<C_BaseEntity*>>
	std::vector<C_BaseEntity*> m_mapGroups[static_cast<int>(EEntGroup::COUNT)] = {};
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
	void ValidateGroups(); // Prune stale (freed) entities from all groups - call each CreateMove before features run

	void ClearModelIndexes()
	{
		m_mapHealthPacks.clear();
		m_mapAmmoPacks.clear();
	}

	const std::vector<C_BaseEntity*>& GetGroup(const EEntGroup group) { return m_mapGroups[static_cast<int>(group)]; }

	// F2P and Party detection methods
	bool IsF2P(int nPlayerIndex);
	int GetPartyIndex(int nPlayerIndex); // Returns 0 if not in party, 1-12 for party index
	int GetPartyCount(); // Returns number of unique parties detected
	void ClearPlayerInfoCache();
	void UpdatePlayerInfoFromGC(); // Update F2P and party info from GC system
	void ForceRefreshPlayerInfo(); // Force immediate refresh (call on level init)
};

MAKE_SINGLETON_SCOPED(CEntityHelper, Entities, H);
