#include "CheaterDatabase.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <functional>

// ==================== Worker Thread Infrastructure ====================

enum class TaskType { FetchSourcebans, FetchVerobay };

struct WorkerTask_t
{
	TaskType type;
	std::vector<uint64_t> steamIDs; // For sourcebans batch
};

static std::queue<WorkerTask_t> g_taskQueue;
static std::mutex g_mtxTaskQueue;
static std::condition_variable g_cvTaskQueue;
static std::atomic<bool> g_bWorkerRunning{ false };
static std::thread g_workerThread;

// Persistent HTTP session (reused across requests)
static HINTERNET g_hSession = nullptr;

// Sourcebans data
static std::map<uint64_t, SourcebanInfo_t> g_mapSourcebans;
static std::mutex g_mtxSourcebans;
static std::set<uint64_t> g_setCheckedPlayers;
static bool g_bWasConnected = false;

// Verobay database
static std::set<uint64_t> g_setVerobayDatabase;
static std::map<uint64_t, VerobayInfo_t> g_mapVerobayInfo;
static std::mutex g_mtxVerobay;
static std::atomic<bool> g_bVerobayFetched{ false };
static std::atomic<bool> g_bVerobayFetching{ false };

// Pending chat alerts
static std::vector<PendingBanAlert_t> g_vecPendingBanAlerts;
static std::mutex g_mtxPendingAlerts;

// Cheat keywords for filtering
static const std::vector<std::string> g_vecCheatKeywords = {
	"hacking", "account", "ip", "cheats", "cheat", "cheating", "convar", "hack", "hacks",
	"aimbot", "triggerbot", "bunnyhop", "dupe", "duplicate", "smac", "silent", "smooth",
	"psilent", "bot", "spinning", "alt", "anti", "anti-cheat", "violation", "anticheat",
	"exploit", "crit", "angle", "groupban"
};

// Chrome User-Agent for HTTP requests
static const wchar_t* g_wszUserAgent = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// ==================== Helper Functions ====================

static bool ContainsCheatKeyword(const std::string& banReason)
{
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


// ==================== HTTP Helper ====================

static std::string HttpGet(const wchar_t* host, const wchar_t* path)
{
	std::string response;
	
	if (!g_hSession)
		return response;

	HINTERNET hConnect = WinHttpConnect(g_hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect)
		return response;

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (hRequest)
	{
		if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
		{
			if (WinHttpReceiveResponse(hRequest, NULL))
			{
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
			}
		}
		WinHttpCloseHandle(hRequest);
	}
	WinHttpCloseHandle(hConnect);
	
	return response;
}

// ==================== Task Processors ====================

static void ProcessSourcebansFetch(const std::vector<uint64_t>& steamIDs)
{
	if (steamIDs.empty())
		return;

	// Build comma-separated list (max 100 per API call)
	std::wstring steamIdList;
	for (size_t i = 0; i < steamIDs.size() && i < 100; i++)
	{
		if (i > 0) steamIdList += L",";
		wchar_t buf[32];
		swprintf_s(buf, L"%llu", steamIDs[i]);
		steamIdList += buf;
	}

	std::wstring path = L"/api/sourcebans?key=ebef9bef3d940cb190b5328697524103&shouldkey=1&steamids=" + steamIdList;
	std::string response = HttpGet(L"steamhistory.net", path.c_str());

	// Initialize all as fetched
	{
		std::lock_guard<std::mutex> lock(g_mtxSourcebans);
		for (uint64_t id : steamIDs)
		{
			g_mapSourcebans[id].m_bFetched = true;
			g_mapSourcebans[id].m_bFetching = false;
		}
	}

	if (response.empty())
		return;

	// Parse JSON response
	try
	{
		auto json = nlohmann::json::parse(response);
		if (!json.contains("response"))
			return;

		const auto& resp = json["response"];
		
		std::lock_guard<std::mutex> lock(g_mtxSourcebans);
		
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
						
						std::string server = ban.value("Server", "Unknown");
						std::string reason = ban.value("BanReason", "No reason");
						std::string state = ban.value("CurrentState", "Unknown");

						g_mapSourcebans[steamId].m_vecBans.push_back(server + ": " + reason + " (" + state + ")");
					}
				}
			}
		}
		else if (resp.is_array())
		{
			for (const auto& ban : resp)
			{
				if (!ban.contains("SteamID") || ban["SteamID"].is_null())
					continue;
					
				uint64_t steamId = std::stoull(ban["SteamID"].get<std::string>());
				g_mapSourcebans[steamId].m_bHasBans = true;
				
				std::string server = ban.value("Server", "Unknown");
				std::string reason = ban.value("BanReason", "No reason");
				std::string state = ban.value("CurrentState", "Unknown");

				g_mapSourcebans[steamId].m_vecBans.push_back(server + ": " + reason + " (" + state + ")");
			}
		}
	}
	catch (...) { return; }

	// Queue chat alerts
	if (!CFG::Visuals_Chat_Ban_Alerts)
		return;

	std::lock_guard<std::mutex> lockSb(g_mtxSourcebans);
	
	for (uint64_t id : steamIDs)
	{
		bool bVerobayFound = IsInVerobayDatabase(id);
		int cheatBanCount = 0;
		
		if (g_mapSourcebans[id].m_bHasBans && !g_mapSourcebans[id].m_bAlertDismissed)
			cheatBanCount = CountCheatRelatedBans(g_mapSourcebans[id].m_vecBans);
		
		if (bVerobayFound)
			cheatBanCount++;
		
		if (cheatBanCount == 0)
			continue;
		
		// Find player name
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
		
		std::lock_guard<std::mutex> alertLock(g_mtxPendingAlerts);
		g_vecPendingBanAlerts.push_back({ playerName, cheatBanCount, id, bVerobayFound });
	}
}


static void ProcessVerobayFetch()
{
	std::string response = HttpGet(L"raw.githubusercontent.com", L"/AveraFox/Tom/main/reported_ids.txt");
	
	if (response.empty())
	{
		g_bVerobayFetching = false;
		return;
	}

	std::set<uint64_t> newDatabase;
	std::istringstream stream(response);
	std::string line;

	while (std::getline(stream, line))
	{
		line.erase(0, line.find_first_not_of(" \t\r\n"));
		line.erase(line.find_last_not_of(" \t\r\n") + 1);

		if (line.empty())
			continue;

		try
		{
			newDatabase.insert(std::stoull(line));
		}
		catch (...) {}
	}

	{
		std::lock_guard<std::mutex> lock(g_mtxVerobay);
		g_setVerobayDatabase = std::move(newDatabase);
	}
	
	g_bVerobayFetched = true;
	g_bVerobayFetching = false;
}

// ==================== Worker Thread ====================

static void WorkerThreadFunc()
{
	while (g_bWorkerRunning)
	{
		WorkerTask_t task;
		
		{
			std::unique_lock<std::mutex> lock(g_mtxTaskQueue);
			g_cvTaskQueue.wait(lock, [] { return !g_taskQueue.empty() || !g_bWorkerRunning; });
			
			if (!g_bWorkerRunning)
				break;
			
			task = std::move(g_taskQueue.front());
			g_taskQueue.pop();
		}

		switch (task.type)
		{
		case TaskType::FetchSourcebans:
			ProcessSourcebansFetch(task.steamIDs);
			break;
		case TaskType::FetchVerobay:
			ProcessVerobayFetch();
			break;
		}
	}
}

// ==================== Public API ====================

void InitCheaterDatabase()
{
	if (g_bWorkerRunning)
		return;

	g_hSession = WinHttpOpen(g_wszUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	
	g_bWorkerRunning = true;
	g_workerThread = std::thread(WorkerThreadFunc);
}

void ShutdownCheaterDatabase()
{
	if (!g_bWorkerRunning)
		return;

	g_bWorkerRunning = false;
	g_cvTaskQueue.notify_all();
	
	if (g_workerThread.joinable())
		g_workerThread.join();

	if (g_hSession)
	{
		WinHttpCloseHandle(g_hSession);
		g_hSession = nullptr;
	}
}

void ProcessPendingBanAlerts()
{
	std::vector<PendingBanAlert_t> alerts;
	{
		std::lock_guard<std::mutex> lock(g_mtxPendingAlerts);
		if (g_vecPendingBanAlerts.empty())
			return;
		alerts = std::move(g_vecPendingBanAlerts);
		g_vecPendingBanAlerts.clear();
	}

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->m_iClass() == TF_CLASS_UNDEFINED)
	{
		// Put alerts back if we can't process them yet
		std::lock_guard<std::mutex> lock(g_mtxPendingAlerts);
		g_vecPendingBanAlerts.insert(g_vecPendingBanAlerts.end(), alerts.begin(), alerts.end());
		return;
	}

	int nLocalTeam = pLocal->m_iTeamNum();
	auto pResource = GetTFPlayerResource();

	for (const auto& alert : alerts)
	{
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

		Color_t banColor = alert.bVerobayFound ? Color_t{ 255, 50, 50, 255 } 
			: (alert.banCount <= 2) ? Color_t{ 255, 255, 0, 255 } : Color_t{ 255, 165, 0, 255 };
		Color_t alertColor = { 255, 50, 50, 255 };
		Color_t nameColor = bIsTeammate ? CFG::Color_Teammate : CFG::Color_Enemy;
		Color_t teamColor = bIsTeammate ? CFG::Color_Teammate : CFG::Color_Enemy;
		const char* teamStr = bIsTeammate ? "Teammate" : "Enemy";

		I::ClientModeShared->m_pChatElement->ChatPrintf(0,
			std::format("\x1PLAYER [\x8{}{}\x1] HAS \x8{}{} BANS \x8{}ALERT! \x8{}({})",
				nameColor.toHexStr(), alert.playerName,
				banColor.toHexStr(), alert.banCount,
				alertColor.toHexStr(),
				teamColor.toHexStr(), teamStr).c_str());
	}
}

void DismissSourcebansAlert(uint64_t steamID64)
{
	std::lock_guard<std::mutex> lock(g_mtxSourcebans);
	if (g_mapSourcebans.count(steamID64) > 0)
		g_mapSourcebans[steamID64].m_bAlertDismissed = true;
}


void FetchSourcebansBatch(const std::vector<uint64_t>& steamIDs)
{
	if (steamIDs.empty())
		return;

	// Mark as fetching
	{
		std::lock_guard<std::mutex> lock(g_mtxSourcebans);
		for (uint64_t id : steamIDs)
		{
			if (!g_mapSourcebans[id].m_bFetched && !g_mapSourcebans[id].m_bFetching)
				g_mapSourcebans[id].m_bFetching = true;
		}
	}

	// Queue task for worker thread
	{
		std::lock_guard<std::mutex> lock(g_mtxTaskQueue);
		g_taskQueue.push({ TaskType::FetchSourcebans, steamIDs });
	}
	g_cvTaskQueue.notify_one();
}

void FetchSourcebans(uint64_t steamID64)
{
	FetchSourcebansBatch({ steamID64 });
}

void CheckAllPlayersSourcebans()
{
	if (!I::EngineClient->IsConnected())
	{
		if (g_bWasConnected)
		{
			g_bWasConnected = false;
			std::lock_guard<std::mutex> lock(g_mtxSourcebans);
			g_setCheckedPlayers.clear();
			F::Players->ClearCurrentSession();
		}
		return;
	}

	g_bWasConnected = true;

	if (!IsVerobayDatabaseLoaded())
		FetchVerobayDatabase();

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

		{
			std::lock_guard<std::mutex> lock(g_mtxSourcebans);
			if (g_setCheckedPlayers.count(steamID64) > 0)
				continue;
			if (g_mapSourcebans[steamID64].m_bFetched || g_mapSourcebans[steamID64].m_bFetching)
			{
				g_setCheckedPlayers.insert(steamID64);
				continue;
			}
			g_setCheckedPlayers.insert(steamID64);
		}

		F::Players->RecordEncounter(steamID64);
		steamIDsToCheck.push_back(steamID64);
	}

	if (!steamIDsToCheck.empty())
		FetchSourcebansBatch(steamIDsToCheck);
}

bool HasSourcebansAlert(uint64_t steamID64)
{
	{
		std::lock_guard<std::mutex> lock(g_mtxSourcebans);
		auto it = g_mapSourcebans.find(steamID64);
		if (it != g_mapSourcebans.end() && it->second.m_bHasBans && !it->second.m_bAlertDismissed)
			return true;
	}
	
	VerobayInfo_t verobayInfo;
	if (GetVerobayInfo(steamID64, verobayInfo))
	{
		if (verobayInfo.m_bFoundInDatabase && !verobayInfo.m_bAlertDismissed)
			return true;
	}
	
	return false;
}

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

void ClearSourcebansCache(uint64_t steamID64)
{
	std::lock_guard<std::mutex> lock(g_mtxSourcebans);
	g_mapSourcebans.erase(steamID64);
}

// ==================== Verobay Database ====================

void FetchVerobayDatabase()
{
	if (g_bVerobayFetching.exchange(true))
		return; // Already fetching

	std::lock_guard<std::mutex> lock(g_mtxTaskQueue);
	g_taskQueue.push({ TaskType::FetchVerobay, {} });
	g_cvTaskQueue.notify_one();
}

bool IsInVerobayDatabase(uint64_t steamID64)
{
	std::lock_guard<std::mutex> lock(g_mtxVerobay);
	return g_setVerobayDatabase.count(steamID64) > 0;
}

bool GetVerobayInfo(uint64_t steamID64, VerobayInfo_t& out)
{
	std::lock_guard<std::mutex> lock(g_mtxVerobay);
	out.m_bFetched = g_bVerobayFetched;
	out.m_bFetching = g_bVerobayFetching;
	out.m_bFoundInDatabase = g_setVerobayDatabase.count(steamID64) > 0;
	
	auto it = g_mapVerobayInfo.find(steamID64);
	out.m_bAlertDismissed = (it != g_mapVerobayInfo.end()) ? it->second.m_bAlertDismissed : false;
	
	return g_bVerobayFetched;
}

bool IsVerobayDatabaseLoaded()
{
	return g_bVerobayFetched;
}

void RefreshVerobayDatabase()
{
	{
		std::lock_guard<std::mutex> lock(g_mtxVerobay);
		g_setVerobayDatabase.clear();
	}
	g_bVerobayFetched = false;
	FetchVerobayDatabase();
}

void DismissVerobayAlert(uint64_t steamID64)
{
	std::lock_guard<std::mutex> lock(g_mtxVerobay);
	g_mapVerobayInfo[steamID64].m_bAlertDismissed = true;
}
