#pragma once

#include "../../../SDK/SDK.h"

struct DodgeDirection
{
	Vec3 vDirection{};
	float flSafety = 0.0f; // Higher = safer
	bool bHasWall = false;
};

class CProjectileDodge
{
private:
	bool IsProjectileThreat(C_BaseEntity* pProjectile, C_TFPlayer* pLocal, Vec3& vImpactPos, float& flTimeToImpact);
	bool SimulateProjectile(C_BaseEntity* pProjectile, Vec3& vImpactPos, float& flTimeToImpact);
	DodgeDirection GetBestDodgeDirection(C_TFPlayer* pLocal, const Vec3& vThreatDir, const Vec3& vImpactPos);
	bool CanWarpInDirection(C_TFPlayer* pLocal, const Vec3& vDirection, float flDistance);
	void ExecuteWarp(CUserCmd* pCmd, C_TFPlayer* pLocal, const Vec3& vDirection);
	void ExecuteStrafe(CUserCmd* pCmd, C_TFPlayer* pLocal, const Vec3& vDirection);

public:
	void Run(C_TFPlayer* pLocal, CUserCmd* pCmd);
	
	bool bWantWarp = false;
	Vec3 vWarpDirection{};
};

MAKE_SINGLETON_SCOPED(CProjectileDodge, ProjectileDodge, F);
