#include "Entities.h"
#include "../../TF2/icliententitylist.h"
#include "../../TF2/ivmodelinfo.h"
#include "../../TF2/c_baseentity.h"
#include "../../TF2/CTFPartyClient.h"
#include "../../TF2/CTFGCClientSystem.h"
#include "../../TF2/c_tf_playerresource.h"
#include <set>

C_TFPlayer* CEntityHelper::GetLocal()
{
	if (const auto pEntity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer()))
		return pEntity->As<C_TFPlayer>();

	return nullptr;
}

C_TFWeaponBase* CEntityHelper::GetWeapon()
{
	if (const auto pLocal = GetLocal())
	{
		if (const auto pEntity = pLocal->m_hActiveWeapon().Get())
			return pEntity->As<C_TFWeaponBase>();
	}

	return nullptr;
}

bool CEntityHelper::IsF2P(int nPlayerIndex)
{
	if (m_mapPlayerInfo.contains(nPlayerIndex))
		return m_mapPlayerInfo[nPlayerIndex].bIsF2P;
	return false;
}

int CEntityHelper::GetPartyIndex(int nPlayerIndex)
{
	if (m_mapPlayerInfo.contains(nPlayerIndex))
		return m_mapPlayerInfo[nPlayerIndex].nPartyIndex;
	return 0;
}

int CEntityHelper::GetPartyCount()
{
	return m_nNextPartyIndex - 1;
}

void CEntityHelper::ClearPlayerInfoCache()
{
	m_mapPlayerInfo.clear();
	m_mapPartyToIndex.clear();
	m_nNextPartyIndex = 1;
}

void CEntityHelper::ForceRefreshPlayerInfo()
{
	// Clear cache and reset the update timer to force immediate refresh
	ClearPlayerInfoCache();
}

// Static tracking for connection state (like sourcebans g_bWasConnected)
static bool g_bWasConnectedForPlayerInfo = false;
static std::set<uint32_t> g_setCheckedPlayersForInfo; // Track which players we've already fetched info for

// Update F2P and party info from GC system (called every frame, only checks new players)
void CEntityHelper::UpdatePlayerInfoFromGC()
{
	if (!I::EngineClient->IsConnected())
	{
		// Reset when disconnected so we check again on next connect
		if (g_bWasConnectedForPlayerInfo)
		{
			g_bWasConnectedForPlayerInfo = false;
			g_setCheckedPlayersForInfo.clear();
			ClearPlayerInfoCache();
		}
		return;
	}

	g_bWasConnectedForPlayerInfo = true;

	// Throttle GC updates to every 0.5 seconds (reduces CPU overhead significantly)
	static float flLastGCUpdate = 0.0f;
	const float flCurrentTime = I::GlobalVars->realtime;
	if (flCurrentTime - flLastGCUpdate < 0.5f)
		return;
	flLastGCUpdate = flCurrentTime;

	// Check if GC system is available
	if (!I::TFGCClientSystem)
		return;

	// Collect GC data for all lobby members (this is cheap, just reading cached data)
	std::unordered_map<uint32_t, uint64_t> mapAccountToParty;  // AccountID -> PartyID
	std::unordered_map<uint32_t, bool> mapAccountToF2P;        // AccountID -> IsF2P

	// Get lobby data (contains F2P status and party info for all players in match)
	if (auto pLobby = I::TFGCClientSystem->GetLobby())
	{
		int nMembers = pLobby->GetNumMembers();
		for (int i = 0; i < nMembers; i++)
		{
			CSteamID steamID;
			pLobby->GetMember(&steamID, i);
			uint32_t uAccountID = steamID.GetAccountID();
			if (!uAccountID)
				continue;

			ConstTFLobbyPlayer details;
			pLobby->GetMemberDetails(&details, i);
			
			if (auto pProto = details.Proto())
			{
				// F2P status: chat_suspension = true means F2P account
				mapAccountToF2P[uAccountID] = pProto->chat_suspension;
				
				// Party ID: players with same original_party_id are in same party
				mapAccountToParty[uAccountID] = pProto->original_party_id;
			}
		}
	}

	// Also check local party (players in your party)
	if (auto pParty = I::TFGCClientSystem->GetParty())
	{
		int64_t nMembers = pParty->GetNumMembers();
		for (int i = 0; i < nMembers; i++)
		{
			CSteamID steamID;
			pParty->GetMember(&steamID, i);
			uint32_t uAccountID = steamID.GetAccountID();
			if (uAccountID)
			{
				// Mark as party 1 (local party)
				mapAccountToParty[uAccountID] = 1;
			}
		}
	}

	// Group players by party ID and filter out solo players
	std::map<uint64_t, std::vector<uint32_t>> mapPartyMembers;
	for (auto& [uAccountID, uPartyID] : mapAccountToParty)
	{
		if (uPartyID != 0)
			mapPartyMembers[uPartyID].push_back(uAccountID);
	}

	// Remove parties with only 1 member (not really a party)
	for (auto it = mapPartyMembers.begin(); it != mapPartyMembers.end();)
	{
		if (it->second.size() <= 1)
			it = mapPartyMembers.erase(it);
		else
			++it;
	}

	// Assign party indices (1-12)
	std::unordered_map<uint32_t, int> mapAccountToPartyIndex;
	int nPartyIndex = 1;
	for (auto& [uPartyID, vMembers] : mapPartyMembers)
	{
		if (nPartyIndex > MAX_PARTY_COLORS)
			break;
		
		for (uint32_t uAccountID : vMembers)
		{
			mapAccountToPartyIndex[uAccountID] = nPartyIndex;
		}
		nPartyIndex++;
	}
	m_nNextPartyIndex = nPartyIndex;

	// Now check each player - only process new players (like sourcebans logic)
	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		if (n == I::EngineClient->GetLocalPlayer())
			continue;

		player_info_t info;
		if (!I::EngineClient->GetPlayerInfo(n, &info))
			continue;
		
		if (info.fakeplayer)
			continue;

		if (info.friendsID == 0)
			continue;

		uint32_t uAccountID = static_cast<uint32_t>(info.friendsID);

		// Skip if already checked this session (like sourcebans g_setCheckedPlayers)
		if (g_setCheckedPlayersForInfo.count(uAccountID) > 0)
		{
			// Player already checked, but update their slot mapping in case they changed slots
			if (!m_mapPlayerInfo.contains(n))
			{
				// Find their cached info by account ID and copy to new slot
				for (auto& [slot, cache] : m_mapPlayerInfo)
				{
					// We need to store account ID in cache to do this properly
					// For now, just re-fetch if slot changed
				}
				
				// Re-add to cache for this slot
				PlayerInfoCache cache;
				cache.bIsF2P = mapAccountToF2P.contains(uAccountID) ? mapAccountToF2P[uAccountID] : false;
				cache.nPartyIndex = mapAccountToPartyIndex.contains(uAccountID) ? mapAccountToPartyIndex[uAccountID] : 0;
				m_mapPlayerInfo[n] = cache;
			}
			continue;
		}

		// Mark as checked
		g_setCheckedPlayersForInfo.insert(uAccountID);

		// Store player info
		PlayerInfoCache cache;
		cache.bIsF2P = mapAccountToF2P.contains(uAccountID) ? mapAccountToF2P[uAccountID] : false;
		cache.nPartyIndex = mapAccountToPartyIndex.contains(uAccountID) ? mapAccountToPartyIndex[uAccountID] : 0;
		m_mapPlayerInfo[n] = cache;
	}
}

void CEntityHelper::UpdateCache()
{
	const auto pLocal = GetLocal();
	if (!pLocal)
		return;

	int nLocalTeam = 0;
	if (!pLocal->IsInValidTeam(&nLocalTeam))
		return;

	// Update F2P and party info from GC system
	UpdatePlayerInfoFromGC();

	// Cache this expensive call once
	const int nHighestIndex = I::ClientEntityList->GetHighestEntityIndex();
	
	// Reserve capacity to avoid reallocation (only on first run)
	static bool bFirstRun = true;
	if (bFirstRun)
	{
		for (auto& group : m_mapGroups | std::views::values)
			group.reserve(64);
		bFirstRun = false;
	}

	// Pre-fetch client entity list pointer to avoid repeated virtual calls
	IClientEntityList* pEntityList = I::ClientEntityList;

	for (int n = 1; n < nHighestIndex; n++)
	{
		IClientEntity* pClientEntity = pEntityList->GetClientEntity(n);
		if (!pClientEntity || pClientEntity->IsDormant())
			continue;

		const auto pEntity = pClientEntity->As<C_BaseEntity>();
		const auto nClassId = pEntity->GetClassId();

		switch (nClassId)
		{
		case ETFClassIds::CTFPlayer:
		{
			const auto pPlayer = pEntity->As<C_TFPlayer>();
			if (pPlayer->deadflag() && pPlayer->m_iObserverMode() != OBS_MODE_NONE)
				m_mapGroups[EEntGroup::PLAYERS_OBSERVER].push_back(pEntity);

			int nPlayerTeam = 0;
			if (!pEntity->IsInValidTeam(&nPlayerTeam))
				continue;

			m_mapGroups[EEntGroup::PLAYERS_ALL].push_back(pEntity);
			m_mapGroups[nLocalTeam != nPlayerTeam ? EEntGroup::PLAYERS_ENEMIES : EEntGroup::PLAYERS_TEAMMATES].push_back(pEntity);
			break;
		}

		case ETFClassIds::CObjectSentrygun:
		case ETFClassIds::CObjectDispenser:
		case ETFClassIds::CObjectTeleporter:
		{
			int nObjectTeam = 0;
			if (!pEntity->IsInValidTeam(&nObjectTeam))
				continue;

			m_mapGroups[EEntGroup::BUILDINGS_ALL].push_back(pEntity);
			m_mapGroups[nLocalTeam != nObjectTeam ? EEntGroup::BUILDINGS_ENEMIES : EEntGroup::BUILDINGS_TEAMMATES].push_back(pEntity);
			break;
		}

		case ETFClassIds::CTFProjectile_Rocket:
		case ETFClassIds::CTFProjectile_SentryRocket:
		case ETFClassIds::CTFProjectile_Jar:
		case ETFClassIds::CTFProjectile_JarGas:
		case ETFClassIds::CTFProjectile_JarMilk:
		case ETFClassIds::CTFProjectile_Arrow:
		case ETFClassIds::CTFProjectile_Flare:
		case ETFClassIds::CTFProjectile_Cleaver:
		case ETFClassIds::CTFProjectile_HealingBolt:
		case ETFClassIds::CTFGrenadePipebombProjectile:
		case ETFClassIds::CTFProjectile_BallOfFire:
		case ETFClassIds::CTFProjectile_EnergyRing:
		case ETFClassIds::CTFProjectile_EnergyBall:
		{
			int nProjectileTeam = 0;
			if (!pEntity->IsInValidTeam(&nProjectileTeam))
				continue;

			if (nClassId == ETFClassIds::CTFGrenadePipebombProjectile)
			{
				const auto pPipebomb = pEntity->As<C_TFGrenadePipebombProjectile>();
				if (pPipebomb->HasStickyEffects() && pPipebomb->As<C_BaseGrenade>()->m_hThrower().Get() == pLocal)
					m_mapGroups[EEntGroup::PROJECTILES_LOCAL_STICKIES].push_back(pEntity);
			}

			m_mapGroups[EEntGroup::PROJECTILES_ALL].push_back(pEntity);
			m_mapGroups[nLocalTeam != nProjectileTeam ? EEntGroup::PROJECTILES_ENEMIES : EEntGroup::PROJECTILES_TEAMMATES].push_back(pEntity);
			break;
		}

		case ETFClassIds::CBaseAnimating:
		{
			if (IsHealthPack(pEntity))
				m_mapGroups[EEntGroup::HEALTHPACKS].push_back(pEntity);
			else if (IsAmmoPack(pEntity))
				m_mapGroups[EEntGroup::AMMOPACKS].push_back(pEntity);
			break;
		}

		case ETFClassIds::CTFAmmoPack:
			m_mapGroups[EEntGroup::AMMOPACKS].push_back(pEntity);
			break;

		case ETFClassIds::CHalloweenGiftPickup:
			m_mapGroups[EEntGroup::HALLOWEEN_GIFT].push_back(pEntity);
			break;

		case ETFClassIds::CCurrencyPack:
		{
			if (!pEntity->As<C_CurrencyPack>()->m_bDistributed())
				m_mapGroups[EEntGroup::MVM_MONEY].push_back(pEntity);
			break;
		}

		default:
			break;
		}
	}
}

void CEntityHelper::UpdateModelIndexes()
{
	// Reserve space to avoid rehashing
	m_mapHealthPacks.clear();
	m_mapHealthPacks.reserve(17);
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/items/medkit_small.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/items/medkit_medium.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/items/medkit_large.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/props_halloween/halloween_medkit_small.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/props_halloween/halloween_medkit_medium.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/props_halloween/halloween_medkit_large.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/items/medkit_small_bday.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/items/medkit_medium_bday.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/items/medkit_large_bday.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/props_medieval/medieval_meat.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/items/plate.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/items/plate_sandwich_xmas.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/items/plate_robo_sandwich.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/workshop/weapons/c_models/c_fishcake/plate_fishcake.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/workshop/weapons/c_models/c_buffalo_steak/plate_buffalo_steak.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/workshop/weapons/c_models/c_chocolate/plate_chocolate.mdl")] = true;
	m_mapHealthPacks[I::ModelInfoClient->GetModelIndex("models/items/banana/plate_banana.mdl")] = true;

	m_mapAmmoPacks.clear();
	m_mapAmmoPacks.reserve(6);
	m_mapAmmoPacks[I::ModelInfoClient->GetModelIndex("models/items/ammopack_small.mdl")] = true;
	m_mapAmmoPacks[I::ModelInfoClient->GetModelIndex("models/items/ammopack_medium.mdl")] = true;
	m_mapAmmoPacks[I::ModelInfoClient->GetModelIndex("models/items/ammopack_large.mdl")] = true;
	m_mapAmmoPacks[I::ModelInfoClient->GetModelIndex("models/items/ammopack_small_bday.mdl")] = true;
	m_mapAmmoPacks[I::ModelInfoClient->GetModelIndex("models/items/ammopack_medium_bday.mdl")] = true;
	m_mapAmmoPacks[I::ModelInfoClient->GetModelIndex("models/items/ammopack_large_bday.mdl")] = true;
}

void CEntityHelper::ClearCache()
{
	for (auto& group : m_mapGroups | std::views::values)
	{
		group.clear();
	}
}
