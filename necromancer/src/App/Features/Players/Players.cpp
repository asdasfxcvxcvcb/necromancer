#include "Players.h"

void CPlayers::Parse()
{
	// Init player data file path
	if (m_LogPath.empty())
	{
		m_LogPath = U::Storage->GetWorkFolder() / "players.json";

		if (!exists(m_LogPath))
		{
			std::ofstream file(m_LogPath, std::ios::app);

			if (!file.is_open())
			{
				return;
			}

			file.close();
		}
	}

	if (!m_Players.empty())
	{
		return;
	}

	// Open the file
	std::ifstream logFile(m_LogPath);
	if (!logFile.is_open() || logFile.peek() == std::ifstream::traits_type::eof())
	{
		return;
	}

	// Load all players with backward compatibility
	try
	{
		nlohmann::json j = nlohmann::json::parse(logFile);
		for (const auto& item : j.items())
		{
			const auto key = HASH_RT(item.key().c_str());
			auto& playerEntry = j[item.key()];

			// Use .value() with defaults for backward compatibility with old playerlists
			m_Players[key] = {
				playerEntry.value("ignored", false),
				playerEntry.value("cheater", false),
				playerEntry.value("retardlegit", false),
				playerEntry.value("targeted", false),
				playerEntry.value("streamer", false),
				playerEntry.value("nigger", false)
			};
		}
	}
	catch (...)
	{
		// Failed to parse, ignore
	}
}

void CPlayers::ParseStats()
{
	// Init stats file path
	if (m_StatsPath.empty())
	{
		m_StatsPath = U::Storage->GetWorkFolder() / "player_stats.json";

		if (!exists(m_StatsPath))
		{
			std::ofstream file(m_StatsPath, std::ios::app);
			if (file.is_open())
				file.close();
			return;
		}
	}

	if (!m_PlayerStats.empty())
		return;

	// Open the file
	std::ifstream statsFile(m_StatsPath);
	if (!statsFile.is_open() || statsFile.peek() == std::ifstream::traits_type::eof())
		return;

	try
	{
		nlohmann::json j = nlohmann::json::parse(statsFile);
		for (const auto& item : j.items())
		{
			uint64_t steamID64 = std::stoull(item.key());
			auto& entry = j[item.key()];

			m_PlayerStats[steamID64] = {
				entry.value("encounters", 0),
				entry.value("kills", 0),
				entry.value("deaths", 0),
				entry.value("last_seen", 0LL),
				entry.value("most_recent", 0LL),
				entry.value("first_seen", 0LL)
			};
		}
	}
	catch (...) {}
}

void CPlayers::SaveStats()
{
	if (m_StatsPath.empty())
		m_StatsPath = U::Storage->GetWorkFolder() / "player_stats.json";

	std::ofstream outFile(m_StatsPath);
	if (!outFile.is_open())
		return;

	nlohmann::json j;
	for (const auto& [steamID64, stats] : m_PlayerStats)
	{
		std::string key = std::to_string(steamID64);
		j[key]["encounters"] = stats.Encounters;
		j[key]["kills"] = stats.Kills;
		j[key]["deaths"] = stats.Deaths;
		j[key]["last_seen"] = stats.LastSeen;
		j[key]["most_recent"] = stats.MostRecent;
		j[key]["first_seen"] = stats.FirstSeen;
	}

	outFile << std::setw(4) << j;
	outFile.close();
}

void CPlayers::RecordEncounter(uint64_t steamID64)
{
	ParseStats(); // Ensure stats are loaded
	
	// Check if we've already seen this player this session
	if (m_CurrentSessionPlayers.count(steamID64) > 0)
		return; // Already recorded this session
	
	m_CurrentSessionPlayers.insert(steamID64);
	
	auto& stats = m_PlayerStats[steamID64];
	
	// Get current time
	int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	
	// If this is the first encounter ever, set FirstSeen
	if (stats.FirstSeen == 0)
		stats.FirstSeen = now;
	
	stats.Encounters++;
	
	// Move MostRecent to LastSeen (the previous encounter becomes "last seen")
	// Then set MostRecent to now
	stats.LastSeen = stats.MostRecent;
	stats.MostRecent = now;
	
	SaveStats();
}

void CPlayers::RecordKill(uint64_t steamID64)
{
	ParseStats(); // Ensure stats are loaded
	
	auto& stats = m_PlayerStats[steamID64];
	stats.Kills++;
	// Don't update LastSeen on kills - only on encounters
	
	SaveStats();
}

void CPlayers::RecordDeath(uint64_t steamID64)
{
	ParseStats(); // Ensure stats are loaded
	
	auto& stats = m_PlayerStats[steamID64];
	stats.Deaths++;
	// Don't update LastSeen on deaths - only on encounters
	
	SaveStats();
}

bool CPlayers::GetStats(uint64_t steamID64, PlayerStats& out)
{
	ParseStats(); // Ensure stats are loaded
	
	auto it = m_PlayerStats.find(steamID64);
	if (it != m_PlayerStats.end())
	{
		out = it->second;
		return true;
	}
	return false;
}

void CPlayers::ClearCurrentSession()
{
	m_CurrentSessionPlayers.clear();
}

void CPlayers::Mark(int entindex, const PlayerPriority& info)
{
	if (entindex == I::EngineClient->GetLocalPlayer())
	{
		return;
	}

	player_info_t playerInfo{};
	if (!I::EngineClient->GetPlayerInfo(entindex, &playerInfo) || playerInfo.fakeplayer)
	{
		return;
	}

	auto steamID = HASH_RT(std::string_view(playerInfo.guid).data());
	m_Players[steamID] = info;

	// Load the current playerlist
	nlohmann::json j{};
	std::ifstream readFile(m_LogPath);
	if (readFile.is_open() && readFile.peek() != std::ifstream::traits_type::eof())
	{
		readFile >> j;
	}

	readFile.close();

	// Open the output file
	std::ofstream outFile(m_LogPath);
	if (!outFile.is_open())
	{
		return;
	}

	auto& playerEntry = j[playerInfo.guid];
	playerEntry["ignored"] = info.Ignored;
	playerEntry["cheater"] = info.Cheater;
	playerEntry["retardlegit"] = info.RetardLegit;
	playerEntry["targeted"] = info.Targeted;
	playerEntry["streamer"] = info.Streamer;
	playerEntry["nigger"] = info.Nigger;

	if (!info.Ignored && !info.Cheater && !info.RetardLegit && !info.Targeted && !info.Streamer && !info.Nigger)
	{
		j.erase(std::string(playerInfo.guid));
	}

	outFile << std::setw(4) << j;
	outFile.close();
}

bool CPlayers::GetInfo(int entindex, PlayerPriority& out)
{
	if (entindex == I::EngineClient->GetLocalPlayer())
	{
		return false;
	}

	player_info_t playerInfo{};
	if (!I::EngineClient->GetPlayerInfo(entindex, &playerInfo) || playerInfo.fakeplayer)
	{
		return false;
	}

	return GetInfoGUID(playerInfo.guid, out);
}

bool CPlayers::GetInfoGUID(const std::string& guid, PlayerPriority& out)
{
	const auto steamID = HASH_RT(guid.c_str());

	if (auto it = m_Players.find(steamID); it != std::end(m_Players))
	{
		const auto& [key, value]{ *it };
		out = value;
		return true;
	}

	return false;
}

void CPlayers::ImportLegacyPlayers()
{
	// Check if legacy seonwdde players.json exists
	const auto legacyPath = std::filesystem::current_path() / "SEOwnedDE" / "players.json";
	
	if (!std::filesystem::exists(legacyPath))
		return;

	// Check if we already have players (don't overwrite)
	if (!m_Players.empty())
		return;

	// Load legacy players
	std::ifstream legacyFile(legacyPath);
	if (!legacyFile.is_open() || legacyFile.peek() == std::ifstream::traits_type::eof())
		return;

	try
	{
		nlohmann::json legacyJson = nlohmann::json::parse(legacyFile);
		legacyFile.close();

		// Import each player with backward compatibility
		for (const auto& item : legacyJson.items())
		{
			const auto key = HASH_RT(item.key().c_str());
			auto& playerEntry = legacyJson[item.key()];

			// Use .value() with defaults for backward compatibility
			m_Players[key] = {
				playerEntry.value("ignored", false),
				playerEntry.value("cheater", false),
				playerEntry.value("retardlegit", false),
				playerEntry.value("targeted", false),
				playerEntry.value("streamer", false)
			};
		}

		// Save to new location
		std::ofstream outFile(m_LogPath);
		if (outFile.is_open())
		{
			outFile << std::setw(4) << legacyJson;
			outFile.close();
		}
	}
	catch (...)
	{
		// Failed to parse legacy file, ignore
	}
}
