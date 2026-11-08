#include "NavEngine.h"
#include "../../../../SDK/SDK.h"
#include "../../CFG.h"
#include "../../amalgam_port/AmalgamCompat.h"

#undef min
#undef max
#include <algorithm>
#include <string>
#include <unordered_set>

bool CNavEngine::Init(const char* szMapName)
{
	if (!szMapName || !szMapName[0])
	{
		return false;
	}

	// GetLevelName() returns "maps/mapname.bsp", we need to convert to full path to .nav file
	// Example: "maps/cp_dustbowl.bsp" -> "tf/maps/cp_dustbowl.nav"
	std::string sMapPath(szMapName);
	
	
	// Remove "maps/" prefix if present
	size_t mapsPos = sMapPath.find("maps/");
	if (mapsPos != std::string::npos)
		sMapPath = sMapPath.substr(mapsPos + 5); // Skip "maps/"
	
	// Replace .bsp with .nav
	size_t bspPos = sMapPath.find(".bsp");
	if (bspPos != std::string::npos)
		sMapPath = sMapPath.substr(0, bspPos) + ".nav";
	
	// Construct full path: "tf/maps/mapname.nav"
	std::string sNavPath = "tf/maps/" + sMapPath;
	

	// Create map instance with nav file
	m_pMap = std::make_unique<CMap>(sNavPath.c_str());
	
	if (!m_pMap || m_pMap->m_eState != NavState::Active)
	{
		if (m_pMap)
		m_pMap.reset();
		return false;
	}

	m_sLastMapName = sMapPath;
	return true;
}

bool CNavEngine::NavTo(const Vec3& vDestination, ENavPriority ePriority, bool bShouldRepath)
{
	if (!m_pMap || m_pMap->m_eState != NavState::Active)
	{
		return false;
	}

	float flCurTime = I::GlobalVars->curtime;

	// Solve rate limiting: a solve whose destination can't be reached costs a full
	// mesh search, and multiple jobs retry NavTo every tick while pathless — that
	// turns into back-to-back exhaustive searches (the "lag at spawn" report).
	// Enforce a floor between any two solve attempts, no matter who is asking.
	if (flCurTime < m_flNextSolveTime)
		return false;
	m_flNextSolveTime = flCurTime + 0.2f;

	// Failed-destination backoff: if the last solve to this destination found no
	// path, don't re-run the (exhaustive) search until the backoff expires.
	if (flCurTime < m_flFailedPathUntil && vDestination.DistTo(m_vFailedDest) < 100.0f)
		return false;

	// Minimum repath interval — prevents NavTo from being called every tick
	// which causes rapid route switching and oscillation.
	// Allow immediate pathing if: no current path, higher priority, or enough time elapsed.
	float flMinRepathInterval = 0.3f;  // Minimum 300ms between repaths to same destination
	bool bIsHigherPriority = ePriority > m_eCurrentPriority;
	bool bIsNewDest = vDestination.DistTo(m_vDestination) > 50.0f;
	bool bNoCurrentPath = !IsPathing();
	if (!bIsHigherPriority && !bIsNewDest && !bNoCurrentPath && (flCurTime - m_flLastPathTime) < flMinRepathInterval)
		return false;

	// Route variety cache invalidation (ported from TF2 bot's CTFBotPathCost)
	// MicroPather caches solved paths, so a fresh solve would return the old route
	// regardless of the shifted per-area preference costs. Reset the cache only when
	// we actually want a NEW route (new destination) — never on a timer, so an
	// actively followed path stays stable.
	if (CFG::NavBot_RouteVariety)
	{
		if (bIsNewDest && !m_bRouteVarietyResetForDest)
		{
			m_pMap->m_pather.Reset();
			m_bRouteVarietyResetForDest = true;
		}
		else if (!bIsNewDest)
		{
			m_bRouteVarietyResetForDest = false;
		}
	}

	// Don't path if priority is too low (like Amalgam)
	if (ePriority < m_eCurrentPriority)
	{
		return false;
	}

	// Find closest nav area to destination
	CNavArea* pDestArea = m_pMap->FindClosestNavArea(vDestination);
	if (!pDestArea)
	{
		NavDebug("NavTo fail: no nav area near dest (%.0f %.0f %.0f)", vDestination.x, vDestination.y, vDestination.z);
		return false;
	}

	// Validate that the destination is actually on a nav area.
	// FindClosestNavArea can return a fallback area that's just the closest center,
	// but the destination itself might not be on any nav area at all.
	// If the destination isn't overlapping its closest nav area, it's in non-navmesh
	// space — don't path there. "If the road is dark, don't drive where you can't see."
	// EXCEPT: teammates routinely stand on props, crates or ledges just off the
	// mesh (the follow goal spammed "dest off-mesh" forever for those). When the
	// destination is near a nav area, path to the nearest walkable point of that
	// area instead of refusing. vDest carries the (possibly snapped) destination
	// through the rest of this function.
	Vec3 vDest = vDestination;
	{
		bool bDestOverlapping = pDestArea->IsOverlapping(vDest) &&
		                        vDest.z >= pDestArea->m_flMinZ && vDest.z <= pDestArea->m_flMaxZ + 100.0f;
		if (!bDestOverlapping)
		{
			Vec3 vSnapped = pDestArea->GetNearestPoint(Vector2D(vDest.x, vDest.y));
			if (vDest.DistTo(vSnapped) > 150.0f)
			{
				// Destination is in non-navmesh space, too far from any area — cancel
				NavDebug("NavTo fail: dest off-mesh (%.0f %.0f %.0f)", vDestination.x, vDestination.y, vDestination.z);
				return false;
			}
			vDest = vSnapped;
		}
	}

	// Get local player to find starting area
	auto pLocal = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	if (!pLocal)
	{
		return false;
	}

	auto pLocalPlayer = pLocal->As<C_TFPlayer>();
	if (!pLocalPlayer)
	{
		return false;
	}

	Vec3 vLocalPos = pLocalPlayer->m_vecOrigin();

	// Find a starting nav area that we can actually reach. The nearest area by
	// center distance can sit on the other side of a wall (the bot then drives at
	// the first crumb through that wall forever), so try the best few candidates
	// and require a hull-trace-clean path to the area center when we're not
	// overlapping it. Also skip graph islands: an area with no usable outbound
	// connections (spawn rooms are full of them) makes every solve return
	// "no path" no matter the destination — prefer a connected overlapping area
	// and only fall back to the island if nothing connected is available.
	CNavArea* pLocalArea = nullptr;
	CNavArea* pOverlapFallback = nullptr;
	{
		auto vStartCandidates = m_pMap->FindClosestNavAreas(vLocalPos, 5);
		for (CNavArea* pCandidate : vStartCandidates)
		{
			if (!pCandidate)
				continue;

			bool bIsOverlapping = pCandidate->IsOverlapping(vLocalPos) &&
			                      vLocalPos.z >= pCandidate->m_flMinZ && vLocalPos.z <= pCandidate->m_flMaxZ + 100.0f;
			if (bIsOverlapping)
			{
				if (!pOverlapFallback)
					pOverlapFallback = pCandidate;
				if (m_pMap->HasUsableOutgoing(pCandidate))
				{
					pLocalArea = pCandidate;
					break;
				}
				continue;
			}

			// Not overlapping: allow a 200-unit tolerance for being slightly off the
			// mesh edge, but verify direct reachability with a hull trace so we never
			// start a path from an area behind a wall.
			Vec3 vDistToArea = pCandidate->m_vCenter - vLocalPos;
			vDistToArea.z = 0.0f;
			if (vDistToArea.Length() > 200.0f)
				continue;

			if (!m_pMap->HasUsableOutgoing(pCandidate))
				continue;

			const Vec3 vHullMin(-NAV_HULL_HALF_WIDTH, -NAV_HULL_HALF_WIDTH, 0.0f);
			const Vec3 vHullMax(NAV_HULL_HALF_WIDTH, NAV_HULL_HALF_WIDTH, 62.0f);
			const Vec3 vStepHeight(0.0f, 0.0f, 18.0f);
			CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
			CGameTrace trace = {};
			H::AimUtils->TraceHull(vLocalPos + vStepHeight, pCandidate->m_vCenter + vStepHeight, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);

			if (trace.fraction >= 0.98f || trace.startsolid)
			{
				pLocalArea = pCandidate;
				break;
			}
		}

		// Everything connected was rejected — accept the overlapping island as a
		// last resort (solves will fail, but EscapeSpawn's fallbacks still run).
		if (!pLocalArea)
			pLocalArea = pOverlapFallback;

		if (!pLocalArea)
		{
			NavDebug("NavTo fail: no start area at (%.0f %.0f %.0f)", vLocalPos.x, vLocalPos.y, vLocalPos.z);
			return false;
		}
	}


	// Find path using MicroPather
	int iResult = 0;
	std::vector<void*> vAreaPath = m_pMap->FindPath(pLocalArea, pDestArea, &iResult);

	// Handle single-area path (start and end in same area)
	if (vAreaPath.empty() && iResult == micropather::MicroPather::START_END_SAME)
	{
		// Validate that the direct path is actually passable with a hull trace.
		// A large nav area can span across a wall/obstacle, so a straight-line path
		// within the same area can still cross walls.
		const Vec3 vHullMin(-NAV_HULL_HALF_WIDTH, -NAV_HULL_HALF_WIDTH, 0.0f);
		const Vec3 vHullMax(NAV_HULL_HALF_WIDTH, NAV_HULL_HALF_WIDTH, 72.0f);
		const Vec3 vStepHeight(0.0f, 0.0f, 18.0f);
		CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
		CGameTrace trace = {};
		H::AimUtils->TraceHull(vLocalPos + vStepHeight, vDest + vStepHeight, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);

		if (trace.fraction >= 1.0f || trace.startsolid)
		{
			// Path is clear (or startsolid which means we're inside geometry, just proceed)
			m_vCrumbs.clear();
			Crumb_t tStartCrumb = {};
			tStartCrumb.m_vPos = vLocalPos;
			tStartCrumb.m_pNavArea = pLocalArea;
			m_vCrumbs.push_back(tStartCrumb);
			Crumb_t tEndCrumb = {};
			tEndCrumb.m_vPos = vDest;
			tEndCrumb.m_pNavArea = pLocalArea;
			m_vCrumbs.push_back(tEndCrumb);
			m_vDestination = vDest;
			m_vLastDestination = vDest;
			m_nCurrentCrumbIndex = 0;
			m_eCurrentPriority = ePriority;
			m_bRepathOnFail = bShouldRepath;
			m_flLastPathTime = flCurTime;
			m_flFailBackoff = 1.0f;
			m_flApproachBlockSince = 0.0f;
			m_pLastSolveStartArea = pLocalArea;   // stuck marks key on this solve's own world
			return true;
		}

		// Direct path is blocked by a wall within the same area.
		// Try to find a multi-area path by resetting the pather and searching again.
		m_pMap->m_pather.Reset();
		m_pMap->ClearTransientConnBlocks();
		vAreaPath = m_pMap->FindPath(pLocalArea, pDestArea, &iResult);
		if (vAreaPath.empty())
		{
			// Failure key uses the RAW caller destination: the backoff matcher
			// compares against the next call's raw destination, and a snapped
			// dest can sit up to 150 units away — beyond the 100-unit match
			// radius, which would defeat the backoff and re-solve every 0.2s.
			m_vFailedDest = vDestination;
			m_flFailedPathUntil = flCurTime + m_flFailBackoff;
			m_flFailBackoff = (std::min)(m_flFailBackoff * 2.0f, 5.0f);
			return false;
		}
	}

	// No path found. MicroPather caches NO_SOLUTION per (start,end) pair and
	// serves it forever after that — a solve that failed for a transient
	// reason (setup gates closed at the time, a capture door still shut)
	// would stay failed for the whole match even after the world opened up,
	// freezing the bot until something moved it onto a different start area.
	// Reset the pather (clears the path cache AND the node pool) and re-solve
	// once with current costs; only a failure that reproduces from a clean
	// state is reported and backed off.
	if (vAreaPath.empty())
	{
		// Ported from Unibox's RetryFailure: a solve that fails outright may have
		// failed BECAUSE of transient corridor blocks (false positives that sealed
		// the only route). Clear the non-stuck blocks, reset the pather, and
		// re-solve once with current state before reporting failure.
		m_pMap->m_pather.Reset();
		m_pMap->ClearTransientConnBlocks();
		vAreaPath = m_pMap->FindPath(pLocalArea, pDestArea, &iResult);
	}

	if (vAreaPath.empty())
	{
		NavDebug("NavTo fail: no path %u -> %u (dest %.0f %.0f %.0f)",
			pLocalArea->m_uId, pDestArea->m_uId, vDestination.x, vDestination.y, vDestination.z);
		m_vFailedDest = vDestination;
		m_flFailedPathUntil = flCurTime + m_flFailBackoff;
		m_flFailBackoff = (std::min)(m_flFailBackoff * 2.0f, 5.0f);
		return false;
	}

	// If this is a new destination (not a repath to same place), reset vischeck failure tracking
	bool bIsNewDestination = vDest.DistTo(m_vDestination) > 1.0f || m_eCurrentPriority == NAV_PRIO_NONE;

	// Set destination BEFORE building path so BuildWorldPath can append it correctly
	m_vDestination = vDest;
	m_vLastDestination = vDest;
	m_bRepathOnFail = bShouldRepath;

	if (bIsNewDestination)
	{
		m_nVischeckFailCount = 0;
		m_flVischeckCooldownUntil = 0.0f;
		m_flFailBackoff = 1.0f;
	}

	// Convert area path to crumbs (TF2-style edge point navigation)
	BuildWorldPath(vAreaPath);

	// Insert a start crumb at the player's actual position.
	// The first edge crumb can be ahead of the bot but in a different direction
	// than the bot is facing — without a start crumb, the bot might walk
	// backward or sideways to reach it. The start crumb ensures smooth
	// departure from the current position.
	if (!m_vCrumbs.empty())
	{
		Crumb_t tStartCrumb = {};
		tStartCrumb.m_vPos = vLocalPos;
		tStartCrumb.m_pNavArea = m_vCrumbs[0].m_pNavArea;  // Same area as first edge crumb
		m_vCrumbs.insert(m_vCrumbs.begin(), tStartCrumb);
	}

	// Skip crumbs that duplicate the bot's exact position (the inserted start
	// crumb can coincide with the first edge crumb on short paths). The loop that
	// used to live here also erased every waypoint behind the bot's VIEW angle —
	// at spawn the bot commonly faces a wall with the exit behind it, so the
	// whole out-of-spawn path got erased and the bot pushed the wall it stared
	// at. Where to walk is a property of the path, not of the view.
	while (m_vCrumbs.size() >= 2 && (m_vCrumbs[1].m_vPos - vLocalPos).Length2D() < 1.0f)
		m_vCrumbs.erase(m_vCrumbs.begin());

	// === Corridor validation of the solved path (ported from Unibox's
	// StoreValidatedCrumbs) ===
	// The solver only sees the nav mesh — a real wall can still stand between
	// two edge crumbs (mesh errors, props, large areas spanning geometry). But
	// like Unibox, DIRECT mesh connections are trusted and never traced: doors,
	// props and tight doorways legitimately intersect player-width corridor
	// tests, and trace-rejecting them freezes the bot at spawn. Every solver
	// edge IS a direct connection, so in practice nothing here gets traced —
	// walls that the mesh claims but reality denies are caught at runtime by
	// the progress sampler + stuck marks instead.
	{
		bool bCorridorBlocked = false;
		for (size_t i = 0; i + 1 < m_vCrumbs.size(); ++i)
		{
			const Crumb_t& tA = m_vCrumbs[i];
			const Crumb_t& tB = m_vCrumbs[i + 1];
			if (tA.m_bRequiresDrop || tB.m_bRequiresDrop)
				continue;   // drop transitions jump floors — the corridor test is planar
			if (!tA.m_pNavArea || !tB.m_pNavArea || tA.m_pNavArea == tB.m_pNavArea)
				continue;   // same-area hops (start crumb, subdivided mids) carry no connection
			if (m_pMap->HasDirectConnection(tA.m_pNavArea, tB.m_pNavArea))
				continue;   // the mesh offers this link — trust it (Unibox: StoreValidatedCrumbs)

			bool bCachedPassable = false;
			if (m_pMap->GetConnVerdict(tA.m_pNavArea, tB.m_pNavArea, bCachedPassable))
			{
				if (bCachedPassable)
					continue;
				bCorridorBlocked = true;
				break;
			}

			const bool bPassable = IsPlayerPassableNavigation(pLocalPlayer, tA.m_vPos, tB.m_vPos);
			// Passable verdicts live 30s (repaths constantly re-solve the same
			// route); blocked ones only 10s so moved props / opened doors recover.
			m_pMap->MarkConn(tA.m_pNavArea, tB.m_pNavArea, bPassable, bPassable ? 30.0f : 10.0f);
			if (!bPassable)
			{
				bCorridorBlocked = true;
				NavDebug("NavTo reject: corridor blocked %u -> %u on path to (%.0f %.0f %.0f)",
					tA.m_pNavArea->m_uId, tB.m_pNavArea->m_uId, vDest.x, vDest.y, vDest.z);
				break;
			}
		}

		if (bCorridorBlocked)
		{
			m_vCrumbs.clear();
			m_nCurrentCrumbIndex = 0;
			m_bApproachingDestination = false;
			m_pMap->m_pather.Reset();   // rebuild the next solve around the blocked link
			m_vFailedDest = vDestination;
			m_flFailedPathUntil = flCurTime + m_flFailBackoff;
			m_flFailBackoff = (std::min)(m_flFailBackoff * 2.0f, 5.0f);
			return false;
		}
	}

	ResetStuckProgress();

	m_nCurrentCrumbIndex = 0;
	m_eCurrentPriority = ePriority;
	m_flLastPathTime = flCurTime;
	m_flFailBackoff = 1.0f;
	m_flApproachBlockSince = 0.0f;
	m_pLastSolveStartArea = pLocalArea;   // stuck marks key on this solve's own world

	NavDebug("NavTo ok: prio %d, %zu crumbs -> (%.0f %.0f %.0f)",
		static_cast<int>(ePriority), m_vCrumbs.size(), vDest.x, vDest.y, vDest.z);

	return !m_vCrumbs.empty();
}

void CNavEngine::BuildWorldPath(const std::vector<void*>& vAreaPath)
{
	m_vCrumbs.clear();

	if (vAreaPath.empty())
		return;

	// Build crumbs for TF2-style path following.
	// Unlike Amalgam which uses area centers as waypoints, the TF2 bot navigates
	// through edge connection points. Area centers can be in spots that require
	// walking through walls for large/irregular areas, causing the bot to walk
	// on non-navmeshed space. Edge points are where the bot actually needs to turn.
	//
	// For each area pair, we create one crumb at the connection edge point
	// (with dropdown/approach metadata). The last area gets the destination.
	for (size_t i = 0; i < vAreaPath.size(); i++)
	{
		CNavArea* pArea = reinterpret_cast<CNavArea*>(vAreaPath[i]);
		if (!pArea)
			continue;

		// All entries besides the last need a crumb at the connection edge
		if (i != vAreaPath.size() - 1)
		{
			CNavArea* pNextArea = reinterpret_cast<CNavArea*>(vAreaPath[i + 1]);
			if (!pNextArea)
			{
				Crumb_t tFallback = {};
				tFallback.m_vPos = pArea->m_vCenter;
				tFallback.m_pNavArea = pArea;
				m_vCrumbs.push_back(tFallback);
				continue;
			}

			bool bIsOneWay = m_pMap->IsOneWay(pArea, pNextArea);
			NavPoints_t tPoints = m_pMap->DeterminePoints(pArea, pNextArea, bIsOneWay);
			DropdownHint_t tDropdown = m_pMap->HandleDropdown(tPoints.m_vCenter, tPoints.m_vNext, bIsOneWay);
			tPoints.m_vCenter = tDropdown.m_vAdjustedPos;

			// Single crumb at the edge connection point (like TF2 bot's path waypoints)
			// This is where the bot needs to actually turn — not the area center.
			Crumb_t tEdgeCrumb = {};
			tEdgeCrumb.m_pNavArea = pArea;
			tEdgeCrumb.m_vPos = tPoints.m_vCenter;
			tEdgeCrumb.m_bRequiresDrop = tDropdown.m_bRequiresDrop;
			tEdgeCrumb.m_flDropHeight = tDropdown.m_flDropHeight;
			tEdgeCrumb.m_flApproachDistance = tDropdown.m_flApproachDistance;
			tEdgeCrumb.m_vApproachDir = tDropdown.m_vApproachDir;
			m_vCrumbs.push_back(tEdgeCrumb);
		}
		else
		{
			// tEndCrumb: destination (like Amalgam's tEndCrumb)
			Crumb_t tEndCrumb = {};
			tEndCrumb.m_pNavArea = pArea;
			tEndCrumb.m_vPos = m_vDestination;
			m_vCrumbs.push_back(tEndCrumb);
		}
	}

	// Remove consecutive duplicate crumbs (same position)
	for (size_t i = 1; i < m_vCrumbs.size(); )
	{
		if ((m_vCrumbs[i].m_vPos - m_vCrumbs[i - 1].m_vPos).Length2D() < 1.0f)
			m_vCrumbs.erase(m_vCrumbs.begin() + i);
		else
			i++;
	}

	// Subdivide long legs into ~200u crumbs (Unibox's AppendAreaSegment idea).
	// One crumb per area edge means a path across a few large areas sends the
	// bot beeline for hundreds of units at the next crumb with only whisker
	// steering — the "walking at one far waypoint" behavior. Intermediate
	// crumbs give the look-ahead skip, vischeck segments and corner-speed
	// logic real geometry to work with. Drop transitions are never subdivided
	// (their approach metadata covers a short push-past-the-edge move).
	if (m_vCrumbs.size() >= 2)
	{
		std::vector<Crumb_t> vDense;
		vDense.reserve(m_vCrumbs.size() + 8);
		for (size_t i = 0; i < m_vCrumbs.size(); i++)
		{
			vDense.push_back(m_vCrumbs[i]);
			if (i + 1 >= m_vCrumbs.size())
				break;

			const Crumb_t& tNext = m_vCrumbs[i + 1];
			if (m_vCrumbs[i].m_bRequiresDrop || tNext.m_bRequiresDrop)
				continue;

			Vec3 vLeg = tNext.m_vPos - m_vCrumbs[i].m_vPos;
			vLeg.z = 0.0f;
			const float flLen = vLeg.Length();
			if (flLen <= 250.0f)
				continue;

			const int nExtra = static_cast<int>(flLen / 200.0f);
			for (int k = 1; k <= nExtra; k++)
			{
				const float t = static_cast<float>(k) / static_cast<float>(nExtra + 1);
				Crumb_t tMid = {};
				tMid.m_vPos = m_vCrumbs[i].m_vPos + (tNext.m_vPos - m_vCrumbs[i].m_vPos) * t;
				tMid.m_pNavArea = m_vCrumbs[i].m_pNavArea;
				vDense.push_back(tMid);
			}
		}
		m_vCrumbs = std::move(vDense);
	}
}

void CNavEngine::FollowPath(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal || !pCmd || m_vCrumbs.empty())
		return;

	// Update respawn room flags once per map load (ported from Amalgam)
	if (!m_bUpdatedRespawnRooms)
		UpdateRespawnRooms();

	// Check if we've reached the end
	if (m_nCurrentCrumbIndex >= m_vCrumbs.size())
	{
		CancelPath();
		return;
	}

	Vec3 vLocalPos = pLocal->m_vecOrigin();
	float flCurTime = I::GlobalVars->curtime;

	// Deliberately NO velocity reset of m_flInactivityTime here. A bot grinding
	// a wall at an angle slides along it well above 40 u/s — velocity is not
	// progress. Unibox's follower has no velocity check anywhere: its stuck
	// clock advances on geometric progress only (the sampler in
	// UpdateStuckTime, plus crumb advancement below). A velocity>40 reset here
	// kept flStuckTime pinned near zero during wall-grinds, so the wall probe
	// and connection blacklist never fired and the bot never rerouted.

	// Track on-ground state for dropdown nudge logic
	bool bOnGround = (pLocal->m_fFlags() & FL_ONGROUND) != 0;

	// === Stairs detection (ported from TF2 bot's PathFollower) ===
	// The TF2 bot checks NAV_MESH_STAIRS on both the bot's area and the goal area.
	// On stairs, the physics engine handles step-ups automatically — the path follower
	// just walks forward and doesn't abandon/skip crumbs due to height differences.
	bool bOnStairs = false;
	{
		// Cache the local nav area query — FindClosestNavArea can return different areas
		// on consecutive frames when the bot is near area boundaries, causing oscillation.
		// Only re-query every 200ms or when moved more than 50 units.
		CNavArea* pLocalArea = m_pCachedLocalArea;
		if (!pLocalArea || (flCurTime - m_flCachedLocalAreaTime) > 0.2f)
		{
			pLocalArea = m_pMap->FindClosestNavArea(vLocalPos);
			m_pCachedLocalArea = pLocalArea;
			m_flCachedLocalAreaTime = flCurTime;
		}
		if (pLocalArea && (pLocalArea->m_iAttributeFlags & NAV_MESH_STAIRS))
			bOnStairs = true;
		// Check current crumb's nav area (like TF2 bot's PathFollower checks goal area)
		Crumb_t& tActiveCrumb0 = m_vCrumbs[m_nCurrentCrumbIndex];
		if (tActiveCrumb0.m_pNavArea && (tActiveCrumb0.m_pNavArea->m_iAttributeFlags & NAV_MESH_STAIRS))
			bOnStairs = true;
		// Also check next crumb's nav area for stair transitions (like TF2 bot checks upcoming segments)
		if (m_nCurrentCrumbIndex + 1 < m_vCrumbs.size())
		{
			Crumb_t& tNextCrumb0 = m_vCrumbs[m_nCurrentCrumbIndex + 1];
			if (tNextCrumb0.m_pNavArea && (tNextCrumb0.m_pNavArea->m_iAttributeFlags & NAV_MESH_STAIRS))
				bOnStairs = true;
		}
	}

	// === Height deviation check ===
	// Ported from TF2 bot's PathFollower::Update: on stairs, the goal can be higher
	// than the bot's jump height, but the bot should still walk toward it because
	// the physics engine handles step-ups. Don't abandon the path on stairs.
	if (!bOnStairs)
	{
		Crumb_t& tActive = m_vCrumbs[m_nCurrentCrumbIndex];
		float flHeightDiff = std::abs(vLocalPos.z - tActive.m_vPos.z);
		if (flHeightDiff > 100.0f)
		{
			bool bNextCrumbAlsoWrongHeight = true;
			if (m_nCurrentCrumbIndex + 1 < m_vCrumbs.size())
			{
				float flNextHeightDiff = std::abs(vLocalPos.z - m_vCrumbs[m_nCurrentCrumbIndex + 1].m_vPos.z);
				if (flNextHeightDiff <= 100.0f)
					bNextCrumbAlsoWrongHeight = false;
			}

			if (bNextCrumbAlsoWrongHeight)
			{
				AbandonPath("Height deviation (wrong floor)");
				return;
			}
		}
	}

	// === Crumb advancement (ported from Amalgam's FollowCrumbs) ===
	// Amalgam uses reach radii: kDefaultReachRadius=50, kDropReachRadius=28
	constexpr float kDefaultReachRadius = 50.f;
	constexpr float kDropReachRadius = 28.f;

	// Bounds look-ahead skips per tick (each skip costs a hull trace)
	int iLookaheadSkips = 0;
	const Vec3 vLookaheadStep(0.0f, 0.0f, 18.0f);

	while (m_nCurrentCrumbIndex < m_vCrumbs.size())
	{
		Crumb_t& tActive = m_vCrumbs[m_nCurrentCrumbIndex];
		Vec3 vDelta = tActive.m_vPos - vLocalPos;
		float flHeightDiff = std::abs(vDelta.z);
		vDelta.z = 0.0f;
		float flDist = vDelta.Length();

		// Use smaller reach radius for drop crumbs (like Amalgam)
		float flReachRadius = tActive.m_bRequiresDrop ? kDropReachRadius : kDefaultReachRadius;

		// Strategy 1: Close enough to current crumb — advance
		// On stairs, relax height check — the physics engine handles step-ups
		// (ported from TF2 bot's PathFollower which doesn't skip ahead on uphill)
		float flHeightTolerance = bOnStairs ? 200.0f : 72.0f;
		if (flDist < flReachRadius && flHeightDiff < flHeightTolerance)
		{
			// === Dropdown edge nudge ===
			// When a crumb requires a drop, the navmesh may not cover the very edge
			// of the structure. The bot reaches the crumb position but hasn't actually
			// fallen because it's not at the real edge. Don't advance — instead nudge
			// forward incrementally until we fall off.
			// Skip this check on stairs (gradual height change is normal on stairs).
			if (tActive.m_bRequiresDrop && bOnGround && !bOnStairs)
			{
				// Check if we've actually dropped — our Z should be lower than the crumb's Z
				float flDropProgress = tActive.m_vPos.z - vLocalPos.z;
				if (flDropProgress < 18.0f) // Haven't dropped at least step height
				{
					// We're at the drop point but haven't fallen — the navmesh edge
					// doesn't match the real geometry edge. Activate nudge mode.
					if (!m_bDropNudgeActive)
					{
						m_bDropNudgeActive = true;
						m_flDropNudgeStartTime = flCurTime;
						m_nDropNudgeAttempts = 0;
						m_flDropNudgeExtraForward = 15.0f; // Start with 15 unit nudge

						// Use the crumb's approach direction, or fall back to direction to next crumb
						m_vDropNudgeDir = tActive.m_vApproachDir;
						m_vDropNudgeDir.z = 0.0f;
						if (m_vDropNudgeDir.Length() < 0.01f && m_nCurrentCrumbIndex + 1 < m_vCrumbs.size())
						{
							m_vDropNudgeDir = m_vCrumbs[m_nCurrentCrumbIndex + 1].m_vPos - tActive.m_vPos;
							m_vDropNudgeDir.z = 0.0f;
							float flDirLen = m_vDropNudgeDir.Length();
							if (flDirLen > 0.01f) m_vDropNudgeDir /= flDirLen;
						}
					}
					// Don't advance past this crumb — let the nudge logic walk us forward
					break;
				}
			}

			// Normal advancement (or we've successfully dropped)
			m_nCurrentCrumbIndex++;
			m_flInactivityTime = flCurTime;
			m_bDropNudgeActive = false;
			m_nDropNudgeAttempts = 0;
			m_flDropNudgeExtraForward = 0.0f;
			continue;
		}

		// Strategy 2: Lookahead — close to next crumb (skip current)
		if (m_nCurrentCrumbIndex + 1 < m_vCrumbs.size())
		{
			Crumb_t& tNext = m_vCrumbs[m_nCurrentCrumbIndex + 1];
			Vec3 vNextDelta = tNext.m_vPos - vLocalPos;
			float flNextHeightDiff = std::abs(vNextDelta.z);
			vNextDelta.z = 0.0f;
			float flNextDist = vNextDelta.Length();

			float flNextReach = tNext.m_bRequiresDrop ? kDropReachRadius : kDefaultReachRadius;

			float flNextHeightTolerance = bOnStairs ? 200.0f : 72.0f;
			if (flNextDist < flNextReach && flNextHeightDiff < flNextHeightTolerance)
			{
				m_nCurrentCrumbIndex++;  // Skip current, land on next (was += 2 for old 2-crumb system)
				m_flInactivityTime = flCurTime;
				continue;
			}

			// Strategy 3: Behind-check — current crumb is behind us along the path
			// If we're past a waypoint (closer to the next one) and the current one is
			// behind us, skip it immediately. Don't walk backward to a waypoint we already passed.
			Vec3 vPathDir = tNext.m_vPos - tActive.m_vPos;
			vPathDir.z = 0.0f;
			float flPathLen = vPathDir.Length();
			if (flPathLen > 1.0f)
			{
				vPathDir /= flPathLen;
				Vec3 vToCrumb = vDelta;
				float flToCrumbLen = vDelta.Length();
				if (flToCrumbLen > 1.0f)
				{
					vToCrumb /= flToCrumbLen;
					float flDot = vPathDir.Dot(vToCrumb);
					// On stairs, allow behind-check even with height differences
					float flBehindHeightTolerance = bOnStairs ? 200.0f : 72.0f;
					// If we're closer to the next crumb than the current crumb,
					// and the current crumb is even slightly behind us, skip it current one.
					// This prevents the bot from walking backward to a waypoint it already passed.
					// Same-area only: this skip is verified by no trace, and where the path
					// bends hard around a wall corner the bot grinds, sliding along the wall
					// makes boundary crumbs read as "behind, next is closer" — the index then
					// crept across area transitions the bot never physically crossed, and the
					// stuck sampler credited failures to connections the solve never used.
					// Crossing into a new area still happens by reaching the boundary crumb
					// (reach radius) or through the hull-traced look-ahead skip below.
					const bool bSameAreaSkip = !tNext.m_pNavArea || !tActive.m_pNavArea
						|| tNext.m_pNavArea == tActive.m_pNavArea;
					bool bNextIsCloser = flNextDist < flDist;
					if (bSameAreaSkip && bNextIsCloser && flDot < -0.1f && flHeightDiff < flBehindHeightTolerance)
					{
						m_nCurrentCrumbIndex++;
						m_flInactivityTime = flCurTime;
						continue;
					}
				}
			}
		}

		// Strategy 4: Verified look-ahead (concept from the TF2 bot's
		// PathFollower::CheckProgress segment skipping). Skip redundant crumbs we
		// can walk straight past — but only when a hull trace confirms the direct
		// line is clear and the crumb isn't uphill, so we never cut corners
		// through walls. This straightens out paths that hug a wall just because
		// many small nav areas chain along it.
		if (CFG::NavBot_PathLookAhead > 1.0f && bOnGround && !m_bDropNudgeActive && iLookaheadSkips < 3)
		{
			size_t uSkipTo = 0;
			for (size_t j = m_vCrumbs.size() - 1; j > m_nCurrentCrumbIndex; j--)
			{
				const Crumb_t& tSkip = m_vCrumbs[j];
				Vec3 vSkipDelta = tSkip.m_vPos - vLocalPos;
				float flSkipHeight = vSkipDelta.z;
				vSkipDelta.z = 0.0f;
				float flSkipDist = vSkipDelta.Length();
				if (flSkipDist > CFG::NavBot_PathLookAhead || flSkipDist < 1.0f)
					continue;
				// Never skip drop crumbs or the final destination crumb
				if (tSkip.m_bRequiresDrop || j == m_vCrumbs.size() - 1)
					continue;
				// Not uphill beyond step height, not below a cliff-sized drop
				if (flSkipHeight > 18.0f || flSkipHeight < -72.0f)
					continue;

				const Vec3 vHullMinLA(-NAV_HULL_HALF_WIDTH, -NAV_HULL_HALF_WIDTH, 0.0f);
				const Vec3 vHullMaxLA(NAV_HULL_HALF_WIDTH, NAV_HULL_HALF_WIDTH, 62.0f);
				CTraceFilterWorldAndPropsOnlyAmalgam filterLA = {};
				CGameTrace traceLA = {};
				H::AimUtils->TraceHull(vLocalPos + vLookaheadStep, tSkip.m_vPos + vLookaheadStep, vHullMinLA, vHullMaxLA, MASK_PLAYERSOLID_BRUSHONLY, &filterLA, &traceLA);

				// startsolid means the probe BEGAN inside geometry — the bot is
				// hugging the wall it's about to be steered along. The old code
				// treated that as "clear" and skipped to the far crumb THROUGH the
				// wall. Embedded start = can't verify the line = don't skip.
				if (traceLA.fraction >= 0.98f && !traceLA.startsolid)
				{
					uSkipTo = j;
					break;
				}
			}

			if (uSkipTo > m_nCurrentCrumbIndex)
			{
				m_nCurrentCrumbIndex = uSkipTo;
				m_flInactivityTime = flCurTime;
				iLookaheadSkips++;
				continue;
			}
		}

		break;
	}

	// Check if we've reached the end after advancing
	if (m_nCurrentCrumbIndex >= m_vCrumbs.size())
	{
		// If we're still far from the actual destination, keep walking toward it.
		// This prevents a path-complete-cancel-repath cycle where the bot reaches
		// the last crumb but hasn't touched the supply entity yet.
		float flDistToDest = (vLocalPos - m_vDestination).Length2D();
		if (flDistToDest > 30.0f)
		{
			// Set approaching flag so IsPathing() returns true — prevents
			// NavBot from re-calling NavTo every tick during this phase
			m_bApproachingDestination = true;
			Vec3 vMoveTarget = m_vDestination;
			vMoveTarget.z = vLocalPos.z;

			// Whisker steering here too — the final stretch to a supply entity
			// often runs along a wall corner the crumbs didn't hug
			if (CFG::NavBot_AvoidObstacles && bOnGround)
			{
				Vec3 vSteerDir = vMoveTarget - vLocalPos;
				vSteerDir.z = 0.0f;
				float flSteerLen = vSteerDir.Length();
				if (flSteerLen > 1.0f)
				{
					vSteerDir /= flSteerLen;
					Vec3 vLeftSteer(-vSteerDir.y, vSteerDir.x, 0.0f);
					vMoveTarget = AvoidObstacles(pLocal, vMoveTarget, vSteerDir, vLeftSteer);
					vMoveTarget.z = vLocalPos.z;
				}
			}

			// Approach gate + no-progress watchdog. The mesh can route the last crumb
			// to a spot from which a wall still stands between the bot and the
			// destination (large areas spanning geometry) — the old code only noticed
			// after 2s of wall-humping. Now the first tick of the approach phase
			// hull-traces straight at the destination:
			//  - world brush hit → blacklist the destination area so the repath comes
			//    at it from the connected side instead of through the wall,
			//  - entity hit (closed door) → stand and wait up to 25s like the gate
			//    logic in UpdateStuckTime; a blocker still closed after that is
			//    blacklisted too.
			if (m_flApproachBlockSince > 0.0f)
			{
				// Standing at an entity blocker on the direct approach.
				if (flCurTime - m_flApproachBlockSince > 25.0f)
				{
					// Still closed after 25s — effectively permanent
					m_flApproachBlockSince = 0.0f;
					if (CNavArea* pDestArea = m_pMap->FindClosestNavArea(m_vDestination))
					{
						m_pMap->m_mStuckBlacklist[pDestArea] = { flCurTime + 20.0f, 20000.0f };
						m_pMap->m_pather.Reset();
					}
					AbandonPath("Approach blocked by door that never opened");
					return;
				}

				// Re-probe every 0.5s so a gate opening resumes movement quickly
				if (flCurTime - m_flApproachBlockSince >= 0.5f)
				{
					const Vec3 vFrom = vLocalPos + Vec3(0.0f, 0.0f, 18.0f);
					const Vec3 vTo = m_vDestination + Vec3(0.0f, 0.0f, 18.0f);
					const Vec3 vHullMin(-NAV_HULL_HALF_WIDTH, -NAV_HULL_HALF_WIDTH, 0.0f);
					const Vec3 vHullMax(NAV_HULL_HALF_WIDTH, NAV_HULL_HALF_WIDTH, 62.0f);
					CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
					CGameTrace trace = {};
					H::AimUtils->TraceHull(vFrom, vTo, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);
					if (trace.fraction >= 0.85f || trace.startsolid)
						m_flApproachBlockSince = 0.0f;  // Way is clear — walk on
					else
						return;  // Still blocked — keep standing
				}
				else
					return;
			}

			if (m_flApproachCheckTime <= 0.0f)
			{
				m_flApproachCheckTime = flCurTime;
				m_flApproachCheckDist = flDistToDest;

				const Vec3 vFrom = vLocalPos + Vec3(0.0f, 0.0f, 18.0f);
				const Vec3 vTo = m_vDestination + Vec3(0.0f, 0.0f, 18.0f);
				const Vec3 vHullMin(-NAV_HULL_HALF_WIDTH, -NAV_HULL_HALF_WIDTH, 0.0f);
				const Vec3 vHullMax(NAV_HULL_HALF_WIDTH, NAV_HULL_HALF_WIDTH, 62.0f);
				CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
				CGameTrace trace = {};
				H::AimUtils->TraceHull(vFrom, vTo, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);

				if (trace.fraction < 0.85f && !trace.startsolid)
				{
					if (trace.m_pEnt && trace.m_pEnt->entindex() != 0)
					{
						// Entity blocker (gate/door) — stand and wait for it to open.
						// Reset the progress sampler so a freshly-opened gate doesn't
						// trip the no-progress watchdog with timestamps from before the wait.
						m_flApproachBlockSince = flCurTime;
						m_flApproachCheckTime = 0.0f;
						return;
					}

					// World wall between the last crumb and the destination — aim the
					// repath at the destination area from another direction.
					if (CNavArea* pDestArea = m_pMap->FindClosestNavArea(m_vDestination))
					{
						m_pMap->m_mStuckBlacklist[pDestArea] = {
							flCurTime + 20.0f,
							20000.0f  // Heavier than the unreliable-connection penalty so
						};            // any real alternate route beats the blocked approach
						m_pMap->m_pather.Reset();
					}
					AbandonPath("Direct approach blocked by world wall");
					return;
				}
			}
			else if (flCurTime - m_flApproachCheckTime >= 2.0f)
			{
				if (m_flApproachCheckDist - flDistToDest < 40.0f)
				{
					m_flApproachCheckTime = 0.0f;
					AbandonPath("Approach phase made no progress");
					return;
				}
				m_flApproachCheckTime = flCurTime;
				m_flApproachCheckDist = flDistToDest;
			}

			MoveTowards(pLocal, pCmd, vMoveTarget);
			return;
		}
		m_bApproachingDestination = false;
		CancelPath();
		return;
	}

	// === Compute move target (ported from Amalgam's FollowCrumbs) ===
	// This is the key difference from the old system: we use the crumb's m_vApproachDir
	// and the direction to the next crumb to determine movement, NOT just walking directly
	// to the current crumb position. This is what prevents cutting through walls at corners.
	Crumb_t& tActiveCrumb = m_vCrumbs[m_nCurrentCrumbIndex];
	size_t uCrumbsSize = m_vCrumbs.size();

	// Determine the movement direction (like Amalgam's vMoveDir)
	// Priority: use m_vApproachDir from the crumb, fallback to direction to next crumb
	Vec3 vMoveDir = tActiveCrumb.m_vApproachDir;
	vMoveDir.z = 0.0f;
	float flDirLen = vMoveDir.Length();
	if (flDirLen < 0.01f && uCrumbsSize > 1 && m_nCurrentCrumbIndex + 1 < uCrumbsSize)
	{
		// No approach dir set — use direction from current crumb to next crumb
		vMoveDir = m_vCrumbs[m_nCurrentCrumbIndex + 1].m_vPos - tActiveCrumb.m_vPos;
		vMoveDir.z = 0.0f;
		flDirLen = vMoveDir.Length();
	}
	if (flDirLen > 0.01f)
		vMoveDir /= flDirLen;
	else
		vMoveDir = {};

	// Store current path direction for external use (like Amalgam's m_vCurrentPathDir)
	m_vCurrentPathDir = vMoveDir;

	// Compute vMoveTarget like Amalgam's FollowCrumbs
	Vec3 vMoveTarget = tActiveCrumb.m_vPos;

	if (!vMoveDir.IsZero())
	{
		// If this crumb has an approach distance (dropdown), push the target forward
		if (tActiveCrumb.m_flApproachDistance > 0.0f)
		{
			vMoveTarget = tActiveCrumb.m_vPos + vMoveDir * tActiveCrumb.m_flApproachDistance;
		}

		// === Cornering logic (ported from TF2 bot's PathFollower) ===
		// The TF2 bot walks directly toward each waypoint and only starts steering
		// toward the next segment when very close. The old Amalgam-style blend
		// started too early (100 units) with an 80-unit look-ahead, which caused
		// the bot to cut corners and walk into walls or outside navmesh areas.
		//
		// New approach: when within reach radius of the current crumb, start
		// blending the move target toward the next crumb. This ensures the bot
		// actually reaches the turn point before cutting.
		Vec3 vToCrumb = tActiveCrumb.m_vPos - vLocalPos;
		vToCrumb.z = 0.0f;
		float flDistToCrumb = vToCrumb.Length();

		if (flDistToCrumb < kDefaultReachRadius && m_nCurrentCrumbIndex + 1 < uCrumbsSize)
		{
			// We're close to the current crumb — start steering toward the next one.
			// Blend factor: at reach radius edge, barely steering; at center, full steer.
			float flBlend = 1.0f - (flDistToCrumb / kDefaultReachRadius);
			flBlend = flBlend * flBlend;  // Quadratic — gradual transition

			// Target: the next crumb position (not a look-ahead past the current crumb)
			Vec3 vNextCrumbPos = m_vCrumbs[m_nCurrentCrumbIndex + 1].m_vPos;
			vNextCrumbPos.z = tActiveCrumb.m_vPos.z;

			// Blend between current crumb and next crumb
			vMoveTarget = tActiveCrumb.m_vPos + (vNextCrumbPos - tActiveCrumb.m_vPos) * flBlend;
		}
	}

	// Reset Z to local height (like Amalgam's bResetHeight logic)
	vMoveTarget.z = vLocalPos.z;

	// === Corner speed modulation (concept from the TF2 bot's
	// PathFollower::AdjustSpeed, which slows the bot by path curvature) ===
	// Slow down into sharp turns so the bot doesn't overshoot the turn point
	// and plow into the wall behind it.
	float flSpeedScale = 1.0f;
	if (CFG::NavBot_CornerSpeed < 1.0f && m_nCurrentCrumbIndex + 1 < uCrumbsSize)
	{
		Vec3 vToCrumbDir = tActiveCrumb.m_vPos - vLocalPos;
		vToCrumbDir.z = 0.0f;
		float flToCrumbLen = vToCrumbDir.Length();
		Vec3 vCrumbToNext = m_vCrumbs[m_nCurrentCrumbIndex + 1].m_vPos - tActiveCrumb.m_vPos;
		vCrumbToNext.z = 0.0f;
		float flCrumbToNextLen = vCrumbToNext.Length();

		// Only slow down when the turn is actually imminent
		if (flToCrumbLen > 1.0f && flCrumbToNextLen > 1.0f && flToCrumbLen < 150.0f)
		{
			float flDot = std::clamp(vToCrumbDir.Dot(vCrumbToNext) / (flToCrumbLen * flCrumbToNextLen), -1.0f, 1.0f);
			float flTurnDeg = acosf(flDot) * (180.0f / 3.14159265f);
			float flTurnFactor = std::clamp(flTurnDeg / 90.0f, 0.0f, 1.0f);
			flSpeedScale = 1.0f - flTurnFactor * (1.0f - CFG::NavBot_CornerSpeed);
		}
	}

	// === Whisker obstacle avoidance (concept from the TF2 bot's
	// PathFollower::Avoid reflex steering) ===
	// Steer the immediate movement goal away from near obstacles (walls, props)
	// detected by small left/right hull probes. Skipped during drop nudges and
	// while airborne (the probes would fire constantly mid-jump).
	if (CFG::NavBot_AvoidObstacles && !m_bDropNudgeActive && bOnGround)
	{
		Vec3 vSteerDir = vMoveTarget - vLocalPos;
		vSteerDir.z = 0.0f;
		float flSteerLen = vSteerDir.Length();
		if (flSteerLen > 1.0f)
		{
			vSteerDir /= flSteerLen;
			Vec3 vLeftSteer(-vSteerDir.y, vSteerDir.x, 0.0f);
			vMoveTarget = AvoidObstacles(pLocal, vMoveTarget, vSteerDir, vLeftSteer);
			vMoveTarget.z = vLocalPos.z;
		}
	}

	// === Dropdown edge nudge move override ===
	// When the bot is at a drop crumb but hasn't actually fallen (navmesh doesn't
	// cover the real edge), override the move target to push further forward.
	// Incrementally increase the nudge distance if we don't fall after each attempt.
	if (m_bDropNudgeActive && !m_vDropNudgeDir.IsZero())
	{
		// If we're no longer on ground, we've fallen — deactivate nudge and advance
		if (!bOnGround)
		{
			m_bDropNudgeActive = false;
			m_nDropNudgeAttempts = 0;
			m_flDropNudgeExtraForward = 0.0f;
			m_nCurrentCrumbIndex++;
			m_flInactivityTime = flCurTime;
		}
		else
		{
			// Override move target — walk forward past the navmesh edge
			vMoveTarget = vLocalPos + m_vDropNudgeDir * (50.0f + m_flDropNudgeExtraForward);
			vMoveTarget.z = vLocalPos.z;

			// Check if this nudge attempt has timed out
			float flNudgeTime = flCurTime - m_flDropNudgeStartTime;
			if (flNudgeTime > 0.4f)
			{
				m_nDropNudgeAttempts++;
				m_flDropNudgeExtraForward += 20.0f; // Increase nudge distance each attempt

				if (m_nDropNudgeAttempts > 4)
				{
					// Can't reach the edge after multiple attempts — abandon path
					m_bDropNudgeActive = false;
					m_nDropNudgeAttempts = 0;
					m_flDropNudgeExtraForward = 0.0f;
					AbandonPath("Drop nudge failed - can't reach edge");
					return;
				}

				m_flDropNudgeStartTime = flCurTime; // Reset timer for next attempt
			}
		}
	}

	MoveTowards(pLocal, pCmd, vMoveTarget, flSpeedScale);

	// Handle smart jump state machine (ported from Amalgam's HandleSmartJump)
	if (CFG::NavBot_AutoJump)
		HandleSmartJump(pLocal, pCmd);

	// Unibox's stuck Jump phase (TickStuckSample -> FollowCrumbs): with ~1.5s of
	// no progress, jump while still pushing toward the crumb. A hull pressed into
	// a door frame while jumping slides around the corner — the physical escape
	// that no amount of repathing provides (this is why Unibox walks out of tight
	// spawn corners where the old code only blacklisted). Ducks midair like
	// Unibox for crouch-jump clearance. Applied AFTER HandleSmartJump so the
	// stuck phase's buttons win over the state machine's.
	if (m_eStuckPhase == STUCK_JUMP)
	{
		if (bOnGround)
		{
			pCmd->buttons &= ~IN_DUCK;
			pCmd->buttons |= IN_JUMP;
		}
		else
			pCmd->buttons |= IN_DUCK;
	}
}

void CNavEngine::MoveTowards(C_TFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vTarget, float flSpeedScale)
{
	if (!pLocal || !pCmd)
		return;

	Vec3 vLocalPos = pLocal->m_vecOrigin();

	// Calculate direction to target (2D only) — like Amalgam's ComputeMove/WalkTo
	Vec3 vDirection = vTarget - vLocalPos;
	vDirection.z = 0.0f;

	float flDist = vDirection.Length();
	if (flDist < 1.0f)
		return;

	vDirection.Normalize();

	// Get view angles
	Vec3 vViewAngles = pCmd->viewangles;

	// Convert direction to forward/side move based on view angles
	Vec3 vForward, vRight;
	Math::AngleVectors(vViewAngles, &vForward, &vRight, nullptr);
	vForward.z = 0.0f;
	vRight.z = 0.0f;
	vForward.Normalize();
	vRight.Normalize();

	// Calculate movement commands (like Amalgam's ComputeMove)
	float flForwardMove = vDirection.Dot(vForward) * 450.0f * flSpeedScale;
	float flSideMove = vDirection.Dot(vRight) * 450.0f * flSpeedScale;

	pCmd->forwardmove = flForwardMove;
	pCmd->sidemove = flSideMove;
}

Vec3 CNavEngine::AvoidObstacles(C_TFPlayer* pLocal, const Vec3& vGoalPos, const Vec3& vForward, const Vec3& vLeft)
{
	if (!pLocal)
		return vGoalPos;

	float flCurTime = I::GlobalVars->curtime;

	// Check sparsely while the way is clear, every few ticks while actually
	// steering around something (mirrors the TF2 bot's avoid timer invalidation)
	if (!m_bWhiskerSteering && flCurTime < m_flNextWhiskerCheckTime)
		return vGoalPos;

	Vec3 vLocalPos = pLocal->m_vecOrigin();

	// Small probe hull (quarter-width whiskers), lifted above step height so
	// stairs and ramps read as clear, crouch-height tall so anything we could
	// crouch under is ignored.
	const float flSize = 10.0f;
	const float flOffset = flSize + 2.0f;
	const float flRange = 55.0f;
	const Vec3 vHullMin(-flSize, -flSize, 18.1f);
	const Vec3 vHullMax(flSize, flSize, 62.0f);

	CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
	CGameTrace trace = {};

	// Left probe
	float flLeftAvoid = 0.0f;
	{
		Vec3 vFrom = vLocalPos + vLeft * flOffset;
		Vec3 vTo = vFrom + vForward * flRange;
		H::AimUtils->TraceHull(vFrom, vTo, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);
		if (trace.startsolid)
			flLeftAvoid = 1.0f;  // probe starts inside geometry — treat as fully blocked
		else if (trace.fraction < 1.0f)
			flLeftAvoid = 1.0f - trace.fraction;
	}

	// Right probe
	float flRightAvoid = 0.0f;
	{
		Vec3 vFrom = vLocalPos - vLeft * flOffset;
		Vec3 vTo = vFrom + vForward * flRange;
		H::AimUtils->TraceHull(vFrom, vTo, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);
		if (trace.startsolid)
			flRightAvoid = 1.0f;
		else if (trace.fraction < 1.0f)
			flRightAvoid = 1.0f - trace.fraction;
	}

	m_flWhiskerLeftAvoid = flLeftAvoid;
	m_flWhiskerRightAvoid = flRightAvoid;
	m_bWhiskerSteering = (flLeftAvoid > 0.0f || flRightAvoid > 0.0f);
	m_flNextWhiskerCheckTime = flCurTime + (m_bWhiskerSteering ? 0.05f : 0.25f);

	if (!m_bWhiskerSteering)
		return vGoalPos;

	// Steer away from the blocked side: keep moving mostly forward, pushed
	// sideways away from the obstacle
	float flAvoidResult = 0.0f;
	if (flLeftAvoid <= 0.0f && flRightAvoid > 0.0f)
		flAvoidResult = -flRightAvoid;   // left clear, right blocked → steer left
	else if (flRightAvoid <= 0.0f && flLeftAvoid > 0.0f)
		flAvoidResult = flLeftAvoid;     // right clear, left blocked → steer right
	else
	{
		// Both blocked — steer away from whichever is deeper. If they're equal
		// we're squarely facing a wall and the path itself must turn; don't fight it.
		const float flEqualTolerance = 0.01f;
		if (std::abs(flRightAvoid - flLeftAvoid) < flEqualTolerance)
			return vGoalPos;
		flAvoidResult = (flRightAvoid > flLeftAvoid) ? -flRightAvoid : flLeftAvoid;
	}

	Vec3 vAvoidDir = vForward * 0.5f - vLeft * flAvoidResult;
	float flAvoidLen = vAvoidDir.Length2D();
	if (flAvoidLen < 0.01f)
		return vGoalPos;
	vAvoidDir /= flAvoidLen;
	vAvoidDir.z = 0.0f;

	return vLocalPos + vAvoidDir * 100.0f;
}

void CNavEngine::CancelPath()
{
	m_vCrumbs.clear();
	m_nCurrentCrumbIndex = 0;
	m_bApproachingDestination = false;
	m_vCurrentPathDir = {};
	m_eCurrentPriority = NAV_PRIO_NONE;
	m_eJumpState = JUMP_AWAITING;
	m_flInactivityTime = 0.0f;
	m_nVischeckFailCount = 0;
	m_flVischeckCooldownUntil = 0.0f;
	m_bDropNudgeActive = false;
	m_nDropNudgeAttempts = 0;
	m_flDropNudgeExtraForward = 0.0f;
	m_nAbandonCount = 0;
	m_flAbandonCooldown = 0.0f;
	m_pCachedLocalArea = nullptr;
	m_flCachedLocalAreaTime = 0.0f;
	m_bWhiskerSteering = false;
	m_flWhiskerLeftAvoid = 0.0f;
	m_flWhiskerRightAvoid = 0.0f;
	m_flApproachCheckTime = 0.0f;
	m_flApproachCheckDist = 0.0f;
	m_flApproachBlockSince = 0.0f;
	m_pLastSolveStartArea = nullptr;
	ResetStuckProgress();
}

void CNavEngine::UpdateStuckTime(C_TFPlayer* pLocal)
{
	if (!pLocal || !m_pMap || m_pMap->m_eState != NavState::Active || m_vCrumbs.empty())
		return;

	float flCurTime = I::GlobalVars->curtime;
	const Vec3 vLocalPos = pLocal->m_vecOrigin();

	// Get current nav area — use cached result from FollowPath if available
	CNavArea* pCurrentArea = m_pCachedLocalArea;
	if (!pCurrentArea || (flCurTime - m_flCachedLocalAreaTime) > 0.2f)
	{
		pCurrentArea = m_pMap->FindClosestNavArea(vLocalPos);
		m_pCachedLocalArea = pCurrentArea;
		m_flCachedLocalAreaTime = flCurTime;
	}

	// === Progress sampling (ported from Unibox's TickStuckSample) ===
	// The old velocity-only check (Length2D > 40 = moving) missed wall-grinding:
	// a bot sliding along a wall reads as "moving" while making zero progress
	// toward its crumb, so it ground into geometry forever without ever being
	// called stuck. Sample every 0.3s while grounded: progress = the nav area
	// changed, or the distance to the active crumb dropped by more than a
	// speed-scaled threshold (fast movement between samples isn't a false stuck,
	// slow deliberate movement still counts).
	const bool bOnGround = (pLocal->m_fFlags() & FL_ONGROUND) != 0;
	if (flCurTime >= m_flNextStuckSampleTime && bOnGround)
	{
		m_flNextStuckSampleTime = flCurTime + 0.3f;

		const Vec3 vCrumbTarget = (m_nCurrentCrumbIndex < m_vCrumbs.size())
			? m_vCrumbs[m_nCurrentCrumbIndex].m_vPos : m_vDestination;
		Vec3 vToCrumb = vCrumbTarget - vLocalPos;
		vToCrumb.z = 0.0f;
		const float flDistToCrumb = vToCrumb.Length();
		const float flSpeed = pLocal->m_vecVelocity().Length2D();
		const bool bCrumbChanged = !m_bHasSampledCrumb
			|| (m_vSampledCrumbPos - vCrumbTarget).LengthSqr() > 1.0f;
		const bool bAreaChanged = m_pLastProgressArea && pCurrentArea && m_pLastProgressArea != pCurrentArea;
		const bool bProgress = m_flLastSampleDistToCrumb == FLT_MAX || bCrumbChanged || bAreaChanged
			|| flDistToCrumb < m_flLastSampleDistToCrumb - (std::max)(8.0f, flSpeed * 0.3f * 0.18f);

		m_vSampledCrumbPos = vCrumbTarget;
		m_bHasSampledCrumb = true;
		m_flLastSampleDistToCrumb = flDistToCrumb;

		if (bProgress)
		{
			m_flInactivityTime = flCurTime;   // HandleSmartJump keys off this
			m_eStuckPhase = STUCK_IDLE;       // escaped — stop jumping (stale phase kept jumping)
			m_pLastProgressArea = pCurrentArea;
			m_pLastNavArea = pCurrentArea;
			return;
		}
	}

	// We're stuck (no progress toward the crumb) — wall-grinding now reaches
	// here too, it used to pass the velocity check
	float flStuckTime = flCurTime - m_flInactivityTime;

	// === Unibox's TickStuckSample phase ladder ===
	// Idle until 1.5s without progress, Jump until 1.85s, Fail after. FollowPath
	// applies the Jump phase (IN_JUMP while still pushing toward the crumb — a hull
	// pressed into a door frame while jumping slides around the corner). Fail was
	// 1.0s before with no jump at all, so the old bot never even tried the one
	// escape that needs no repathing.
	if (flStuckTime <= 1.5f)
	{
		m_eStuckPhase = STUCK_IDLE;
		m_pLastNavArea = pCurrentArea;
		return;
	}
	m_eStuckPhase = (flStuckTime <= 1.85f) ? STUCK_JUMP : STUCK_FAIL;
	if (m_eStuckPhase == STUCK_JUMP || !bOnGround || !pCurrentArea)
	{
		// Airborne: no fail — descents take time and midair repathing is useless.
		// An issued stuck jump keeps its midair duck (Unibox crouch-jumps it).
		if (!bOnGround && m_eStuckPhase == STUCK_FAIL)
			m_eStuckPhase = STUCK_JUMP;
		m_pLastNavArea = pCurrentArea;
		return;
	}

	// === Fail phase (stuck > 1.85s with no progress) ===
	// Unibox's AbandonPath(bStuck) -> RecordStuckFailure: credit the fail to the
	// exact path connection and corridor-block it briefly; the destination AREA is
	// only marked after repeated strikes on the same link. The old response
	// blacklisted the area past the blocker on the FIRST fail with a 20000 penalty
	// — at Dustbowl spawn that poisoned the only exit area, every repath failed
	// until the mark lapsed, and the bot cycled between grinding the door frame
	// and standing sealed out of its own exit.

	// Attribute the fail to an edge the NEXT solve would actually traverse. Marks
	// key on the area the last solve STARTED from (m_pLastSolveStartArea): NavTo's
	// start candidate can be an area past the wall the bot is grinding (the hull
	// trace to its center threads a door opening), and anchoring on
	// FindClosestNavArea(bot) then sealed links in a world the solver never
	// visited — Dustbowl logged 20+ strikes on one link while every solve
	// regenerated the identical grinding route. The current crumb's stamped area
	// is the fallback (crumbs stamp the LEAVING area, so it is the from-side of
	// the leg the path is walking — Unibox's lastCrumb->currentCrumb).
	CNavArea* pFromArea = m_pLastSolveStartArea
		? m_pLastSolveStartArea
		: ((m_nCurrentCrumbIndex < m_vCrumbs.size() && m_vCrumbs[m_nCurrentCrumbIndex].m_pNavArea)
			? m_vCrumbs[m_nCurrentCrumbIndex].m_pNavArea : pCurrentArea);
	CNavArea* pBlockTarget = nullptr;
	Vec3 vBlockCrumbPos = {};
	{
		size_t uProbe = m_nCurrentCrumbIndex;
		int iTried = 0;
		while (uProbe < m_vCrumbs.size() && iTried < 6)
		{
			CNavArea* pCrumbArea = m_vCrumbs[uProbe].m_pNavArea
				? m_vCrumbs[uProbe].m_pNavArea : m_pMap->FindClosestNavArea(m_vCrumbs[uProbe].m_vPos);
			if (pCrumbArea && pCrumbArea != pFromArea)
			{
				pBlockTarget = pCrumbArea;
				vBlockCrumbPos = m_vCrumbs[uProbe].m_vPos;
				break;
			}
			uProbe++;
			iTried++;
		}
	}

	// Classify the blocker with a brush-only hull trace to the target crumb: a
	// crowd of players never hits it, so onlookers can't poison the marks.
	bool bBlockedByWorld = false;
	if (pBlockTarget)
	{
		const Vec3 vFrom = vLocalPos + Vec3(0.0f, 0.0f, 18.0f);
		const Vec3 vTo = vBlockCrumbPos + Vec3(0.0f, 0.0f, 18.0f);
		const Vec3 vHullMin(-NAV_HULL_HALF_WIDTH, -NAV_HULL_HALF_WIDTH, 0.0f);
		const Vec3 vHullMax(NAV_HULL_HALF_WIDTH, NAV_HULL_HALF_WIDTH, 62.0f);
		CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
		CGameTrace trace = {};
		H::AimUtils->TraceHull(vFrom, vTo, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);
		bBlockedByWorld = trace.fraction < 0.98f;

		// Entity blockers (func_door stage gate, respawn-room door, func_brush)
		// move or vanish on their own: stand and wait instead of failing — jumping
		// at a shut gate and re-solving into it buys nothing. A blocker still shut
		// after 25s is effectively permanent and falls through to the strikes.
		if (bBlockedByWorld && trace.m_pEnt && trace.m_pEnt->entindex() != 0
			&& flStuckTime < 25.0f)
		{
			m_pLastNavArea = pCurrentArea;
			return;
		}
	}

	// The to-side: the probe's first different-area crumb, else the from area
	// itself (Unibox's pTo→pTo self-mark when the whole leg stays in one area).
	CNavArea* pToArea = pBlockTarget ? pBlockTarget : pFromArea;
	auto& rEntry = m_pMap->m_mConnectionStuckTime[pFromArea][pToArea];
	if (rEntry.m_flExpireTime > 0.0f && rEntry.m_flExpireTime <= flCurTime)
	{
		rEntry.m_iStuckTicks = 0;
		rEntry.m_iFailStrikes = 0;
	}
	rEntry.m_iStuckTicks += 5;          // +250 solve cost per fail (AdjacentCost: ticks * 50)
	rEntry.m_iFailStrikes++;
	rEntry.m_flExpireTime = flCurTime + 100.0f;  // Unibox StuckExpireTime — strikes
	                                             // accumulate across grind cycles

	// Corridor-block the exact failed link (Unibox's stuck MarkConnection):
	// AdjacentCost hard-excludes it for 6s (Unibox StuckBlacklistTime default) so
	// the repath must find a different way in.
	if (pFromArea != pToArea)
		m_pMap->MarkConn(pFromArea, pToArea, false, 6.0f, true);

	// Unibox's 3-strike rule: the same link failing over and over means the area
	// past it is the problem (sealed gate, one-way trap) — make the repath and the
	// goal machine rotate elsewhere. A penalty, not a skip: when the link is the
	// only route, the solve still goes through instead of sealing.
	if (rEntry.m_iFailStrikes >= 3)
		m_pMap->m_mStuckBlacklist[pToArea] = { flCurTime + 6.0f, 20000.0f };

	// Unibox's EnsureLocalExit: never let our own marks seal us in. If every exit
	// of the current area is now corridor-blocked, shorten them so the next solve
	// can retry the only route (with the Jump phase backing the retry).
	if (!m_pMap->HasUsableExit(pCurrentArea))
		m_pMap->ShortenExitBlocks(pCurrentArea, 5.0f);

	NavDebug("stuck fail: link %u->%u strike %d%s (probe %u)",
		pFromArea->m_uId, pToArea->m_uId, rEntry.m_iFailStrikes,
		bBlockedByWorld ? " (world blocked)" : "",
		pCurrentArea->m_uId);

	// Reset pather so the reroute sees the new marks, then abandon into a repath
	m_pMap->m_pather.Reset();
	AbandonPath(bBlockedByWorld ? "Stuck (wall on path connection)" : "Stuck (no progress)");
	m_pLastNavArea = pCurrentArea;
}

void CNavEngine::AbandonPath(const char* sReason)
{
	float flCurTime = I::GlobalVars->curtime;

	// Check if we're within a cooldown from a previous abandon for the same destination.
	// This breaks the AbandonPath→NavTo→AbandonPath→NavTo oscillation loop.
	Vec3 vLastDest = m_vLastDestination;
	bool bSameDest = !vLastDest.IsZero() && m_vDestination.DistTo(vLastDest) < 50.0f;

	if (bSameDest && m_nAbandonCount > 1 && (flCurTime - m_flLastAbandonTime) < m_flAbandonCooldown)
	{
		// Within cooldown for same destination — clear path but DON'T auto-repath
		m_vCrumbs.clear();
		m_nCurrentCrumbIndex = 0;
		m_vCurrentPathDir = {};
		m_eCurrentPriority = NAV_PRIO_NONE;
		// Keep m_vLastDestination so NavBot can repath when cooldown expires
		return;
	}

	// Track consecutive abandons — if abandoning rapidly, increase cooldown
	if (bSameDest && (flCurTime - m_flLastAbandonTime) < 2.0f)
	{
		m_nAbandonCount++;
		// Exponential cooldown: 0.5s, 1s, 2s, 4s... capped at 5s
		m_flAbandonCooldown = (std::min)(0.5f * (1 << (std::min)(m_nAbandonCount, 4)), 5.0f);
	}
	else
	{
		// Different destination or enough time passed — reset
		m_nAbandonCount = 0;
		m_flAbandonCooldown = 0.3f;  // Small base cooldown
	}

	m_flLastAbandonTime = flCurTime;

	// Save destination and priority before clearing
	ENavPriority eLastPrio = m_eCurrentPriority;
	bool bLastRepath = m_bRepathOnFail;

	// Clear current path
	m_vCrumbs.clear();
	m_nCurrentCrumbIndex = 0;
	m_bApproachingDestination = false;   // an abandon during the approach phase must not leave IsPathing() true
	m_vCurrentPathDir = {};
	m_eCurrentPriority = NAV_PRIO_NONE;

	// Auto-repath to same destination (like Amalgam's AbandonPath)
	if (bLastRepath && !vLastDest.IsZero())
	{
		NavTo(vLastDest, eLastPrio, true);
	}
}

// Ported from Unibox's IsPlayerPassableNavigation: can the player walk the
// straight line vFrom -> vTo? Two ray traces run along the walk line, offset to
// its sides by the nav hull half-width and lifted above step height (so stairs
// and ramps don't read as blockers; near-equal endpoint heights are levelled so
// gentle slopes don't clip the ray through rising ground). A hit on either side
// means geometry is in the way. Zero-extent hulls are treated as rays by the
// engine (m_IsRay in cmodel.h).
bool CNavEngine::IsPlayerPassableNavigation(C_TFPlayer* pLocal, const Vec3& vFrom, const Vec3& vTo)
{
	if (!pLocal)
		return false;

	Vec3 vDelta = vTo - vFrom;
	vDelta.z = 0.0f;
	if (vDelta.Length() < 16.0f)
		return true;   // portal-to-portal hops this short are too noisy to trace

	Vec3 vRight(-vDelta.y, vDelta.x, 0.0f);
	vRight.Normalize();
	const Vec3 vOffset = vRight * NAV_HULL_HALF_WIDTH;

	Vec3 vStart = vFrom;
	vStart.z += PLAYER_CROUCHED_JUMP_HEIGHT;   // ray height above the mesh floor
	Vec3 vEnd = vTo;
	vEnd.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	if (std::abs(vEnd.z - vStart.z) <= PLAYER_JUMP_HEIGHT)
		vEnd.z = vStart.z;   // level corridor test for ramps/steps within jump height

	const Vec3 vRayMins(0.0f, 0.0f, 0.0f);
	const Vec3 vRayMaxs(0.0f, 0.0f, 0.0f);
	CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
	CGameTrace trace = {};

	H::AimUtils->TraceHull(vStart - vOffset, vEnd - vOffset, vRayMins, vRayMaxs, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);
	if (trace.fraction < 1.0f)
		return false;

	H::AimUtils->TraceHull(vStart + vOffset, vEnd + vOffset, vRayMins, vRayMaxs, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);
	return trace.fraction >= 1.0f;
}

void CNavEngine::ResetStuckProgress()
{
	m_flNextStuckSampleTime = 0.0f;
	m_flLastSampleDistToCrumb = FLT_MAX;
	m_vSampledCrumbPos = {};
	m_bHasSampledCrumb = false;
	m_pLastProgressArea = nullptr;
	m_eStuckPhase = STUCK_IDLE;   // fresh path — no stale jump/fail phase
}

void CNavEngine::VischeckPath(C_TFPlayer* pLocal)
{
	if (!pLocal || m_vCrumbs.size() < 2)
		return;

	float flCurTime = I::GlobalVars->curtime;

	// If vischeck has failed too many times for this destination, enter cooldown
	// to break the vischeck-abandon-repath infinite loop
	if (flCurTime < m_flVischeckCooldownUntil)
		return;

	// Throttle vischecks (every 0.5s, like Amalgam)
	if (flCurTime < m_flNextVischeckTime)
		return;
	m_flNextVischeckTime = flCurTime + 0.5f;

	// Player hull dimensions (like Amalgam's IsWalkable)
	const Vec3 vHullMin(-NAV_HULL_HALF_WIDTH, -NAV_HULL_HALF_WIDTH, 0.0f);
	const Vec3 vHullMax(NAV_HULL_HALF_WIDTH, NAV_HULL_HALF_WIDTH, 72.0f);
	const Vec3 vStepHeight(0.0f, 0.0f, 18.0f);  // m_flStepSize approximation

	// Only check segments ahead of the current crumb index
	// (no point checking segments we already passed)
	size_t nStartIdx = (m_nCurrentCrumbIndex > 0) ? m_nCurrentCrumbIndex - 1 : 0;

	// Only check segments near the bot (within 200 units).
	// Checking distant segments is unreliable — edge-to-edge traces between adjacent
	// areas can clip through corners on valid paths, causing false positives that
	// trigger unnecessary AbandonPath calls and rapid route switching.
	Vec3 vLocalPos = pLocal->m_vecOrigin();
	const float flVischeckRange = 200.0f;

	for (size_t i = nStartIdx; i < m_vCrumbs.size() - 1; i++)
	{
		// Skip segments that are far from the bot
		Vec3 vSegMid = (m_vCrumbs[i].m_vPos + m_vCrumbs[i + 1].m_vPos) * 0.5f;
		Vec3 vToSeg = vSegMid - vLocalPos;
		vToSeg.z = 0.0f;
		if (vToSeg.Length() > flVischeckRange)
			continue;

		Vec3 vFrom = m_vCrumbs[i].m_vPos;
		Vec3 vTo = m_vCrumbs[i + 1].m_vPos;

		// Direct mesh connections are trusted (ported from Unibox's VischeckPath,
		// which never abandons them: "if (!bPassable && bConnected) continue").
		// Doorways, props and corners legitimately intersect this hull test on
		// real routes — penalizing + abandoning them every 0.5s was the spawn
		// freeze ("spawn pinball"). Genuine blockers on these links are caught on
		// contact by the progress sampler + stuck branches instead.
		CNavArea* pSegFrom = m_vCrumbs[i].m_pNavArea;
		CNavArea* pSegTo = m_vCrumbs[i + 1].m_pNavArea;
		if (pSegFrom && pSegTo && m_pMap->HasDirectConnection(pSegFrom, pSegTo))
			continue;

		CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
		CGameTrace trace = {};

		H::AimUtils->TraceHull(vFrom + vStepHeight, vTo + vStepHeight, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);

		// Only a substantially blocked segment fails. Edge-to-edge traces between
		// adjacent areas legitimately graze corners on valid paths (in tight rooms
		// like spawn, the old 0.85 threshold blocked its own doorway every 0.5s and
		// froze the bot); anything above half-blocked is left to the whiskers and
		// stuck logic, which handle genuine blockers on contact.
		if (trace.fraction < 0.5f && !trace.startsolid)
		{
			// Penalize the specific CONNECTION this segment traverses (FROM area →
			// TO area), not the whole TO area. A whole-area blacklist excluded the
			// doorway/room for 30s and the bot refused to path through its own
			// spawn; a connection penalty keeps the link usable when it's the only
			// route and lapses after 15s instead of poisoning the mesh.
			CNavArea* pFromArea = m_vCrumbs[i].m_pNavArea;
			CNavArea* pToArea = m_vCrumbs[i + 1].m_pNavArea;

			if (pFromArea && pToArea && pFromArea != pToArea)
			{
				auto& rEntry = m_pMap->m_mConnectionStuckTime[pFromArea][pToArea];
				// Escalate instead of pinning: a fixed penalty solved back into the
				// SAME path every repath (vischeck blocked it, abandoned, repathed,
				// blocked again — the spawn pinball in the debug log). Each block
				// makes this connection costlier, so the next solve routes around
				// it as soon as ANY alternative exists; the cap keeps a last-resort
				// route available when the connection is the only way through.
				rEntry.m_iStuckTicks = (std::min)(rEntry.m_iStuckTicks + 30, 600);
				rEntry.m_flExpireTime = flCurTime + 15.0f;
				NavDebug("vischeck: conn %u->%u blocked (frac %.2f), penalty now %.0f",
					pFromArea->m_uId, pToArea->m_uId, trace.fraction, rEntry.m_iStuckTicks * 50.0f);
			}

			// Track vischeck failures for this destination
			m_nVischeckFailCount++;
			if (m_nVischeckFailCount >= 3)
			{
				// Too many failures — enter cooldown to break the loop
				// During cooldown, the bot will just follow the path as-is
				// (the stuck detection system will handle it if the bot actually gets stuck)
				m_flVischeckCooldownUntil = flCurTime + 5.0f;
				return;
			}

			// Reset pather so it recalculates with the new penalties
			m_pMap->m_pather.Reset();

			AbandonPath("Path blocked by world (hull trace)");
			return;
		}
	}

	// Path is clear — reset failure count
	m_nVischeckFailCount = 0;
}

void CNavEngine::Reset(bool bForced)
{
	CancelPath();

	// Clear solve throttling so a fresh map solves immediately
	m_flNextSolveTime = 0.0f;
	m_flFailedPathUntil = 0.0f;
	m_vFailedDest = {};
	m_flFailBackoff = 1.0f;

	// Get current map name from engine
	const char* szLevelName = I::EngineClient->GetLevelName();
	if (!szLevelName || !szLevelName[0])
	{
		m_pMap.reset();
		m_sLastMapName.clear();
		return;
	}

	// Extract map name from "maps/mapname.bsp"
	std::string sMapPath(szLevelName);
	size_t mapsPos = sMapPath.find("maps/");
	if (mapsPos != std::string::npos)
		sMapPath = sMapPath.substr(mapsPos + 5);
	size_t bspPos = sMapPath.find(".bsp");
	if (bspPos != std::string::npos)
		sMapPath = sMapPath.substr(0, bspPos) + ".nav";

	// If map hasn't changed and not forced, skip reload
	if (!bForced && sMapPath == m_sLastMapName && m_pMap && m_pMap->m_eState == NavState::Active)
	{
		return;
	}

	// Reload nav mesh for new map
	std::string sNavPath = "tf/maps/" + sMapPath;

	// A failed load used to be retried from scratch every CreateMove tick: each
	// attempt re-opens the .nav file and builds + frees the whole CMap including
	// the MicroPather pools (~0.9 MB of allocator traffic per tick). Throttle
	// retries of the same missing mesh; real map changes still load immediately
	// through the forced Reset(true) from LevelInitPostEntity.
	static std::string s_sFailedMap;
	static float s_flLastFailTime = -5.0f;
	if (!bForced && sMapPath == s_sFailedMap && I::GlobalVars->realtime - s_flLastFailTime < 5.0f)
		return;

	// Scrub state tied to the OLD map's clock and mesh: curtime restarts at 0
	// on a level change, so absolute-time throttles left over from the previous
	// map read as "not yet" for the entire new map (min repath interval,
	// vischeck, jump throttle, abandon cooldown), and CNavArea* caches point
	// into the mesh we're about to free. CancelPath above covered the rest.
	m_flNextVischeckTime = 0.0f;
	m_flLastJumpTime = 0.0f;
	m_flLastAbandonTime = 0.0f;
	m_flLastPathTime = 0.0f;
	m_flDropNudgeStartTime = 0.0f;
	m_vDropNudgeDir = {};
	m_pLastNavArea = nullptr;
	m_vDestination = {};
	m_vLastDestination = {};
	m_bRouteVarietyResetForDest = false;

	m_pMap = std::make_unique<CMap>(sNavPath.c_str());

	if (!m_pMap || m_pMap->m_eState != NavState::Active)
	{
		m_pMap.reset();
		m_sLastMapName.clear();
		s_sFailedMap = sMapPath;
		s_flLastFailTime = I::GlobalVars->realtime;
		return;
	}

	s_sFailedMap.clear();
	m_sLastMapName = sMapPath;
	m_vRespawnRoomExitAreas.clear();
	m_bUpdatedRespawnRooms = false;
}

bool CNavEngine::IsReady() const
{
	return m_pMap && m_pMap->m_eState == NavState::Active;
}

CNavArea* CNavEngine::GetLocalNavArea() const
{
	if (!m_pMap || m_pMap->m_eState != NavState::Active)
		return nullptr;
	auto pLocal = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	if (!pLocal) return nullptr;
	auto pPlayer = pLocal->As<C_TFPlayer>();
	if (!pPlayer) return nullptr;
	return m_pMap->FindClosestNavArea(pPlayer->m_vecOrigin());
}

bool CNavEngine::IsSurfaceWalkable(const Vec3& vNormal) const
{
	// Surface is walkable if it's not too steep (normal Z > 0.7, about 45 degrees)
	// This matches Amalgam's IsSurfaceWalkable check
	return vNormal.z > 0.7f;
}

namespace
{
	// Shared read for IsSetupTime()/AreSetupGatesClosed(). The round-state
	// props ride on the game rules proxy's RecvTable, but Source delivers them
	// through SendProxy_GameRules, which redirects the proxy's "game_rules"
	// sub-table into the GAME RULES OBJECT — the offsets are relative to
	// CTFGameRules, not the proxy entity, so they resolve against
	// I::TFGameRules() (the same object Unibox reads with the same netvar
	// class names). The previous version scanned the entity list for the
	// proxy ENTITY and applied these offsets to it, which read garbage:
	// setup was never detected and the bot ground against the closed gates.
	struct SetupRoundState_t
	{
		bool bValid = false;   // rules object found and at least one prop resolved
		bool bMvM = false;
		bool bPipeline = false;   // plr_pipeline's preround isn't a gate hold
		int iRoundState = -1;
		bool bInSetup = false;
		bool bInWaitingForPlayers = false;
	};

	// Round-state trace, throttled to ~1/s (and only with CFG::NavBot_Debug on):
	// shows the live values IsSetupTime/AreSetupGatesClosed act on, so a stuck
	// wait-for-setup test tells us whether the game-rules read itself works
	void TraceRoundState(const char* szFmt, ...)
	{
		static float flLastTraceTime = -1.0f;
		const float flNow = I::GlobalVars->curtime;
		if (flLastTraceTime >= 0.0f && flNow >= flLastTraceTime && flNow - flLastTraceTime < 1.0f)
			return;
		flLastTraceTime = flNow;

		char szBuf[512];
		va_list args;
		va_start(args, szFmt);
		vsnprintf_s(szBuf, sizeof(szBuf), _TRUNCATE, szFmt, args);
		va_end(args);
		NavDebug("%s", szBuf);
	}

	SetupRoundState_t QuerySetupRoundState()
	{
		SetupRoundState_t tState;

		const auto pGameRules = I::TFGameRules();
		if (!pGameRules)
		{
			TraceRoundState("no game rules object (I::TFGameRules() == null) — setup state unreadable");
			return tState;
		}

		// The TF proxy (CTFGameRulesProxy) embeds the base round table
		// (CTeamplayRoundBasedRulesProxy); try the derived class name first
		// and the base name second so either recv-table layout resolves
		auto GetProxyOffset = [](const char* szProp) -> int
		{
			int nOffset = NetVars::GetNetVar("CTFGameRulesProxy", szProp);
			if (nOffset <= 0)
				nOffset = NetVars::GetNetVar("CTeamplayRoundBasedRulesProxy", szProp);
			return nOffset;
		};
		static int nRoundStateOffset = GetProxyOffset("m_iRoundState");
		static int nInSetupOffset = GetProxyOffset("m_bInSetup");
		static int nInWaitingOffset = GetProxyOffset("m_bInWaitingForPlayers");
		static int nMvMOffset = GetProxyOffset("m_bPlayingMannVsMachine");

		if (nRoundStateOffset <= 0 && nInSetupOffset <= 0 && nInWaitingOffset <= 0)
		{
			TraceRoundState("game rules object %p but every round-state prop resolved to offset 0 — netvar lookup failed", pGameRules);
			return tState;
		}

		const auto pBase = reinterpret_cast<std::uintptr_t>(pGameRules);
		tState.bValid = true;
		tState.bMvM = nMvMOffset > 0 && *reinterpret_cast<bool*>(pBase + nMvMOffset);

		// GetLevelName() returns "maps/mapname.bsp" — match substrings
		const std::string sLevelName = I::EngineClient->GetLevelName();
		tState.bPipeline = sLevelName.find("plr_pipeline") != std::string::npos;

		if (nRoundStateOffset > 0)
			tState.iRoundState = *reinterpret_cast<int*>(pBase + nRoundStateOffset);
		if (nInSetupOffset > 0)
			tState.bInSetup = *reinterpret_cast<bool*>(pBase + nInSetupOffset);
		if (nInWaitingOffset > 0)
			tState.bInWaitingForPlayers = *reinterpret_cast<bool*>(pBase + nInWaitingOffset);

		TraceRoundState("roundstate=%d inSetup=%d waiting=%d mvm=%d pipeline=%d (%s)",
			tState.iRoundState, tState.bInSetup, tState.bInWaitingForPlayers,
			tState.bMvM, tState.bPipeline, sLevelName.c_str());

		return tState;
	}
}

bool CNavEngine::IsSetupTime()
{
	// Hold semantics (Unibox IsSetupTime): GR_STATE_PREROUND gates everyone;
	// m_bInSetup / waiting-for-players gate only BLU on PL/CP maps. MvM wave
	// setup is a respawn-room hold with no gates to wait behind.
	static float flLastCheckTime = 0.0f;
	static bool bSetupTime = false;

	float flNow = I::GlobalVars->curtime;
	// curtime resets to 0 on a map change: a negative delta means the clock
	// went backwards, so fall through and recompute — otherwise this throttle
	// never expires and the bot serves the previous map's answer forever
	const float flSinceLastCheck = flNow - flLastCheckTime;
	if (flSinceLastCheck >= 0.0f && flSinceLastCheck < 0.5f)
		return bSetupTime;
	flLastCheckTime = flNow;

	bSetupTime = false;

	const SetupRoundState_t t = QuerySetupRoundState();
	if (!t.bValid || t.bMvM)
		return bSetupTime;

	// GR_STATE_PREROUND = 3 — the gates are closed for everyone (Unibox
	// excludes plr_pipeline's quirky preround)
	if (!t.bPipeline && t.iRoundState == 3)
		return bSetupTime = true;

	// BLU is the gate-blocked team on PL/CP maps: m_bInSetup, and the
	// waiting-for-players phase before the first round reads as setup too
	auto pLocal = H::Entities->GetLocal();
	if (!t.bPipeline && pLocal && pLocal->IsAlive() && pLocal->m_iTeamNum() == TF_TEAM_BLUE)
	{
		if (t.bInSetup)
			return bSetupTime = true;

		if (t.bInWaitingForPlayers)
		{
			const std::string sLevelName = I::EngineClient->GetLevelName();
			if (sLevelName.find("pl_") != std::string::npos || sLevelName.find("cp_") != std::string::npos
				|| sLevelName.find("plr_") != std::string::npos)
				return bSetupTime = true;
		}
	}

	return bSetupTime;
}

bool CNavEngine::AreSetupGatesClosed()
{
	// Team-agnostic companion for the path-cost gate seal: a closed setup
	// gate brush is solid for BOTH teams, so while setup runs even RED's
	// paths must route around the BLU gates. Gating the seal on
	// IsSetupTime() (BLU-hold semantics) let defenders solve paths straight
	// through the closed gates and grind them.
	static float flLastCheckTime = 0.0f;
	static bool bClosed = false;

	float flNow = I::GlobalVars->curtime;
	const float flSinceLastCheck = flNow - flLastCheckTime;
	if (flSinceLastCheck >= 0.0f && flSinceLastCheck < 0.5f)
		return bClosed;
	flLastCheckTime = flNow;

	bClosed = false;

	const SetupRoundState_t t = QuerySetupRoundState();
	if (!t.bValid || t.bMvM)
		return bClosed;

	if (!t.bPipeline && (t.iRoundState == 3 || t.bInSetup))
		return bClosed = true;

	// Waiting for players before the first round: gates still closed on
	// PL/CP maps
	if (t.bInWaitingForPlayers)
	{
		const std::string sLevelName = I::EngineClient->GetLevelName();
		if (sLevelName.find("pl_") != std::string::npos || sLevelName.find("cp_") != std::string::npos
			|| sLevelName.find("plr_") != std::string::npos)
			return bClosed = true;
	}

	return bClosed;
}

bool CNavEngine::IsMovementLocked()
{
	// Unibox NavRuntime::IsMovementLocked: during waiting-for-players, preround
	// (gates closed) and between-rounds the bot cannot move at all — its job
	// system stands down and its engine cancels any live path. This IS the
	// "wait for setup" behavior; there is no wander helper in Unibox. The lock
	// is team-agnostic (RED defenders hold too) and skips MvM, whose wave
	// setup is a respawn-room hold with no gates.
	static float flLastCheckTime = 0.0f;
	static bool bLocked = false;

	float flNow = I::GlobalVars->curtime;
	const float flSinceLastCheck = flNow - flLastCheckTime;
	if (flSinceLastCheck >= 0.0f && flSinceLastCheck < 0.5f)
		return bLocked;
	flLastCheckTime = flNow;

	bLocked = false;

	const SetupRoundState_t t = QuerySetupRoundState();
	if (!t.bValid || t.bMvM)
		return bLocked;

	// GR_STATE_PREROUND = 3, GR_STATE_BETWEEN_RNDS = 10; m_bInSetup covers
	// quirky states where the gates are shut outside a plain preround
	if (t.bInWaitingForPlayers || t.bInSetup || t.iRoundState == 3 || t.iRoundState == 10)
		bLocked = true;

	return bLocked;
}

bool CNavEngine::IsRoundLive()
{
	// Unibox CanCaptureObjective: capture logic only runs in a live round
	const SetupRoundState_t t = QuerySetupRoundState();
	if (!t.bValid)
		return true;   // unreadable — don't blind the capture job (Unibox: no rules -> true)

	if (t.bInWaitingForPlayers)
		return false;

	// GR_STATE_RND_RUNNING = 4, GR_STATE_STALEMATE = 7
	return t.iRoundState == 4 || t.iRoundState == 7;
}

bool CNavEngine::SmartJump(C_TFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vTarget)
{
	if (!pLocal || !pLocal->IsAlive())
		return false;

	bool bOnGround = (pLocal->m_fFlags() & FL_ONGROUND) != 0;
	if (!bOnGround)
	{
		// Already in air - if we're pressing jump, that's fine
		if (pCmd->buttons & IN_JUMP)
			return true;
		return false;
	}

	// Calculate jump trajectory prediction (ported from Amalgam's SmartJump)
	Vec3 vVelocity = pLocal->m_vecVelocity();

	// Factor in current movement input
	Vec3 vMoveInput(pCmd->forwardmove, -pCmd->sidemove, 0.0f);
	if (vMoveInput.Length() > 0.0f)
	{
		Vec3 vViewAngles = pCmd->viewangles;
		Vec3 vForward, vRight;
		Math::AngleVectors(vViewAngles, &vForward, &vRight, nullptr);
		vForward.z = vRight.z = 0.0f;
		vForward.Normalize();
		vRight.Normalize();

		Vec3 vRotatedMoveDir = vForward * vMoveInput.x + vRight * vMoveInput.y;
		vVelocity = vRotatedMoveDir.Normalized() * std::max(10.0f, vVelocity.Length2D());
	}

	// TF2 jump physics constants
	constexpr float flJumpForce = 277.0f;
	constexpr float flGravity = 800.0f;
	float flTimeToPeak = flJumpForce / flGravity;
	float flDistTravelled = vVelocity.Length2D() * flTimeToPeak;

	Vec3 vJumpDirection = vVelocity.Normalized();

	// Check if jump direction aligns with path direction
	Vec3 vPathDir = (vTarget - pLocal->m_vecOrigin());
	vPathDir.z = 0.0f;
	float flPathLen = vPathDir.Length();
	if (flPathLen > 0.1f)
	{
		vPathDir /= flPathLen;
		// Don't jump if we're not moving toward the target
		if (vJumpDirection.Dot(vPathDir) < 0.5f)
			return false;

		// Don't jump if the path turns sharply right after current crumb
		if (m_nCurrentCrumbIndex + 1 < m_vCrumbs.size())
		{
			Vec3 vNextDir = m_vCrumbs[m_nCurrentCrumbIndex + 1].m_vPos - m_vCrumbs[m_nCurrentCrumbIndex].m_vPos;
			vNextDir.z = 0.0f;
			if (vNextDir.Normalize() > 0.1f && vPathDir.Dot(vNextDir) < 0.707f)
			{
				// Path turns sharply - only jump if we're far from current crumb
				Vec3 vDistToCrumb = m_vCrumbs[m_nCurrentCrumbIndex].m_vPos - pLocal->m_vecOrigin();
				vDistToCrumb.z = 0.0f;
				if (vDistToCrumb.Length() < 100.0f)
					return false;
			}
		}

		vJumpDirection = vPathDir;
	}

	// Trace forward to predict landing position
	Vec3 vJumpPeakPos = pLocal->m_vecOrigin() + vJumpDirection * flDistTravelled;

	const Vec3 vHullMinSjump(-16.0f, -16.0f, 0.0f);
	const Vec3 vHullMaxSjump(16.0f, 16.0f, 62.0f);
	const Vec3 vHullMin(-23.99f, -23.99f, 0.0f);
	const Vec3 vHullMax(23.99f, 23.99f, 62.0f);
	const Vec3 vStepHeight(0.0f, 0.0f, 18.0f);
	const Vec3 vMaxJumpHeight(0.0f, 0.0f, 72.0f);

	Vec3 vTraceStart = pLocal->m_vecOrigin() + vStepHeight;
	Vec3 vTraceEnd = vTraceStart + vJumpDirection * flDistTravelled;

	CGameTrace forwardTrace = {};
	CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
	H::AimUtils->TraceHull(vTraceStart, vTraceEnd, vHullMinSjump, vHullMaxSjump, MASK_PLAYERSOLID_BRUSHONLY, &filter, &forwardTrace);

	if (forwardTrace.fraction < 1.0f && !IsSurfaceWalkable(forwardTrace.plane.normal))
	{
		// Hit a wall - trace down from hit point to find landing
		CGameTrace downwardTrace = {};
		H::AimUtils->TraceHull(forwardTrace.endpos, forwardTrace.endpos - vMaxJumpHeight, vHullMinSjump, vHullMaxSjump, MASK_PLAYERSOLID_BRUSHONLY, &filter, &downwardTrace);

		Vec3 vLandingPos = downwardTrace.endpos + vJumpDirection * 10.0f;
		CGameTrace landingTrace = {};
		H::AimUtils->TraceHull(vLandingPos + vMaxJumpHeight, vLandingPos, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &landingTrace);

		if (landingTrace.fraction > 0.0f && landingTrace.fraction < 0.75f)
		{
			if (IsSurfaceWalkable(landingTrace.plane.normal))
				return true;
		}
	}
	else if (forwardTrace.fraction >= 1.0f)
	{
		// No wall hit - trace down from end of trajectory to find ground
		CGameTrace downwardTrace = {};
		H::AimUtils->TraceHull(vTraceEnd, vTraceEnd - vMaxJumpHeight, vHullMinSjump, vHullMaxSjump, MASK_PLAYERSOLID_BRUSHONLY, &filter, &downwardTrace);

		if (downwardTrace.fraction > 0.0f && downwardTrace.fraction < 0.75f)
		{
			if (IsSurfaceWalkable(downwardTrace.plane.normal))
				return true;
		}
	}

	return false;
}

void CNavEngine::HandleSmartJump(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive())
	{
		m_eJumpState = JUMP_AWAITING;
		return;
	}

	bool bOnGround = (pLocal->m_fFlags() & FL_ONGROUND) != 0;
	bool bDucking = (pLocal->m_fFlags() & FL_DUCKING) != 0;
	float flCurTime = I::GlobalVars->curtime;

	// Reset state if we're on ground and ducking (landed from a jump)
	if (bOnGround && bDucking)
		m_eJumpState = JUMP_AWAITING;

	// Not pathing? Reset jump state
	if (!IsPathing())
	{
		m_eJumpState = JUMP_AWAITING;
		return;
	}

	// Get current crumb for jump prediction
	Vec3 vTarget = m_vCrumbs[m_nCurrentCrumbIndex].m_vPos;

	switch (m_eJumpState)
	{
	case JUMP_AWAITING:
	{
		// Check if we should jump:
		// 1. We're stuck (inactivity timer exceeded) - like Amalgam's stuck detection
		// 2. SmartJump predicts we can land the jump
		// 3. The next waypoint is significantly higher (height-based trigger)

		bool bShouldJump = false;

		// Check if we're stuck (like Amalgam's inactivity timer check)
		float flStuckTime = flCurTime - m_flInactivityTime;
		bool bIsStuck = flStuckTime > 1.0f;  // Stuck after 1 second of no progress

		// Check if we're on a nav area that disallows jumping
		bool bPreventJump = false;
		if (m_pMap && m_pMap->m_eState == NavState::Active)
		{
			CNavArea* pLocalArea = m_pMap->FindClosestNavArea(pLocal->m_vecOrigin());
			if (pLocalArea && (pLocalArea->m_iAttributeFlags & (NAV_MESH_NO_JUMP | NAV_MESH_STAIRS)))
				bPreventJump = true;
		}

		// Don't jump if next crumb is significantly below (going down a drop)
		if (m_nCurrentCrumbIndex + 1 < m_vCrumbs.size())
		{
			float flHeightDiff = m_vCrumbs[m_nCurrentCrumbIndex].m_vPos.z - m_vCrumbs[m_nCurrentCrumbIndex + 1].m_vPos.z;
			if (flHeightDiff < 0.0f && flHeightDiff <= -72.0f)  // PLAYER_JUMP_HEIGHT
				bPreventJump = true;
		}

		if (!bPreventJump && bOnGround)
		{
			if (bIsStuck)
			{
				// We're stuck - try SmartJump prediction
				if (SmartJump(pLocal, pCmd, vTarget))
					bShouldJump = true;
				else if (flStuckTime > 2.0f)
				{
					// Stuck for too long - just force a jump even without prediction
					// (like Amalgam's fallback stuck jump)
					bShouldJump = true;
				}
			}
			else
			{
				// Not stuck but check if we need to jump for height
				float flHeightDiff = vTarget.z - pLocal->m_vecOrigin().z;
				if (flHeightDiff > 18.0f)
				{
					// Target is higher - use SmartJump to verify we can make it
					if (SmartJump(pLocal, pCmd, vTarget))
						bShouldJump = true;
				}
			}
		}

		if (bShouldJump && (flCurTime - m_flLastJumpTime) > 0.2f)
		{
			m_eJumpState = JUMP_JUMPING;
			m_flLastJumpTime = flCurTime;
		}
		break;
	}

	case JUMP_JUMPING:
		// Execute the jump - release duck, press jump
		pCmd->buttons &= ~IN_DUCK;
		pCmd->buttons |= IN_JUMP;
		m_eJumpState = JUMP_ASCENDING;
		break;

	case JUMP_ASCENDING:
		// Duck while ascending for height optimization (like Amalgam)
		pCmd->buttons |= IN_DUCK;
		if (pLocal->m_vecVelocity().z <= 0.0f)
			m_eJumpState = JUMP_DESCENDING;
		else if (bOnGround)
			m_eJumpState = JUMP_AWAITING;
		break;

	case JUMP_DESCENDING:
		// Unduck while descending to land faster
		pCmd->buttons &= ~IN_DUCK;
		if (bOnGround)
		{
			// Landed - duck to absorb impact, then reset
			pCmd->buttons |= IN_DUCK;
			m_eJumpState = JUMP_AWAITING;
		}
		else
		{
			// Still in air - check if we should chain another jump
			if (SmartJump(pLocal, pCmd, vTarget))
			{
				pCmd->buttons &= ~IN_DUCK;
				pCmd->buttons |= IN_JUMP;
				m_eJumpState = JUMP_JUMPING;
				m_flLastJumpTime = flCurTime;
			}
		}
		break;
	}
}

// Ported from Amalgam's UpdateRespawnRooms
// Scans for CFuncRespawnRoom entities and sets TF_NAV_SPAWN_ROOM flags on nav areas
void CNavEngine::UpdateRespawnRooms()
{
	if (!m_pMap || m_pMap->m_eState != NavState::Active)
		return;

	// Collect all CFuncRespawnRoom entities
	struct SpawnRoom_t
	{
		int m_iTeam;
		Vec3 m_vWorldMins;
		Vec3 m_vWorldMaxs;
	};

	std::vector<SpawnRoom_t> vSpawnRooms;

	for (int i = 0; i < I::ClientEntityList->GetMaxEntities(); i++)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pEntity)
			continue;

		if (pEntity->GetClassId() != ETFClassIds::CFuncRespawnRoom)
			continue;

		auto pBase = pEntity->As<C_BaseEntity>();
		if (!pBase)
			continue;

		int iTeam = pBase->m_iTeamNum();
		Vec3 vOrigin = pBase->m_vecOrigin();
		Vec3 vMins = pBase->m_vecMins();
		Vec3 vMaxs = pBase->m_vecMaxs();

		// Compute world-space AABB
		Vec3 vWorldMins(vOrigin.x + vMins.x, vOrigin.y + vMins.y, vOrigin.z + vMins.z);
		Vec3 vWorldMaxs(vOrigin.x + vMaxs.x, vOrigin.y + vMaxs.y, vOrigin.z + vMaxs.z);

		vSpawnRooms.push_back({ iTeam, vWorldMins, vWorldMaxs });
	}

	if (vSpawnRooms.empty())
	{
		// No func_respawnroom entities visible client-side. Don't bail out —
		// the nav file usually carries authored SPAWN_ROOM flags, and the exit
		// derivation below only needs those. (The old early return latched the
		// update flag with an empty exit cache, so EscapeSpawn had no exit
		// candidates for the rest of the map.)
	}
	// Flag nav areas that are within spawn room entity volumes
	std::unordered_set<CNavArea*> setSpawnRoomAreas;

	for (auto& tRoom : vSpawnRooms)
	{
		uint32_t uFlags = (tRoom.m_iTeam == TF_TEAM_BLUE) ? TF_NAV_SPAWN_ROOM_BLUE : TF_NAV_SPAWN_ROOM_RED;

		for (auto& tArea : m_pMap->m_navfile.m_vAreas)
		{
			if (setSpawnRoomAreas.contains(&tArea))
				continue;

			// Check if any corner of the nav area is within the spawn room AABB
			static Vec3 vStepHeight(0.0f, 0.0f, 18.0f);
			Vec3 vPoints[] = {
				tArea.m_vCenter + vStepHeight,
				tArea.m_vNwCorner + vStepHeight,
				tArea.GetNeCorner() + vStepHeight,
				tArea.GetSwCorner() + vStepHeight,
				tArea.m_vSeCorner + vStepHeight
			};

			for (auto& vPoint : vPoints)
			{
				if (vPoint.x >= tRoom.m_vWorldMins.x && vPoint.x <= tRoom.m_vWorldMaxs.x &&
					vPoint.y >= tRoom.m_vWorldMins.y && vPoint.y <= tRoom.m_vWorldMaxs.y &&
					vPoint.z >= tRoom.m_vWorldMins.z && vPoint.z <= tRoom.m_vWorldMaxs.z)
				{
					setSpawnRoomAreas.insert(&tArea);
					if (!(tArea.m_iTFAttributeFlags & uFlags))
						tArea.m_iTFAttributeFlags |= uFlags;
					break;
				}
			}
		}
	}

	// Set spawn room exit attributes on areas connected to spawn rooms.
	// Derived from the SPAWN_ROOM flags themselves (set by the entity scan
	// above, or authored directly in the nav file) rather than the scan's set,
	// so exits exist even when no func_respawnroom entities are visible — the
	// file flags are always present. The EXIT flag check dedupes areas
	// connected from more than one spawn area.
	for (auto& tArea : m_pMap->m_navfile.m_vAreas)
	{
		if (!(tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE)))
			continue;
		for (auto& tConnection : tArea.m_vConnections)
		{
			CNavArea* pNext = tConnection.m_pArea;
			if (!pNext || pNext == &tArea)
				continue;
			if (pNext->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT))
				continue;
			pNext->m_iTFAttributeFlags |= TF_NAV_SPAWN_ROOM_EXIT;
			m_vRespawnRoomExitAreas.push_back(pNext);
		}
	}

	if (CFG::NavBot_Debug)
		NavDebug("UpdateRespawnRooms: %zu spawn exits (%zu rooms, %zu flagged areas)",
			m_vRespawnRoomExitAreas.size(), vSpawnRooms.size(), setSpawnRoomAreas.size());

	m_bUpdatedRespawnRooms = true;
}
