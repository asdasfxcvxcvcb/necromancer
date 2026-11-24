#pragma once

#include "../../../SDK/SDK.h"

struct LagRecord_t
{
	C_TFPlayer* Player = nullptr;
	int PlayerIndex = 0;	// Entity index for safe validation (avoids virtual call on dangling pointer)
	matrix3x4_t BoneMatrix[128] = {};
	float SimulationTime = -1.0f;
	Vec3 AbsOrigin = {};
	Vec3 VecOrigin = {};
	Vec3 AbsAngles = {};
	Vec3 EyeAngles = {};
	Vec3 Velocity = {};
	Vec3 Center = {};
	Vec3 VecMinsPreScaled = {};  // Server lag comp restores these (LC_SIZE_CHANGED)
	Vec3 VecMaxsPreScaled = {};
	int Flags = 0;
	float FeetYaw = 0.0f;
	bool bIsAlive = false;  // Server checks LC_ALIVE flag - dead records are invalid
	bool bInvalid = false;  // Marked when player teleported (sv_lagcompensation_teleport_dist) — bones/origin are stale
};

struct Sequence_t
{
	int nInReliableState = 0;
	int nSequenceNr = 0;
	float flTime = 0.f;
};

// Player-indexed storage must cover EngineClient->GetMaxClients(), which can be
// higher than vanilla MAX_PLAYERS on community/high-player-count servers.
constexpr int MAX_LAG_RECORDS_SLOTS = ABSOLUTE_PLAYER_LIMIT + 1;

class CLagRecords
{
	// Flat array indexed by entity index - O(1) lookup, no hash overhead, great cache locality
	// Replaces unordered_map<C_TFPlayer*, deque<LagRecord_t>> which had hash + allocation overhead
	std::deque<LagRecord_t> m_LagRecords[MAX_LAG_RECORDS_SLOTS] = {};
	bool m_bSettingUpBones = false;

	// Fake latency system (Amalgam-style)
	std::deque<Sequence_t> m_dSequences = {};
	int m_iLastInSequence = 0;
	int m_nOldInSequenceNr = 0;
	int m_nOldInReliableState = 0;
	int m_nLastInSequenceNr = 0;
	int m_nOldTickBase = 0;
	float m_flMaxUnlag = 1.0f;
	float m_flFakeLatency = 0.0f;
	std::deque<float> m_dAppliedLatency = {};
	float m_flWishFakeLatency = 0.0f;
	float m_flFakeInterp = 0.0f;  // cl_interp value the server has APPLIED (math claims only this)

	// Teleport dist tracking per player (Amalgam-style)
	bool m_bLagCompensation[MAX_LAG_RECORDS_SLOTS] = {};

	// Cached per-frame values to avoid repeated NetChannel queries
	mutable float m_flCachedServerAcceptedAge = -1.0f;
	mutable float m_flCachedAgeRange = -1.0f;
	mutable int m_nCacheFrame = -1;

	bool IsSimulationTimeValid(float flCurSimTime, float flCmprSimTime);

public:
	// Server validation: |fake_latency + lerp - clientAge| <= ageRange (see .cpp for full derivation)
	// The ideal client-side record age is fake_latency + lerp
	float GetServerAcceptedAge() const;
	float GetServerAcceptedAgeRange() const;  // Dynamic: based on real ping (not fake latency)
	bool IsRecordAgeValidForServer(float flRecordAge) const;

	// Record interpolation matching server's BacktrackPlayer
	static LagRecord_t InterpolateRecords(const LagRecord_t* pOlder, const LagRecord_t* pNewer, float flFrac)
	{
		LagRecord_t out = {};
		out.Player = pNewer->Player;
		out.SimulationTime = std::lerp(pOlder->SimulationTime, pNewer->SimulationTime, flFrac);

		// Vec3 has no static Lerp — use component-wise interpolation
		// LerpAngle is a member function: a.LerpAngle(b, t)
		out.AbsOrigin = pOlder->AbsOrigin + (pNewer->AbsOrigin - pOlder->AbsOrigin) * flFrac;
		out.VecOrigin = pOlder->VecOrigin + (pNewer->VecOrigin - pOlder->VecOrigin) * flFrac;
		out.AbsAngles = pOlder->AbsAngles.LerpAngle(pNewer->AbsAngles, flFrac);
		out.VecMinsPreScaled = pOlder->VecMinsPreScaled + (pNewer->VecMinsPreScaled - pOlder->VecMinsPreScaled) * flFrac;
		out.VecMaxsPreScaled = pOlder->VecMaxsPreScaled + (pNewer->VecMaxsPreScaled - pOlder->VecMaxsPreScaled) * flFrac;
		out.bIsAlive = pNewer->bIsAlive;
		out.EyeAngles = pOlder->EyeAngles.LerpAngle(pNewer->EyeAngles, flFrac);
		out.Velocity = pOlder->Velocity + (pNewer->Velocity - pOlder->Velocity) * flFrac;
		out.Center = pOlder->Center + (pNewer->Center - pOlder->Center) * flFrac;
		out.Flags = pNewer->Flags;
		out.FeetYaw = std::lerp(pOlder->FeetYaw, pNewer->FeetYaw, flFrac);

		// Bones: use the closer record's bones (can't meaningfully interpolate 3x4 matrices)
		if (flFrac < 0.5f)
			memcpy(out.BoneMatrix, pOlder->BoneMatrix, sizeof(out.BoneMatrix));
		else
			memcpy(out.BoneMatrix, pNewer->BoneMatrix, sizeof(out.BoneMatrix));

		return out;
	}

	LagRecord_t GetInterpolatedRecord(C_TFPlayer* pPlayer, float flTargetSimTime, bool* pOutIsInterpolated = nullptr);

	// Get outgoing latency (cached per frame)
	float GetOutgoingLatency() const;

	void UpdateDatagram();
	void AddRecord(C_TFPlayer* pPlayer, int nKnownIndex = 0);
	const LagRecord_t* GetRecord(C_TFPlayer* pPlayer, int nRecord, bool bSafe = true);
	bool HasRecords(C_TFPlayer* pPlayer, int* pTotalRecords = nullptr);
	void UpdateRecords();
	bool DiffersFromCurrent(const LagRecord_t* pRecord);
	bool IsSettingUpBones() { return m_bSettingUpBones; }
	
	// Fake latency functions
	void AdjustPing(INetChannel* pNetChan);
	void RestorePing(INetChannel* pNetChan);
	void SetFakeLatency(float flLatency) { m_flWishFakeLatency = flLatency; }
	float GetFakeLatency() const;
	float GetMaxUnlag() const { return m_flMaxUnlag; }

	// Fake interp: the value the server has APPLIED to m_fLerpTime (set only
	// after a gate-open NET_SetConVar send). The server's lagcomp subtracts
	// m_fLerpTime from cmd tick_count, so the tick_count claim must equal it
	// exactly or every rewind lands shifted off the record
	void SetFakeInterp(float flInterp) { m_flFakeInterp = flInterp; }
	float GetFakeInterp() const { return m_flFakeInterp; }
	float GetServerLerp() const { return std::max(SDKUtils::GetLerp(), m_flFakeInterp); }
	void ResetInterp() { m_flFakeInterp = 0.0f; }
};

MAKE_SINGLETON_SCOPED(CLagRecords, LagRecords, F);

class CLagRecordMatrixHelper
{
	C_TFPlayer* m_pPlayer = nullptr;
	Vec3 m_vAbsOrigin = {};
	Vec3 m_vAbsAngles = {};
	Vec3 m_vRestoreMinsPreScaled = {};  // Server restores OBBMinsPreScaled (LC_SIZE_CHANGED)
	Vec3 m_vRestoreMaxsPreScaled = {};  // Server restores OBBMaxsPreScaled (LC_SIZE_CHANGED)
	Vec3 m_vRestoreMins = {};           // Actual collision bounds used by trace
	Vec3 m_vRestoreMaxs = {};
	matrix3x4_t m_BoneMatrix[128] = {};

	bool m_bSuccessfullyStored = false;

public:
	void Set(const LagRecord_t* pRecord);
	void Restore();
	bool WasSuccessful() const { return m_bSuccessfullyStored; }
	C_TFPlayer* GetRestoringPlayer() const { return m_pPlayer; }
	bool IsRestoringPlayer(C_TFPlayer* pPlayer) const { return m_bSuccessfullyStored && m_pPlayer && m_pPlayer == pPlayer; }
};

MAKE_SINGLETON_SCOPED(CLagRecordMatrixHelper, LagRecordMatrixHelper, F);
