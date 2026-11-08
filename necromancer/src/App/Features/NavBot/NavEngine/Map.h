#pragma once
// SDK first: NavDebug (below) calls I::CVar, and Map.cpp includes this header
// before its own SDK include
#include "../../../../SDK/SDK.h"
#include "FileReader/CNavFile.h"
#include "MicroPather/micropather.h"
#include "../../CFG.h"
#include <unordered_map>
#include <cstdint>
#include <limits>
#include <string>
#include <cstdarg>
#include <cstdio>

// Undef Windows min/max macros that conflict with std::min/std::max
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// Nav debug output — gated behind CFG::NavBot_Debug so the console stays clean
// in normal play. Traces why the bot isn't moving / where a path died.
inline void NavDebug(const char* szFmt, ...)
{
	if (!CFG::NavBot_Debug)
		return;

	char szBuf[1024];
	va_list args;
	va_start(args, szFmt);
	vsnprintf_s(szBuf, sizeof(szBuf), _TRUNCATE, szFmt, args);
	va_end(args);
	I::CVar->ConsoleColorPrintf({ 120, 200, 255, 255 }, "[NavBot] %s\n", szBuf);
}

// Player dimensions for pathfinding
#define PLAYER_WIDTH 49.0f
#define HALF_PLAYER_WIDTH (PLAYER_WIDTH / 2.0f)
#define PLAYER_HEIGHT 83.0f
#define PLAYER_JUMP_HEIGHT 50.0f
#define PLAYER_CROUCHED_JUMP_HEIGHT 62.0f
#define PLAYER_DEATH_DROP_HEIGHT 1000.0f

// Player hull constant for navigation traces. The TF2 player collision hull is
// 32x32x72 standing; pathing/vischeck hulls use 40 wide (4 units of margin per
// side) so paths the bot picks are actually walkable by the real hull.
constexpr float NAV_HULL_HALF_WIDTH = 20.0f;

// Navigation points structure for pathfinding
struct NavPoints_t
{
	Vec3 m_vCurrent;      // Current area center
	Vec3 m_vCenter;       // Connection point (adjusted for safe pathing)
	Vec3 m_vCenterNext;   // Next area connection point
	Vec3 m_vNext;         // Next area center

	NavPoints_t() = default;
	NavPoints_t(const Vec3& vCurrent, const Vec3& vCenter, const Vec3& vCenterNext, const Vec3& vNext)
		: m_vCurrent(vCurrent), m_vCenter(vCenter), m_vCenterNext(vCenterNext), m_vNext(vNext) {}
};

// Dropdown hint structure for vertical navigation
struct DropdownHint_t
{
	bool m_bRequiresDrop = false;
	float m_flDropHeight = 0.f;
	float m_flApproachDistance = 0.f;
	Vec3 m_vApproachDir;
	Vec3 m_vAdjustedPos;
};

// Map state
enum class NavState
{
	Unavailable,
	Active
};

// Stuck connection tracking (ported from Amalgam's ConnectionStuckTime_t)
struct StuckConnection_t
{
	float m_flExpireTime = 0.0f;	// When this entry expires (game time)
	int m_iStuckTicks = 0;			// How many ticks we've been stuck on this connection
	int m_iFailStrikes = 0;			// Failed grind cycles credited to this link (Unibox's m_iTimeStuck) — drives the 3-strike area rule
};

// Stuck blacklist entry (ported from Amalgam's BlacklistReason_t)
struct BlacklistEntry_t
{
	float m_flExpireTime = 0.0f;	// When this blacklist entry expires
	float m_flPenalty = 2500.0f;	// Cost penalty for pathing through this area
};

// Danger blacklist reason enum (ported from Amalgam's BlacklistReasonEnum)
enum EDangerReason
{
	DANGER_NONE = 0,
	DANGER_SENTRY,			// Sentry gun in range - very high danger
	DANGER_SENTRY_MEDIUM,	// Sentry at medium range
	DANGER_SENTRY_LOW,		// Sentry at long range
	DANGER_ENEMY_NORMAL,	// Visible enemy nearby
	DANGER_ENEMY_DORMANT,	// Dormant enemy last-known position
	DANGER_STICKY,			// Enemy sticky trap
	DANGER_ENEMY_INVULN,	// Invulnerable enemy (uber)
	DANGER_BAD_BUILD_SPOT	// Engineer building spot (not real danger)
};

// Danger blacklist entry (ported from Amalgam's FreeBlacklist)
struct DangerBlacklistEntry_t
{
	EDangerReason m_eReason = DANGER_NONE;
	float m_flExpireTime = 0.0f;	// When this entry expires (0 = no expiry)
	int m_iExpireTick = 0;		// Tick-based expiry for stickies

	DangerBlacklistEntry_t() = default;
	DangerBlacklistEntry_t(EDangerReason eReason, float flExpireTime = 0.0f, int iExpireTick = 0)
		: m_eReason(eReason), m_flExpireTime(flExpireTime), m_iExpireTick(iExpireTick) {}
};

// Get penalty for a danger reason (ported from Amalgam's GetBlacklistPenalty)
inline float GetDangerPenalty(EDangerReason eReason)
{
	switch (eReason)
	{
	case DANGER_SENTRY:			return 8000.f;	// Extremely dangerous - must avoid
	case DANGER_ENEMY_INVULN:	return 5000.f;
	case DANGER_STICKY:			return 4000.f;
	case DANGER_SENTRY_MEDIUM:	return 2500.f;
	case DANGER_ENEMY_NORMAL:	return 1500.f;
	case DANGER_SENTRY_LOW:		return 800.f;
	case DANGER_ENEMY_DORMANT:	return 400.f;
	case DANGER_BAD_BUILD_SPOT:	return 100.f;
	default:					return 0.f;
	}
}

// Pre-evaluated connection between two areas (memoized).
// The geometric work — DeterminePoints, HandleDropdown (up to 10 hull traces per
// dropdown connection), portal heights, gap checks — only depends on the mesh,
// so it is computed once per (from,to) pair and reused by every solve. Without
// this, each A* expansion redoes all of it, and a cold full-mesh solve (unreachable
// destination, blacklist reset) costs thousands of hull traces.
struct ConnEval_t
{
	bool m_bSkip = false;           // Impassable by static rules (blocked flags, height, gap)
	bool m_bIsOneWay = false;
	float m_flHeightDiff = 0.0f;
	float m_flForwardDistance = 1.0f;
	float m_flUnreliablePenalty = 0.0f;
	float m_flStaticCost = 1.0f;    // Geometric part of the connection cost (team/danger parts stay dynamic)
	NavPoints_t m_tPoints;
	DropdownHint_t m_tDropdown;
};

// Simplified Map class - handles pathfinding with MicroPather
class CMap : public micropather::Graph
{
public:
	CNavFile m_navfile;
	std::string m_sMapName;
	NavState m_eState;

	// Constructor - loads nav file
	CMap(const char* sMapName)
	{
		m_navfile = CNavFile(sMapName);
		m_sMapName = sMapName;
		m_eState = m_navfile.m_bOK ? NavState::Active : NavState::Unavailable;
	}

	// MicroPather instance (3000 nodes, 6 typical adjacent, caching enabled)
	micropather::MicroPather m_pather{ this, 3000, 6, true };

	// Memoized connection evaluation (see ConnEval_t). Rebuilt if SafePathing toggles.
	const ConnEval_t& GetConnEval(CNavArea* pFrom, CNavArea* pTo);

	// Stuck connection tracking (ported from Amalgam)
	// Key: pair of (from area, to area) pointers - tracks how long we've been stuck on each connection
	std::unordered_map<CNavArea*, std::unordered_map<CNavArea*, StuckConnection_t>> m_mConnectionStuckTime;

	// Stuck blacklist (ported from Amalgam)
	// Key: area pointer - areas that are temporarily blacklisted due to being stuck
	std::unordered_map<CNavArea*, BlacklistEntry_t> m_mStuckBlacklist;

	// Corridor passability cache (ported from Unibox's m_mVischeckCache).
	// Solved paths are validated with real traces before being accepted; a
	// segment that hits world geometry marks its connection blocked here, and
	// AdjacentCost excludes blocked connections from the solve — the next repath
	// routes around the wall instead of solving straight back into it.
	// Passable verdicts are cached too, so repaths over the same route don't
	// re-trace. Stuck-derived blocks survive transient clears (Unibox keeps
	// m_bStuckBlacklist entries when wiping its recovery cache).
	struct ConnBlock_t
	{
		float m_flExpireTime = 0.0f;   // game time; expired entries are dropped on read
		bool m_bPassable = false;      // false = corridor blocked
		bool m_bStuck = false;         // stuck-derived block (kept on transient clears)
	};
	std::unordered_map<uint64_t, ConnBlock_t> m_mConnBlocks;

	static uint64_t ConnKey(const CNavArea* pFrom, const CNavArea* pTo)
	{
		return (static_cast<uint64_t>(pFrom->m_uId) << 32) | static_cast<uint64_t>(pTo->m_uId);
	}

	// Cached corridor verdict for this connection. Returns false when no
	// unexpired entry exists (rbPassable untouched then).
	bool GetConnVerdict(const CNavArea* pFrom, const CNavArea* pTo, bool& rbPassable)
	{
		auto it = m_mConnBlocks.find(ConnKey(pFrom, pTo));
		if (it == m_mConnBlocks.end())
			return false;
		if (it->second.m_flExpireTime > 0.0f && it->second.m_flExpireTime <= I::GlobalVars->curtime)
		{
			m_mConnBlocks.erase(it);
			return false;
		}
		rbPassable = it->second.m_bPassable;
		return true;
	}

	void MarkConn(const CNavArea* pFrom, const CNavArea* pTo, bool bPassable, float flDuration, bool bStuck = false)
	{
		auto& tEntry = m_mConnBlocks[ConnKey(pFrom, pTo)];
		tEntry.m_flExpireTime = I::GlobalVars->curtime + flDuration;
		tEntry.m_bPassable = bPassable;
		tEntry.m_bStuck = bStuck;
	}

	// Unibox's HasUsableExit: does pArea still have any exit connection that isn't
	// corridor-blocked? Checked after a stuck mark so a self-sealed area is noticed.
	bool HasUsableExit(CNavArea* pArea)
	{
		if (!pArea)
			return true;
		for (auto& tConnection : pArea->m_vConnections)
		{
			CNavArea* pNextArea = tConnection.m_pArea;
			if (!pNextArea || pNextArea == pArea)
				continue;
			bool bPassable = false;
			if (GetConnVerdict(pArea, pNextArea, bPassable) && !bPassable)
				continue;
			return true;
		}
		return false;
	}

	// Unibox's ShortenExitBlocks: when the local area has no usable exit left, pull
	// its exit blocks forward so the next solve can retry the only route out instead
	// of standing sealed until the marks lapse on their own.
	void ShortenExitBlocks(CNavArea* pArea, float flDuration)
	{
		if (!pArea)
			return;
		for (auto& tConnection : pArea->m_vConnections)
		{
			CNavArea* pNextArea = tConnection.m_pArea;
			if (!pNextArea || pNextArea == pArea)
				continue;
			auto it = m_mConnBlocks.find(ConnKey(pArea, pNextArea));
			if (it != m_mConnBlocks.end() && !it->second.m_bPassable)
				it->second.m_flExpireTime = I::GlobalVars->curtime + flDuration;
		}
	}

	// Drop non-stuck blocked entries — ported from Unibox's recovery retry: when
	// a solve fails outright, transient blocks may be false positives that sealed
	// the only route. Clear them and re-solve once before backing off.
	void ClearTransientConnBlocks()
	{
		std::erase_if(m_mConnBlocks, [](const auto& tPair)
			{ return !tPair.second.m_bPassable && !tPair.second.m_bStuck; });
	}

	// Danger blacklist (ported from Amalgam's FreeBlacklist)
	// Key: area pointer - areas marked as dangerous with reason and penalty
	std::unordered_map<CNavArea*, DangerBlacklistEntry_t> m_mDangerBlacklist;

	// MicroPather Graph interface - heuristic cost estimate (straight-line distance)
	float LeastCostEstimate(void* pStartArea, void* pEndArea) override
	{
		CNavArea* pStart = reinterpret_cast<CNavArea*>(pStartArea);
		CNavArea* pEnd = reinterpret_cast<CNavArea*>(pEndArea);
		return pStart->m_vCenter.DistTo(pEnd->m_vCenter);
	}

	// MicroPather Graph interface - get adjacent areas and their costs
	void AdjacentCost(void* pArea, std::vector<micropather::StateCost>* pAdjacent) override;

	// Find closest nav area to a position (returns nullptr if no area within flMaxDist)
	CNavArea* FindClosestNavArea(const Vec3& vPos, float flMaxDist = 500.0f);

	// Find up to nCount closest nav areas, best-first (overlapping areas rank before
	// same-floor nearest-center fallbacks). Used to try multiple start areas when the
	// nearest one turns out to be unreachable through geometry.
	std::vector<CNavArea*> FindClosestNavAreas(const Vec3& vPos, int nCount, float flMaxDist = 500.0f);

	// True if the area has at least one outbound connection that the pather can
	// actually traverse (not skipped by the static connection evaluation).
	// Spawn rooms contain mesh islands — areas whose every link is skipped or
	// that have no links at all. A bot standing on one makes EVERY solve return
	// "no path" regardless of destination, so start-area selection must skip it.
	bool HasUsableOutgoing(CNavArea* pArea);

	// True when the mesh itself offers this directed link (ported from Unibox's
	// CMap::HasDirectConnection). Direct connections are TRUSTED by the engine:
	// corridor traces never reject them — doors, props and tight doorways
	// legitimately intersect player-width corridor tests, and trace-rejecting
	// them freezes the bot at spawn. Only runtime stuck detection may mark
	// them blocked (m_mConnBlocks), which the solve then honors.
	bool HasDirectConnection(CNavArea* pFrom, CNavArea* pTo) const;

	// Find path between two areas
	std::vector<void*> FindPath(CNavArea* pLocalArea, CNavArea* pDestArea, int* pOutResult = nullptr)
	{
		if (m_eState != NavState::Active)
			return {};

		float flCost;
		std::vector<void*> vPath;
		int iResult = m_pather.Solve(reinterpret_cast<void*>(pLocalArea), reinterpret_cast<void*>(pDestArea), &vPath, &flCost);
		if (pOutResult)
			*pOutResult = iResult;

		if (iResult == micropather::MicroPather::START_END_SAME)
			return { reinterpret_cast<void*>(pLocalArea) };

		return vPath;
	}

	// Reset pathfinding state
	void Reset()
	{
		m_pather.Reset();
	}

	// Required by MicroPather but unused
	void PrintStateInfo(void*) {}

	// Determine navigation points between two areas
	NavPoints_t DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea, bool bIsOneWay);

	// Handle dropdown/vertical navigation
	DropdownHint_t HandleDropdown(const Vec3& vCurrentPos, const Vec3& vNextPos, bool bIsOneWay);

	// Check if connection is one-way
	bool IsOneWay(CNavArea* pFrom, CNavArea* pTo) const;

	// Find nearest cover/hiding spot (IN_COVER flag) within max distance from position
	// TF2 bots use these for taking cover from sentries, healing behind walls, etc.
	Vec3 FindCoverSpot(const Vec3& vFrom, float flMaxDist = 1500.0f, int iTeam = 0);

	// Find nearest good sniper spot within max distance
	Vec3 FindSniperSpot(const Vec3& vFrom, float flMaxDist = 2000.0f, int iTeam = 0);

	// Find nearest hiding spot with specific flags within max distance
	Vec3 FindHidingSpot(const Vec3& vFrom, float flMaxDist, unsigned char uRequiredFlags, int iTeam);

	// Ported from Unibox's NavAreaUtils::FindClosestHidingSpot with vischeck:
	// closest IN_COVER spot near a defend anchor that CANNOT see the threat
	// position. With no threat (bHasThreat false) it degrades to the nearest
	// IN_COVER spot. Used by the defend job so holding a point means holding
	// cover, not standing in the open.
	Vec3 FindDefendSpot(const Vec3& vAnchor, const Vec3& vThreatPos, bool bHasThreat,
		float flMaxDist = 400.0f, int iTeam = 0);

private:
	// Build the memoized evaluation for one connection (static geometry + cost)
	ConnEval_t BuildConnEval(CNavArea* pCurrentArea, CNavArea* pNextArea);

	// Connection evaluation memo, keyed on (fromId << 32) | toId
	std::unordered_map<uint64_t, ConnEval_t> m_mConnEvalCache;
	bool m_bConnCacheSafePathing = false;   // SafePathing value the cache was built with
};
