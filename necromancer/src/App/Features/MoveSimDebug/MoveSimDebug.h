#pragma once

#include "../../../SDK/SDK.h"

#include <deque>
#include <unordered_map>
#include <vector>

struct MoveSimTickSample_t
{
	int m_nTick = 0;
	Vec3 m_vOrigin = {};
	Vec3 m_vVelocity = {};
	int m_nFlags = 0;
	int m_nButtons = 0;
	float m_flEyeYaw = 0.f;
	float m_flSimTime = 0.f;
};

struct MoveSimCapture_t
{
	int m_nStartTick = 0;
	int m_nHorizon = 0;
	int m_iEntIndex = 0;
	int m_iClass = 0;
	MoveSimTickSample_t m_tStart = {};
	std::vector<Vec3> m_vecPredicted = {};
	bool m_bComplete = false;
};

class CMoveSimDebug
{
public:
	void NotifyProjectileTarget(C_TFPlayer* pPlayer, const std::vector<Vec3>& vecPath, bool bFresh);
	void Run(CUserCmd* pCmd);
	void Draw();

private:
	struct PlayerTrack_t
	{
		std::deque<MoveSimTickSample_t> m_vecReal = {};
		std::deque<MoveSimCapture_t> m_vecCaptures = {};
		int m_nLastCmd = 0;
		int m_nLastSeenCmd = 0;
	};

	PlayerTrack_t* GetTrack(int iEntIndex);
	const MoveSimTickSample_t* FindRealSample(const PlayerTrack_t& tTrack, int nCmd) const;
	const MoveSimCapture_t* FindNewestCompleted(int iPreferredIndex) const;
	void TrySave();
	void SaveCapture(const MoveSimCapture_t& tCapture, const PlayerTrack_t& tTrack);

	std::unordered_map<int, PlayerTrack_t> m_mTracks;
	int m_iNotifyIndex = -1;
	int m_nTrackUntilCmd = 0;
	int m_iNotifyClass = 0;
	bool m_bNotifyThisCmd = false;
	bool m_bFreshNotify = false;
	std::vector<Vec3> m_vecNotifyPath;

	int m_nNotifyTry = 0;
	int m_nNotifyOk = 0;
	int m_nRecordCount = 0;
};

MAKE_SINGLETON_SCOPED(CMoveSimDebug, MoveSimDebug, F);
