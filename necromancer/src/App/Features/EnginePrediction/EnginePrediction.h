#pragma once

#include "../../../SDK/SDK.h"

struct DatamapRestore_t
{
	byte* m_pData = nullptr;
	size_t m_iSize = 0;
};

struct RestoreInfo_t
{
	Vec3 m_vOrigin = {};
	Vec3 m_vMins = {};
	Vec3 m_vMaxs = {};
};

class CEnginePrediction
{
private:
	void Simulate(C_TFPlayer* pLocal, CUserCmd* pCmd);

	CMoveData m_MoveData = {};

	int m_nOldTickCount = 0;
	float m_flOldCurrentTime = 0.f;
	float m_flOldFrameTime = 0.f;

	DatamapRestore_t m_tLocal = {};

	std::unordered_map<C_TFPlayer*, RestoreInfo_t> m_mRestore = {};

public:
	void Start(C_TFPlayer* pLocal, CUserCmd* pCmd);
	void End(C_TFPlayer* pLocal, CUserCmd* pCmd);

	void Unload();

	void AdjustPlayers(C_BaseEntity* pLocal);
	void RestorePlayers();

	bool m_bInPrediction = false;

	// Prediction output for MovementSimulation use
	Vec3 m_vOrigin = {};
	Vec3 m_vVelocity = {};
	Vec3 m_vDirection = {};
	Vec3 m_vAngles = {};
	
	// Legacy support
	int flags = 0;
};

MAKE_SINGLETON_SCOPED(CEnginePrediction, EnginePrediction, F)
