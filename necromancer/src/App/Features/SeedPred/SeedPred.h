#pragma once

#include "../../../SDK/SDK.h"
#include <deque>
#include <array>

class CSeedPred
{
	bool m_Synced{false};
	double m_ServerTime{0.0};
	double m_PrevServerTime{0.0};
	double m_AskTime{0.0};
	double m_RecvTime{0.0};
	double m_SyncOffset{0.0};
	bool m_WaitingForPP{false};
	double m_WaitingSince{0.0};
	float m_MantissaStep{0.0f};
	int m_CachedSeed{0};
	bool m_bLoopback{false};

	// Delta averaging for jitter reduction
	std::deque<double> m_TimeDeltas{};

	// Pre-computed spread offsets LUT (from nospread)
	std::array<Vec2, 256> m_SpreadOffsets{};
	bool m_SpreadInit{false};

	// Resync interval (configurable)
	static constexpr float RESYNC_INTERVAL = 5.0f;
	static constexpr float RESYNC_INTERVAL_PISTOL = 2.5f;
	static constexpr float REPLY_TIMEOUT = 2.0f;

	void InitSpreadLUT();
	float CalcMantissaStep(float val) const;
	bool IsLoopback() const;

public:
	void AskForPlayerPerf();
	bool ParsePlayerPerf(bf_read& msgData);
	int GetSeed() const;
	int GetSeedForCmd(const CUserCmd* cmd);
	int GetCachedSeed() const { return m_CachedSeed; }
	float GetMantissaStep() const { return m_MantissaStep; }
	double GetServerTime() const { return m_ServerTime; }
	bool IsSynced() const { return m_Synced; }
	bool IsAllowed() const;
	void Reset();
	void AdjustAngles(CUserCmd* cmd);
	void Paint();
};

MAKE_SINGLETON_SCOPED(CSeedPred, SeedPred, F);
