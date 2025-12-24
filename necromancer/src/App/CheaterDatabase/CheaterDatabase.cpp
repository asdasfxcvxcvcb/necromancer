#include "CheaterDatabase.h"
#include <algorithm>
#include <cctype>
#include <sstream>

static std::map<uint64_t, SourcebanInfo_t> g_mapSourcebans;
static std::mutex g_mtxSourcebans;
static std::set<uint64_t> g_setCheckedPlayers; // Track which players we've already queued for checking
static bool g_bWasConnected = false; // Track connection state to detect new game joins

// Verobay database (Tom's reported_ids.txt)
static std::set<uint64_t> g_setVerobayDatabase;
static std::map<uint64_t, VerobayInfo_t> g_mapVerobayInfo; // Per-player info for UI
static std::mutex g_mtxVerobay;
static bool g_bVerobayFetched = false;
static bool g_bVerobayFetching = false;

// Pending chat alerts to be processed on main thread
static std::vector<PendingBanAlert_t> g_vecPendingBanAlerts;
static std::mutex g_mtxPendingAlerts;

// Cheat-related keywords to filter ban alerts
static const std::vector<std::string> g_vecCheatKeywords = {
	"hacking", "account", "ip", "cheats", "cheat", "cheating", "convar", "hack", "hacks",
	"aimbot", "triggerbot", "bunnyhop", "dupe", "duplicate", "smac", "silent", "smooth",
	"psilent", "bot", "spinning", "alt", "anti", "anti-cheat", "violation", "anticheat",
	"exploit", "crit", "angle", "groupban"
};

// Helper function to check if a ban reason contains any cheat-related keyword (case-insensitive)
static bool ContainsCheatKeyword(const std::string& banReason)
{
	// Convert ban reason to lowercase for case-insensitive comparison
	std::string lowerReason = banReason;
	std::transform(lowerReason.begin(), lowerReason.end(), lowerReason.begin(),
		[](unsigned char c) { return std::tolower(c); });

	for (const auto& keyword : g_vecCheatKeywords)
	{
		if (lowerReason.find(keyword) != std::string::npos)
			return true;
	}
	return false;
}

// Helper function to count bans with cheat-related keywords
static int CountCheatRelatedBans(const std::vector<std::string>& bans)
{
	int count = 0;
	for (const auto& ban : bans)
	{
		if (ContainsCheatKeyword(ban))
			count++;
	}
	return count;
}

// Process pending ban alerts on main thread (call from Paint hook or similar)
void ProcessPendingBanAlerts()
{
	std::lock_guard<std::mutex> lock(g_mtxPendingAlerts);
	if (g_vecPendingBanAlerts.empty())
		return;

	// Wait until local player exists and has chosen a class
	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->m_iClass() == TF_CLASS_UNDEFINED)
		return;

	int nLocalTeam = pLocal->m_iTeamNum();
	auto pResource = GetTFPlayerResource();

	for (const auto& alert : g_vecPendingBanAlerts)
	{
		// Find player's team
		bool bIsTeammate = false;
		for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
		{
			player_info_t pi{};
			if (I::EngineClient->GetPlayerInfo(n, &pi) && !pi.fakeplayer)
			{
				uint64_t playerSteamID = static_cast<uint64_t>(pi.friendsID) + 0x0110000100000000ULL;
				if (playerSteamID == alert.steamID64)
				{
					if (pResource)
						bIsTeammate = (pResource->GetTeam(n) == nLocalTeam);
					break;
				}
			}
		}

		// Color based on ban count: yellow (1-2), orange (3+), RED if found in Verobay database
		Color_t banColor;
		if (alert.bVerobayFound)
			banColor = { 255, 50, 50, 255 }; // RED - found in Verobay database
		else
			banColor = (alert.banCount <= 2) ? Color_t{ 255, 255, 0, 255 } : Color_t{ 255, 165, 0, 255 };
		
		Color_t alertColor = { 255, 50, 50, 255 }; // Red for ALERT!
		Color_t nameColor = bIsTeammate ? CFG::Color_Teammate : CFG::Color_Enemy;
		Color_t teamColor = bIsTeammate ? CFG::Color_Teammate : CFG::Color_Enemy;
		const char* teamStr = bIsTeammate ? "Teammate" : "Enemy";

		I::ClientModeShared->m_pChatElement->ChatPrintf(0,
			std::format("\x1PLAYER [\x8{}{}] \x1HAS \x8{}{} BANS \x8{}ALERT! \x8{}({})",
				nameColor.toHexStr(),
				alert.playerName,
				banColor.toHexStr(),
				alert.banCount,
				alertColor.toHexStr(),
				teamColor.toHexStr(),
				teamStr).c_str());
	}
	g_vecPendingBanAlerts.clear();
}


// Dismiss alert for a player (called when viewing their profile)
void DismissSourcebansAlert(uint64_t steamID64)
{
	std::lock_guard<std::mutex> lock(g_mtxSourcebans);
	if (g_mapSourcebans.count(steamID64) > 0)
		g_mapSourcebans[steamID64].m_bAlertDismissed = true;
}

// Async function to fetch sourcebans for multiple players (batch)
void FetchSourcebansBatch(const std::vector<uint64_t>& steamIDs)
{
	if (steamIDs.empty())
		return;

	// Mark all as fetching
	{
		std::lock_guard<std::mutex> lock(g_mtxSourcebans);
		for (uint64_t id : steamIDs)
		{
			if (!g_mapSourcebans[id].m_bFetched && !g_mapSourcebans[id].m_bFetching)
				g_mapSourcebans[id].m_bFetching = true;
		}
	}

	std::thread([steamIDs]()
	{
		// Build comma-separated list of Steam IDs (max 100 per API call)
		std::wstring steamIdList;
		for (size_t i = 0; i < steamIDs.size() && i < 100; i++)
		{
			if (i > 0) steamIdList += L",";
			wchar_t buf[32];
			swprintf_s(buf, L"%llu", steamIDs[i]);
			steamIdList += buf;
		}

		HINTERNET hSession = WinHttpOpen(L"Necromancer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (hSession)
		{
			HINTERNET hConnect = WinHttpConnect(hSession, L"steamhistory.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
			if (hConnect)
			{
				std::wstring path = L"/api/sourcebans?key=ebef9bef3d940cb190b5328697524103&shouldkey=1&steamids=" + steamIdList; //dummy account generated this key

				HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
				if (hRequest)
				{
					if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
					{
						if (WinHttpReceiveResponse(hRequest, NULL))
						{
							std::string response;
							DWORD dwSize = 0;
							DWORD dwDownloaded = 0;

							do
							{
								dwSize = 0;
								WinHttpQueryDataAvailable(hRequest, &dwSize);
								if (dwSize > 0)
								{
									std::vector<char> buffer(dwSize + 1, 0);
									WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded);
									response.append(buffer.data(), dwDownloaded);
								}
							} while (dwSize > 0);

							// Initialize all as fetched with no bans
							std::lock_guard<std::mutex> lock(g_mtxSourcebans);
							for (uint64_t id : steamIDs)
							{
								g_mapSourcebans[id].m_bFetched = true;
								g_mapSourcebans[id].m_bFetching = false;
							}

							// Parse JSON response (with shouldkey=1, response is keyed by SteamID)
							try
							{
								auto json = nlohmann::json::parse(response);
								if (json.contains("response"))
								{
									const auto& resp = json["response"];
									
									// If it's an object (keyed by SteamID)
									if (resp.is_object())
									{
										for (auto& [steamIdStr, bans] : resp.items())
										{
											uint64_t steamId = std::stoull(steamIdStr);
											
											if (bans.is_array())
											{
												for (const auto& ban : bans)
												{
													g_mapSourcebans[steamId].m_bHasBans = true;
													std::string banStr;
													
													std::string server = "Unknown";
													std::string reason = "No reason";
													std::string state = "Unknown";
													
													if (ban.contains("Server") && !ban["Server"].is_null())
														server = ban["Server"].get<std::string>();
													if (ban.contains("BanReason") && !ban["BanReason"].is_null())
														reason = ban["BanReason"].get<std::string>();
													if (ban.contains("CurrentState") && !ban["CurrentState"].is_null())
														state = ban["CurrentState"].get<std::string>();

													banStr = server + ": " + reason + " (" + state + ")";
													g_mapSourcebans[steamId].m_vecBans.push_back(banStr);
												}
											}
										}
									}
									// If it's an array (old format without shouldkey)
									else if (resp.is_array())
									{
										for (const auto& ban : resp)
										{
											if (ban.contains("SteamID") && !ban["SteamID"].is_null())
											{
												uint64_t steamId = std::stoull(ban["SteamID"].get<std::string>());
												g_mapSourcebans[steamId].m_bHasBans = true;
												
												std::string banStr;
												std::string server = "Unknown";
												std::string reason = "No reason";
												std::string state = "Unknown";
												
												if (ban.contains("Server") && !ban["Server"].is_null())
													server = ban["Server"].get<std::string>();
												if (ban.contains("BanReason") && !ban["BanReason"].is_null())
													reason = ban["BanReason"].get<std::string>();
												if (ban.contains("CurrentState") && !ban["CurrentState"].is_null())
													state = ban["CurrentState"].get<std::string>();

												banStr = server + ": " + reason + " (" + state + ")";
												g_mapSourcebans[steamId].m_vecBans.push_back(banStr);
											}
										}
									}
								}

								// Queue chat alerts for players with cheat-related bans or in Verobay database (will be processed on main thread)
								if (CFG::Visuals_Chat_Ban_Alerts)
								{
									for (uint64_t id : steamIDs)
									{
										// Check if player is in Verobay database
										bool bVerobayFound = IsInVerobayDatabase(id);
										
										// Count cheat-related bans
										int cheatBanCount = 0;
										bool bHasSourcebans = g_mapSourcebans[id].m_bHasBans && !g_mapSourcebans[id].m_bAlertDismissed;
										
										if (bHasSourcebans)
											cheatBanCount = CountCheatRelatedBans(g_mapSourcebans[id].m_vecBans);
										
										// Add +1 to ban count if found in Verobay
										if (bVerobayFound)
											cheatBanCount++;
										
										// Skip if no cheat-related bans AND not in Verobay
										if (cheatBanCount == 0)
											continue;
										
										// Find player name from entity list
										std::string playerName = "Unknown";
										for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
										{
											player_info_t pi{};
											if (I::EngineClient->GetPlayerInfo(n, &pi) && !pi.fakeplayer)
											{
												uint64_t playerSteamID = static_cast<uint64_t>(pi.friendsID) + 0x0110000100000000ULL;
												if (playerSteamID == id)
												{
													playerName = pi.name;
													break;
												}
											}
										}
										
										// Queue alert for main thread (use cheat-related ban count, flag if Verobay found)
										{
											std::lock_guard<std::mutex> alertLock(g_mtxPendingAlerts);
											g_vecPendingBanAlerts.push_back({ playerName, cheatBanCount, id, bVerobayFound });
										}
									}
								}
							}
							catch (const std::exception& e)
							{
								(void)e; // Suppress unused variable warning
							}
						}
					}
					WinHttpCloseHandle(hRequest);
				}
				WinHttpCloseHandle(hConnect);
			}
			WinHttpCloseHandle(hSession);
		}
	}).detach();
}


// Single player fetch (for manual refresh)
void FetchSourcebans(uint64_t steamID64)
{
	FetchSourcebansBatch({ steamID64 });
}

// Check all players in the server (checks new players automatically)
void CheckAllPlayersSourcebans()
{
	if (!I::EngineClient->IsConnected())
	{
		// Reset when disconnected so we check again on next connect
		if (g_bWasConnected)
		{
			g_bWasConnected = false;
			std::lock_guard<std::mutex> lock(g_mtxSourcebans);
			g_setCheckedPlayers.clear();
			// Don't clear g_mapSourcebans - keep the cache for players we've seen before
			
			// Clear current session for player stats
			F::Players->ClearCurrentSession();
		}
		return;
	}

	g_bWasConnected = true;

	// Fetch Verobay database if not loaded yet
	if (!IsVerobayDatabaseLoaded())
	{
		FetchVerobayDatabase();
	}

	std::vector<uint64_t> steamIDsToCheck;

	for (int n = 1; n < I::EngineClient->GetMaxClients() + 1; n++)
	{
		if (n == I::EngineClient->GetLocalPlayer())
			continue;

		player_info_t player_info{};
		if (!I::EngineClient->GetPlayerInfo(n, &player_info) || player_info.fakeplayer)
			continue;

		if (player_info.friendsID == 0)
			continue;

		uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;

		// Skip if already checked or currently checking
		{
			std::lock_guard<std::mutex> lock(g_mtxSourcebans);
			if (g_setCheckedPlayers.count(steamID64) > 0)
				continue;
			if (g_mapSourcebans[steamID64].m_bFetched || g_mapSourcebans[steamID64].m_bFetching)
			{
				g_setCheckedPlayers.insert(steamID64); // Mark as checked if already in cache
				continue;
			}
			g_setCheckedPlayers.insert(steamID64);
		}

		// Record encounter for this player (first time seeing them this session)
		F::Players->RecordEncounter(steamID64);

		steamIDsToCheck.push_back(steamID64);
	}

	if (!steamIDsToCheck.empty())
	{
		FetchSourcebansBatch(steamIDsToCheck);
	}
}

// Helper to check if a player has sourcebans alert (not dismissed)
bool HasSourcebansAlert(uint64_t steamID64)
{
	std::lock_guard<std::mutex> lock(g_mtxSourcebans);
	auto it = g_mapSourcebans.find(steamID64);
	if (it != g_mapSourcebans.end())
		return it->second.m_bHasBans && !it->second.m_bAlertDismissed;
	return false;
}

// Get sourcebans info for a player (for UI display)
bool GetSourcebansInfo(uint64_t steamID64, SourcebanInfo_t& out)
{
	std::lock_guard<std::mutex> lock(g_mtxSourcebans);
	auto it = g_mapSourcebans.find(steamID64);
	if (it != g_mapSourcebans.end())
	{
		out = it->second;
		return true;
	}
	return false;
}

// Clear sourcebans cache for a player (for refresh)
void ClearSourcebansCache(uint64_t steamID64)
{
	std::lock_guard<std::mutex> lock(g_mtxSourcebans);
	g_mapSourcebans.erase(steamID64);
}

// ==================== Verobay Database ====================

// Fetch the Verobay database (Tom's reported_ids.txt from GitHub)
void FetchVerobayDatabase()
{
	{
		std::lock_guard<std::mutex> lock(g_mtxVerobay);
		if (g_bVerobayFetching)
			return; // Already fetching
		g_bVerobayFetching = true;
	}

	std::thread([]()
	{
		HINTERNET hSession = WinHttpOpen(L"Necromancer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (hSession)
		{
			HINTERNET hConnect = WinHttpConnect(hSession, L"raw.githubusercontent.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
			if (hConnect)
			{
				HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/AveraFox/Tom/main/reported_ids.txt", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
				if (hRequest)
				{
					if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
					{
						if (WinHttpReceiveResponse(hRequest, NULL))
						{
							std::string response;
							DWORD dwSize = 0;
							DWORD dwDownloaded = 0;

							do
							{
								dwSize = 0;
								WinHttpQueryDataAvailable(hRequest, &dwSize);
								if (dwSize > 0)
								{
									std::vector<char> buffer(dwSize + 1, 0);
									WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded);
									response.append(buffer.data(), dwDownloaded);
								}
							} while (dwSize > 0);

							// Parse the response - each line is a Steam64 ID
							std::set<uint64_t> newDatabase;
							std::istringstream stream(response);
							std::string line;
							int nParsed = 0;
							int nFailed = 0;

							while (std::getline(stream, line))
							{
								// Trim whitespace
								line.erase(0, line.find_first_not_of(" \t\r\n"));
								line.erase(line.find_last_not_of(" \t\r\n") + 1);

								if (line.empty())
									continue;

								try
								{
									uint64_t steamID64 = std::stoull(line);
									newDatabase.insert(steamID64);
									nParsed++;
								}
								catch (...)
								{
									nFailed++;
								}
							}

							// Update the database
							{
								std::lock_guard<std::mutex> lock(g_mtxVerobay);
								g_setVerobayDatabase = std::move(newDatabase);
								g_bVerobayFetched = true;
								g_bVerobayFetching = false;
							}
						}
						else
						{
							std::lock_guard<std::mutex> lock(g_mtxVerobay);
							g_bVerobayFetching = false;
						}
					}
					else
					{
						std::lock_guard<std::mutex> lock(g_mtxVerobay);
						g_bVerobayFetching = false;
					}
					WinHttpCloseHandle(hRequest);
				}
				WinHttpCloseHandle(hConnect);
			}
			WinHttpCloseHandle(hSession);
		}
		else
		{
			std::lock_guard<std::mutex> lock(g_mtxVerobay);
			g_bVerobayFetching = false;
		}
	}).detach();
}

// Check if a player is in the Verobay database
bool IsInVerobayDatabase(uint64_t steamID64)
{
	std::lock_guard<std::mutex> lock(g_mtxVerobay);
	return g_setVerobayDatabase.count(steamID64) > 0;
}

// Get Verobay info for a player (for UI display)
bool GetVerobayInfo(uint64_t steamID64, VerobayInfo_t& out)
{
	std::lock_guard<std::mutex> lock(g_mtxVerobay);
	out.m_bFetched = g_bVerobayFetched;
	out.m_bFetching = g_bVerobayFetching;
	out.m_bFoundInDatabase = g_setVerobayDatabase.count(steamID64) > 0;
	
	// Check if we have per-player alert dismissed state
	auto it = g_mapVerobayInfo.find(steamID64);
	if (it != g_mapVerobayInfo.end())
		out.m_bAlertDismissed = it->second.m_bAlertDismissed;
	else
		out.m_bAlertDismissed = false;
	
	return g_bVerobayFetched;
}

// Check if Verobay database has been loaded
bool IsVerobayDatabaseLoaded()
{
	std::lock_guard<std::mutex> lock(g_mtxVerobay);
	return g_bVerobayFetched;
}

// Refresh Verobay database
void RefreshVerobayDatabase()
{
	{
		std::lock_guard<std::mutex> lock(g_mtxVerobay);
		g_bVerobayFetched = false;
		g_setVerobayDatabase.clear();
	}
	FetchVerobayDatabase();
}
