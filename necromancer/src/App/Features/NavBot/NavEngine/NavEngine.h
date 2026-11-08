#pragma once
#include "../../../../SDK/SDK.h"
#include "Map.h"
#include "../../CFG.h"
#include <vector>
#include <memory>
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

// Priority enum for NavBot goals (reworked from Amalgam's PriorityListEnum)
// Higher value = higher priority. NavTo with higher priority cancels lower.
// Order mirrors the TF2 bot's action stack: the committed main goals (roam,
// stalk, capture, follow) sit at the bottom; supply fetches suspend them like
// CTFBotTacticalMonitor's GetHealth/GetAmmo; escapes sit on top. Supply above
// follow/capture is what lets an interrupt legitimately take the path without
// the old FOLLOW_SUPPLY special case.
enum ENavPriority
{
	NAV_PRIO_NONE = 0,
	NAV_PRIO_PATROL = 5,
	NAV_PRIO_STAYNEAR,
	NAV_PRIO_CAPTURE,
	NAV_PRIO_FOLLOW_TEAMMATE,
	NAV_PRIO_FOLLOW_TAGGED,
	NAV_PRIO_GETAMMO,
	NAV_PRIO_GETHEALTH,
	NAV_PRIO_ESCAPE_SPAWN,
	NAV_PRIO_ESCAPE_DANGER
};

// Jump state machine (ported from Amalgam's EJumpState)
enum EJumpState
{
	JUMP_AWAITING,
	JUMP_CTAP,
	JUMP_JUMPING,
	JUMP_ASCENDING,
	JUMP_DESCENDING
};

// Stuck phase ladder (ported from Unibox's StuckPhase). After 1.5s of no progress
// the bot JUMPS while still pushing toward the crumb — a hull pressed into a door
// frame while jumping slides around the corner, the physical escape no repath can
// provide. Only after 1.85s does the fail response (connection marks) run.
enum ENavStuckPhase
{
	STUCK_IDLE = 0,
	STUCK_JUMP,
	STUCK_FAIL
};

// Crumb structure (ported from Amalgam's Crumb_t)
// Stores a waypoint with metadata for approach direction and dropdown handling.
struct Crumb_t
{
	Vec3 m_vPos = {};              // Waypoint position
	Vec3 m_vApproachDir = {};      // Direction to walk when reaching this crumb
	float m_flApproachDistance = 0.f; // How far to push past this crumb (for drops)
	bool m_bRequiresDrop = false;  // Whether this crumb requires a drop/dropdown
	float m_flDropHeight = 0.f;    // Height of the drop at this crumb
	CNavArea* m_pNavArea = nullptr; // Nav area this crumb belongs to
};

// Navigation hull half-width lives in Map.h (NAV_HULL_HALF_WIDTH) so the map
// layer's connection validation and the engine's pathing traces agree on it.

// NavEngine - handles high-level pathfinding and path following
// Ported from Amalgam-v2 NavEngine with simplified wandering support
class CNavEngine
{
public:
	CNavEngine() = default;
	~CNavEngine() = default;

	// Public map instance for external access
	std::unique_ptr<CMap> m_pMap;

	// Current priority (public so NavBot can check it)
	ENavPriority m_eCurrentPriority = NAV_PRIO_NONE;

	// Initialize nav mesh for current map
	bool Init(const char* szMapName);

	// Start navigating to a position with priority
	bool NavTo(const Vec3& vDestination, ENavPriority ePriority = NAV_PRIO_PATROL, bool bShouldRepath = true);

	// Follow current path (call every frame)
	void FollowPath(C_TFPlayer* pLocal, CUserCmd* pCmd);

	// Cancel current path completely
	void CancelPath();

	// Abandon current path and auto-repath to same destination (like Amalgam)
	void AbandonPath(const char* sReason);

	// Check if currently pathing (including approaching destination after crumbs exhausted)
	bool IsPathing() const { return !m_vCrumbs.empty() || m_bApproachingDestination; }

	// Get current destination
	Vec3 GetDestination() const { return m_vDestination; }

	// Get last destination (for auto-repath after AbandonPath)
	Vec3 GetLastDestination() const { return m_vLastDestination; }

	// Get current crumbs (for visualization)
	const std::vector<Crumb_t>& GetCrumbs() const { return m_vCrumbs; }

	// Index of the crumb we're currently walking toward. The NavBot's
	// ahead-on-path danger scan starts here so ground already covered
	// doesn't count as "ahead".
	size_t GetCurrentCrumbIndex() const { return m_nCurrentCrumbIndex; }

	// Get current waypoint the bot is walking toward (for LookAtPath)
	Vec3 GetCurrentWaypoint() const
	{
		if (m_nCurrentCrumbIndex < m_vCrumbs.size())
			return m_vCrumbs[m_nCurrentCrumbIndex].m_vPos;
		return m_vDestination;
	}

	// Get current path direction (ported from Amalgam's GetCurrentPathDir)
	Vec3 GetCurrentPathDir() const { return m_vCurrentPathDir; }

	// Vischeck the current path - cancel if blocked (ported from Amalgam)
	void VischeckPath(C_TFPlayer* pLocal);

	// Player-width corridor test (ported from Unibox's IsPlayerPassableNavigation):
	// two ray traces along the walk line, offset to its sides and lifted above
	// step height. Either ray hitting world geometry means the bot cannot walk
	// this segment.
	bool IsPlayerPassableNavigation(C_TFPlayer* pLocal, const Vec3& vFrom, const Vec3& vTo);

	// Update stuck time tracking and blacklist stuck connections (ported from Amalgam)
	void UpdateStuckTime(C_TFPlayer* pLocal);

	// Reset nav engine - reloads nav mesh for current map (call on map change)
	void Reset(bool bForced = false);

	// Check if nav engine is ready (nav mesh loaded and active)
	bool IsReady() const;

	// Check if nav mesh is loaded
	bool IsNavMeshLoaded() const { return m_pMap && m_pMap->m_eState == NavState::Active; }

	// Get nav file path
	std::string GetNavFilePath() const { return m_pMap ? m_pMap->m_sMapName : ""; }

	// Danger blacklist access (ported from Amalgam's FreeBlacklist)
	std::unordered_map<CNavArea*, DangerBlacklistEntry_t>* GetDangerBlacklist() { return m_pMap ? &m_pMap->m_mDangerBlacklist : nullptr; }
	void ClearDangerBlacklist() { if (m_pMap) m_pMap->m_mDangerBlacklist.clear(); }
	void ClearDangerBlacklist(EDangerReason eReason) {
		if (!m_pMap) return;
		std::erase_if(m_pMap->m_mDangerBlacklist, [eReason](const auto& entry) { return entry.second.m_eReason == eReason; });
	}

	// Update respawn room nav area flags (ported from Amalgam's UpdateRespawnRooms)
	void UpdateRespawnRooms();

	// Populate the respawn-room flags/exit cache on first need. The only other
	// call site is FollowPath, which returns before reaching it while the bot
	// has no path to follow — exactly the frozen state EscapeSpawn exists to
	// fix, so EscapeSpawn triggers this itself.
	void EnsureRespawnRoomsUpdated() { if (!m_bUpdatedRespawnRooms) UpdateRespawnRooms(); }

	// Get respawn room exit areas (for EscapeSpawn)
	std::vector<CNavArea*>* GetRespawnRoomExitAreas() { return &m_vRespawnRoomExitAreas; }

	// Get local player's nav area
	CNavArea* GetLocalNavArea() const;

	// Check if we're in setup time (blue team on PL/CP maps during setup)
	// Ported from Unibox's IsSetupTime
	bool IsSetupTime();

	// Team-agnostic "are setup gates physically closed right now" — used by
	// the path-cost gate seal, where IsSetupTime()'s BLU-hold semantics would
	// let RED paths solve straight through the closed BLU gates
	bool AreSetupGatesClosed();

	// Unibox NavRuntime::IsMovementLocked — the REAL "wait for setup" gate:
	// waiting-for-players, preround and between-rounds lock ALL bot movement
	// (team-agnostic; MvM never locks). Unibox's job system stands down and
	// its engine cancels paths in these states
	bool IsMovementLocked();

	// Unibox CanCaptureObjective: objective capture plays only in a live round
	// (RND_RUNNING/STALEMATE, never while waiting for players)
	bool IsRoundLive();

	// Get inactivity time (for stuck wiggle logic in NavBot)
	float GetInactivityTime() const { return m_flInactivityTime; }

private:
	std::vector<Crumb_t> m_vCrumbs;  // Current path as crumbs (ported from Amalgam)
	bool m_bApproachingDestination = false;  // Walking toward destination after crumbs exhausted
	float m_flApproachCheckTime = 0.0f;      // Approach-phase progress sampling clock
	float m_flApproachCheckDist = 0.0f;      // Distance to destination at last sample
	Vec3 m_vDestination;
	Vec3 m_vLastDestination;  // Last destination for auto-repath
	size_t m_nCurrentCrumbIndex = 0;
	Vec3 m_vCurrentPathDir = {};  // Current movement direction (like Amalgam's m_vCurrentPathDir)
	std::string m_sLastMapName;  // Track last map for auto-reset
	bool m_bRepathOnFail = true;  // Auto-repath on AbandonPath
	float m_flNextVischeckTime = 0.0f;  // Throttle vischecks
	int m_nVischeckFailCount = 0;       // How many times vischeck blocked the current destination
	float m_flVischeckCooldownUntil = 0.0f;  // Don't vischeck until this time (after repeated failures)

	// Progress-based stuck sampling (ported from Unibox's TickStuckSample).
	// The old velocity-only check (Length2D > 40 = moving) misses wall-grinding:
	// a bot sliding along a wall reads as "moving" while making zero progress
	// toward its crumb, so it ground into geometry forever. Samples every 0.3s
	// while grounded; progress = nav area changed or distance to the active
	// crumb dropped by a speed-scaled threshold.
	float m_flNextStuckSampleTime = 0.0f;
	float m_flLastSampleDistToCrumb = FLT_MAX;
	Vec3 m_vSampledCrumbPos = {};
	bool m_bHasSampledCrumb = false;
	CNavArea* m_pLastProgressArea = nullptr;

	// Reset the progress sampler for a fresh path (first sample counts as progress)
	void ResetStuckProgress();

	// Smart jump state machine (ported from Amalgam)
	EJumpState m_eJumpState = JUMP_AWAITING;
	float m_flInactivityTime = 0.0f;  // How long we've been stuck on current waypoint
	float m_flLastJumpTime = 0.0f;    // Throttle jumps (0.2s between jumps)

	// Stuck detection tracking (ported from Amalgam's UpdateStuckTime)
	CNavArea* m_pLastNavArea = nullptr;  // Previous nav area for connection tracking

	// Unibox's TickStuckSample result for this tick — FollowPath applies the
	// Jump phase (IN_JUMP on ground / IN_DUCK midair) before the fail response runs
	ENavStuckPhase m_eStuckPhase = STUCK_IDLE;

	// The area the last successful solve started from. Stuck marks must name edges
	// the NEXT solve actually traverses, and NavTo's start candidate can be an area
	// the bot hasn't physically reached (a door frame: the hull trace to the
	// candidate's center threads the opening). Anchoring marks on
	// FindClosestNavArea instead sealed links in a world the solver never visited,
	// and the grind looped forever (Dustbowl: 20+ strikes on a phantom link while
	// every solve started past the door).
	CNavArea* m_pLastSolveStartArea = nullptr;

	// Dropdown edge nudge - when bot reaches a drop crumb but hasn't actually fallen
	// because the navmesh doesn't cover the very edge of the structure
	bool m_bDropNudgeActive = false;       // Currently nudging forward on a drop
	Vec3 m_vDropNudgeDir = {};             // Direction to nudge (approach direction)
	float m_flDropNudgeStartTime = 0.0f;   // When the current nudge attempt started
	int m_nDropNudgeAttempts = 0;          // How many forward nudges we've tried
	float m_flDropNudgeExtraForward = 0.0f; // Extra forward distance for current nudge

	// Route variety tracking (ported from TF2 bot's CTFBotPathCost)
	// Path costs shift with a 10s time modulus; the pather cache is reset on new
	// destinations (not on a timer) so active paths stay stable.
	bool m_bRouteVarietyResetForDest = false;

	// Whisker obstacle avoidance (concept from TF2 bot's PathFollower::Avoid).
	// Two small hull probes offset left/right of the feet detect near obstacles
	// and steer the movement goal away from the blocked side.
	float m_flNextWhiskerCheckTime = 0.0f;  // Throttle: re-check frequently while steering, otherwise sparsely
	float m_flWhiskerLeftAvoid = 0.0f;      // Left probe blockage magnitude (0 = clear, 1 = fully blocked)
	float m_flWhiskerRightAvoid = 0.0f;     // Right probe blockage magnitude
	bool m_bWhiskerSteering = false;        // Any whisker currently blocked (re-check every tick)

	// AbandonPath cooldown — prevents rapid route switching oscillation
	// Without this, AbandonPath→NavTo→AbandonPath→NavTo can cycle between 2 routes every tick
	float m_flLastAbandonTime = 0.0f;
	float m_flAbandonCooldown = 0.0f;  // Dynamic cooldown, increases with repeated abandons
	int m_nAbandonCount = 0;           // How many consecutive abandons for same destination

	// Minimum repath interval — prevents NavTo from being called too rapidly
	float m_flLastPathTime = 0.0f;

	// Solve throttling — a solve that fails (unreachable destination) costs a full
	// mesh search even with the memoized connection cache, and several jobs retry
	// NavTo every tick while pathless. Enforce a floor between any two solve
	// attempts, and a growing backoff after failures to the same destination.
	float m_flNextSolveTime = 0.0f;     // No A* solve attempts before this time
	Vec3 m_vFailedDest = {};            // Destination of the last failed solve
	float m_flFailedPathUntil = 0.0f;   // Reject solves to m_vFailedDest until this time
	float m_flFailBackoff = 1.0f;       // Escalating backoff duration (reset on success/new dest)

	// Approach-phase entity blocker — when the direct walk to the destination is
	// stopped by a closed door/gate, this is when the stand-and-wait started (0 = not blocked)
	float m_flApproachBlockSince = 0.0f;

	// Cached local nav area — prevents FindClosestNavArea oscillation in FollowPath
	CNavArea* m_pCachedLocalArea = nullptr;
	float m_flCachedLocalAreaTime = 0.0f;

	// Respawn room tracking (ported from Amalgam's UpdateRespawnRooms)
	bool m_bUpdatedRespawnRooms = false;
	std::vector<CNavArea*> m_vRespawnRoomExitAreas;

	// Convert nav area path to world position path
	void BuildWorldPath(const std::vector<void*>& vAreaPath);

	// Calculate movement commands to reach target
	// flSpeedScale lets the caller slow down for upcoming turns (1.0 = full speed)
	void MoveTowards(C_TFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vTarget, float flSpeedScale = 1.0f);

	// Whisker obstacle avoidance — returns an adjusted movement goal that steers
	// around near obstacles detected by left/right hull probes (concept from the
	// TF2 bot's PathFollower::Avoid reflex steering)
	Vec3 AvoidObstacles(C_TFPlayer* pLocal, const Vec3& vGoalPos, const Vec3& vForward, const Vec3& vLeft);

	// Smart jump prediction - traces forward to check if jump will land on walkable surface
	bool SmartJump(C_TFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vTarget);

	// Handle smart jump state machine (c-tap, duck timing, etc.)
	void HandleSmartJump(C_TFPlayer* pLocal, CUserCmd* pCmd);

	// Force a jump (called by stuck detection)
	void ForceJump() { if (m_eJumpState == JUMP_AWAITING) m_eJumpState = JUMP_JUMPING; }

	// Check if a surface normal is walkable (not too steep)
	bool IsSurfaceWalkable(const Vec3& vNormal) const;
};

// Global nav engine instance
inline CNavEngine G_NavEngine;
