#pragma once

#include "../../../SDK/SDK.h"
#include <deque>
#include <array>

class CSeedPred
{
	bool m_Synced{false};
	float m_ServerTime{0.0f};
	float m_PrevServerTime{0.0f};
	float m_AskTime{0.0f};
	float m_RecvTime{0.0f};
	float m_SyncOffset{0.0f};
	bool m_WaitingForPP{false};
	float m_MantissaStep{0.0f};
	int m_CachedSeed{0};

	// Delta averaging for jitter reduction
	std::deque<double> m_TimeDeltas{};

	// Pre-computed spread offsets LUT (from nospread)
	std::array<Vec2, 256> m_SpreadOffsets{};
	bool m_SpreadInit{false};

	// Resync interval (configurable)
	static constexpr float RESYNC_INTERVAL = 5.0f;
	static constexpr float RESYNC_INTERVAL_PISTOL = 2.5f;

	void InitSpreadLUT();
	float CalcMantissaStep(float val) const;

public:
	void AskForPlayerPerf();
	bool ParsePlayerPerf(bf_read& msgData);
	int GetSeed() const;
	int GetSeedForCmd(const CUserCmd* cmd);
	int GetCachedSeed() const { return m_CachedSeed; }
	float GetMantissaStep() const { return m_MantissaStep; }
	bool IsSynced() const { return m_Synced; }
	void Reset();
	void AdjustAngles(CUserCmd* cmd);
	void Paint();
};

MAKE_SINGLETON_SCOPED(CSeedPred, SeedPred, F);
