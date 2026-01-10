#pragma once

#include "../../../../SDK/SDK.h"

// Forward declarations for aimbot support
struct Target_t;

// Projectile type for aimbot support
enum class EReflectProjectileType
{
	Rocket,      // Straight line (rockets, sentry rockets)
	Arc,         // Gravity affected (pipes, arrows, flares, etc.)
	Unknown
};

class CAutoAirblast
{
private:
	// Aimbot support - finds best target to reflect rocket at (straight line projectiles)
	// Returns aim angles and paths for visualization
	bool FindAimbotTarget(C_TFPlayer* pLocal, C_BaseProjectile* pProjectile, Vec3& outAngles,
		std::vector<Vec3>& outPlayerPath, std::vector<Vec3>& outRocketPath);

	// Aimbot support for arc projectiles (gravity affected)
	// Uses simplified prediction - aims directly at predicted player position
	bool FindAimbotTargetArc(C_TFPlayer* pLocal, C_BaseProjectile* pProjectile, Vec3& outAngles,
		std::vector<Vec3>& outPlayerPath, std::vector<Vec3>& outProjectilePath);

	// Get projectile type for aimbot support
	EReflectProjectileType GetProjectileType(C_BaseProjectile* pProjectile);

	// Get projectile speed (for any reflectable projectile)
	float GetReflectedProjectileSpeed(C_BaseProjectile* pProjectile);

	// Get rocket speed based on weapon type (for reflected rockets)
	float GetReflectedRocketSpeed(C_BaseProjectile* pProjectile);

	// Get splash radius for reflected rocket
	float GetReflectedSplashRadius(C_BaseProjectile* pProjectile);

	// Check if projectile type supports aimbot
	bool SupportsAimbot(C_BaseProjectile* pProjectile);

public:
	void Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
};

MAKE_SINGLETON_SCOPED(CAutoAirblast, AutoAirblast, F);
