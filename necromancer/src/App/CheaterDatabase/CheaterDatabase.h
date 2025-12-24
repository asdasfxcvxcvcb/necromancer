#pragma once

#include "../../SDK/SDK.h"
#include "../Features/CFG.h"
#include "../Features/Players/Players.h"

#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <set>

#pragma comment(lib, "winhttp.lib")

// Sourcebans data structure
struct SourcebanInfo_t
{
	bool m_bFetched = false;
	bool m_bFetching = false;
	bool m_bHasBans = false;
	bool m_bAlertDismissed = false; // Alert dismissed when user views profile
	std::vector<std::string> m_vecBans; // "Server: Reason (State)"
};

// Verobay database info structure
struct VerobayInfo_t
{
	bool m_bFetched = false;
	bool m_bFetching = false;
	bool m_bFoundInDatabase = false;
	bool m_bAlertDismissed = false;
};

// Pending chat alerts to be processed on main thread
struct PendingBanAlert_t
{
	std::string playerName;
	int banCount;
	uint64_t steamID64; // To look up team at display time
	bool bVerobayFound; // If found in Verobay database, force red color
};

// Process pending ban alerts on main thread (call from Paint hook or similar)
void ProcessPendingBanAlerts();

// Dismiss alert for a player (called when viewing their profile)
void DismissSourcebansAlert(uint64_t steamID64);

// Async function to fetch sourcebans for multiple players (batch)
void FetchSourcebansBatch(const std::vector<uint64_t>& steamIDs);

// Single player fetch (for manual refresh)
void FetchSourcebans(uint64_t steamID64);

// Check all players in the server (checks new players automatically)
void CheckAllPlayersSourcebans();

// Helper to check if a player has sourcebans alert (not dismissed)
bool HasSourcebansAlert(uint64_t steamID64);

// Get sourcebans info for a player (for UI display)
bool GetSourcebansInfo(uint64_t steamID64, SourcebanInfo_t& out);

// Clear sourcebans cache for a player (for refresh)
void ClearSourcebansCache(uint64_t steamID64);

// ==================== Verobay Database ====================

// Fetch the Verobay database (Tom's reported_ids.txt)
void FetchVerobayDatabase();

// Check if a player is in the Verobay database
bool IsInVerobayDatabase(uint64_t steamID64);

// Get Verobay info for a player (for UI display)
bool GetVerobayInfo(uint64_t steamID64, VerobayInfo_t& out);

// Check if Verobay database has been loaded
bool IsVerobayDatabaseLoaded();

// Refresh Verobay database
void RefreshVerobayDatabase();
