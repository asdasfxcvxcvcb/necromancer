#pragma once

#include "../../../../SDK/SDK.h"
#include <unordered_map>
#include <vector>

struct EnemyTrackingData
{
	int entityIndex;
	float firstSeenTime;
	float lastDistance;
	Vec3 lastPosition;
};

class CAutoDetonate
{
private:
	std::unordered_map<int, std::vector<EnemyTrackingData>> m_mStickyEnemyTracking;

	// Helper methods for tracking and detection
	void UpdateEnemyTracking(int stickyIndex, C_BaseEntity* pEnemy, const Vec3& vStickyPos, float flCurTime);
	bool IsEnemyMovingAway(const EnemyTrackingData& tracking, const Vec3& vCurrentPos, const Vec3& vStickyPos);
	void ClearStickyTracking(int stickyIndex);
	bool ShouldDetonateImmediate(C_BaseEntity* pEnemy, const Vec3& vStickyPos, float flRadius);

public:
	void Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
};

MAKE_SINGLETON_SCOPED(CAutoDetonate, AutoDetonate, F);
