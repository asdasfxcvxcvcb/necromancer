#pragma once
#include "../../../SDK/SDK.h"
#include <vector>
#include <unordered_map>

class CNavArea;  // Forward declaration for danger blacklist escape target

// Undef Windows min/max macros that conflict with std::min/std::max
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// NavBot - Objective-based navigation with proper nav mesh pathfinding
// Captures objectives and wanders when idle using A* pathfinding
// NO aiming functionality - aimbot handles that separately

// === Goal machine (ported from the TF2 bot's NextBot action stack) ===
// ONE committed main goal at a time (follow / capture / stalk / snipe / roam),
// picked only when idle or on a preemption event — like the TF2 bot picking
// its scenario action once per life instead of re-deciding every tick.
// Reflex interrupts (projectile dodge, danger retreat, spawn exit, supply
// fetch) temporarily suspend the main goal and resume it when done, exactly
// like CTFBotTacticalMonitor's SuspendFor/Done discipline. The old per-tick
// job chain re-decided everything every tick, so jobs canceled each other's
// paths in loops ("bot changes path, cancels it, changes back, cancels...").
enum EBotGoal
{
	GOAL_NONE = 0,
	GOAL_WANDER,            // roam fallback (TF2 seek & destroy)
	GOAL_STAYNEAR,          // stalk nearby enemies
	GOAL_SNIPER_SPOT,       // hold navmesh sniper sightlines
	GOAL_CAPTURE,           // objective: flag / control point / payload cart
	GOAL_DEFEND,            // hold cover near the objective the enemy pushes (Unibox DefendObjectives)
	GOAL_FOLLOW_TEAMMATE,   // escort the closest valid teammate
	GOAL_FOLLOW_TAGGED,     // escort a player with the Follow tag (user intent)
	GOAL_GROUP,             // fall into formation behind the earliest-joined teammate (Unibox GroupWithOthers)
	GOAL_SUPPLY,            // interrupt: fetch health / ammo
	GOAL_ESCAPE_SPAWN,      // interrupt: leave the spawn room
	GOAL_ESCAPE_DANGER,     // interrupt: retreat from sentries / danger areas
	GOAL_ESCAPE_PROJECTILE, // interrupt: dodge incoming projectiles
	GOAL_RUN_RELOAD         // interrupt: retreat to cover while a ranged slot reloads (Unibox RunReload)
};

class CNavBot
{
private:
	bool m_bActive = false;
	C_BaseEntity* m_pTargetEntity = nullptr;
	Vec3 m_vTargetPos = Vec3(0, 0, 0);
	Vec3 m_vWanderTarget = Vec3(0, 0, 0);
	float m_flNextWanderTime = 0.0f;
	float m_flNextRepathTime = 0.0f;  // Throttle repathing (like Amalgam's Timer)

	// === Goal machine state ===
	EBotGoal m_eGoal = GOAL_NONE;
	EBotGoal m_eSuspendedGoal = GOAL_NONE;  // Main goal parked under an interrupt
	float m_flGoalStartTime = 0.0f;         // When the current goal was picked
	float m_flNextGoalScanTime = 0.0f;      // Re-pick / preemption scan cadence
	float m_flNextDangerCheckTime = 0.0f;   // Danger-escape cadence (TF2 maintain timer: 0.3-0.5s)
	float m_flNextEscapeSpawnCheck = 0.0f;  // Spawn-exit solve throttle (was a function-static)

	// Spawn exits that failed to path recently. The exit list is "any area adjacent
	// to a spawn room", which includes areas only VERTICALLY adjacent (the floor
	// above, a roof) that have no walk route from inside — pathing to the nearest
	// such exit fails forever. EscapeSpawn rotates to the next-nearest exit
	// instead of hammering the same dead one.
	struct FailedExit_t { CNavArea* m_pArea = nullptr; float m_flUntil = 0.0f; };
	std::vector<FailedExit_t> m_vFailedExits;

	// Exit commitment: once EscapeSpawn is walking to a specific exit, stick
	// with it while that path is alive. Re-sorting exits nearest-first on every
	// check let the pick flip between two candidates as the bot moved along the
	// first route (prio-12 NavTo alternating between two destinations forever —
	// the "running back and forth inside spawn" report). If the committed exit
	// hasn't gotten us out in 10s (sealed setup gate) or its path died while
	// we're still flagged in-spawn, it's blacklisted briefly and another exit
	// is picked.
	CNavArea* m_pCommittedExitArea = nullptr;
	float m_flCommittedExitTime = 0.0f;
	float m_flNextObjectiveScanTime = 0.0f; // Capture objective rescan (TF2: 1-2s)
	float m_flUnreachableSince = 0.0f;      // How long the capture target has been unreachable

	// === Capture-spot state (ported from Unibox's CNavBotCapture) ===
	// Teammates spread on a ring around the point instead of stacking on its
	// center; the slot survives while the point stays the same and no teammate
	// stands in it. m_bObjectiveStaging marks the Unibox GetStagedPointGoal
	// case: nothing is cappable yet, so the "objective" is a hold position on
	// our side of the next point (attacker staging / defender post).
	Vec3 m_vCaptureSpotPos = Vec3(0, 0, 0);
	Vec3 m_vCaptureSpotCenter = Vec3(0, 0, 0);   // Which point the slot belongs to
	bool m_bHasCaptureSpot = false;
	bool m_bObjectiveStaging = false;

	float m_flNextFollowRepathTime = 0.0f;  // Follow repath timer — actually enforced (the old m_flNextRepathTime follow path set it but never checked it, so a walking target caused a re-solve every few ticks)
	float m_flNextFollowScanTime = 0.0f;    // Follow target reselection cadence (~1s, TF2 patient re-eval)
	bool m_bSupplyIsHealth = false;         // Current supply run is for health (vs ammo)

	// Objective reachability memo — the A* connectivity check is far too heavy
	// per tick, so its result is cached per objective position for 2 seconds
	Vec3 m_vReachCachePos = Vec3(0, 0, 0);
	float m_flReachCacheUntil = 0.0f;
	bool m_bReachCacheResult = false;

	// Supply search throttle — evaluating each pack costs a pather solve, so
	// searches run at most every 0.5s per kind ([0]=ammo, [1]=health)
	float m_flNextSupplySearchTime[2] = { 0.0f, 0.0f };

	// Per-tick position tracking for instant teleport detection
	Vec3 m_vLastTickPos = Vec3(0, 0, 0);	// Position from previous tick
	bool m_bHasLastTickPos = false;			// Whether m_vLastTickPos has been set

	// Stuck detection (windowed - compares position every N ticks to avoid false positives in water/slow movement)
	Vec3 m_vStuckCheckPos = Vec3(0, 0, 0);	// Position snapshot for windowed stuck check
	int m_nStuckCheckTicks = 0;			// Ticks since last snapshot
	int m_nStuckCount = 0;				// Number of consecutive stuck windows
	int m_nStuckJumpCooldown = 0;			// Cooldown between stuck jumps
	bool m_bStuckWiggle = false;		// Alternate strafe direction when stuck

	// Stuck anchor (Valve StuckMonitor-style, works with and without a nav mesh):
	// re-armed whenever we move; the time since the last re-arm is how long we've
	// been stuck in one spot
	float m_flStuckAnchorTime = 0.0f;
	float m_flNextWiggleToggleTime = 0.0f;	// Throttle for wiggle direction flips

	// === Blind wander (maps without a nav mesh) ===
	float m_flBlindWanderExpireTime = 0.0f;	// Give up the current blind target at this time
	float m_flNextBlindJumpTime = 0.0f;		// Throttle blind-wander stuck jumps

	// Amalgam-style visited areas tracking (prevents revisiting same spots)
	std::vector<Vec3> m_vVisitedAreaCenters;
	float m_flNextVisitedClearTime = 0.0f;
	int m_nConsecutiveFails = 0;

	// === LookAtPath system (ported from Amalgam's BotUtils) ===
	Vec3 m_vLastAngles = Vec3(0, 0, 0);		// Last view angles for smooth interpolation
	float m_flLookSpeed = 6.0f;			// Smooth aim speed

	// === StayNear / Enemy tracking (ported from Unibox's StayNear + BotUtils) ===
	int m_iStayNearTargetIdx = -1;			// Entity index of the player we're stalking
	float m_flNextStayNearCheck = 0.0f;	// Cooldown for StayNear repathing
	bool StayNearTarget(C_TFPlayer* pLocal, int iEntIndex);  // Score nav areas and path to the best stalk spot

	// Stalk scoring state (Unibox StayNear): the persistent flank side so the
	// approach direction doesn't flip every solve
	int m_iStalkFlankTarget = -1;      // Ent index the flank side belongs to
	float m_flStalkFlankSide = 1.0f;   // -1 = left of the target, +1 = right
	float m_flNextStalkSolveTime = 0.0f;     // Stalk re-solve cadence (Unibox: 0.5s / 2s far)

	// Dormant position cache - stores last known positions for dormant players
	struct DormantPos_t {
		Vec3 m_vOrigin = Vec3(0, 0, 0);
		float m_flTime = 0.0f;
		bool m_bValid = false;
	};
	std::unordered_map<int, DormantPos_t> m_tDormantPositions;

	// Closest enemy tracking
	struct ClosestEnemy_t {
		int m_iEntIdx = -1;
		C_TFPlayer* m_pPlayer = nullptr;
		float m_flDist = FLT_MAX;
	};
	ClosestEnemy_t m_tClosestEnemy;

	// Find nearest capturable objective (control point, flag, payload cart)
	bool FindObjective(C_TFPlayer* pLocal, Vec3& vOut);

	// Find nearest health/ammo pack when low (ported from Amalgam's GetSupplies)
	bool FindSupply(C_TFPlayer* pLocal, Vec3& vOut, bool bHealth);

	// Check if we should search for health/ammo (ported from Amalgam's ShouldSearchHealth/ShouldSearchAmmo)
	bool ShouldSearchHealth(C_TFPlayer* pLocal);
	bool ShouldSearchAmmo(C_TFPlayer* pLocal);

	// Pick random nav area as wander target (uses nav mesh if available)
	// Ported from Amalgam's Roam.cpp - avoids spawn rooms, visited areas, nearby areas
	void PickRandomWanderTarget(C_TFPlayer* pLocal);

	// === Goal machine (TF2-bot-style committed goals) ===
	bool PickMainGoal(C_TFPlayer* pLocal);   // TF2 DesiredScenarioAndClassAction
	bool CaptureTick(C_TFPlayer* pLocal);    // TF2 CapturePoint / PayloadPush march
	void WanderTick(C_TFPlayer* pLocal);     // TF2 SeekAndDestroy roam
	bool RunSupplyInterrupt(C_TFPlayer* pLocal, CUserCmd* pCmd);  // TF2 GetHealth/GetAmmo suspend+resume
	void SupplyRelease();
	void BeginInterrupt(EBotGoal eGoal);     // TF2 SuspendFor
	void ResumeFromInterrupt();              // TF2 Done -> OnResume
	void ClearGoal();
	bool HasTaggedTarget(C_TFPlayer* pLocal);   // Light preemption check for Follow tags
	void FollowAndLook(C_TFPlayer* pLocal, CUserCmd* pCmd);  // Shared path-follow + look epilogue
	void MoveAndFinish(C_TFPlayer* pLocal, CUserCmd* pCmd, float flCurTime);  // Full movement epilogue every branch funnels through
	C_TFPlayer* FindFollowTarget(C_TFPlayer* pLocal);  // Throttled teammate selection scan

	// Pick a wander point with local hull probes when the map has NO nav mesh
	// (blind wander): probes 8 directions, rejects blocked paths and cliffs
	Vec3 PickBlindWanderPoint(C_TFPlayer* pLocal);

	// Apply stuck wiggle/unstick movement to the usercmd AFTER path following has
	// written forwardmove/sidemove (otherwise MoveTowards overwrites it)
	void ApplyStuckWiggle(C_TFPlayer* pLocal, CUserCmd* pCmd, float flCurTime);

	// === LookAtPath - smooth view angle control while pathing (ported from Amalgam) ===
	void LookAtPath(C_TFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vTarget);

	// === Sniper Spot - move to navmesh sniper sightline spots (ported from TF2 bot's CTFBotSniperLurk) ===
	bool SniperSpot(C_TFPlayer* pLocal);
	Vec3 m_vSniperSpotPos = Vec3(0, 0, 0);  // Current sniper spot we're pathing to
	float m_flSniperPatienceEnd = 0.0f;   // When our patience at this spot expires (pick new spot)
	float m_flNextSniperSearchTime = 0.0f; // Throttle how often we search for new spots

	// === StayNear - stalk enemy players (ported from Amalgam's StayNear) ===
	bool StayNear(C_TFPlayer* pLocal);

	// === Dormant tracking - get last known position of dormant players ===
	bool GetDormantOrigin(int iEntIndex, Vec3& vOut);

	// === Update closest enemy tracker ===
	void UpdateCloseEnemies(C_TFPlayer* pLocal);

	// === EscapeDanger - flee from enemies/sentries when taking fire (ported from Amalgam) ===
	bool EscapeDanger(C_TFPlayer* pLocal);
	bool EscapeProjectiles(C_TFPlayer* pLocal);
	bool EscapeSpawn(C_TFPlayer* pLocal);
	void UpdateDangerBlacklist(C_TFPlayer* pLocal);
	CNavArea* m_pEscapeTargetArea = nullptr;  // Area we're escaping to
	Vec3 m_vPreEscapeDestination = Vec3(0, 0, 0);  // Where we were going before escaping (stay near objective)
	float m_flEscapeCooldownEnd = 0.0f;  // Prevent Capture from re-pathing through danger immediately after escape

	// Escape upkeep (ported from Unibox EscapeDanger/EscapeProjectiles): a
	// shared 0.35s repath cooldown while an escape path is alive caps the
	// re-solve rate, and the persistent projectile dodge target is validated
	// each tick instead of re-solved.
	float m_flNextEscapeRepathTime = 0.0f;
	CNavArea* m_pProjectileEscapeTargetArea = nullptr;

	// === Defend goal (ported from Unibox's Roam DefendObjectives / RunDefend) ===
	// Hold the nearest IN_COVER spot near the objective the enemy team is
	// pushing (their cart, or the closest point they can take), breaking line
	// of sight to the closest threat. Runs at patrol priority.
	bool FindDefendAnchor(C_TFPlayer* pLocal, Vec3& vOut);
	bool DefendTick(C_TFPlayer* pLocal);
	Vec3 ComputeCaptureSpot(C_TFPlayer* pLocal, const Vec3& vPoint);
	CNavArea* m_pDefendSpotArea = nullptr;   // Cover spot we're holding / walking to
	Vec3 m_vDefendAnchorPos;                 // Anchor the current spot was solved for (cart moved = re-path)
	float m_flNextDefendScanTime = 0.0f;     // Defend re-scan cadence (Unibox roam timer: 0.5s)


	// === Weapon switching (ported from Amalgam's UpdateBestSlot/SetSlot) ===
	void UpdateBestSlot(C_TFPlayer* pLocal);
	void SwitchWeapon(C_TFPlayer* pLocal);
	int m_iBestSlot = -1;
	int m_iCurrentSlot = -1;

	// === Follow Teammates (ported from Amalgam's FollowBot) ===
	bool FollowTeammates(C_TFPlayer* pLocal);
	int m_iFollowTargetIdx = -1;  // Ent index of teammate we're following
	bool m_bIsFollowingTeammate = false;  // Are we currently following a teammate?

	// === Group formation (ported from Unibox's CNavBotGroup) ===
	// Line up behind the earliest-joined teammate (lowest userID) when idle.
	// Position in the column = how many teammates sorted before us; the offset
	// is -forward * spacing * position. If we can't hold our slot 3 times in a
	// row, spacing widens by 50u (up to 300u, then back to 120u).
	void UpdateFormation(C_TFPlayer* pLocal);
	bool GroupTick(C_TFPlayer* pLocal);
	int m_iPositionInFormation = -1;
	float m_flFormationDistance = 120.0f;
	int m_iFormationFailures = 0;
	Vec3 m_vLastFormationTargetPos = Vec3(0, 0, 0);
	float m_flNextFormationScanTime = 0.0f;   // Teammate scan cadence (Unibox: 0.5s)
	float m_flNextFormationNavTime = 0.0f;    // Formation repath cadence (Unibox: 0.5s)
	std::vector<int> m_vFormationUserIds;

	// === Reload retreat (ported from Unibox's CNavBotReload) ===
	// While a ranged slot is mid-reload and the closest enemy is known, back off
	// to a spot that breaks their sightline; also swap TO slots that need reload
	int GetReloadWeaponSlot(C_TFPlayer* pLocal);
	bool RunReloadInterrupt(C_TFPlayer* pLocal);
	int m_iLastReloadSlot = -1;       // Slot that needs reloading (-1 = none)
	float m_flNextReloadSolveTime = 0.0f;  // Retreat re-solve cadence (Unibox: 1s)

	// === Follow Tagged Players (follow players with "Follow Player" tag) ===
	bool FollowTaggedPlayer(C_TFPlayer* pLocal);
	int m_iFollowTaggedTargetIdx = -1;  // Ent index of tagged player we're following
	bool m_bIsFollowingTaggedPlayer = false;  // Are we currently following a tagged player?
	C_BaseEntity* m_pSupplyEntity = nullptr;  // The supply entity we're pathing to (for cancellation checks)
	int m_iSupplyEntityIdx = -1;  // Cached entindex for safe validation without dereferencing dangling pointer
	bool m_bSupplyIsDispenser = false;  // Is the supply entity a dispenser? (needs proximity wait behavior)
	bool m_bWaitingAtDispenser = false;  // Standing near dispenser waiting to be healed/refilled (don't restart path)

	// === Death pause (stop moving after respawn for a configurable duration) ===
	float m_flDeathPauseEndTime = 0.0f;  // Time at which death pause expires
	bool m_bWasDeadLastTick = false;     // Track death state to detect respawn

	// === Auto Join Class (ported from Amalgam's AutoJoin) ===
	void AutoJoinClass(C_TFPlayer* pLocal);
	void AutoJoinTeam(C_TFPlayer* pLocal);
	float m_flNextAutoJoinTeamTime = 0.0f;
	float m_flNextAutoJoinClassTime = 0.0f;
	int m_iLastRandomTeamAttempt = 0;  // Track which team random mode tried last (1=Blue, 2=Red)

	// === Auto Scope (ported from Amalgam's BotUtils::AutoScope) ===
	void AutoScope(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool m_bAutoScopeKeep = false;  // Keep scope active
	float m_flAutoScopeTimer = 0.0f;  // Timer for cancel timeout
	std::unordered_map<int, bool> m_mAutoScopeCache;  // Cached visibility results
	float m_flAutoScopeLastShotTime = 0.0f;  // Time of last scoped shot (for wait-after-shot)
	bool m_bAutoScopeWasScopedLastTick = false;  // Were we scoped last tick?

public:
	// Reset auto-join timers on level change/reconnect
	void ResetAutoJoinTimers();

	// Check if a destination is reachable from local player's position
	// Step 1: Quick check - is there a nav area at/under the destination? If not, skip immediately.
	// Step 2: Full check - is there a connected path from local to destination through nav areas?
	bool IsReachable(C_TFPlayer* pLocal, const Vec3& vDestination);

	// Main update function - call every tick when Active is enabled
	void Run(C_TFPlayer* pLocal, CUserCmd* pCmd);

	// Stop navigation
	void Stop();

	// Check if currently navigating
	bool IsActive() const { return m_bActive; }

	// Debug rendering
	void Draw();
};

inline CNavBot g_NavBot;
