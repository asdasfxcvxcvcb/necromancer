#pragma once

#include "../../../SDK/SDK.h"
#include <set>

struct PlayerPriority
{
	bool Ignored{};
	bool Cheater{};
	bool RetardLegit{};
};

struct PlayerStats
{
	int Encounters{};       // How many times you've seen this player
	int Kills{};            // How many times you killed this player
	int Deaths{};           // How many times this player killed you
	int64_t LastSeen{};     // Timestamp of the PREVIOUS encounter (before the most recent)
	int64_t MostRecent{};   // Timestamp of the most recent encounter (saved, becomes LastSeen next time)
	int64_t FirstSeen{};    // Timestamp of first ever encounter
};

class CPlayers
{
	struct Player
	{
		hash::hash_t SteamID = {};
		PlayerPriority Info = {};
	};

	std::unordered_map<hash::hash_t, PlayerPriority> m_Players;
	std::unordered_map<uint64_t, PlayerStats> m_PlayerStats; // Keyed by Steam64 ID
	std::set<uint64_t> m_CurrentSessionPlayers; // Players seen this session (to track LastSeen properly)
	std::filesystem::path m_LogPath;
	std::filesystem::path m_StatsPath;

public:
	void Parse();
	void ParseStats();
	void SaveStats();
	void ImportLegacyPlayers(); // Import players.json from old seonwdde folder
	void Mark(int entindex, const PlayerPriority& info);
	bool GetInfo(int entindex, PlayerPriority& out);
	bool GetInfoGUID(const std::string& guid, PlayerPriority& out);
	
	// Stats tracking
	void RecordEncounter(uint64_t steamID64);
	void RecordKill(uint64_t steamID64);
	void RecordDeath(uint64_t steamID64);
	bool GetStats(uint64_t steamID64, PlayerStats& out);
	void ClearCurrentSession(); // Call when disconnecting from server
};

MAKE_SINGLETON_SCOPED(CPlayers, Players, F);
