#pragma once

#include "../../../SDK/SDK.h"

struct LagRecord_t
{
	C_TFPlayer* Player = nullptr;
	matrix3x4_t BoneMatrix[128] = {};
	float SimulationTime = -1.0f;
	Vec3 AbsOrigin = {};
	Vec3 VecOrigin = {};
	Vec3 AbsAngles = {};
	Vec3 EyeAngles = {};
	Vec3 Velocity = {};
	Vec3 Center = {};
	int Flags = 0;
	float FeetYaw = 0.0f;
};

struct Sequence_t
{
	int nInReliableState = 0;
	int nSequenceNr = 0;
	float flTime = 0.f;
};

class CLagRecords
{
	std::unordered_map<C_TFPlayer*, std::deque<LagRecord_t>> m_LagRecords = {};
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
	float m_flWishFakeLatency = 0.0f;

	bool IsSimulationTimeValid(float flCurSimTime, float flCmprSimTime);

public:
	void UpdateDatagram();
	void AddRecord(C_TFPlayer* pPlayer);
	const LagRecord_t* GetRecord(C_TFPlayer* pPlayer, int nRecord, bool bSafe = false);
	bool HasRecords(C_TFPlayer* pPlayer, int* pTotalRecords = nullptr);
	void UpdateRecords();
	bool DiffersFromCurrent(const LagRecord_t* pRecord);
	bool IsSettingUpBones() { return m_bSettingUpBones; }
	
	// Resolver support - invalidate cached records when resolver changes
	void ResolverUpdate(C_TFPlayer* pPlayer);
	
	// Fake latency functions
	void AdjustPing(INetChannel* pNetChan);
	void RestorePing(INetChannel* pNetChan);
	void SetFakeLatency(float flLatency) { m_flWishFakeLatency = flLatency; }
	float GetFakeLatency() const;
	float GetMaxUnlag() const { return m_flMaxUnlag; }
};

MAKE_SINGLETON_SCOPED(CLagRecords, LagRecords, F);

class CLagRecordMatrixHelper
{
	C_TFPlayer* m_pPlayer = nullptr;
	Vec3 m_vAbsOrigin = {};
	Vec3 m_vAbsAngles = {};
	matrix3x4_t m_BoneMatrix[128] = {};

	bool m_bSuccessfullyStored = false;

public:
	void Set(const LagRecord_t* pRecord);
	void Restore();
};

MAKE_SINGLETON_SCOPED(CLagRecordMatrixHelper, LagRecordMatrixHelper, F);
