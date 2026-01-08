#pragma once
#include "../../../SDK/SDK.h"
#include <functional>
#include <deque>
#include <unordered_map>

// Per-player behavior profile - learned over the match
struct PlayerBehavior
{
	// === STRAFE TENDENCIES ===
	float m_flCounterStrafeRate = 0.0f;      // How often they A-D spam (0-1)
	float m_flStrafeIntensity = 0.0f;        // How aggressive their strafes are (avg yaw change)
	float m_flAvgStrafePeriod = 0.0f;        // Average ticks between direction changes
	
	// === MOVEMENT STYLE ===
	float m_flBunnyHopRate = 0.0f;           // How often they bhop (0-1)
	float m_flAggressionScore = 0.5f;        // 0=coward/retreating, 1=aggressive/pushing
	float m_flCornerPeekRate = 0.0f;         // How often they peek and retreat (0-1)
	
	// === HEALTH-BASED BEHAVIOR ===
	float m_flLowHealthRetreatRate = 0.0f;   // Do they run when low HP?
	float m_flHealedAggroBoost = 0.0f;       // Do they push when being healed?
	
	// === TEAM/OBJECTIVE BEHAVIOR ===
	float m_flCartProximityRate = 0.0f;      // How often near payload cart
	float m_flTeamProximityRate = 0.0f;      // Do they stick with teammates?
	float m_flSoloPlayRate = 0.0f;           // Do they flank alone?
	
	// === CLASS-SPECIFIC TENDENCIES ===
	int m_nPlayerClass = 0;                  // TF2 class (scout=1, soldier=2, etc)
	float m_flClassAggroModifier = 1.0f;     // Class-based aggression adjustment
	
	// === SITUATIONAL AWARENESS ===
	float m_flReactionToThreat = 0.5f;       // How they react when shot at (0=freeze, 1=dodge)
	float m_flRetreatWhenOutnumbered = 0.0f; // Do they run when outnumbered?
	float m_flPushWhenAdvantage = 0.0f;      // Do they push when team has numbers?
	
	// === WEAPON AWARENESS (are they shooting?) ===
	bool m_bIsAttacking = false;             // Currently shooting/attacking
	float m_flLastAttackTime = 0.0f;         // When they last attacked
	float m_flAttackPredictability = 0.0f;   // Higher = more predictable when attacking (0-1)
	int m_nAttackingSamples = 0;             // Times we saw them attacking
	int m_nAttackingStillSamples = 0;        // Times they stood still while attacking
	int m_nAttackingMovingSamples = 0;       // Times they moved while attacking
	
	// === REACTION DETECTION (how do they react when shot at?) ===
	float m_flLastShotAtTime = 0.0f;         // When we last shot at them
	float m_flLastAnalyzedShotTime = 0.0f;   // Last shot time we analyzed (prevents double-counting)
	Vec3 m_vPosWhenShotAt = {};              // Their position when we shot
	int m_nDodgeLeftCount = 0;               // Times they dodged left after being shot at
	int m_nDodgeRightCount = 0;              // Times they dodged right
	int m_nDodgeJumpCount = 0;               // Times they jumped
	int m_nDodgeBackCount = 0;               // Times they retreated
	int m_nNoReactionCount = 0;              // Times they didn't react
	float m_flAvgReactionTime = 0.3f;        // Average time to react (seconds)
	int m_nReactionSamples = 0;              // Total reaction samples
	
	// === TRACKING STATS ===
	int m_nSampleCount = 0;                  // Total samples collected
	int m_nCounterStrafeSamples = 0;         // Times we detected counter-strafe
	int m_nBunnyHopSamples = 0;              // Times we detected bhop
	int m_nCornerPeekSamples = 0;            // Times we detected corner peek
	int m_nAggressiveSamples = 0;            // Times moving toward enemies
	int m_nDefensiveSamples = 0;             // Times moving away from enemies
	int m_nNearCartSamples = 0;              // Times near payload cart
	int m_nNearTeamSamples = 0;              // Times near teammates
	int m_nAloneSamples = 0;                 // Times alone
	int m_nLowHPRetreatSamples = 0;          // Times retreated when low HP
	int m_nLowHPFightSamples = 0;            // Times fought when low HP
	int m_nHealedPushSamples = 0;            // Times pushed while being healed
	int m_nHealedPassiveSamples = 0;         // Times passive while being healed
	int m_nAirSamples = 0;                   // Times in air
	int m_nGroundSamples = 0;                // Times on ground
	
	// === RECENT DATA ===
	std::deque<float> m_vRecentYawChanges;   // Last N yaw deltas
	std::deque<float> m_vRecentYawTimes;     // Timestamps of yaw changes
	std::deque<float> m_vRecentHealthPct;    // Recent health percentages
	std::deque<Vec3> m_vRecentPositions;     // Recent positions for pattern detection
	
	// === POSITION HISTORY ===
	Vec3 m_vLastKnownPos = {};
	float m_flLastUpdateTime = 0.0f;
	float m_flLastHealth = 0.0f;
	bool m_bWasBeingHealed = false;
	bool m_bWasOnGround = true;              // For bhop detection
	int m_nConsecutiveAirTicks = 0;          // For bhop detection
	
	void Reset()
	{
		m_flCounterStrafeRate = 0.0f;
		m_flStrafeIntensity = 0.0f;
		m_flAvgStrafePeriod = 0.0f;
		m_flBunnyHopRate = 0.0f;
		m_flAggressionScore = 0.5f;
		m_flCornerPeekRate = 0.0f;
		m_flLowHealthRetreatRate = 0.0f;
		m_flHealedAggroBoost = 0.0f;
		m_flCartProximityRate = 0.0f;
		m_flTeamProximityRate = 0.0f;
		m_flSoloPlayRate = 0.0f;
		m_nPlayerClass = 0;
		m_flClassAggroModifier = 1.0f;
		m_flReactionToThreat = 0.5f;
		m_flRetreatWhenOutnumbered = 0.0f;
		m_flPushWhenAdvantage = 0.0f;
		m_bIsAttacking = false;
		m_flLastAttackTime = 0.0f;
		m_flAttackPredictability = 0.0f;
		m_nAttackingSamples = 0;
		m_nAttackingStillSamples = 0;
		m_nAttackingMovingSamples = 0;
		m_flLastShotAtTime = 0.0f;
		m_flLastAnalyzedShotTime = 0.0f;
		m_vPosWhenShotAt = {};
		m_nDodgeLeftCount = 0;
		m_nDodgeRightCount = 0;
		m_nDodgeJumpCount = 0;
		m_nDodgeBackCount = 0;
		m_nNoReactionCount = 0;
		m_flAvgReactionTime = 0.3f;
		m_nReactionSamples = 0;
		m_nSampleCount = 0;
		m_nCounterStrafeSamples = 0;
		m_nBunnyHopSamples = 0;
		m_nCornerPeekSamples = 0;
		m_nAggressiveSamples = 0;
		m_nDefensiveSamples = 0;
		m_nNearCartSamples = 0;
		m_nNearTeamSamples = 0;
		m_nAloneSamples = 0;
		m_nLowHPRetreatSamples = 0;
		m_nLowHPFightSamples = 0;
		m_nHealedPushSamples = 0;
		m_nHealedPassiveSamples = 0;
		m_nAirSamples = 0;
		m_nGroundSamples = 0;
		m_vRecentYawChanges.clear();
		m_vRecentYawTimes.clear();
		m_vRecentHealthPct.clear();
		m_vRecentPositions.clear();
		m_vLastKnownPos = {};
		m_flLastUpdateTime = 0.0f;
		m_flLastHealth = 0.0f;
		m_bWasBeingHealed = false;
		m_bWasOnGround = true;
		m_nConsecutiveAirTicks = 0;
	}
	
	// Get overall prediction confidence (0-1)
	float GetConfidence() const
	{
		return std::min(static_cast<float>(m_nSampleCount) / 100.0f, 1.0f);
	}
	
	// Get movement prediction modifier based on current situation
	float GetMovementModifier(bool bLowHealth, bool bBeingHealed, bool bNearTeam, bool bNearCart) const
	{
		float flMod = 1.0f;
		
		// Low health behavior
		if (bLowHealth && m_flLowHealthRetreatRate > 0.5f)
			flMod *= 0.6f;  // Likely to retreat/slow down
		
		// Being healed behavior  
		if (bBeingHealed && m_flHealedAggroBoost > 0.5f)
			flMod *= 1.3f;  // Likely to push harder
		
		// Near cart behavior (payload)
		if (bNearCart && m_flCartProximityRate > 0.5f)
			flMod *= 0.7f;  // Likely to stay near cart
		
		// Team proximity
		if (bNearTeam && m_flTeamProximityRate > 0.5f)
			flMod *= 0.85f; // Moves with team, more predictable
		
		return flMod;
	}
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
	void DetectCounterStrafeSpam(MoveStorage& tStorage);  // A-D spam detection

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
	void LearnAggression(C_TFPlayer* pPlayer, PlayerBehavior& behavior);
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

public:
	void Store();
	void ClearBehaviors() { m_mPlayerBehaviors.clear(); }  // Call on map change
	PlayerBehavior* GetPlayerBehavior(int nEntIndex);
	
	// Call when local player fires a projectile at a target
	void OnShotFired(int nTargetEntIndex);
	
	// Get predicted dodge direction: -1=left, 0=none, 1=right, 2=jump, 3=back
	int GetPredictedDodge(int nEntIndex)
	{
		auto* pBehavior = GetPlayerBehavior(nEntIndex);
		if (!pBehavior || pBehavior->m_nReactionSamples < 5)
			return 0;
		
		int nMax = pBehavior->m_nNoReactionCount;
		int nDir = 0;
		
		if (pBehavior->m_nDodgeLeftCount > nMax) { nMax = pBehavior->m_nDodgeLeftCount; nDir = -1; }
		if (pBehavior->m_nDodgeRightCount > nMax) { nMax = pBehavior->m_nDodgeRightCount; nDir = 1; }
		if (pBehavior->m_nDodgeJumpCount > nMax) { nMax = pBehavior->m_nDodgeJumpCount; nDir = 2; }
		if (pBehavior->m_nDodgeBackCount > nMax) { nMax = pBehavior->m_nDodgeBackCount; nDir = 3; }
		
		return nDir;
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
