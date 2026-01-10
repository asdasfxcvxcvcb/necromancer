#pragma once

#include "../../../SDK/SDK.h"

// Renamed to avoid conflict with amalgam_port's ProjectileInfo
struct ProjSimInfo
{
	ProjectileType_t m_type{};

	Vec3 m_pos{};
	Vec3 m_ang{};

	float m_speed{};
	float m_gravity_mod{};

	bool no_spin{};
};

class CProjectileSim
{
public:
	bool GetInfo(C_TFPlayer* player, C_TFWeaponBase* weapon, const Vec3& angles, ProjSimInfo& out);
	bool Init(const ProjSimInfo& info, bool no_vec_up = false);
	void RunTick();
	Vec3 GetOrigin();
};

MAKE_SINGLETON_SCOPED(CProjectileSim, ProjectileSim, F);