#pragma once
#include "../../../../SDK/SDK.h"
#include "../AmalgamCompat.h"
#include "../MoveSim/MoveSimAmalgam.h"
#include "../AimbotGlobal/AimbotGlobal.h"

#include <deque>
#include <unordered_map>
#include <vector>

// Amalgam-faithful melee aimbot, ported from
// Amalgam-master/Amalgam/src/Features/Aimbot/AimbotMelee.
//
// What it adds over Necromancer's native melee aimbot (the reasons the native
// melee felt broken next to Amalgam):
//  - Swing prediction (UpdateInfo): the local player and every target are
//    simulated through the weapon's smack delay with the ported Amalgam
//    MoveSim, and the target's future positions are stored as sim records.
//    The melee swing lands ~13 ticks after the server processes the attack,
//    so aiming at where everyone stands right now misses every moving target.
//  - CanHit merges those sim records with Necromancer's lag records and
//    traces a hull from the (simulated) eye position at swing time to the
//    AABB-clamped point of each record, moving the entity onto the record
//    origin with origin-compression compensation, exactly like Amalgam.
//  - AutoBackstab: CTFKnife backstab geometry (dot thresholds + origin
//    compression compensation) gates every knife swing.
//
// Necromancer adaptations (documented at each site):
//  - F::Backtrack records -> F::LagRecords, exposed as lightweight
//    MeleeRecord_t views (the melee trace is hull based and never touches
//    bones). No Amalgam GetValidRecords window: melee's lag comp runs at
//    SMACK time, so records are ranked by how close their age-at-smack is
//    to the depth the server honors (fake latency + lerp), and the trace
//    loop takes the best one that connects - the server clamps out-of-window
//    rewinds to that same edge, so no record needs hard-rejecting.
//  - No doubletap for melee (Necromancer's RapidFire excludes MELEE), so
//    m_iDoubletapTicks is always 0 and the AntiWarp paths are dead.
//  - Sapper weapons stay with the native module / AutoSapper triggerbot.
//  - BackstabFlags::AccountPing needs stored eye-angle deltas which
//    Necromancer doesn't keep; the backstab test runs on the current
//    networked eye angles only (stricter, never more permissive than Amalgam).
//  - WorldNPC targets skipped (no matching entity group in Necromancer).

// Amalgam's TickRecord as a lightweight view: past records point into
// Necromancer's lag record store, sim records are our own predictions.
struct MeleeRecord_t
{
	float m_flSimTime = 0.f;
	Vec3 m_vOrigin = {};
	Vec3 m_vMins = {};
	Vec3 m_vMaxs = {};
	bool m_bBacktrack = false;                 // past server record vs future sim record
	const LagRecord_t* m_pLagRecord = nullptr; // lag record this view came from
};

class CAmalgamAimbotMelee
{
private:
	// Amalgam AimbotGeneral::AimType equivalent for the melee CFG
	// (Necromancer: 0 Normal, 1 Silent, 2 Smooth). Static so it can serve as
	// the Aim overloads' default argument, like Amalgam's AimType default.
	static int GetMeleeAimMethod();

	bool IsFiring(const CUserCmd* pCmd, C_TFWeaponBase* pWeapon);

	void UpdateInfo(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd, const std::vector<Target_t>& vTargets);
	bool CanBackstab(C_BaseEntity* pTarget, C_TFPlayer* pLocal, Vec3 vEyeAngles);
	int CanHit(Target_t& tTarget, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon);

	bool Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod = GetMeleeAimMethod());
	void Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod = GetMeleeAimMethod());

	Vec3 m_vEyePos = {};
	float m_flRange = 0.f;
	bool m_bShouldSwing = false;
	bool m_bSimulatedLocal = false;

	int m_iSwingTicks = 0;
	int m_iSimulatedTicks = 0;
	int m_iDoubletapTicks = 0;

	// Future target positions produced by the swing prediction sim, keyed by
	// entindex (Amalgam's m_mRecordMap).
	std::unordered_map<int, std::deque<MeleeRecord_t>> m_mRecordMap = {};
	// Scratch storage for the lag-record views of the CanHit call in progress
	// (Amalgam reads these straight from F::Backtrack).
	std::deque<MeleeRecord_t> m_dBacktrackScratch = {};
	// The record CanHit hit with; Run reads it for the backtrack tick_count.
	MeleeRecord_t m_tHitRecord = {};

public:
	// Main entry: claims melee weapons while Aimbot_Melee_Active is on; returns
	// false to let the native melee aimbot run (aimbot disabled, sapper).
	bool Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
};

MAKE_SINGLETON_SCOPED(CAmalgamAimbotMelee, AmalgamAimbotMelee, F);
