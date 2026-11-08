#include "Entities.h"
#include "../../TF2/icliententitylist.h"
#include "../../TF2/ivmodelinfo.h"
#include "../../TF2/c_baseentity.h"
#include "../../TF2/CTFPartyClient.h"
#include "../../TF2/CTFGCClientSystem.h"
#include "../../TF2/c_tf_playerresource.h"
#include "../../../App/Features/CFG.h"
#include <algorithm>
#include <set>
#include <vector>

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
	auto it = m_mapPlayerInfo.find(nPlayerIndex);
	if (it != m_mapPlayerInfo.end())
		return it->second.bIsF2P;
	return false;
}

int CEntityHelper::GetPartyIndex(int nPlayerIndex)
{
	auto it = m_mapPlayerInfo.find(nPlayerIndex);
	if (it != m_mapPlayerInfo.end())
		return it->second.nPartyIndex;
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
	// Safety check - don't access during early init
	if (!I::EngineClient || !I::GlobalVars)
		return;

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
			if (m_mapPlayerInfo.find(n) == m_mapPlayerInfo.end())
			{
				// Re-add to cache for this slot
				PlayerInfoCache cache;
				auto itF2P = mapAccountToF2P.find(uAccountID);
				cache.bIsF2P = (itF2P != mapAccountToF2P.end()) ? itF2P->second : false;
				auto itParty = mapAccountToPartyIndex.find(uAccountID);
				cache.nPartyIndex = (itParty != mapAccountToPartyIndex.end()) ? itParty->second : 0;
				m_mapPlayerInfo[n] = cache;
			}
			continue;
		}

		// Mark as checked
		g_setCheckedPlayersForInfo.insert(uAccountID);

		// Store player info
		PlayerInfoCache cache;
		auto itF2P = mapAccountToF2P.find(uAccountID);
		cache.bIsF2P = (itF2P != mapAccountToF2P.end()) ? itF2P->second : false;
		auto itParty = mapAccountToPartyIndex.find(uAccountID);
		cache.nPartyIndex = (itParty != mapAccountToPartyIndex.end()) ? itParty->second : 0;
		m_mapPlayerInfo[n] = cache;
	}
}

static bool IsWeaponNetworkClass(int nRawClassId, ClientClass* pClientClass)
{
	static unsigned char s_ucClassCache[512] = {};

	if (nRawClassId <= 0 || nRawClassId >= 512)
		return false;

	unsigned char& ucState = s_ucClassCache[nRawClassId];
	if (ucState)
		return ucState == 1;

	ucState = 2;

	if (pClientClass && pClientClass->m_pRecvTable)
	{
		std::vector<RecvTable*> vecStack;
		vecStack.push_back(pClientClass->m_pRecvTable);

		while (!vecStack.empty())
		{
			RecvTable* pTable = vecStack.back();
			vecStack.pop_back();

			if (!pTable || !pTable->m_pNetTableName)
				continue;

			if (!_stricmp(pTable->m_pNetTableName, "DT_TFWeaponBase"))
			{
				ucState = 1;
				break;
			}

			for (int n = 0; n < pTable->m_nProps; n++)
			{
				const RecvProp* pProp = pTable->GetProp(n);
				if (pProp && pProp->GetType() == DPT_DataTable && pProp->GetDataTable())
					vecStack.push_back(pProp->GetDataTable());
			}
		}
	}

	return ucState == 1;
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
		for (int i = 0; i < static_cast<int>(EEntGroup::COUNT); i++)
			m_mapGroups[i].reserve(64);
		
		bFirstRun = false;
	}

	// Pre-compute class-aware minimal entity flags
	const int nLocalClass = pLocal->m_iClass();
	const auto pLocalWeaponEnt = pLocal->m_hActiveWeapon().Get();
	const auto pLocalWeapon = pLocalWeaponEnt ? pLocalWeaponEnt->As<C_TFWeaponBase>() : nullptr;
	const int nLocalWeaponID = pLocalWeapon ? pLocalWeapon->GetWeaponID() : 0;
	const int nLocalWeaponDefIndex = pLocalWeapon ? pLocalWeapon->m_iItemDefinitionIndex() : 0;

	// Pyro with flamethrower (not Phlogistinator) needs projectiles for airblast
	const bool bPyroCanAirblast = CFG::Perf_Minimal_Entities
		&& nLocalClass == TF_CLASS_PYRO
		&& (nLocalWeaponID == TF_WEAPON_FLAMETHROWER || nLocalWeaponID == TF_WEAPON_FLAME_BALL)
		&& nLocalWeaponDefIndex != Pyro_m_ThePhlogistinator;

	// Demoman always cares about stickies (own for DT, enemy for awareness)
	const bool bDemoCareStickies = CFG::Perf_Minimal_Entities
		&& nLocalClass == TF_CLASS_DEMOMAN;

	// Engineer with melee needs to see teammate buildings and own buildings
	const bool bEngieMeleeCareBuildings = CFG::Perf_Minimal_Entities
		&& nLocalClass == TF_CLASS_ENGINEER
		&& pLocalWeapon && pLocalWeapon->GetSlot() == 2; // WEAPON_SLOT_MELEE

	// Soldier with the Disciplinary Action whips teammates for the speed buff
	const bool bSoldierWhipCareTeammates = CFG::Perf_Minimal_Entities
		&& nLocalClass == TF_CLASS_SOLDIER
		&& nLocalWeaponDefIndex == Soldier_t_TheDisciplinaryAction;

	// Pre-fetch client entity list pointer to avoid repeated virtual calls
	IClientEntityList* pEntityList = I::ClientEntityList;

	for (int n = 1; n < nHighestIndex; n++)
	{
		IClientEntity* pClientEntity = pEntityList->GetClientEntity(n);
		if (!pClientEntity || pClientEntity->IsDormant())
			continue;

		// Safety: Get client class first and validate it exists
		// This prevents crashes on custom/unknown entities from community servers
		auto pNetworkable = pClientEntity->GetClientNetworkable();
		if (!pNetworkable)
			continue;

		auto pClientClass = pNetworkable->GetClientClass();
		if (!pClientClass)
			continue;

		const auto pEntity = pClientEntity->As<C_BaseEntity>();
		const int nRawClassId = pClientClass->m_ClassID;
		
		// Skip invalid class IDs (0 or negative, or way too high for TF2)
		// TF2 has ~360 class IDs, anything above 500 is definitely invalid
		if (nRawClassId <= 0 || nRawClassId > 500)
			continue;

		const auto nClassId = static_cast<ETFClassIds>(nRawClassId);

		switch (nClassId)
		{
		case ETFClassIds::CTFPlayer:
		{
			const auto pPlayer = pEntity->As<C_TFPlayer>();

			int nPlayerTeam = 0;
			if (!pEntity->IsInValidTeam(&nPlayerTeam))
				continue;

			// PERF: Skip teammate players — only care about enemies in minimal mode
			if (CFG::Perf_Minimal_Entities && nLocalTeam == nPlayerTeam && !bSoldierWhipCareTeammates)
				break;

			if (pPlayer->deadflag() && pPlayer->m_iObserverMode() != OBS_MODE_NONE)
				m_mapGroups[static_cast<int>(EEntGroup::PLAYERS_OBSERVER)].push_back(pEntity);

			m_mapGroups[static_cast<int>(EEntGroup::PLAYERS_ALL)].push_back(pEntity);
			m_mapGroups[static_cast<int>(nLocalTeam != nPlayerTeam ? EEntGroup::PLAYERS_ENEMIES : EEntGroup::PLAYERS_TEAMMATES)].push_back(pEntity);
			break;
		}

		case ETFClassIds::CObjectDispenser:
		case ETFClassIds::CObjectTeleporter:
		{
			// PERF: Skip dispensers and teleporters in minimal mode
			// Exception: Engineer holding melee needs to see teammate/own buildings
			if (CFG::Perf_Minimal_Entities && !bEngieMeleeCareBuildings)
				break;
			[[fallthrough]]; // Fall through to sentry handling when not skipped
		}
		case ETFClassIds::CObjectSentrygun:
		{
			// EXTREME: Skip ALL building caching
			if (CFG::Perf_Extreme_Limit_Entity_Cache)
				break;

			int nObjectTeam = 0;
			if (!pEntity->IsInValidTeam(&nObjectTeam))
				continue;

			// PERF: Skip teammate buildings in minimal mode
			// Exception: Engineer holding melee needs teammate/own buildings
			if (CFG::Perf_Minimal_Entities && !bEngieMeleeCareBuildings && nLocalTeam == nObjectTeam)
				break;

			m_mapGroups[static_cast<int>(EEntGroup::BUILDINGS_ALL)].push_back(pEntity);
			m_mapGroups[static_cast<int>(nLocalTeam != nObjectTeam ? EEntGroup::BUILDINGS_ENEMIES : EEntGroup::BUILDINGS_TEAMMATES)].push_back(pEntity);
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
			// EXTREME: Skip ALL projectile caching
			if (CFG::Perf_Extreme_Limit_Entity_Cache)
				break;

			int nProjectileTeam = 0;
			if (!pEntity->IsInValidTeam(&nProjectileTeam))
				continue;

			// PERF: In minimal mode, class-aware projectile filtering:
			// - Pyro with flamethrower (not Phlogistinator): cache all projectiles (for airblast)
			// - Demoman: cache all stickies (own for DT, enemy for awareness)
			// - Otherwise: only cache local player's stickies (for sticky DT)
			if (CFG::Perf_Minimal_Entities)
			{
				if (bPyroCanAirblast)
				{
					// Pyro needs all projectiles for airblast — cache them
				}
				else if (bDemoCareStickies && nClassId == ETFClassIds::CTFGrenadePipebombProjectile)
				{
					// Demoman cares about all stickies — cache them
				}
				else
				{
					// Default minimal: only local player's own stickies
					if (nClassId == ETFClassIds::CTFGrenadePipebombProjectile)
					{
						const auto pPipebomb = pEntity->As<C_TFGrenadePipebombProjectile>();
						if (pPipebomb->HasStickyEffects() && pPipebomb->As<C_BaseGrenade>()->m_hThrower().Get() == pLocal)
							m_mapGroups[static_cast<int>(EEntGroup::PROJECTILES_LOCAL_STICKIES)].push_back(pEntity);
					}
					break;
				}
			}

			if (nClassId == ETFClassIds::CTFGrenadePipebombProjectile)
			{
				const auto pPipebomb = pEntity->As<C_TFGrenadePipebombProjectile>();
				if (pPipebomb->HasStickyEffects() && pPipebomb->As<C_BaseGrenade>()->m_hThrower().Get() == pLocal)
					m_mapGroups[static_cast<int>(EEntGroup::PROJECTILES_LOCAL_STICKIES)].push_back(pEntity);
			}

			m_mapGroups[static_cast<int>(EEntGroup::PROJECTILES_ALL)].push_back(pEntity);
			m_mapGroups[static_cast<int>(nLocalTeam != nProjectileTeam ? EEntGroup::PROJECTILES_ENEMIES : EEntGroup::PROJECTILES_TEAMMATES)].push_back(pEntity);
			break;
		}

		case ETFClassIds::CBaseAnimating:
		{
			// EXTREME: Skip pickup caching
			if (CFG::Perf_Extreme_Limit_Entity_Cache)
				break;

			if (IsHealthPack(pEntity))
				m_mapGroups[static_cast<int>(EEntGroup::HEALTHPACKS)].push_back(pEntity);
			else if (IsAmmoPack(pEntity))
			{
				// PERF: Skip ammo packs in minimal mode
				if (!CFG::Perf_Minimal_Entities)
					m_mapGroups[static_cast<int>(EEntGroup::AMMOPACKS)].push_back(pEntity);
			}
			break;
		}

		case ETFClassIds::CTFAmmoPack:
		{
			if (CFG::Perf_Extreme_Limit_Entity_Cache)
				break;
			// PERF: Skip ammo packs in minimal mode
			if (CFG::Perf_Minimal_Entities)
				break;
			m_mapGroups[static_cast<int>(EEntGroup::AMMOPACKS)].push_back(pEntity);
			break;
		}

		case ETFClassIds::CHalloweenGiftPickup:
		{
			if (CFG::Perf_Extreme_Limit_Entity_Cache)
				break;
			m_mapGroups[static_cast<int>(EEntGroup::HALLOWEEN_GIFT)].push_back(pEntity);
			break;
		}

		case ETFClassIds::CCurrencyPack:
		{
			if (CFG::Perf_Extreme_Limit_Entity_Cache)
				break;
			if (!pEntity->As<C_CurrencyPack>()->m_bDistributed())
				m_mapGroups[static_cast<int>(EEntGroup::MVM_MONEY)].push_back(pEntity);
			break;
		}

		default:
		{
			if (!CFG::Perf_Extreme_Limit_Entity_Cache
				&& IsWeaponNetworkClass(nRawClassId, pClientClass)
				&& !pEntity->m_hOwnerEntity().Get())
				m_mapGroups[static_cast<int>(EEntGroup::WEAPONS_DROPPED)].push_back(pEntity);
			break;
		}
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
	for (int i = 0; i < static_cast<int>(EEntGroup::COUNT); i++)
		m_mapGroups[i].clear();
}

void CEntityHelper::ValidateGroups()
{
	// UpdateCache rebuilds the groups in FrameStageNotify, but entity deletion is
	// processed after that and before the following CreateMove ticks - on those
	// ticks a group entry can point at a freed entity, and the first field read
	// through it (e.g. InCond checking m_nPlayerCondEx2) faults. Prune every
	// group against the pointers the entity list currently returns. Call this
	// once per CreateMove before any feature consumes a group.
	static std::vector<const void*> vLive;
	vLive.clear();

	const int nHighestIndex = I::ClientEntityList->GetHighestEntityIndex();
	vLive.reserve(static_cast<size_t>(nHighestIndex) + 1);
	for (int n = 0; n <= nHighestIndex; n++)
	{
		if (const auto pEntity = I::ClientEntityList->GetClientEntity(n))
			vLive.push_back(pEntity);
	}

	if (vLive.empty())
	{
		ClearCache();
		return;
	}

	std::sort(vLive.begin(), vLive.end());

	for (int i = 0; i < static_cast<int>(EEntGroup::COUNT); i++)
	{
		auto& vGroup = m_mapGroups[i];
		if (vGroup.empty())
			continue;

		vGroup.erase(std::remove_if(vGroup.begin(), vGroup.end(),
			[](C_BaseEntity* pEntity)
			{
				return !std::binary_search(vLive.begin(), vLive.end(), pEntity);
			}), vGroup.end());
	}
}
