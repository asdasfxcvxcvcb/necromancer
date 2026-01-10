#pragma once
#include "../../../SDK/SDK.h"
#include <functional>
#include <deque>
#include <unordered_map>

// ============================================================================
// BEHAVIOR SUB-STRUCTURES - Organized for clarity and cache efficiency
// ============================================================================

// ============================================================================
// CONTEXTUAL COUNTER-STRAFE TRACKING
// Tracks WHEN and WHY players counter-strafe - different triggers cause different behavior
// ============================================================================
struct CounterStrafeContext
{
	// When do they counter-strafe? (counts)
	int m_nWhenShotAt = 0;           // Started counter-strafing after being shot
	int m_nWhenSawEnemy = 0;         // Started when enemy came into view
	int m_nWhenTeammateEngaged = 0;  // Started when teammate started fighting nearby
	int m_nWhenLowHealth = 0;        // Started when health dropped below threshold
	int m_nWhenBeingHealed = 0;      // Started while being healed (confident play)
	int m_nWhenUnprovoked = 0;       // Started with no obvious trigger (habitual)
	
	// Total samples for each context (to calculate rates)
	int m_nShotAtSamples = 0;        // Times they were shot at
	int m_nSawEnemySamples = 0;      // Times they saw an enemy
	int m_nTeammateEngagedSamples = 0; // Times teammate engaged nearby
	int m_nLowHealthSamples = 0;     // Times they were low health
	int m_nBeingHealedSamples = 0;   // Times they were being healed
	
	// Health thresholds when they counter-strafe
	float m_flAvgHealthWhenCS = 100.0f;  // Average health % when they start counter-strafing
	int m_nHealthSamples = 0;
	
	// Class-specific counter-strafe rates (some classes don't bother)
	// Index by TF_CLASS_* enum (0-9)
	float m_flClassCSRate[10] = {0.0f};  // Counter-strafe rate per class
	int m_nClassCSSamples[10] = {0};     // Samples per class
	
	// Distance-based counter-strafe (do they only CS at certain ranges?)
	float m_flAvgDistanceWhenCS = 500.0f;  // Average distance to enemy when they CS
	int m_nDistanceSamples = 0;
	int m_nCSAtCloseRange = 0;    // <400 units
	int m_nCSAtMidRange = 0;      // 400-800 units
	int m_nCSAtLongRange = 0;     // >800 units
	int m_nCloseRangeSamples = 0;
	int m_nMidRangeSamples = 0;
	int m_nLongRangeSamples = 0;
	
	// Get counter-strafe likelihood for a specific context (0-1)
	float GetCSRateWhenShotAt() const { return m_nShotAtSamples > 5 ? static_cast<float>(m_nWhenShotAt) / m_nShotAtSamples : 0.5f; }
	float GetCSRateWhenSawEnemy() const { return m_nSawEnemySamples > 5 ? static_cast<float>(m_nWhenSawEnemy) / m_nSawEnemySamples : 0.5f; }
	float GetCSRateWhenLowHealth() const { return m_nLowHealthSamples > 5 ? static_cast<float>(m_nWhenLowHealth) / m_nLowHealthSamples : 0.5f; }
	float GetCSRateWhenHealed() const { return m_nBeingHealedSamples > 5 ? static_cast<float>(m_nWhenBeingHealed) / m_nBeingHealedSamples : 0.5f; }
	
	// Get counter-strafe rate for a specific class
	float GetCSRateForClass(int nClass) const 
	{ 
		if (nClass < 0 || nClass >= 10) return 0.5f;
		return m_nClassCSSamples[nClass] > 5 ? m_flClassCSRate[nClass] : 0.5f; 
	}
	
	// Get counter-strafe rate for a distance range
	float GetCSRateAtRange(float flDistance) const
	{
		if (flDistance < 400.0f)
			return m_nCloseRangeSamples > 5 ? static_cast<float>(m_nCSAtCloseRange) / m_nCloseRangeSamples : 0.5f;
		else if (flDistance < 800.0f)
			return m_nMidRangeSamples > 5 ? static_cast<float>(m_nCSAtMidRange) / m_nMidRangeSamples : 0.5f;
		else
			return m_nLongRangeSamples > 5 ? static_cast<float>(m_nCSAtLongRange) / m_nLongRangeSamples : 0.5f;
	}
};

// Strafe and movement pattern data
struct StrafeBehavior
{
	float m_flCounterStrafeRate = 0.0f;      // How often they A-D spam (0-1)
	float m_flStrafeIntensity = 0.0f;        // How aggressive their strafes are (avg yaw change)
	float m_flAvgStrafePeriod = 0.0f;        // Average ticks between direction changes
	float m_flBunnyHopRate = 0.0f;           // How often they bhop (0-1)
	float m_flCornerPeekRate = 0.0f;         // How often they peek and retreat (0-1)
	int m_nCounterStrafeSamples = 0;
	int m_nBunnyHopSamples = 0;
	int m_nCornerPeekSamples = 0;
	int m_nAirSamples = 0;
	int m_nGroundSamples = 0;
	bool m_bWasOnGround = true;
	int m_nConsecutiveAirTicks = 0;
	std::deque<float> m_vRecentYawChanges;
	std::deque<float> m_vRecentYawTimes;
	
	// =========================================================================
	// CIRCLE STRAFE TIMING - Track how long they spend in each quadrant
	// Quadrants: 0=Forward, 1=Right, 2=Back, 3=Left (clockwise from forward)
	// Each player has different timing for each direction
	// =========================================================================
	float m_flQuadrantTime[4] = {0.12f, 0.12f, 0.12f, 0.12f};  // Time spent in each quadrant (EMA)
	float m_flQuadrantYawRate[4] = {3.0f, 3.0f, 3.0f, 3.0f};   // Yaw rate per quadrant (EMA)
	int m_nQuadrantSamples[4] = {0, 0, 0, 0};                   // Sample count per quadrant
	int m_nLastQuadrant = -1;                                   // Last quadrant they were in
	float m_flLastQuadrantChangeTime = 0.0f;                    // When they entered current quadrant
	float m_flCircleStrafeYawPerTick = 0.0f;                    // Average yaw change per tick (EMA)
	float m_flYawRateVariance = 0.0f;                           // Variance in yaw rate (for consistency check)
	std::deque<float> m_vRecentYawRates;                        // Recent yaw rates for variance calc
	int m_nCircleStrafeSamples = 0;                             // Total circle strafe samples
	bool m_bIsCircleStrafing = false;                           // Currently circle strafing?
	int m_nCircleStrafeDirection = 0;                           // 1=clockwise, -1=counter-clockwise
	
	// Counter-strafe pattern detection (A-D spam)
	// Tracks: direction → opposite direction → original direction within time window
	int m_nLastStrafeSign = 0;               // -1 = left, 0 = none, 1 = right
	float m_flLastStrafeTime = 0.0f;         // When last significant strafe started
	float m_flLastDirectionChangeTime = 0.0f; // When direction last changed
	int m_nConsecutiveReversals = 0;         // How many times they've reversed in a row
	int m_nCounterStrafeDetections = 0;      // Total counter-strafe patterns detected
	int m_nNormalStrafeDetections = 0;       // Total normal strafe patterns (no reversal)
	float m_flAvgReversalTime = 0.15f;       // Average time between reversals (EMA)
	
	// Per-session counter-strafe likelihood
	bool m_bIsCurrentlyCounterStrafing = false; // Currently in a counter-strafe pattern
	int m_nCurrentReversalStreak = 0;        // Current streak of reversals
	
	// Directional strafe timing - people have different speeds for each direction
	// Some players are faster going left→right than right→left (finger dexterity)
	float m_flAvgTimeLeftToRight = 0.15f;    // Average time to switch from left to right (EMA)
	float m_flAvgTimeRightToLeft = 0.15f;    // Average time to switch from right to left (EMA)
	int m_nLeftToRightSamples = 0;           // How many left→right transitions we've seen
	int m_nRightToLeftSamples = 0;           // How many right→left transitions we've seen
	float m_flLastLeftStartTime = 0.0f;      // When they started strafing left
	float m_flLastRightStartTime = 0.0f;     // When they started strafing right
	
	// CONTEXTUAL counter-strafe tracking - WHEN and WHY they counter-strafe
	CounterStrafeContext m_Context;
	
	// State tracking for context detection
	float m_flLastShotAtTime = 0.0f;         // When they were last shot at
	float m_flLastSawEnemyTime = 0.0f;       // When they last saw an enemy
	float m_flLastTeammateEngagedTime = 0.0f; // When teammate last engaged nearby
	float m_flCSStartTime = 0.0f;            // When current counter-strafe started
	bool m_bCSContextRecorded = false;       // Have we recorded context for current CS?
	
	// Helper to get predicted time until next direction change
	float GetPredictedTimeToReversal(int nCurrentDirection) const
	{
		if (nCurrentDirection == -1)  // Currently going left, will switch to right
			return m_nLeftToRightSamples > 3 ? m_flAvgTimeLeftToRight : m_flAvgReversalTime;
		else if (nCurrentDirection == 1)  // Currently going right, will switch to left
			return m_nRightToLeftSamples > 3 ? m_flAvgTimeRightToLeft : m_flAvgReversalTime;
		return m_flAvgReversalTime;
	}
};

// Combat and reaction data
struct CombatBehavior
{
	float m_flReactionToThreat = 0.5f;       // How they react when shot at (0=freeze, 1=dodge)
	float m_flAvgReactionTime = 0.3f;        // Average time to react (seconds)
	float m_flAttackPredictability = 0.0f;   // Higher = more predictable when attacking (0-1)
	float m_flLastAttackTime = 0.0f;
	float m_flLastShotAtTime = 0.0f;
	float m_flLastAnalyzedShotTime = 0.0f;
	Vec3 m_vPosWhenShotAt = {};
	int m_nDodgeLeftCount = 0;
	int m_nDodgeRightCount = 0;
	int m_nDodgeJumpCount = 0;
	int m_nDodgeBackCount = 0;
	int m_nNoReactionCount = 0;
	int m_nReactionSamples = 0;
	int m_nAttackingSamples = 0;
	int m_nAttackingStillSamples = 0;
	int m_nAttackingMovingSamples = 0;
	bool m_bIsAttacking = false;
	
	// Melee and look direction tracking
	bool m_bHasMeleeOut = false;             // Currently holding melee weapon
	bool m_bIsLookingAtUs = false;           // Looking toward local player
	float m_flMeleeChargeRate = 0.0f;        // How often they charge with melee (0-1)
	int m_nMeleeChargeSamples = 0;           // Times they ran at us with melee
	int m_nMeleePassiveSamples = 0;          // Times they had melee but didn't charge
};

// Positioning and team behavior data
struct PositioningBehavior
{
	float m_flAggressionScore = 0.5f;        // 0=coward/retreating, 1=aggressive/pushing
	float m_flLowHealthRetreatRate = 0.0f;
	float m_flHealedAggroBoost = 0.0f;
	float m_flCartProximityRate = 0.0f;
	float m_flTeamProximityRate = 0.0f;
	float m_flSoloPlayRate = 0.0f;
	float m_flRetreatWhenOutnumbered = 0.0f;
	float m_flPushWhenAdvantage = 0.0f;
	
	// Conditional objective behavior - what do they do in specific situations?
	float m_flLeavesCartToFight = 0.5f;      // 0=stays on cart, 1=chases enemies when on cart
	float m_flLeavesTeamToFight = 0.5f;      // 0=stays grouped, 1=breaks off to chase
	float m_flObjectiveVsFragger = 0.5f;     // 0=pure objective player, 1=pure fragger
	int m_nOnCartChasedEnemy = 0;            // Was on cart, left to chase
	int m_nOnCartStayedOnCart = 0;           // Was on cart, stayed despite enemy nearby
	int m_nWithTeamChasedAlone = 0;          // Was grouped, broke off to chase
	int m_nWithTeamStayedGrouped = 0;        // Was grouped, stayed with team
	bool m_bWasNearCart = false;             // For tracking state changes
	bool m_bWasNearTeam = false;             // For tracking state changes
	
	int m_nAggressiveSamples = 0;
	int m_nDefensiveSamples = 0;
	int m_nNearCartSamples = 0;
	int m_nNearTeamSamples = 0;
	int m_nAloneSamples = 0;
	int m_nLowHPRetreatSamples = 0;
	int m_nLowHPFightSamples = 0;
	int m_nHealedPushSamples = 0;
	int m_nHealedPassiveSamples = 0;
	std::deque<float> m_vRecentHealthPct;
	std::deque<Vec3> m_vRecentPositions;
};

// Pattern verification and confidence tracking
struct PatternConfidence
{
	float m_flBehaviorConsistency = 0.0f;    // EMA of how consistent their patterns are (0-1)
	int m_nConsistencyChecks = 0;
	int m_nPatternMatches = 0;
	int m_nPatternMisses = 0;
	float m_flLastDecayTime = 0.0f;          // For time-weighted decay
};

// ============================================================================
// Per-player behavior profile - learned over the match
// ============================================================================
struct PlayerBehavior
{
	// Sub-structures for organization
	StrafeBehavior m_Strafe;
	CombatBehavior m_Combat;
	PositioningBehavior m_Positioning;
	PatternConfidence m_Confidence;
	
	// Core tracking
	int m_nSampleCount = 0;
	int m_nPlayerClass = 0;
	float m_flClassAggroModifier = 1.0f;
	Vec3 m_vLastKnownPos = {};
	float m_flLastUpdateTime = 0.0f;
	float m_flLastHealth = 0.0f;
	bool m_bWasBeingHealed = false;
	
	// ========================================================================
	// CONVENIENCE ACCESSORS - For backward compatibility and cleaner code
	// ========================================================================
	float& CounterStrafeRate() { return m_Strafe.m_flCounterStrafeRate; }
	float& StrafeIntensity() { return m_Strafe.m_flStrafeIntensity; }
	float& BunnyHopRate() { return m_Strafe.m_flBunnyHopRate; }
	float& CornerPeekRate() { return m_Strafe.m_flCornerPeekRate; }
	float& AggressionScore() { return m_Positioning.m_flAggressionScore; }
	float& LowHealthRetreatRate() { return m_Positioning.m_flLowHealthRetreatRate; }
	float& HealedAggroBoost() { return m_Positioning.m_flHealedAggroBoost; }
	float& ReactionToThreat() { return m_Combat.m_flReactionToThreat; }
	float& AttackPredictability() { return m_Combat.m_flAttackPredictability; }
	float& AvgReactionTime() { return m_Combat.m_flAvgReactionTime; }
	bool& IsAttacking() { return m_Combat.m_bIsAttacking; }
	float& LastAttackTime() { return m_Combat.m_flLastAttackTime; }
	
	void Reset()
	{
		m_Strafe = StrafeBehavior{};
		m_Combat = CombatBehavior{};
		m_Positioning = PositioningBehavior{};
		m_Confidence = PatternConfidence{};
		m_nSampleCount = 0;
		m_nPlayerClass = 0;
		m_flClassAggroModifier = 1.0f;
		m_vLastKnownPos = {};
		m_flLastUpdateTime = 0.0f;
		m_flLastHealth = 0.0f;
		m_bWasBeingHealed = false;
	}
	
	// Get overall prediction confidence (0-1)
	// Based on how CONSISTENT/PREDICTABLE the player actually is, not just time watched
	float GetConfidence() const
	{
		// Need minimum data before we can be confident
		if (m_nSampleCount < 30)
			return 0.1f;  // ~1.5 sec minimum
		
		// SPECIAL CASE: Low strafe intensity = VERY predictable (straight line movement)
		// This is actually the MOST predictable case - bots walking in straight lines
		if (m_Strafe.m_flStrafeIntensity < 2.0f && m_nSampleCount > 40)
		{
			// They barely change direction - this is highly predictable!
			return 0.9f;
		}
		
		// Base confidence from pattern consistency (how often our predictions match reality)
		float flConsistencyConf = m_Confidence.m_flBehaviorConsistency;
		
		// Boost confidence if we have enough consistency checks to trust the data
		float flDataTrust = 0.0f;
		if (m_Confidence.m_nConsistencyChecks > 0)
		{
			// More checks = more trust in our consistency score
			// 10 checks: 50% trust, 30 checks: 80% trust, 50+ checks: 95% trust
			flDataTrust = std::min(static_cast<float>(m_Confidence.m_nConsistencyChecks) / 50.0f, 0.95f);
		}
		
		// If we don't have enough consistency data yet, fall back to data quality
		if (m_Confidence.m_nConsistencyChecks < 5)
		{
			float flQuality = GetDataQuality();
			// For low strafe intensity, give higher base confidence
			if (m_Strafe.m_flStrafeIntensity < 3.0f)
				return std::min(0.5f + flQuality * 0.3f, 0.75f);
			return std::min(0.3f + flQuality * 0.3f, 0.5f);  // Cap at 50% until we verify patterns
		}
		
		// Final confidence = consistency score weighted by how much we trust the data
		float flQualityBonus = GetDataQuality() * 0.1f;
		
		return std::clamp(flConsistencyConf * flDataTrust + flQualityBonus, 0.1f, 0.95f);
	}
	
	// Record a pattern match/miss - call this when verifying predictions
	void RecordPatternCheck(bool bMatched)
	{
		m_Confidence.m_nConsistencyChecks++;
		if (bMatched)
			m_Confidence.m_nPatternMatches++;
		else
			m_Confidence.m_nPatternMisses++;
		
		// Update consistency EMA (0.9/0.1 for smooth updates)
		float flMatchRate = bMatched ? 1.0f : 0.0f;
		m_Confidence.m_flBehaviorConsistency = m_Confidence.m_flBehaviorConsistency * 0.9f + flMatchRate * 0.1f;
	}
	
	// Apply time-weighted decay to old data - call periodically
	// This allows the system to adapt if a player changes playstyle
	void ApplyDecay(float flCurTime)
	{
		// Only decay every 30 seconds
		if (flCurTime - m_Confidence.m_flLastDecayTime < 30.0f)
			return;
		m_Confidence.m_flLastDecayTime = flCurTime;
		
		// Decay factor - reduces old data influence by ~10% every 30 sec
		const float flDecay = 0.9f;
		
		// Decay sample counts (keeps ratios but reduces absolute counts)
		m_Strafe.m_nCounterStrafeSamples = static_cast<int>(m_Strafe.m_nCounterStrafeSamples * flDecay);
		m_Strafe.m_nBunnyHopSamples = static_cast<int>(m_Strafe.m_nBunnyHopSamples * flDecay);
		m_Strafe.m_nCornerPeekSamples = static_cast<int>(m_Strafe.m_nCornerPeekSamples * flDecay);
		m_Strafe.m_nAirSamples = static_cast<int>(m_Strafe.m_nAirSamples * flDecay);
		m_Strafe.m_nGroundSamples = static_cast<int>(m_Strafe.m_nGroundSamples * flDecay);
		
		m_Positioning.m_nAggressiveSamples = static_cast<int>(m_Positioning.m_nAggressiveSamples * flDecay);
		m_Positioning.m_nDefensiveSamples = static_cast<int>(m_Positioning.m_nDefensiveSamples * flDecay);
		m_Positioning.m_nNearCartSamples = static_cast<int>(m_Positioning.m_nNearCartSamples * flDecay);
		m_Positioning.m_nNearTeamSamples = static_cast<int>(m_Positioning.m_nNearTeamSamples * flDecay);
		m_Positioning.m_nAloneSamples = static_cast<int>(m_Positioning.m_nAloneSamples * flDecay);
		
		// Decay total sample count too (but keep minimum for stability)
		m_nSampleCount = std::max(static_cast<int>(m_nSampleCount * flDecay), 50);
	}
	
	// Get pattern match rate (0-1)
	float GetPatternMatchRate() const
	{
		if (m_Confidence.m_nConsistencyChecks == 0)
			return 0.0f;
		return static_cast<float>(m_Confidence.m_nPatternMatches) / 
		       static_cast<float>(m_Confidence.m_nConsistencyChecks);
	}
	
	// Get data quality score (0-1) - how diverse is our data?
	// Higher = we've seen them in more situations = better predictions
	float GetDataQuality() const
	{
		int nFactors = 0;
		constexpr int nMaxFactors = 12;
		
		// Movement patterns
		if (m_Strafe.m_flStrafeIntensity > 0.5f) nFactors++;
		if (m_Strafe.m_nBunnyHopSamples > 2) nFactors++;
		if (m_Strafe.m_nCornerPeekSamples > 2) nFactors++;
		
		// Aggression/positioning
		if (m_Positioning.m_nAggressiveSamples > 10) nFactors++;
		if (m_Positioning.m_nDefensiveSamples > 10) nFactors++;
		if (m_Positioning.m_nNearTeamSamples + m_Positioning.m_nAloneSamples > 10) nFactors++;
		
		// Health-based behavior
		if (m_Positioning.m_nLowHPRetreatSamples + m_Positioning.m_nLowHPFightSamples > 3) nFactors++;
		if (m_Positioning.m_nHealedPushSamples + m_Positioning.m_nHealedPassiveSamples > 2) nFactors++;
		
		// Combat behavior
		if (m_Combat.m_nReactionSamples > 2) nFactors++;
		if (m_Combat.m_nAttackingSamples > 5) nFactors++;
		
		// Air time (important for projectile prediction)
		if (m_Strafe.m_nAirSamples > 50) nFactors++;
		if (m_Strafe.m_nGroundSamples > 50) nFactors++;
		
		return static_cast<float>(nFactors) / static_cast<float>(nMaxFactors);
	}
	
	// Get learning rate indicator (0-1) - how fast are we still learning?
	// Higher = still gathering new data, lower = behavior is stable/known
	float GetLearningRate() const
	{
		if (m_nSampleCount < 50)
			return 1.0f;  // Still in initial learning phase (~2.5 sec)
		
		// Learning rate decreases as we get more samples (EMA converges)
		float flRate = 50.0f / static_cast<float>(m_nSampleCount);
		return std::clamp(flRate * 3.0f, 0.05f, 1.0f);
	}
	
	// Get movement prediction modifier based on current situation
	float GetMovementModifier(bool bLowHealth, bool bBeingHealed, bool bNearTeam, bool bNearCart) const
	{
		float flMod = 1.0f;
		
		// Low health behavior
		if (bLowHealth && m_Positioning.m_flLowHealthRetreatRate > 0.5f)
			flMod *= 0.6f;  // Likely to retreat/slow down
		
		// Being healed behavior  
		if (bBeingHealed && m_Positioning.m_flHealedAggroBoost > 0.5f)
			flMod *= 1.3f;  // Likely to push harder
		
		// Near cart behavior (payload)
		if (bNearCart && m_Positioning.m_flCartProximityRate > 0.5f)
			flMod *= 0.7f;  // Likely to stay near cart
		
		// Team proximity
		if (bNearTeam && m_Positioning.m_flTeamProximityRate > 0.5f)
			flMod *= 0.85f; // Moves with team, more predictable
		
		return flMod;
	}
	
	// ========================================================================
	// PLAYSTYLE ANALYSIS - Think logically about what kind of player this is
	// Returns: -1.0 (very defensive/scared) to +1.0 (very aggressive/brave)
	// Also provides category name for display
	// Implementation in MovementSimulation.cpp (uses TF_CLASS constants)
	// ========================================================================
	float GetPlaystyle(const char** pOutCategory = nullptr) const;
};

struct MoveStorage
{
	C_TFPlayer* m_pPlayer = nullptr;
	CMoveData m_MoveData = {};
	byte* m_pData = nullptr;

	float m_flAverageYaw = 0.f;
	bool m_bBunnyHop = false;

	float m_flSimTime = 0.f;
	float m_flPredictedDelta = 0.f;
	float m_flPredictedSimTime = 0.f;
	bool m_bDirectMove = true;

	bool m_bPredictNetworked = true;
	Vec3 m_vPredictedOrigin = {};

	std::vector<Vec3> m_vPath = {};

	bool m_bFailed = false;
	bool m_bInitFailed = false;
	
	// Counter-strafe spam detection (A-D spam)
	bool m_bCounterStrafeSpam = false;
	
	// Counter-strafe simulation state
	// When counter-strafing, we simulate the actual L-R-L pattern
	int m_nCSCurrentDir = 0;              // Current strafe direction: -1=left, +1=right
	float m_flCSTimeInCurrentDir = 0.0f;  // How long they've been going this direction
	float m_flCSTimeToSwitch = 0.15f;     // When to switch direction (learned timing)
	float m_flCSStartYaw = 0.0f;          // Yaw when counter-strafe started
	
	// Circle strafe simulation state
	// When circle strafing, we simulate movement through quadrants with learned timing
	bool m_bCircleStrafeMode = false;     // Using circle strafe simulation?
	int m_nCircleStrafeDir = 1;           // 1=clockwise, -1=counter-clockwise
	int m_nCurrentQuadrant = 0;           // Current quadrant: 0=fwd, 1=right, 2=back, 3=left
	float m_flTimeInQuadrant = 0.0f;      // Time spent in current quadrant
	float m_flQuadrantTiming[4] = {0.12f, 0.12f, 0.12f, 0.12f};  // Learned timing per quadrant
	float m_flQuadrantYawRate[4] = {3.0f, 3.0f, 3.0f, 3.0f};     // Learned yaw rate per quadrant
	float m_flYawPerTick = 0.0f;          // Learned yaw change per tick
	
	// Cached values for RunTick performance (set in Initialize)
	PlayerBehavior* m_pCachedBehavior = nullptr;
	bool m_bCachedLowHealth = false;
	bool m_bCachedBeingHealed = false;
	bool m_bCachedAiming = false;
};

struct MoveData
{
	Vec3 m_vDirection = {};
	float m_flSimTime = 0.f;
	int m_iMode = 0;
	Vec3 m_vVelocity = {};
	Vec3 m_vOrigin = {};
};

class CMovementSimulation
{
private:
	void Store(MoveStorage& tStorage);
	void Reset(MoveStorage& tStorage);

	bool SetupMoveData(MoveStorage& tStorage);
	void GetAverageYaw(MoveStorage& tStorage, int iSamples);
	bool StrafePrediction(MoveStorage& tStorage, int iSamples);

	bool m_bOldInPrediction = false;
	bool m_bOldFirstTimePredicted = false;
	float m_flOldFrametime = 0.f;

	std::unordered_map<int, std::deque<MoveData>> m_mRecords = {};
	std::unordered_map<int, std::deque<float>> m_mSimTimes = {};
	
	// Per-player behavior profiles (learned over the match)
	std::unordered_map<int, PlayerBehavior> m_mPlayerBehaviors = {};
	
	// Current active storage for simple API
	MoveStorage m_CurrentStorage = {};
	
	// Behavior learning
	void UpdatePlayerBehavior(C_TFPlayer* pPlayer);
	void LearnCounterStrafe(int nEntIndex, PlayerBehavior& behavior);
	void LearnCounterStrafeContext(C_TFPlayer* pPlayer, PlayerBehavior& behavior);
	void LearnHealthBehavior(C_TFPlayer* pPlayer, PlayerBehavior& behavior);
	void LearnTeamBehavior(C_TFPlayer* pPlayer, PlayerBehavior& behavior);
	void LearnObjectiveBehavior(C_TFPlayer* pPlayer, PlayerBehavior& behavior);
	void LearnClassBehavior(C_TFPlayer* pPlayer, PlayerBehavior& behavior);
	void LearnWeaponAwareness(C_TFPlayer* pPlayer, PlayerBehavior& behavior);
	void LearnReactionPattern(C_TFPlayer* pPlayer, PlayerBehavior& behavior);
	void LearnBunnyHop(C_TFPlayer* pPlayer, PlayerBehavior& behavior);
	void LearnCornerPeek(C_TFPlayer* pPlayer, PlayerBehavior& behavior);
	bool ShouldPredictCounterStrafe(int nEntIndex);
	
	// Helpers
	C_BaseEntity* FindPayloadCart();
	int CountNearbyTeammates(C_TFPlayer* pPlayer, float flRadius);
	int CountNearbyEnemies(C_TFPlayer* pPlayer, float flRadius);
	
	// Bounds adjustment for origin compression fix (non-local players)
	void SetBounds(C_TFPlayer* pPlayer);
	void RestoreBounds(C_TFPlayer* pPlayer);

public:
	void Store();
	void ClearBehaviors() { m_mPlayerBehaviors.clear(); }  // Call on map change
	PlayerBehavior* GetPlayerBehavior(int nEntIndex);      // Read-only access (returns nullptr if not found)
	PlayerBehavior& GetOrCreateBehavior(int nEntIndex);    // Write access (creates if not found)
	
	// Call when local player fires a projectile at a target
	void OnShotFired(int nTargetEntIndex);
	
	// Get counter-strafe likelihood for a target (0-1) based on current context
	// Takes into account: when shot at, when saw enemy, health, class, distance, etc.
	float GetCounterStrafeLikelihood(int nEntIndex, C_TFPlayer* pTarget);
	
	// Get predicted dodge direction: -1=left, 0=none, 1=right, 2=jump, 3=back
	int GetPredictedDodge(int nEntIndex)
	{
		auto* pBehavior = GetPlayerBehavior(nEntIndex);
		if (!pBehavior || pBehavior->m_Combat.m_nReactionSamples < 5)
			return 0;
		
		int nMax = pBehavior->m_Combat.m_nNoReactionCount;
		int nDir = 0;
		
		if (pBehavior->m_Combat.m_nDodgeLeftCount > nMax) { nMax = pBehavior->m_Combat.m_nDodgeLeftCount; nDir = -1; }
		if (pBehavior->m_Combat.m_nDodgeRightCount > nMax) { nMax = pBehavior->m_Combat.m_nDodgeRightCount; nDir = 1; }
		if (pBehavior->m_Combat.m_nDodgeJumpCount > nMax) { nMax = pBehavior->m_Combat.m_nDodgeJumpCount; nDir = 2; }
		if (pBehavior->m_Combat.m_nDodgeBackCount > nMax) { nMax = pBehavior->m_Combat.m_nDodgeBackCount; nDir = 3; }
		
		return nDir;
	}
	
	// Get predicted time until next strafe direction change
	// nCurrentDirection: -1=left, 1=right (from current velocity)
	// Returns time in seconds until they'll likely reverse
	float GetPredictedStrafeTime(int nEntIndex, int nCurrentDirection)
	{
		auto* pBehavior = GetPlayerBehavior(nEntIndex);
		if (!pBehavior)
			return 0.15f;  // Default 150ms
		
		return pBehavior->m_Strafe.GetPredictedTimeToReversal(nCurrentDirection);
	}
	
	// Get contextual counter-strafe info for display/debugging
	const CounterStrafeContext* GetCounterStrafeContext(int nEntIndex)
	{
		auto* pBehavior = GetPlayerBehavior(nEntIndex);
		if (!pBehavior)
			return nullptr;
		return &pBehavior->m_Strafe.m_Context;
	}

	// Full API with MoveStorage
	bool Initialize(C_TFPlayer* pPlayer, MoveStorage& tStorage, bool bStrafe = true);
	void RunTick(MoveStorage& tStorage, bool bPath = true);
	void Restore(MoveStorage& tStorage);

	float GetPredictedDelta(C_TFPlayer* pPlayer);
	
	const Vec3& GetOrigin(MoveStorage& tStorage) { return tStorage.m_MoveData.m_vecAbsOrigin; }
	const Vec3& GetVelocity(MoveStorage& tStorage) { return tStorage.m_MoveData.m_vecVelocity; }
	
	// Simple API (uses internal storage) - for compatibility with existing code
	bool Initialize(C_TFPlayer* pPlayer, bool bStrafe = true) 
	{ 
		m_CurrentStorage = {}; 
		return Initialize(pPlayer, m_CurrentStorage, bStrafe); 
	}
	void RunTick(float flTimeToTarget = 0.0f) { RunTick(m_CurrentStorage, true); }
	void Restore() { Restore(m_CurrentStorage); }
	const Vec3& GetOrigin() { return m_CurrentStorage.m_MoveData.m_vecAbsOrigin; }
	const Vec3& GetVelocity() { return m_CurrentStorage.m_MoveData.m_vecVelocity; }
	MoveStorage& GetCurrentStorage() { return m_CurrentStorage; }
};

MAKE_SINGLETON_SCOPED(CMovementSimulation, MovementSimulation, F);
