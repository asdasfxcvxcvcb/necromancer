#include "Map.h"
#include "../../../../SDK/SDK.h"
#include "../../CFG.h"
// G_NavEngine for the live setup-time check in AdjacentCost (gate areas are
// impassable only while setup actually runs). Map.h does not include this
// header, so there is no include cycle.
#include "NavEngine.h"

// Must come after SDK.h to avoid Windows min/max macro pollution
#undef min
#undef max
#include <algorithm>
#include <cmath>

void CMap::AdjacentCost(void* pArea, std::vector<micropather::StateCost>* pAdjacent)
{
	if (!pArea)
		return;

	CNavArea* pCurrentArea = reinterpret_cast<CNavArea*>(pArea);

	// Get local player for team-based filtering
	auto pLocal = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	int iTeam = 0;
	if (pLocal)
	{
		auto pLocalPlayer = pLocal->As<C_TFPlayer>();
		if (pLocalPlayer)
			iTeam = pLocalPlayer->m_iTeamNum();
	}

	// Iterate through all connections from this area
	for (NavConnect_t& tConnection : pCurrentArea->m_vConnections)
	{
		CNavArea* pNextArea = tConnection.m_pArea;
		if (!pNextArea || pNextArea == pCurrentArea)
			continue;

		// Memoized static evaluation — points, dropdown traces, heights, gaps.
		// Everything here only depends on the mesh, not on game state.
		const ConnEval_t& tEval = GetConnEval(pCurrentArea, pNextArea);
		if (tEval.m_bSkip)
			continue;

		// Corridor-blocked connection (ported from Unibox's GetAdjacent): a link
		// that path validation or stuck detection proved impassable with real
		// traces is excluded from the solve entirely, so repaths route around
		// the wall instead of solving straight back into it.
		bool bConnPassable = false;
		if (GetConnVerdict(pCurrentArea, pNextArea, bConnPassable) && !bConnPassable)
			continue;

		// === Team-dependent pass rules (cheap flag tests, kept dynamic) ===

		// Enemy spawn room check (ported from TF2 bot's CTFBotPathCost).
		// TF2 bot returns -1.0f (impassable) for enemy spawn rooms unless the
		// round has been won. EXCLUSIVE semantics: an area flagged for BOTH
		// teams (cp_dustbowl's shared spawn room carries RED|BLUE on every
		// room area — verified by parsing the shipped nav file) is also OUR
		// spawn and must stay walkable. The bare flag test sealed every edge
		// out of spawn there: area 11's three links all enter 0x6-flagged
		// areas, its reachable set was {11}, and every solve returned
		// "no path" until the bot was physically dragged past the door band.
		const bool bNextRedSpawn = pNextArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED;
		const bool bNextBlueSpawn = pNextArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE;
		if (iTeam == TF_TEAM_RED && bNextBlueSpawn && !bNextRedSpawn)
			continue;
		if (iTeam == TF_TEAM_BLUE && bNextRedSpawn && !bNextBlueSpawn)
			continue;

		// Setup gate check. Valve's CTFBotPathCost has NO setup-gate rule — the
		// server blocks gate areas through the door-blocker system while the gate
		// brush is physically closed and clears it the moment the gate opens; the
		// static flag only marks that block "temporary" (both server call sites
		// that check the flag do so to IGNORE the block). The old port skipped
		// gate areas by team FOREVER, permanently sealing the spawn basin behind
		// its gate after setup ended — every solve out of spawn returned
		// "no path" until the bot was physically walked past the gate band.
		// Impassable only while setup is actually running right now (a closed
		// gate brush is solid for both teams, hence no team condition —
		// AreSetupGatesClosed() is the team-agnostic check; IsSetupTime()
		// holds only BLU, so RED defenders' paths would otherwise solve
		// straight through the closed BLU gates).
		if (G_NavEngine.AreSetupGatesClosed() &&
			(pNextArea->m_iTFAttributeFlags & (TF_NAV_BLUE_SETUP_GATE | TF_NAV_RED_SETUP_GATE)))
			continue;

		// === Cost: static geometric part + dynamic penalties ===
		float flCost = tEval.m_flStaticCost;
		const float flForwardDistance = tEval.m_flForwardDistance;

		// Spawn room penalty (ported from TF2 bot's CTFBotPathCost)
		// Enemy spawn rooms are already blocked above; penalize own spawn to
		// discourage loitering. Shared rooms (both flags) count as own spawn
		// for either team.
		if ((iTeam == TF_TEAM_RED && bNextRedSpawn) || (iTeam == TF_TEAM_BLUE && bNextBlueSpawn))
			flCost += 100.f;

		// Jump area penalty (ported from TF2 bot's CTFBotPathCost)
		if (pNextArea->m_iAttributeFlags & NAV_MESH_JUMP)
			flCost *= 2.0f;

		// Cliff area penalty (NAV_MESH_CLIFF — adjacent to a lethal drop)
		if (pNextArea->m_iAttributeFlags & NAV_MESH_CLIFF)
			flCost += flForwardDistance * 3.0f;

		// Sentry danger area penalty (ported from TF2 bot's CTFBotPathCost SAFEST_ROUTE)
		if (iTeam == TF_TEAM_RED && (pNextArea->m_iTFAttributeFlags & TF_NAV_BLUE_SENTRY_DANGER))
			flCost *= 5.0f;
		else if (iTeam == TF_TEAM_BLUE && (pNextArea->m_iTFAttributeFlags & TF_NAV_RED_SENTRY_DANGER))
			flCost *= 5.0f;

		// AVOID flag penalty
		if (pNextArea->m_iAttributeFlags & NAV_MESH_AVOID)
			flCost += 100000.f;

		// Crouch area penalty
		if (pNextArea->m_iAttributeFlags & NAV_MESH_CROUCH)
			flCost += flForwardDistance * 5.f;

		// === Cover spot preference (ported from TF2 bot's hiding spot usage) ===
		// TF2 bots use IN_COVER hiding spots to take cover from sentries and heal safely.
		// When the local player is low on health, prefer areas that have cover spots.
		// This makes the pathfinder route through safe spots when seeking health.
		if (!m_mDangerBlacklist.empty() || (iTeam != 0 && pNextArea->m_uHidingSpotCount > 0))
		{
			// Check if this area has cover spots
			bool bHasCover = false;
			for (const auto& tSpot : pNextArea->m_vHidingSpots)
			{
				if (tSpot.HasGoodCover())
				{
					bHasCover = true;
					break;
				}
			}

			if (bHasCover)
			{
				// Discount for areas with cover — makes pathfinder prefer routes through cover
				// Larger discount when in danger (escaping), smaller discount otherwise
				bool bInDanger = false;
				if (auto itDanger = m_mDangerBlacklist.find(pNextArea); itDanger != m_mDangerBlacklist.end())
					bInDanger = false; // This area IS in danger, no discount
				else
					bInDanger = !m_mDangerBlacklist.empty(); // Other areas are in danger

				if (bInDanger)
					flCost -= flForwardDistance * 0.5f; // Strong preference when escaping
				else
					flCost -= flForwardDistance * 0.15f; // Mild preference otherwise
			}

			// Penalty for exposed spots — avoid walking through the open when in danger
			bool bHasExposed = false;
			for (const auto& tSpot : pNextArea->m_vHidingSpots)
			{
				if (tSpot.IsExposed())
				{
					bHasExposed = true;
					break;
				}
			}
			if (bHasExposed && !m_mDangerBlacklist.empty())
				flCost += flForwardDistance * 0.3f; // Avoid exposed areas when in danger
		}

		// Apply unreliable connection penalty
		if (tEval.m_flUnreliablePenalty > 0.0f)
			flCost += tEval.m_flUnreliablePenalty;

		// Stuck blacklist penalty (ported from Amalgam)
		// If this area is blacklisted due to being stuck, add a massive penalty
		float flCurTime = I::GlobalVars->curtime;
		if (auto itBlacklist = m_mStuckBlacklist.find(pNextArea); itBlacklist != m_mStuckBlacklist.end())
		{
			if (itBlacklist->second.m_flExpireTime <= 0.0f || itBlacklist->second.m_flExpireTime > flCurTime)
				flCost += itBlacklist->second.m_flPenalty;
			else
				m_mStuckBlacklist.erase(itBlacklist);  // Expired, remove
		}

		// Danger blacklist penalty (ported from Amalgam's FreeBlacklist)
		if (auto itDanger = m_mDangerBlacklist.find(pNextArea); itDanger != m_mDangerBlacklist.end())
		{
			auto& entry = itDanger->second;
			bool bExpired = false;
			if (entry.m_iExpireTick > 0 && I::GlobalVars->tickcount > entry.m_iExpireTick)
				bExpired = true;
			if (entry.m_flExpireTime > 0.0f && flCurTime > entry.m_flExpireTime)
				bExpired = true;

			if (bExpired)
				m_mDangerBlacklist.erase(itDanger);
			else
				flCost += GetDangerPenalty(entry.m_eReason);
		}

		// Connection stuck time penalty (ported from Amalgam)
		// If we've been stuck on this specific connection before, add a penalty
		if (auto itFrom = m_mConnectionStuckTime.find(pCurrentArea); itFrom != m_mConnectionStuckTime.end())
		{
			if (auto itTo = itFrom->second.find(pNextArea); itTo != itFrom->second.end())
			{
				if (itTo->second.m_flExpireTime <= 0.0f || itTo->second.m_flExpireTime > flCurTime)
					flCost += itTo->second.m_iStuckTicks * 50.0f;  // Penalty scales with stuck duration
				else
					itFrom->second.erase(itTo);  // Expired, remove
			}
		}

		// Route variety system (ported from TF2 bot's CTFBotPathCost DEFAULT_ROUTE)
		// Adds a time-varying per-area preference penalty so the bot chooses different routes
		// over time, but keeps the same route long enough for repaths to work.
		// Without this, the bot always picks the mathematically shortest path and never varies.
		if (CFG::NavBot_RouteVariety)
		{
			// Time modulus changes every ~10 seconds, causing route preference to shift
			int iTimeMod = static_cast<int>(I::GlobalVars->curtime / 10.0f) + 1;
			// Use local player entindex and area ID as seeds (like TF2 bot's entindex * areaID)
			int iLocalPlayerIndex = I::EngineClient->GetLocalPlayer();
			// Hash the area pointer for a stable per-area value
			auto nAreaID = reinterpret_cast<uintptr_t>(pNextArea);
			nAreaID = (nAreaID >> 4) ^ (nAreaID & 0xF);  // Mix bits
			// Strength-scaled multiplier: at strength 0.5 costs vary between 1x and 2x,
			// which shifts the choice between near-equal routes without producing wild
			// detours. (The old fixed 50x factor made costs range 1x-101x and regularly
			// picked paths much longer than necessary.)
			float flPreference = 1.0f + CFG::NavBot_RouteVarietyStrength *
				(1.0f + cosf(static_cast<float>(iLocalPlayerIndex * nAreaID * iTimeMod)));
			flCost *= flPreference;
		}

		// Skip if cost is invalid
		flCost = (std::max)(flCost, 1.f);
		if (!std::isfinite(flCost) || flCost <= 0.f)
			continue;

		// Add to adjacent list
		pAdjacent->push_back({ reinterpret_cast<void*>(pNextArea), flCost });
	}
}

DropdownHint_t CMap::HandleDropdown(const Vec3& vCurrentPos, const Vec3& vNextPos, bool bIsOneWay)
{
	DropdownHint_t tHint{};
	tHint.m_vAdjustedPos = vCurrentPos;

	Vec3 vToTarget = vNextPos - vCurrentPos;
	const float flHeightDiff = vToTarget.z;

	Vec3 vHorizontal = vToTarget;
	vHorizontal.z = 0.f;
	const float flHorizontalLength = vHorizontal.Length();

	const float kSmallDropGrace = 18.f;

	if (flHeightDiff < 0.f)
	{
		const float flDropDistance = -flHeightDiff;
		if (flDropDistance > kSmallDropGrace && flHorizontalLength > 1.f)
		{
			tHint.m_bRequiresDrop = true;
			tHint.m_flDropHeight = flDropDistance;
			tHint.m_vApproachDir = vHorizontal / flHorizontalLength;

			if (!bIsOneWay)
			{
				Vec3 vDirection = tHint.m_vApproachDir;

				// === TF2 NextBot-style edge detection (ported from NextBotPath.cpp) ===
				// The TF2 bot traces a hull downward at incremental positions to find
				// where there's actual clearance to drop. The navmesh area boundary
				// often doesn't align with the real geometry edge, so we need to
				// push the drop position outward until we find open air.
				//
				// Original TF2 code:
				//   const float inc = 10.0f;
				//   const float maxPushDist = 2.0f * hullWidth;
				//   for (pushDist = 0.0f; pushDist <= maxPushDist; pushDist += inc)
				//   {
				//       UTIL_TraceHull(pos, lowerPos, hullMin, hullMax, mask, filter, &result);
				//       if (result.fraction >= 1.0f) break;  // found clearance
				//   }
				float flPushDist = 0.0f;
				const float flInc = 10.0f;
				const float flMaxPushDist = 2.0f * PLAYER_WIDTH;  // ~98 units
				const float flHalfWidth = PLAYER_WIDTH / 2.0f;     // ~24.5 units
				const float flHullHeight = PLAYER_HEIGHT;           // 83 units
				const float flStepHeight = 18.0f;

				// Use world-only trace filter — we only care about solid geometry, not players/entities
				CTraceFilterWorldAndPropsOnlyAmalgam traceFilter;
				traceFilter.pSkip = nullptr;

				for (flPushDist = 0.0f; flPushDist <= flMaxPushDist; flPushDist += flInc)
				{
					// Candidate position: pushed outward from the current pos along approach direction
					Vec3 vCandidate(vCurrentPos.x + flPushDist * vDirection.x,
					                vCurrentPos.y + flPushDist * vDirection.y,
					                vCurrentPos.z);

					// Lower position: same XY, but at the next area's Z height
					Vec3 vLower(vCandidate.x, vCandidate.y, vNextPos.z);

					trace_t tr;
					Vec3 vHullMin(-flHalfWidth, -flHalfWidth, flStepHeight);
					Vec3 vHullMax(flHalfWidth, flHalfWidth, flHullHeight);

					H::AimUtils->TraceHull(vCandidate, vLower, vHullMin, vHullMax,
						MASK_PLAYERSOLID, &traceFilter, &tr);

					if (tr.fraction >= 1.0f && !tr.startsolid)
					{
						// Found clearance to drop — this is the real edge
						break;
					}
				}

				// Set the approach distance to the push distance we found
				// If we couldn't find clearance (flPushDist > maxPushDist), use the max
				flPushDist = (std::min)(flPushDist, flMaxPushDist);
				tHint.m_flApproachDistance = (std::max)(flPushDist, 0.f);

				// Adjusted position: pushed outward to the real geometry edge
				tHint.m_vAdjustedPos = vCurrentPos + vDirection * tHint.m_flApproachDistance;
				tHint.m_vAdjustedPos.z = vCurrentPos.z;
			}
		}
	}
	else if (!bIsOneWay && flHeightDiff > 0.f && flHorizontalLength > 1.f)
	{
		Vec3 vDirection = vHorizontal / flHorizontalLength;

		// Step back slightly to help with climbing onto the next area.
		const float retreat = (std::clamp)(flHeightDiff * 0.35f, PLAYER_WIDTH * 0.3f, PLAYER_WIDTH);
		tHint.m_vAdjustedPos = vCurrentPos - vDirection * retreat;
		tHint.m_vAdjustedPos.z = vCurrentPos.z;
		tHint.m_vApproachDir = -vDirection;
		tHint.m_flApproachDistance = retreat;
	}

	return tHint;
}

NavPoints_t CMap::DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea, bool bIsOneWay)
{
	auto vCurrentCenter = pCurrentArea->m_vCenter;
	auto vNextCenter = pNextArea->m_vCenter;
	
	// Gets a vector on the edge of the current area that is as close as possible to the center of the next area
	auto vCurrentClosest = pCurrentArea->GetNearestPoint(Vector2D(vNextCenter.x, vNextCenter.y));
	// Do the same for the other area
	auto vNextClosest = pNextArea->GetNearestPoint(Vector2D(vCurrentCenter.x, vCurrentCenter.y));

	// Use one of them as a center point, the one that is either x or y aligned with a center
	// Of the areas. This will avoid walking into walls.
	auto vClosest = vCurrentClosest;

	// Determine if aligned, if not, use the other one as the center point
	if (vClosest.x != vCurrentCenter.x && vClosest.y != vCurrentCenter.y && vClosest.x != vNextCenter.x && vClosest.y != vNextCenter.y)
	{
		vClosest = vNextClosest;
		// Use the point closest to next_closest on the "original" mesh for z
		vClosest.z = pCurrentArea->GetNearestPoint(Vector2D(vNextClosest.x, vNextClosest.y)).z;
	}

	// If safepathing is enabled, adjust points to stay more centered and avoid corners
	// (ported from Amalgam's SafePathing — conditional, not always-on)
	if (!bIsOneWay && CFG::NavBot_SafePathing)
	{
		// Calculate center point as a weighted average between area centers
		// Use a 60/40 split to favor the current area more
		vClosest = vCurrentCenter + (vNextCenter - vCurrentCenter) * 0.4f;

		// Add extra safety margin near corners
		float flCornerMargin = PLAYER_WIDTH * 0.75f;

		// Check if we're near a corner by comparing distances to area edges
		bool bNearCorner = false;
		Vec3 vCurrentMins = pCurrentArea->m_vNwCorner;
		Vec3 vCurrentMaxs = pCurrentArea->m_vSeCorner;

		if (vClosest.x - vCurrentMins.x < flCornerMargin ||
			vCurrentMaxs.x - vClosest.x < flCornerMargin ||
			vClosest.y - vCurrentMins.y < flCornerMargin ||
			vCurrentMaxs.y - vClosest.y < flCornerMargin)
			bNearCorner = true;

		// If near corner, move point more towards center
		if (bNearCorner)
		{
			Vec3 vToCenter = vCurrentCenter - vClosest;
			float flLen = vToCenter.Length();
			if (flLen > 0.f)
				vClosest = vClosest + (vToCenter / flLen) * flCornerMargin;
		}

		// Ensure the point is within the current area
		vClosest = pCurrentArea->GetNearestPoint(Vector2D(vClosest.x, vClosest.y));
	}

	// Nearest point to center on "next" (like Amalgam — no extra corner margin)
	auto vCenterNext = pNextArea->GetNearestPoint(Vector2D(vClosest.x, vClosest.y));

	return NavPoints_t(vCurrentCenter, vClosest, vCenterNext, vNextCenter);
}

bool CMap::IsOneWay(CNavArea* pFrom, CNavArea* pTo) const
{
	if (!pFrom || !pTo)
		return true;

	for (auto& tBackConnection : pTo->m_vConnections)
	{
		if (tBackConnection.m_pArea == pFrom)
			return false;
	}

	return true;
}

const ConnEval_t& CMap::GetConnEval(CNavArea* pFrom, CNavArea* pTo)
{
	if (!pFrom || !pTo)
	{
		static ConnEval_t tInvalid;
		return tInvalid;
	}

	// SafePathing changes the connection points — rebuild the whole cache on toggle
	if (m_bConnCacheSafePathing != CFG::NavBot_SafePathing)
	{
		m_mConnEvalCache.clear();
		m_bConnCacheSafePathing = CFG::NavBot_SafePathing;
	}

	const uint64_t uKey = (static_cast<uint64_t>(pFrom->m_uId) << 32) | static_cast<uint64_t>(pTo->m_uId);
	auto it = m_mConnEvalCache.find(uKey);
	if (it != m_mConnEvalCache.end())
		return it->second;

	ConnEval_t tEval = BuildConnEval(pFrom, pTo);
	return m_mConnEvalCache.emplace(uKey, tEval).first->second;
}

ConnEval_t CMap::BuildConnEval(CNavArea* pCurrentArea, CNavArea* pNextArea)
{
	ConnEval_t tEval;

	// Determine if this is a one-way connection
	tEval.m_bIsOneWay = IsOneWay(pCurrentArea, pNextArea);

	// Determine navigation points
	tEval.m_tPoints = DeterminePoints(pCurrentArea, pNextArea, tEval.m_bIsOneWay);

	// Handle dropdown/vertical navigation
	tEval.m_tDropdown = HandleDropdown(tEval.m_tPoints.m_vCenter, tEval.m_tPoints.m_vNext, tEval.m_bIsOneWay);
	tEval.m_tPoints.m_vCenter = tEval.m_tDropdown.m_vAdjustedPos;

	// === Ported from TF2 bot's CTFBotPathCost::operator() and IsAreaTraversable ===
	// These checks determine if an area is completely impassable (skip it)
	// vs. just costly (add penalty). The TF2 bot returns -1.0f for impassable,
	// which we emulate by skipping the connection.

	// TF_NAV_BLOCKED check (ported from TF2 bot's IsAreaTraversable)
	// Areas can be blocked by doors, capture point logic, etc.
	if (pNextArea->m_iTFAttributeFlags & TF_NAV_BLOCKED)
	{
		if (CFG::NavBot_Debug)
			NavDebug("conn %u->%u skipped: dest area flagged TF_NAV_BLOCKED",
				pCurrentArea->m_uId, pNextArea->m_uId);
		tEval.m_bSkip = true;
		return tEval;
	}

	// Point-capture gated areas (TF2 marks these on the mesh; the server opens
	// and closes them as points are captured). The FILE flag alone says neither
	// which point gates the area nor whether that cap already happened, so
	// skipping on the flag alone can permanently seal map regions — the same
	// failure class as the setup-gate skip. Penalize instead: UNTIL areas sit
	// behind a closed door early round (avoided unless they are the only route —
	// the approach phase stands and waits at the door if they truly are), and
	// AFTER areas are open until a capture closes them, where the door wait plus
	// blacklist self-heals.
	if (pNextArea->m_iTFAttributeFlags & TF_NAV_BLOCKED_UNTIL_POINT_CAPTURE)
		tEval.m_flUnreliablePenalty += 20000.0f;
	if (pNextArea->m_iTFAttributeFlags & TF_NAV_BLOCKED_AFTER_POINT_CAPTURE)
		tEval.m_flUnreliablePenalty += 100.0f;

	// Check height difference at the shared edge between areas (ported from TF2 bot)
	// TF2 bot uses ComputeAdjacentConnectionHeightChange which checks the height at the
	// boundary where you actually step, NOT center-to-center. For stairs, the edge height
	// is much smaller than the center height — center-to-center incorrectly blocks stairs.
	tEval.m_flHeightDiff = pCurrentArea->ComputeAdjacentConnectionHeightChange(pNextArea);

	// On stairs, skip the height block entirely — the nav mesh marks stairs as walkable,
	// and the physics engine handles step-ups automatically (like TF2 bot's PathFollower).
	bool bStairConnection = (pCurrentArea->m_iAttributeFlags & NAV_MESH_STAIRS) ||
	                        (pNextArea->m_iAttributeFlags & NAV_MESH_STAIRS);

	if (!bStairConnection)
	{
		// Block if the step-up at the edge exceeds max jump height
		// TF2 bot: if deltaZ >= GetMaxJumpHeight() return -1 (impassable)
		if (tEval.m_flHeightDiff > PLAYER_CROUCHED_JUMP_HEIGHT)
		{
			tEval.m_bSkip = true;
			return tEval;
		}
	}

	// Death drop height check (ported from TF2 bot's CTFBotPathCost)
	// TF2 bot's GetDeathDropHeight() returns 1000.0f — drops beyond this are lethal
	if (tEval.m_flHeightDiff < -1000.0f)
	{
		tEval.m_bSkip = true;
		return tEval;
	}

	// === Connection walk validation ===
	// The mesh generator only marks areas ADJACENT; it says nothing about the
	// straight walk between them. DeterminePoints lands the transition crumb on
	// the area edge nearest the next center, and that point can sit on a walled
	// stretch of the shared boundary (door frames, L-joints, off-center doors).
	// A path through such a connection aims the bot at a crumb behind a wall —
	// the "walking into walls" report. Hull-trace the actual walk (center → edge,
	// edge → next center) and penalize blocked connections hard so the pather
	// only uses them when no clean route exists. Memoized like everything else
	// here, so each connection pays the traces once per map.
	// Stair connections skip validation: the mesh vouches for them and a straight
	// probe cuts across every riser (the first version of this check flagged
	// whole stairwells as blocked).
	if (!bStairConnection)
	{
		const Vec3 vHullMin(-NAV_HULL_HALF_WIDTH, -NAV_HULL_HALF_WIDTH, 0.0f);
		const Vec3 vHullMax(NAV_HULL_HALF_WIDTH, NAV_HULL_HALF_WIDTH, 62.0f);
		CTraceFilterWorldAndPropsOnlyAmalgam filter = {};
		CGameTrace trace = {};

		// Probe the straight walk at one height. 1 = blocked by static world,
		// 0 = clear, -1 = no usable signal (started inside geometry, or the
		// blocker is a mover that will open later).
		auto TraceBlockedAt = [&vHullMin, &vHullMax, &filter, &trace](const Vec3& vFrom, const Vec3& vTo, float flProbeZ) -> int
		{
			const Vec3 vOffset(0.0f, 0.0f, flProbeZ);
			H::AimUtils->TraceHull(vFrom + vOffset, vTo + vOffset, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &trace);
			if (trace.startsolid)
				return -1;   // mesh imprecision — no usable signal
			if (trace.fraction >= 0.9f)
				return 0;
			// Only STATIC world geometry counts. This evaluation is memoized for
			// the whole map, and brush-entity blockers (setup gates, doors) open
			// later — baking their closed state in would permanently penalize the
			// main spawn exit. Same world-vs-entity rule as the approach blocker.
			if (trace.m_pEnt && trace.m_pEnt->entindex() != 0)
				return -1;
			return 1;
		};

		// A low probe alone is not evidence of a wall: on stairs and ramps the
		// straight line clips the next riser even though the walk is fine. Probe
		// at step height, then near chest height — only geometry that blocks
		// BOTH probes is a real wall, and real walls are taller than 48.
		auto IsWalkBlocked = [&TraceBlockedAt](const Vec3& vFrom, const Vec3& vTo) -> bool
		{
			if ((vTo - vFrom).Length2D() < 1.0f)
				return false;
			if (TraceBlockedAt(vFrom, vTo, 18.0f) != 1)
				return false;
			return TraceBlockedAt(vFrom, vTo, 48.0f) == 1;
		};

		if (IsWalkBlocked(tEval.m_tPoints.m_vCurrent, tEval.m_tPoints.m_vCenter))
			tEval.m_flUnreliablePenalty += 4000.0f;
		if (IsWalkBlocked(tEval.m_tPoints.m_vCenter, tEval.m_tPoints.m_vNext))
			tEval.m_flUnreliablePenalty += 4000.0f;

		if (tEval.m_flUnreliablePenalty > 0.0f && CFG::NavBot_Debug)
			NavDebug("conn %u->%u blocked walk (penalty %.0f)", pCurrentArea->m_uId, pNextArea->m_uId, tEval.m_flUnreliablePenalty);
	}

	// === Two-way connection reliability check ===
	// Navmesh generation can mark two areas as two-way connected even when there's
	// a significant horizontal gap + height difference between them that makes the
	// connection impassable on foot (and sometimes even with jumping).
	// If the gap between the connection points is large AND there's a height step,
	// add a heavy penalty to make the pathfinder avoid this unreliable connection.
	if (!tEval.m_bIsOneWay)
	{
		Vec3 vGap = tEval.m_tPoints.m_vCenterNext - tEval.m_tPoints.m_vCenter;
		float flGapHorizontal = Vec3(vGap.x, vGap.y, 0.0f).Length();
		float flGapHeight = std::abs(vGap.z);

		// Player can step up 18 units normally. If the gap is wider than the player
		// AND there's a height difference beyond step height, the connection is suspect.
		// Penalty-only, never a skip: the mesh generator marks two-way links it
		// vouches for, and long steep ramps read as wide-gap-with-height from the
		// transition point to the next center. The old skip branch could disconnect
		// whole ramp routes out of spawn, and the old assignment also OVERWROTE any
		// walk-block penalty accumulated above instead of adding to it.
		if (flGapHorizontal > PLAYER_WIDTH && flGapHeight > 18.0f)
			tEval.m_flUnreliablePenalty += (std::max)(flGapHorizontal * flGapHeight * 0.5f, 5000.0f);
	}

	// === Static geometric cost (ported from TF2 bot's CTFBotPathCost) ===
	auto HorizontalDistance = [](const Vec3& vStart, const Vec3& vEnd) -> float
	{
		Vec3 vFlat = vEnd - vStart;
		vFlat.z = 0.f;
		float flLen = vFlat.Length();
		return flLen > 0.f ? flLen : 0.f;
	};

	float flForwardDistance = (std::max)(HorizontalDistance(tEval.m_tPoints.m_vCurrent, tEval.m_tPoints.m_vNext), 1.f);
	tEval.m_flForwardDistance = flForwardDistance;
	float flDeviationStart = HorizontalDistance(tEval.m_tPoints.m_vCurrent, tEval.m_tPoints.m_vCenter);
	float flDeviationEnd = HorizontalDistance(tEval.m_tPoints.m_vCenter, tEval.m_tPoints.m_vNext);

	// Base cost is forward distance
	float flCost = flForwardDistance;

	// Add deviation penalties (SafePathing cost)
	flCost += flDeviationStart * 0.3f;
	flCost += flDeviationEnd * 0.2f;

	// Height penalties — reduced for stairs (physics engine handles step-ups automatically)
	if (tEval.m_flHeightDiff > 0.f)
	{
		// On stairs, height penalty is minimal — just walking up steps
		float flHeightMultiplier = bStairConnection ? 0.2f : 1.8f;
		flCost += tEval.m_flHeightDiff * flHeightMultiplier;
	}
	else if (tEval.m_flHeightDiff < -8.f)
		flCost += std::abs(tEval.m_flHeightDiff) * 0.9f;

	// Dropdown penalties
	if (tEval.m_tDropdown.m_bRequiresDrop)
	{
		flCost += tEval.m_tDropdown.m_flDropHeight * 2.2f;
		flCost += tEval.m_tDropdown.m_flApproachDistance * 0.45f;
	}
	else if (tEval.m_tDropdown.m_flApproachDistance > 0.f)
		flCost += tEval.m_tDropdown.m_flApproachDistance * 0.25f;

	// Turn penalty - prefer straighter paths
	Vec3 vForward = tEval.m_tPoints.m_vCenter - tEval.m_tPoints.m_vCurrent;
	Vec3 vForwardNext = tEval.m_tPoints.m_vNext - tEval.m_tPoints.m_vCenter;
	vForward.z = 0.f;
	vForwardNext.z = 0.f;
	float flLen1 = vForward.Length();
	float flLen2 = vForwardNext.Length();
	if (flLen1 > 1.f && flLen2 > 1.f)
	{
		vForward = vForward / flLen1;
		vForwardNext = vForwardNext / flLen2;
		float flDot = (std::clamp)(vForward.Dot(vForwardNext), -1.f, 1.f);
		float flTurnPenalty = (1.f - flDot) * 30.f;
		flCost += flTurnPenalty;
	}

	// Bonus for larger areas (more room to maneuver)
	Vec3 vAreaExtent = pNextArea->m_vSeCorner - pNextArea->m_vNwCorner;
	vAreaExtent.z = 0.f;
	float flAreaSize = vAreaExtent.Length();
	if (flAreaSize > 0.f)
		flCost -= (std::clamp)(flAreaSize * 0.01f, 0.f, 12.f);

	tEval.m_flStaticCost = flCost;
	return tEval;
}

std::vector<CNavArea*> CMap::FindClosestNavAreas(const Vec3& vPos, int nCount, float flMaxDist)
{
	// Like TF2 NextBot's GetNearestNavArea: prefer areas the position is actually inside
	// (IsOverlapping), falling back to closest center distance on the same floor.
	// Returns up to nCount areas, best-first.

	std::vector<CNavArea*> vResult;
	if (nCount <= 0)
		return vResult;

	const float flMaxDistSqr = flMaxDist * flMaxDist;

	// Overlapping areas always rank before non-overlapping ones regardless of distance
	// (standing inside an area beats any area you're merely near).
	struct Candidate_t
	{
		CNavArea* m_pArea = nullptr;
		float m_flScore = 0.0f;
		bool m_bOverlapping = false;
	};
	std::vector<Candidate_t> vCandidates;
	vCandidates.reserve(64);

	for (auto& tArea : m_navfile.m_vAreas)
	{
		bool bOverlapping = tArea.IsOverlapping(vPos) &&
			vPos.z >= tArea.m_flMinZ && vPos.z <= tArea.m_flMaxZ + 100.0f;

		// The center-distance cap only applies to fallback (non-overlapping)
		// candidates. An area the position stands ON must never be excluded by how
		// far away its center is: spawn rooms are meshed as a few huge rectangles,
		// and a bot at a spawn point can be 600+ units from the area center. With
		// the cap applied there, the local area came back null at spawn, every
		// NavTo failed, and the bot froze in place (gone once nudged off the mesh).
		if (!bOverlapping)
		{
			float flDist = tArea.m_vCenter.DistToSqr(vPos);
			if (flDist > flMaxDistSqr)
				continue;

			// Fallback candidates must be on the same floor. Without this Z check the
			// fallback picks areas on different floors or across gaps, causing the bot
			// to path through non-navmesh space or oscillate between areas.
			float flHeightDiff = std::abs(tArea.m_vCenter.z - vPos.z);
			if (flHeightDiff >= 100.0f)
				continue;
		}

		vCandidates.push_back({ &tArea, tArea.m_vCenter.DistToSqr(vPos), bOverlapping });
	}

	size_t uTake = (std::min)(static_cast<size_t>(nCount), vCandidates.size());
	std::partial_sort(vCandidates.begin(), vCandidates.begin() + uTake, vCandidates.end(),
		[](const Candidate_t& a, const Candidate_t& b)
		{
			if (a.m_bOverlapping != b.m_bOverlapping)
				return a.m_bOverlapping;
			return a.m_flScore < b.m_flScore;
		});

	for (size_t i = 0; i < uTake; i++)
		vResult.push_back(vCandidates[i].m_pArea);

	return vResult;
}

CNavArea* CMap::FindClosestNavArea(const Vec3& vPos, float flMaxDist)
{
	auto vAreas = FindClosestNavAreas(vPos, 1, flMaxDist);
	return vAreas.empty() ? nullptr : vAreas[0];
}

bool CMap::HasUsableOutgoing(CNavArea* pArea)
{
	if (!pArea || pArea->m_vConnections.empty())
		return false;

	// Static connection evaluations are memoized, so this is cheap after the
	// first call per connection. Team/setup-gate rules are deliberately NOT
	// considered — graph connectivity is what matters for choosing a start area.
	// Dynamic corridor blocks count too: a start area whose every exit is
	// currently marked blocked can't produce a solve, and picking it anyway
	// starts paths in a world the bot hasn't physically reached (stuck marks
	// then rotate over its out-edges forever instead of converging).
	for (auto& tConnection : pArea->m_vConnections)
	{
		CNavArea* pNextArea = tConnection.m_pArea;
		if (!pNextArea || pNextArea == pArea)
			continue;

		if (GetConnEval(pArea, pNextArea).m_bSkip)
			continue;

		bool bPassable = false;
		if (GetConnVerdict(pArea, pNextArea, bPassable) && !bPassable)
			continue;

		return true;
	}

	return false;
}

// Ported from Unibox (Map.cpp:615): a directed mesh link with sane height
// deltas. Upward steps beyond jump height and lethal drops disqualify the link
// even though the connection list contains it.
bool CMap::HasDirectConnection(CNavArea* pFrom, CNavArea* pTo) const
{
	if (!pFrom || !pTo)
		return false;
	if (pFrom == pTo)
		return true;

	for (auto& tConnection : pFrom->m_vConnections)
	{
		if (tConnection.m_pArea != pTo)
			continue;
		if (pTo->m_flMinZ - pFrom->m_flMaxZ > PLAYER_JUMP_HEIGHT)
			return false;
		if (pFrom->m_flMinZ - pTo->m_flMaxZ > PLAYER_DEATH_DROP_HEIGHT)
			return false;
		return true;
	}
	return false;
}

Vec3 CMap::FindHidingSpot(const Vec3& vFrom, float flMaxDist, unsigned char uRequiredFlags, int iTeam)
{
	if (m_eState != NavState::Active)
		return Vec3(0, 0, 0);

	float flBestDist = FLT_MAX;
	Vec3 vBestSpot = Vec3(0, 0, 0);

	float flCurTime = I::GlobalVars->curtime;

	for (auto& tArea : m_navfile.m_vAreas)
	{
		// Skip enemy spawn rooms
		if (iTeam == TF_TEAM_RED && (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE))
			continue;
		if (iTeam == TF_TEAM_BLUE && (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED))
			continue;

		// Skip blocked areas
		if (tArea.m_iTFAttributeFlags & TF_NAV_BLOCKED)
			continue;

		// Skip areas with active danger blacklist entries
		if (auto itDanger = m_mDangerBlacklist.find(&tArea); itDanger != m_mDangerBlacklist.end())
		{
			auto& entry = itDanger->second;
			bool bExpired = false;
			if (entry.m_iExpireTick > 0 && I::GlobalVars->tickcount > entry.m_iExpireTick)
				bExpired = true;
			if (entry.m_flExpireTime > 0.0f && flCurTime > entry.m_flExpireTime)
				bExpired = true;
			if (!bExpired)
				continue;
		}

		// Skip areas that don't allow hiding
		if (tArea.m_iAttributeFlags & NAV_MESH_DONT_HIDE)
			continue;

		float flAreaDist = tArea.m_vCenter.DistTo(vFrom);
		if (flAreaDist > flMaxDist)
			continue;

		// Search hiding spots in this area
		for (auto& tSpot : tArea.m_vHidingSpots)
		{
			// Check if this spot has the required flags
			if (!(tSpot.m_fFlags & uRequiredFlags))
				continue;

			float flSpotDist = tSpot.m_vPos.DistTo(vFrom);
			if (flSpotDist < flBestDist)
			{
				flBestDist = flSpotDist;
				vBestSpot = tSpot.m_vPos;
			}
		}
	}

	return vBestSpot;
}

Vec3 CMap::FindCoverSpot(const Vec3& vFrom, float flMaxDist, int iTeam)
{
	// First try to find a spot with good hard cover (IN_COVER)
	// This is what TF2 bots use for hiding from sentries and healing behind walls
	Vec3 vResult = FindHidingSpot(vFrom, flMaxDist, CHidingSpot::IN_COVER, iTeam);
	if (!vResult.IsZero())
		return vResult;

	// No IN_COVER spot found — fall back to any non-exposed spot
	// (IN_COVER | GOOD_SNIPER_SPOT | IDEAL_SNIPER_SPOT — anything that isn't EXPOSED)
	vResult = FindHidingSpot(vFrom, flMaxDist,
		CHidingSpot::IN_COVER | CHidingSpot::GOOD_SNIPER_SPOT | CHidingSpot::IDEAL_SNIPER_SPOT, iTeam);
	return vResult;
}

Vec3 CMap::FindSniperSpot(const Vec3& vFrom, float flMaxDist, int iTeam)
{
	// Prefer ideal sniper spots (can see very far or a large area)
	Vec3 vResult = FindHidingSpot(vFrom, flMaxDist, CHidingSpot::IDEAL_SNIPER_SPOT, iTeam);
	if (!vResult.IsZero())
		return vResult;

	// Fall back to good sniper spots
	vResult = FindHidingSpot(vFrom, flMaxDist, CHidingSpot::GOOD_SNIPER_SPOT, iTeam);
	return vResult;
}

Vec3 CMap::FindDefendSpot(const Vec3& vAnchor, const Vec3& vThreatPos, bool bHasThreat,
	float flMaxDist, int iTeam)
{
	// Same exclusions as FindHidingSpot (spawn rooms, blocked, active danger,
	// DONT_HIDE), but spots are measured from the defend ANCHOR and a spot that
	// can see the threat is rejected — "in cover" means the enemy's line is broken.
	float flBestDist = flMaxDist;
	Vec3 vBestSpot = Vec3(0, 0, 0);

	float flCurTime = I::GlobalVars->curtime;

	for (auto& tArea : m_navfile.m_vAreas)
	{
		if (iTeam == TF_TEAM_RED && (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE))
			continue;
		if (iTeam == TF_TEAM_BLUE && (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED))
			continue;
		if (tArea.m_iTFAttributeFlags & TF_NAV_BLOCKED)
			continue;

		if (auto itDanger = m_mDangerBlacklist.find(&tArea); itDanger != m_mDangerBlacklist.end())
		{
			auto& entry = itDanger->second;
			bool bExpired = false;
			if (entry.m_iExpireTick > 0 && I::GlobalVars->tickcount > entry.m_iExpireTick)
				bExpired = true;
			if (entry.m_flExpireTime > 0.0f && flCurTime > entry.m_flExpireTime)
				bExpired = true;
			if (!bExpired)
				continue;
		}

		if (tArea.m_iAttributeFlags & NAV_MESH_DONT_HIDE)
			continue;

		// Areas bigger than the search ring can still hold valid spots near their
		// edge; the spot distance below is the real filter.
		if (tArea.m_vCenter.DistTo(vAnchor) > flMaxDist + 400.0f)
			continue;

		for (auto& tSpot : tArea.m_vHidingSpots)
		{
			if (!(tSpot.m_fFlags & CHidingSpot::IN_COVER))
				continue;

			const float flSpotDist = tSpot.m_vPos.DistTo(vAnchor);
			if (flSpotDist >= flBestDist)
				continue;

			if (bHasThreat)
			{
				Vec3 vEye = tSpot.m_vPos; vEye.z += PLAYER_CROUCHED_JUMP_HEIGHT;
				Vec3 vThreat = vThreatPos; vThreat.z += PLAYER_CROUCHED_JUMP_HEIGHT;
				// True = clear line of sight = the threat sees this spot
				if (H::AimUtils->TracePositionWorld(vEye, vThreat))
					continue;
			}

			flBestDist = flSpotDist;
			vBestSpot = tSpot.m_vPos;
		}
	}

	return vBestSpot;
}
