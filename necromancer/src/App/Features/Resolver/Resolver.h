#pragma once

#include "../../../SDK/SDK.h"
#include <unordered_map>
#include <optional>

// Resolver data per player (similar to Amalgam but improved)
struct ResolverData_t
{
	float m_flYaw = 0.0f;           // Yaw offset to add to eye angles
	float m_flPitch = 0.0f;         // Pitch to set (for OOB pitch)
	
	bool m_bYaw = false;            // Is yaw resolver active?
	bool m_bPitch = false;          // Is pitch resolver active?
	bool m_bMinwalk = true;         // Minwalk detection
	bool m_bView = false;           // Aim towards local player instead of static offset
	
	bool m_bAutoSetYaw = true;      // Auto-resolve yaw on miss
	bool m_bAutoSetPitch = true;    // Auto-resolve pitch on miss
	bool m_bFirstOOBPitch = false;  // First time seeing OOB pitch
	bool m_bInversePitch = false;   // Use inverse pitch
	
	int m_nMissCount = 0;           // Number of consecutive misses
	float m_flLastResolveTime = 0.0f;
	
	// Shoot detection - save the tick when enemy shoots (real angles exposed)
	float m_flLastShootTime = 0.0f;  // When did enemy last shoot?
	float m_flShootSimTime = 0.0f;   // Simulation time when they shot
	bool m_bHasShootRecord = false;  // Do we have a valid shoot record?
};

class CResolver
{
private:
	std::unordered_map<int, ResolverData_t> m_mResolverData;
	std::unordered_map<int, Vec3> m_mSniperDots; // Sniper dot positions for pitch detection
	
	int m_iWaitingForTarget = -1;
	float m_flWaitingForDamage = 0.0f;
	bool m_bWaitingForHeadshot = false;
	
	void StoreSniperDots();
	std::optional<float> GetPitchForSniperDot(C_TFPlayer* pPlayer);

public:
	// Main resolver functions
	void FrameStageNotify();
	void CreateMove(C_TFPlayer* pLocal);
	
	// Called when hitscan aimbot fires at a target
	void HitscanRan(C_TFPlayer* pLocal, C_TFPlayer* pTarget, C_TFWeaponBase* pWeapon, int nHitbox = HITBOX_MAX);
	
	// Called on player_hurt event to detect hits/misses
	void OnPlayerHurt(C_TFPlayer* pAttacker, C_TFPlayer* pVictim, bool bCrit);
	
	// Detect when enemy shoots (exposes real angles)
	void DetectEnemyShoot(C_TFPlayer* pPlayer);
	
	// Check if we should prioritize the shoot record for this player
	bool ShouldUseShootRecord(C_TFPlayer* pPlayer, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);
	
	// Get the simulation time of the shoot record
	float GetShootRecordSimTime(C_TFPlayer* pPlayer);
	
	// Get resolver angles for a player (modifies bone matrices)
	bool GetAngles(C_TFPlayer* pPlayer, float* pYaw = nullptr, float* pPitch = nullptr, bool* pMinwalk = nullptr);
	
	// Manual resolver controls (for menu)
	void SetYaw(int nEntIndex, float flValue, bool bAuto = false);
	void SetPitch(int nEntIndex, float flValue, bool bInverse = false, bool bAuto = false);
	
	// Reset all resolver data
	void Reset();
};

MAKE_SINGLETON_SCOPED(CResolver, Resolver, F);
