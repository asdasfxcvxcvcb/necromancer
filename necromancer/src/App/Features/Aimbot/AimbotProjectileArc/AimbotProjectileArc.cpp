#include "AimbotProjectileArc.h"

#include "../../LagRecords/LagRecords.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../../VisualUtils/VisualUtils.h"
#include "../../CFG.h"
#include "../../../../SDK/Impl/TraceFilters/TraceFilters.h"
#include "../../Crits/Crits.h"

// Use Amalgam's simulations and compatibility layer
#include "../../amalgam_port/Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../../amalgam_port/Simulation/MovementSimulation/AmalgamMoveSim.h"
#include "../../amalgam_port/AmalgamCompat.h"
#include "../../amalgam_port/AimbotProjectile/AimbotProjectile.h"

#include <vector>
#include <algorithm>

// BOUNDS_HEAD, BOUNDS_BODY, BOUNDS_FEET are defined in AimbotGlobal.h (included via AimbotProjectile.h)

// Projectile weapon info structure
struct ArcWeaponInfo_t
{
	float m_flVelocity = 0.f;
	float m_flGravity = 0.f;
	float m_flUpVelocity = 0.f;  // Initial upward velocity (pipes/stickies have +200)
	Vec3 m_vHull = {};
	Vec3 m_vOffset = {};
	float m_flMaxTime = 2.5f;
	int m_nArmTicks = 0;         // Sticky arm time in ticks
};

// Solution structure for angle calculation
struct ArcSolution_t
{
	float m_flPitch = 0.f;
	float m_flYaw = 0.f;
	float m_flTime = 0.f;
	bool m_bValid = false;
};

// Check if weapon is an arc weapon (has gravity)
bool CAimbotProjectileArc::IsArcWeapon(C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	// Arc weapons (have gravity)
	case TF_WEAPON_GRENADELAUNCHER:
	case TF_WEAPON_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_FLAREGUN:
	case TF_WEAPON_FLAREGUN_REVENGE:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_CROSSBOW:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
	case TF_WEAPON_SYRINGEGUN_MEDIC:
	case TF_WEAPON_CLEAVER:
	case TF_WEAPON_BAT_WOOD:
	case TF_WEAPON_BAT_GIFTWRAP:
	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_LUNCHBOX:
		return true;
	default:
		return false;
	}
}

// Get projectile info for arc weapons
static bool GetArcWeaponInfo(C_TFWeaponBase* pWeapon, C_TFPlayer* pLocal, ArcWeaponInfo_t& info)
{
	if (!pWeapon || !pLocal)
		return false;

	static auto sv_gravity = I::CVar->FindVar("sv_gravity");
	float flGravity = sv_gravity ? sv_gravity->GetFloat() / 800.f : 1.f;
	bool bDucking = pLocal->m_fFlags() & FL_DUCKING;

	info.m_flMaxTime = CFG::Aimbot_Projectile_Max_Simulation_Time;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_GRENADELAUNCHER:
	case TF_WEAPON_CANNON:
		info.m_flVelocity = SDKUtils::AttribHookValue(1200.f, "mult_projectile_speed", pWeapon);
		info.m_flVelocity = SDKUtils::AttribHookValue(info.m_flVelocity, "mult_projectile_range", pWeapon);
		info.m_flGravity = 1.f;
		info.m_flUpVelocity = 200.f;
		info.m_vHull = { 6.f, 6.f, 6.f };
		info.m_vOffset = { 16.f, 8.f, -6.f };
		return true;

	case TF_WEAPON_PIPEBOMBLAUNCHER:
	{
		auto pPipeLauncher = static_cast<C_TFPipebombLauncher*>(pWeapon);
		float flCharge = pPipeLauncher->m_flChargeBeginTime() > 0.f 
			? I::GlobalVars->curtime - pPipeLauncher->m_flChargeBeginTime() 
			: 0.f;
		float flChargeRate = SDKUtils::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon);
		info.m_flVelocity = Math::RemapValClamped(flCharge, 0.f, flChargeRate, 900.f, 2400.f);
		info.m_flVelocity = SDKUtils::AttribHookValue(info.m_flVelocity, "mult_projectile_range", pWeapon);
		info.m_flGravity = 1.f;
		info.m_flUpVelocity = 200.f;
		info.m_vHull = { 6.f, 6.f, 6.f };
		info.m_vOffset = { 16.f, 8.f, -6.f };
		
		static auto tf_grenadelauncher_livetime = I::CVar->FindVar("tf_grenadelauncher_livetime");
		float flLiveTime = tf_grenadelauncher_livetime ? tf_grenadelauncher_livetime->GetFloat() : 0.8f;
		float flArmTime = SDKUtils::AttribHookValue(flLiveTime, "sticky_arm_time", pWeapon);
		info.m_nArmTicks = TIME_TO_TICKS(flArmTime);
		return true;
	}

	case TF_WEAPON_FLAREGUN:
		info.m_flVelocity = SDKUtils::AttribHookValue(2000.f, "mult_projectile_speed", pWeapon);
		info.m_flGravity = 0.3f * flGravity;
		info.m_vHull = { 0.f, 0.f, 0.f };
		info.m_vOffset = { 23.5f, 12.f, bDucking ? 8.f : -3.f };
		return true;

	case TF_WEAPON_FLAREGUN_REVENGE:
		info.m_flVelocity = 3000.f;
		info.m_flGravity = 0.45f * flGravity;
		info.m_vHull = { 0.f, 0.f, 0.f };
		info.m_vOffset = { 23.5f, 12.f, bDucking ? 8.f : -3.f };
		return true;

	case TF_WEAPON_COMPOUND_BOW:
	{
		auto pBow = static_cast<C_TFPipebombLauncher*>(pWeapon);
		float flCharge = pBow->m_flChargeBeginTime() > 0.f 
			? I::GlobalVars->curtime - pBow->m_flChargeBeginTime() 
			: 0.f;
		info.m_flVelocity = Math::RemapValClamped(flCharge, 0.f, 1.f, 1800.f, 2600.f);
		info.m_flGravity = Math::RemapValClamped(flCharge, 0.f, 1.f, 0.5f, 0.1f) * flGravity;
		info.m_vHull = { 1.f, 1.f, 1.f };
		info.m_vOffset = { 23.5f, 8.f, -3.f };
		info.m_flMaxTime = 10.f;
		return true;
	}

	case TF_WEAPON_CROSSBOW:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
		info.m_flVelocity = 2400.f;
		info.m_flGravity = 0.2f * flGravity;
		info.m_vHull = { 3.f, 3.f, 3.f };
		info.m_vOffset = { 23.5f, 8.f, -3.f };
		info.m_flMaxTime = 10.f;
		return true;

	case TF_WEAPON_SYRINGEGUN_MEDIC:
		info.m_flVelocity = 1000.f;
		info.m_flGravity = 0.3f * flGravity;
		info.m_vHull = { 1.f, 1.f, 1.f };
		info.m_vOffset = { 16.f, 6.f, -8.f };
		return true;

	case TF_WEAPON_CLEAVER:
		info.m_flVelocity = 3000.f;
		info.m_flGravity = 1.f;
		info.m_vHull = { 1.f, 1.f, 10.f };
		info.m_vOffset = { 16.f, 8.f, -6.f };
		info.m_flMaxTime = 2.2f;
		return true;

	case TF_WEAPON_BAT_WOOD:
	case TF_WEAPON_BAT_GIFTWRAP:
		info.m_flVelocity = 3000.f;
		info.m_flGravity = 1.f;
		info.m_vHull = { 3.f, 3.f, 3.f };
		info.m_vOffset = { 0.f, 0.f, 0.f };
		info.m_flMaxTime = pWeapon->GetWeaponID() == TF_WEAPON_BAT_GIFTWRAP ? 2.3f : 100.f;
		return true;

	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
		info.m_flVelocity = 1000.f;
		info.m_flGravity = 1.f;
		info.m_vHull = { 3.f, 3.f, 3.f };
		info.m_vOffset = { 16.f, 8.f, -6.f };
		info.m_flMaxTime = 2.2f;
		return true;

	case TF_WEAPON_JAR_GAS:
		info.m_flVelocity = 2000.f;
		info.m_flGravity = 1.f;
		info.m_vHull = { 3.f, 3.f, 3.f };
		info.m_vOffset = { 16.f, 8.f, -6.f };
		info.m_flMaxTime = 2.2f;
		return true;

	case TF_WEAPON_LUNCHBOX:
		info.m_flVelocity = 500.f;
		info.m_flGravity = 1.f * flGravity;
		info.m_vHull = { 17.f, 17.f, 7.f };
		info.m_vOffset = { 0.f, 0.f, -8.f };
		info.m_flMaxTime = 2.2f;
		return true;
	}

	return false;
}


// Get drag coefficient for projectile based on velocity
static float GetArcProjectileDrag(int nWeaponID, float flVelocity)
{
	constexpr float k_flMaxVelocity = 3500.f;
	
	switch (nWeaponID)
	{
	case TF_WEAPON_GRENADELAUNCHER:
		return Math::RemapValClamped(flVelocity, 1217.f, k_flMaxVelocity, 0.120f, 0.200f);
	case TF_WEAPON_CANNON:
		return Math::RemapValClamped(flVelocity, 1454.f, k_flMaxVelocity, 0.385f, 0.530f);
	case TF_WEAPON_PIPEBOMBLAUNCHER:
		return Math::RemapValClamped(flVelocity, 922.f, k_flMaxVelocity, 0.085f, 0.190f);
	case TF_WEAPON_CLEAVER:
		return 0.310f;
	case TF_WEAPON_BAT_WOOD:
		return 0.180f;
	case TF_WEAPON_BAT_GIFTWRAP:
		return 0.285f;
	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
		return 0.057f;
	}
	return 0.f;
}

// Solve for effective velocity accounting for drag
static void SolveArcProjectileSpeed(int nWeaponID, const Vec3& vShootPos, const Vec3& vTargetPos, 
	float& flVelocity, float& flDragTime, float flGravity)
{
	float flDrag = GetArcProjectileDrag(nWeaponID, flVelocity);
	if (flDrag <= 0.f || flGravity <= 0.f)
		return;
	
	const float flGrav = flGravity * 800.f;
	const Vec3 vDelta = vTargetPos - vShootPos;
	const float flDist = vDelta.Length2D();
	
	if (flDist < 1.f)
		return;
	
	float flRoot = powf(flVelocity, 4) - flGrav * (flGrav * powf(flDist, 2) + 2.f * vDelta.z * powf(flVelocity, 2));
	if (flRoot < 0.f)
		return;
	
	const float flPitch = atanf((powf(flVelocity, 2) - sqrtf(flRoot)) / (flGrav * flDist));
	const float flTime = flDist / (cosf(flPitch) * flVelocity);
	
	flDragTime = powf(flTime, 2) * flDrag / 1.5f;
	flVelocity = flVelocity - flVelocity * flTime * flDrag;
}

// Calculate projectile angle for arc weapons
static bool CalculateArcAngle(const Vec3& vShootPos, const Vec3& vTargetPos, float flVelocity, float flGravity, 
	ArcSolution_t& tSolution, float flUpVelocity = 0.f, int nWeaponID = 0)
{
	Vec3 vDelta = vTargetPos - vShootPos;
	float flDistance = vDelta.Length2D();
	float flHeight = vDelta.z;

	if (flVelocity <= 0.f || flDistance < 0.1f)
		return false;

	// No gravity - simple linear trajectory
	if (flGravity <= 0.f)
	{
		tSolution.m_flTime = vDelta.Length() / flVelocity;
		Vec3 vAngle;
		Math::VectorAngles(vDelta, vAngle);
		tSolution.m_flPitch = vAngle.x;
		tSolution.m_flYaw = vAngle.y;
		tSolution.m_bValid = true;
		return true;
	}

	// Account for drag
	float flEffectiveVelocity = flVelocity;
	float flDragTime = 0.f;
	if (nWeaponID != 0)
		SolveArcProjectileSpeed(nWeaponID, vShootPos, vTargetPos, flEffectiveVelocity, flDragTime, flGravity);

	float flGrav = 800.f * flGravity;
	
	// Adjust for upward velocity (pipes/stickies)
	float flAdjustedHeight = flHeight;
	if (flUpVelocity > 0.f)
	{
		float flEstTime = flDistance / flEffectiveVelocity;
		float flUpContribution = flUpVelocity * flEstTime;
		flAdjustedHeight = flHeight - flUpContribution;
	}
	
	float v2 = flEffectiveVelocity * flEffectiveVelocity;
	float v4 = v2 * v2;
	float x2 = flDistance * flDistance;
	
	float flRoot = v4 - flGrav * (flGrav * x2 + 2.f * flAdjustedHeight * v2);
	
	if (flRoot < 0.f)
	{
		tSolution.m_bValid = false;
		return false;
	}

	flRoot = sqrtf(flRoot);
	float flPitch = atanf((v2 - flRoot) / (flGrav * flDistance));
	
	float flCos = cosf(flPitch);
	if (fabsf(flCos) < 0.001f)
	{
		tSolution.m_bValid = false;
		return false;
	}
	
	tSolution.m_flTime = flDistance / (flEffectiveVelocity * flCos) + flDragTime;
	tSolution.m_flPitch = -RAD2DEG(flPitch);
	tSolution.m_flYaw = Math::VelocityToAngles(vDelta).y;
	tSolution.m_bValid = true;
	
	return true;
}

// Amalgam-style hitbox priority system
// Uses Vars::Aimbot::Projectile::Hitboxes from menu settings
// Returns priority (lower = better), -1 = disabled
static int GetArcHitboxPriority(int nHitbox, C_TFWeaponBase* pWeapon, C_TFPlayer* pTarget)
{
	// Check if hitbox is enabled in settings
	int iHitboxes = Vars::Aimbot::Projectile::Hitboxes.Value;
	
	// Validate hitbox against user settings
	switch (nHitbox)
	{
	case BOUNDS_HEAD:
		if (!(iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::Head))
			return -1;
		break;
	case BOUNDS_BODY:
		if (!(iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::Body))
			return -1;
		break;
	case BOUNDS_FEET:
		if (!(iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::Feet))
			return -1;
		break;
	default:
		return -1;
	}

	// Default priorities (Body > Feet > Head for most projectiles)
	int iHeadPriority = 2;
	int iBodyPriority = 0;
	int iFeetPriority = 1;

	// Auto mode - adjust priorities based on weapon type and target state
	if (iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::Auto)
	{
		if (!pWeapon)
		{
			// No weapon info, use defaults
		}
		else
		{
			int nWeaponID = pWeapon->GetWeaponID();
			bool bTargetOnGround = pTarget && IsOnGround(pTarget);
			bool bPrioritizeFeet = (iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::PrioritizeFeet) != 0;
			
			switch (nWeaponID)
			{
			// Huntsman: Head > Body > Feet (headshots do more damage)
			case TF_WEAPON_COMPOUND_BOW:
				iHeadPriority = 0;
				iBodyPriority = 1;
				iFeetPriority = 2;
				break;

			// Grenade Launcher / Loose Cannon: Body > Feet > Head (direct hits are best)
			case TF_WEAPON_GRENADELAUNCHER:
			case TF_WEAPON_CANNON:
				iHeadPriority = 2;
				iBodyPriority = 0;
				iFeetPriority = 1;
				break;

			// Sticky Launcher: 
			// - Grounded targets + PrioritizeFeet: Feet > Body > Head (splash damage)
			// - Airborne targets: Body > Feet > Head (direct air shots)
			case TF_WEAPON_PIPEBOMBLAUNCHER:
				if (bTargetOnGround && bPrioritizeFeet)
				{
					iHeadPriority = 2;
					iBodyPriority = 1;
					iFeetPriority = 0;
				}
				else
				{
					iHeadPriority = 2;
					iBodyPriority = 0;
					iFeetPriority = 1;
				}
				break;

			// All other arc weapons: Body > Feet > Head
			default:
				iHeadPriority = 2;
				iBodyPriority = 0;
				iFeetPriority = 1;
				break;
			}
		}
	}

	switch (nHitbox)
	{
	case BOUNDS_HEAD: return iHeadPriority;
	case BOUNDS_BODY: return iBodyPriority;
	case BOUNDS_FEET: return iFeetPriority;
	}

	return -1;
}

// Get Z offset for a hitbox type
static float GetArcHitboxZOffset(int nHitbox, float flHeight)
{
	switch (nHitbox)
	{
	case BOUNDS_FEET: return Vars::Aimbot::Projectile::VerticalShift.Value;
	case BOUNDS_BODY: return flHeight * 0.5f;
	case BOUNDS_HEAD: return flHeight - Vars::Aimbot::Projectile::VerticalShift.Value;
	}
	return flHeight * 0.5f;
}

// Get aim position using Amalgam's hitbox priority system
static Vec3 GetArcAimPosition(C_TFPlayer* pTarget, C_TFWeaponBase* pWeapon)
{
	if (!pTarget)
		return {};
	
	Vec3 vPos = pTarget->m_vecOrigin();
	float flHeight = pTarget->m_vecMaxs().z - pTarget->m_vecMins().z;
	
	// Find the highest priority enabled hitbox
	int iBestPriority = 999;
	int nBestHitbox = BOUNDS_BODY;
	
	for (int i = 0; i < 3; i++)
	{
		int iPriority = GetArcHitboxPriority(i, pWeapon, pTarget);
		if (iPriority >= 0 && iPriority < iBestPriority)
		{
			iBestPriority = iPriority;
			nBestHitbox = i;
		}
	}
	
	vPos.z += GetArcHitboxZOffset(nBestHitbox, flHeight);
	return vPos;
}

// Helper function to check if two AABBs intersect
static bool ArcAABBIntersect(const Vec3& vMins1, const Vec3& vMaxs1, const Vec3& vMins2, const Vec3& vMaxs2)
{
	return vMins1.x <= vMaxs2.x && vMaxs1.x >= vMins2.x &&
	       vMins1.y <= vMaxs2.y && vMaxs1.y >= vMins2.y &&
	       vMins1.z <= vMaxs2.z && vMaxs1.z >= vMins2.z;
}

// Validate projectile trajectory
static bool ValidateArcPath(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const Vec3& vAngle, 
                            const Vec3& vTargetPos, C_BaseEntity* pTarget, int nSimTicks,
                            std::vector<Vec3>* pOutPath = nullptr)
{
	if (!pLocal || !pWeapon || !pTarget || nSimTicks <= 0)
		return false;

	// Get projectile info using Amalgam's simulation
	ProjectileInfo tProjInfo = {};
	int iFlags = ProjSimEnum::Trace | ProjSimEnum::InitCheck | ProjSimEnum::NoRandomAngles;
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, vAngle, tProjInfo, iFlags))
		return false;

	if (!F::ProjSim.Initialize(tProjInfo, true, false))
		return false;

	int nMaxTime = TIME_TO_TICKS(tProjInfo.m_flLifetime);
	if (nSimTicks > nMaxTime) nSimTicks = nMaxTime;
	if (nSimTicks > 250) nSimTicks = 250;

	Vec3 vHull = tProjInfo.m_vHull;

	const float flHitboxHalfWidth = 12.f;
	const float flHitboxHalfHeight = 12.f;
	
	Vec3 vTargetMins = vTargetPos - Vec3(flHitboxHalfWidth, flHitboxHalfWidth, flHitboxHalfHeight);
	Vec3 vTargetMaxs = vTargetPos + Vec3(flHitboxHalfWidth, flHitboxHalfWidth, flHitboxHalfHeight);

	CTraceFilterArc filter = {};
	filter.m_pSkip = pLocal;
	
	if (pOutPath)
	{
		pOutPath->clear();
		pOutPath->reserve(nSimTicks + 2);
		pOutPath->push_back(tProjInfo.m_vPos);
	}

	Vec3 vLastPos = tProjInfo.m_vPos;
	const int nTraceInterval = 2;
	bool bPassedThroughTarget = false;

	for (int n = 0; n < nSimTicks; n++)
	{
		F::ProjSim.RunTick(tProjInfo);
		Vec3 vNewPos = F::ProjSim.GetOrigin();

		if (n % nTraceInterval == 0 || n == nSimTicks - 1)
		{
			if (pOutPath)
				pOutPath->push_back(vNewPos);

			Vec3 vProjMins, vProjMaxs;
			vProjMins.x = std::min(vLastPos.x, vNewPos.x) - vHull.x;
			vProjMins.y = std::min(vLastPos.y, vNewPos.y) - vHull.y;
			vProjMins.z = std::min(vLastPos.z, vNewPos.z) - vHull.z;
			vProjMaxs.x = std::max(vLastPos.x, vNewPos.x) + vHull.x;
			vProjMaxs.y = std::max(vLastPos.y, vNewPos.y) + vHull.y;
			vProjMaxs.z = std::max(vLastPos.z, vNewPos.z) + vHull.z;
			
			if (ArcAABBIntersect(vProjMins, vProjMaxs, vTargetMins, vTargetMaxs))
				bPassedThroughTarget = true;

			CGameTrace trace = {};
			Ray_t ray;
			ray.Init(vLastPos, vNewPos, vHull * -1, vHull);
			I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &trace);

			if (trace.DidHit())
			{
				if (pOutPath)
					pOutPath->push_back(trace.endpos);
				
				if (trace.m_pEnt == pTarget)
					return true;
				
				if (bPassedThroughTarget)
					return true;
				
				return false;
			}
			
			vLastPos = vNewPos;
		}
	}

	return bPassedThroughTarget;
}

float CAimbotProjectileArc::GetSplashRadius(C_TFWeaponBase* pWeapon, C_TFPlayer* pPlayer)
{
	if (!pWeapon)
		return 0.f;

	float flRadius = 0.f;
	switch (pWeapon->GetWeaponID())
	{
	// Sticky launcher has splash
	case TF_WEAPON_PIPEBOMBLAUNCHER:
		flRadius = 146.f;
		break;
	// Grenade launcher pipes also have splash (but we prefer direct hits)
	case TF_WEAPON_GRENADELAUNCHER:
	case TF_WEAPON_CANNON:
		flRadius = 146.f;
		break;
	}
	
	if (flRadius > 0.f)
	{
		flRadius = SDKUtils::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
		// Apply user's splash radius percentage setting
		flRadius *= Vars::Aimbot::Projectile::SplashRadius.Value / 100.f;
	}
	
	return flRadius;
}


void CAimbotProjectileArc::Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	G::bSimulatingProjectile = false;

	if (!CFG::Aimbot_Active || !CFG::Aimbot_Projectile_Active)
	{
		m_LockedTarget.Reset();
		m_LooseCannonState.Reset();
		return;
	}

	ArcWeaponInfo_t weaponInfo;
	if (!GetArcWeaponInfo(pWeapon, pLocal, weaponInfo))
	{
		m_LockedTarget.Reset();
		m_LooseCannonState.Reset();
		return;
	}

	if (CFG::Aimbot_Projectile_Sort == 0)
		G::flAimbotFOV = CFG::Aimbot_Projectile_FOV;

	if (Shifting::bShifting && !Shifting::bShiftingWarp)
		return;

	const int nWeaponID = pWeapon->GetWeaponID();
	const bool bIsLooseCannon = (nWeaponID == TF_WEAPON_CANNON);
	const bool bAimbotKeyDown = H::Input->IsDown(CFG::Aimbot_Key);
	
	// Track aimbot key state changes for Loose Cannon
	bool bAimbotKeyJustReleased = m_LooseCannonState.m_bWasAimbotKeyDown && !bAimbotKeyDown;
	m_LooseCannonState.m_bWasAimbotKeyDown = bAimbotKeyDown;
	
	// Check if Loose Cannon is currently charging
	bool bLooseCannonCharging = false;
	float flFuseTimeRemaining = 0.f;
	if (bIsLooseCannon)
	{
		auto pCannon = pWeapon->As<CTFGrenadeLauncher>();
		float flDetonateTime = pCannon->m_flDetonateTime();
		bLooseCannonCharging = flDetonateTime > 0.f;
		
		// Calculate remaining fuse time
		if (bLooseCannonCharging)
		{
			flFuseTimeRemaining = flDetonateTime - I::GlobalVars->curtime;
		}
	}
	
	// Cancel charge quick-switch: if fuse is about to expire (< 26ms), quick-switch to avoid self-damage
	// Only triggers if cancel charge option is enabled
	// Works for both aimbot-initiated and manual charges
	// Uses multi-tick approach: hold attack -> switch away -> wait -> switch back
	if (bIsLooseCannon && bLooseCannonCharging && CFG::Aimbot_Projectile_Cannon_Cancel_Charge && 
		flFuseTimeRemaining > 0.f && flFuseTimeRemaining < 0.026f &&
		m_LooseCannonState.m_nQuickSwitchPhase == 0)
	{
		// Fuse is about to expire! Start quick-switch
		// Phase 0: Hold attack and switch away
		pCmd->buttons |= IN_ATTACK; // Hold attack to prevent firing
		I::EngineClient->ClientCmd_Unrestricted("lastinv");
		m_LooseCannonState.m_nQuickSwitchPhase = 1;
		m_LooseCannonState.m_nQuickSwitchCooldownTick = I::GlobalVars->tickcount + 1;
		return;
	}
	
	// Handle multi-tick quick-switch phase 1 (switch back to cannon)
	if (m_LooseCannonState.m_nQuickSwitchPhase == 1)
	{
		if (I::GlobalVars->tickcount >= m_LooseCannonState.m_nQuickSwitchCooldownTick)
		{
			// Phase 1: Switch back to cannon
			I::EngineClient->ClientCmd_Unrestricted("lastinv");
			m_LooseCannonState.Reset();
			m_LooseCannonState.m_nQuickSwitchCooldownTick = I::GlobalVars->tickcount + 1;
		}
		return;
	}
	
	// Check cooldown after quick-switch - don't start charging again immediately
	if (bIsLooseCannon && m_LooseCannonState.m_nQuickSwitchCooldownTick > 0)
	{
		if (I::GlobalVars->tickcount < m_LooseCannonState.m_nQuickSwitchCooldownTick)
		{
			// Still in cooldown, don't do anything
			return;
		}
		else
		{
			// Cooldown expired
			m_LooseCannonState.m_nQuickSwitchCooldownTick = 0;
		}
	}
	
	if (!bAimbotKeyDown)
	{
		m_LockedTarget.Reset();
		if (!bIsLooseCannon)
			m_LooseCannonState.Reset();
		return;
	}

	G::bSimulatingProjectile = true;

	// Check if this is a charge weapon (sticky launchers, bow, and Loose Cannon)
	// Loose Cannon: holding fire controls the fuse time (1 second fuse, can be shortened)
	bool bIsChargeWeapon = (nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || 
		nWeaponID == TF_WEAPON_STICKY_BALL_LAUNCHER || 
		nWeaponID == TF_WEAPON_GRENADE_STICKY_BALL ||
		nWeaponID == TF_WEAPON_COMPOUND_BOW ||
		nWeaponID == TF_WEAPON_CANNON); // Loose Cannon
	
	// Check if user was already charging before aimbot key was pressed (manual charge)
	// For Loose Cannon: if charging but aimbot didn't initiate it, skip timed double donk
	bool bUserManualCharge = false;
	if (bIsLooseCannon && bLooseCannonCharging && !m_LooseCannonState.m_bAimbotInitiatedCharge)
	{
		bUserManualCharge = true;
	}
	
	// Track charge state for auto-charge (using ticks)
	static int s_nChargeStartTick = 0;
	
	bool bIsCharging = false;
	int nTicksCharged = 0;
	
	if (bIsChargeWeapon)
	{
		if (nWeaponID == TF_WEAPON_CANNON)
		{
			// Loose Cannon uses m_flDetonateTime to track charge state
			auto pCannon = pWeapon->As<CTFGrenadeLauncher>();
			float flDetonateTime = pCannon->m_flDetonateTime();
			bIsCharging = flDetonateTime > 0.f;
			
			// Track when charge started
			if (bIsCharging && s_nChargeStartTick == 0)
				s_nChargeStartTick = I::GlobalVars->tickcount;
			else if (!bIsCharging)
				s_nChargeStartTick = 0;
				
			nTicksCharged = bIsCharging ? (I::GlobalVars->tickcount - s_nChargeStartTick) : 0;
		}
		else
		{
			// Sticky launchers and bow use m_flChargeBeginTime
			auto pChargeWeapon = static_cast<C_TFPipebombLauncher*>(pWeapon);
			float flChargeBegin = pChargeWeapon->m_flChargeBeginTime();
			bIsCharging = flChargeBegin > 0.f;
			
			// Track when charge started
			if (bIsCharging && s_nChargeStartTick == 0)
				s_nChargeStartTick = I::GlobalVars->tickcount;
			else if (!bIsCharging)
				s_nChargeStartTick = 0;
				
			nTicksCharged = bIsCharging ? (I::GlobalVars->tickcount - s_nChargeStartTick) : 0;
		}
	}
	
	// Minimum charge ticks from config
	const int nMinChargeTicks = CFG::Aimbot_Projectile_Charge_Ticks;
	
	if (!bIsChargeWeapon)
	{
		m_LockedTarget.Reset();
	}

	// Get shoot position
	Vec3 vShootPos;
	if (!F::EnginePrediction->m_vOrigin.IsZero())
		vShootPos = F::EnginePrediction->m_vOrigin + pLocal->m_vecViewOffset();
	else
		vShootPos = pLocal->GetShootPos();
	Vec3 vViewAngles = I::EngineClient->GetViewAngles();

	// Old locked target section removed - using new advanced auto-charge flow instead
	// The new flow: run simulation -> if can hit, start charging -> wait for charge -> re-run simulation -> fire

	// Find best target
	C_BaseEntity* pBestTarget = nullptr;
	Vec3 vBestAngle = {};
	float flBestFOV = CFG::Aimbot_Projectile_FOV;
	float flBestTime = weaponInfo.m_flMaxTime;
	bool bHasValidPrediction = false;

	// Check if we're using Crusader's Crossbow and should heal teammates
	const bool bIsCrossbowHealing = (nWeaponID == TF_WEAPON_CROSSBOW) && CFG::Aimbot_Crossbow_Heal_Teammates;

	// Collect potential targets
	struct PotentialTarget_t {
		C_TFPlayer* pPlayer;
		float flFOV;
		Vec3 vInitialPos;
		bool bIsTeammate; // For crossbow healing priority
		float flHealthPercent; // For healing priority
	};
	std::vector<PotentialTarget_t> vPotentialTargets;
	
	if (CFG::Aimbot_Target_Players)
	{
		for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
		{
			auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pClientEntity) continue;

			auto pEntity = pClientEntity->GetBaseEntity();
			if (!pEntity || pEntity == pLocal) continue;

			auto pPlayer = pEntity->As<C_TFPlayer>();
			if (pPlayer->deadflag()) continue;
			
			bool bIsTeammate = pPlayer->m_iTeamNum() == pLocal->m_iTeamNum();
			
			// For Crusader's Crossbow: target teammates who need healing
			if (bIsCrossbowHealing && bIsTeammate)
			{
				int iHealth = pPlayer->m_iHealth();
				int iMaxHealth = pPlayer->GetMaxHealth();
				
				// Skip teammates at or above max health (can't heal them)
				if (iHealth >= iMaxHealth)
					continue;
				
				Vec3 vInitialPos = GetArcAimPosition(pPlayer, pWeapon);
				Vec3 vInitialAngle = Math::CalcAngle(vShootPos, vInitialPos);
				float flInitialFOV = Math::CalcFov(vViewAngles, vInitialAngle);
				
				if (flInitialFOV > CFG::Aimbot_Projectile_FOV * 3.0f) continue;
				
				float flHealthPercent = static_cast<float>(iHealth) / static_cast<float>(iMaxHealth);
				vPotentialTargets.push_back({ pPlayer, flInitialFOV, vInitialPos, true, flHealthPercent });
				continue;
			}
			
			// Skip teammates for non-healing weapons
			if (bIsTeammate) continue;

			if (CFG::Aimbot_Ignore_Invisible && pPlayer->IsInvisible()) continue;
			if (CFG::Aimbot_Ignore_Invulnerable && pPlayer->IsInvulnerable()) continue;
			if (CFG::Aimbot_Ignore_Friends && pPlayer->IsPlayerOnSteamFriendsList()) continue;
			if (CFG::Aimbot_Ignore_Taunting && pPlayer->InCond(TF_COND_TAUNTING)) continue;

			Vec3 vInitialPos = GetArcAimPosition(pPlayer, pWeapon);
			Vec3 vInitialAngle = Math::CalcAngle(vShootPos, vInitialPos);
			float flInitialFOV = Math::CalcFov(vViewAngles, vInitialAngle);
			
			if (flInitialFOV > CFG::Aimbot_Projectile_FOV * 3.0f) continue;
			
			vPotentialTargets.push_back({ pPlayer, flInitialFOV, vInitialPos, false, 1.0f });
		}
		
		// Sort targets based on weapon type and priority settings
		if (bIsCrossbowHealing)
		{
			// For crossbow healing, sort by priority setting
			switch (CFG::Aimbot_Crossbow_Heal_Priority)
			{
			case 0: // Lowest HP first
				std::sort(vPotentialTargets.begin(), vPotentialTargets.end(),
					[](const PotentialTarget_t& a, const PotentialTarget_t& b) {
						// Teammates (healing targets) first, then by health percent
						if (a.bIsTeammate != b.bIsTeammate)
							return a.bIsTeammate > b.bIsTeammate;
						if (a.bIsTeammate)
							return a.flHealthPercent < b.flHealthPercent; // Lower health = higher priority
						return a.flFOV < b.flFOV;
					});
				break;
			case 1: // Closest first (by distance, approximated by FOV for now)
			case 2: // FOV first
			default:
				std::sort(vPotentialTargets.begin(), vPotentialTargets.end(),
					[](const PotentialTarget_t& a, const PotentialTarget_t& b) {
						// Teammates (healing targets) first, then by FOV
						if (a.bIsTeammate != b.bIsTeammate)
							return a.bIsTeammate > b.bIsTeammate;
						return a.flFOV < b.flFOV;
					});
				break;
			}
		}
		else
		{
			std::sort(vPotentialTargets.begin(), vPotentialTargets.end(), 
				[](const PotentialTarget_t& a, const PotentialTarget_t& b) {
					return a.flFOV < b.flFOV;
				});
		}
		
		const int nMaxTargets = CFG::Aimbot_Projectile_Max_Targets;
		int nTargetsSimulated = 0;
		
		for (const auto& target : vPotentialTargets)
		{
			if (nTargetsSimulated >= nMaxTargets) break;
			nTargetsSimulated++;
			
			C_TFPlayer* pPlayer = target.pPlayer;
			Vec3 vPredictedPos;
			Vec3 vAngle;
			float flTime = 0.f;
			std::vector<Vec3> vTempPath;

			// Use Amalgam movement simulation
			MoveStorage tStorage;
			F::MoveSim.Initialize(pPlayer, tStorage);
			if (tStorage.m_bInitFailed)
				continue;

			float flInitialDist = vShootPos.DistTo(target.vInitialPos);
			float flEstTime = flInitialDist / weaponInfo.m_flVelocity;
			if (flEstTime > weaponInfo.m_flMaxTime)
			{
				F::MoveSim.Restore(tStorage);
				continue;
			}

			int nMaxSimTicks = TIME_TO_TICKS(weaponInfo.m_flMaxTime);
			int nEstTicks = TIME_TO_TICKS(flEstTime) + 20;
			int nMaxTicks = std::min(nEstTicks, nMaxSimTicks);
			if (nMaxTicks < 30) nMaxTicks = 30;
			if (weaponInfo.m_nArmTicks > 0 && nMaxTicks < weaponInfo.m_nArmTicks + 10)
				nMaxTicks = weaponInfo.m_nArmTicks + 10;

			// Run simulation and store positions
			std::vector<Vec3> vTargetPositions;
			vTargetPositions.reserve(nMaxTicks + 1);
			vTargetPositions.push_back(pPlayer->m_vecOrigin());
			
			for (int nTick = 1; nTick <= nMaxTicks; nTick++)
			{
				if (!tStorage.m_bFailed)
				{
					F::MoveSim.RunTick(tStorage);
					Vec3 vPos = tStorage.m_vPredictedOrigin;
					vTargetPositions.push_back(vPos);
					
					if (nTick % 3 == 0)
						vTempPath.push_back(vPos);
				}
				else
				{
					// If simulation failed, use last known position
					vTargetPositions.push_back(vTargetPositions.back());
				}
			}
			
			F::MoveSim.Restore(tStorage);
			
			// Get hitbox priority using Amalgam's system (respects Vars::Aimbot::Projectile::Hitboxes)
			float flHeight = pPlayer->m_vecMaxs().z - pPlayer->m_vecMins().z;
			bool bTargetAirborne = !(pPlayer->m_fFlags() & FL_ONGROUND);
			
			// Build sorted list of hitboxes by priority
			std::vector<std::pair<int, int>> vHitboxPriorities; // <hitbox, priority>
			for (int i = 0; i < 3; i++)
			{
				int iPriority = GetArcHitboxPriority(i, pWeapon, pPlayer);
				if (iPriority >= 0)
					vHitboxPriorities.push_back({ i, iPriority });
			}
			std::sort(vHitboxPriorities.begin(), vHitboxPriorities.end(),
				[](const auto& a, const auto& b) { return a.second < b.second; });
			
			// Try each hitbox in priority order
			bool bFound = false;
			int nBestTick = 0;
			float flBestDelta = std::numeric_limits<float>::max();
			
			for (const auto& [nHitbox, iPriority] : vHitboxPriorities)
			{
				if (bFound) break;
				
				for (int nTick = 1; nTick <= nMaxTicks; nTick++)
				{
					Vec3 vTargetPos = vTargetPositions[nTick];
					Vec3 vAimPos = vTargetPos;
					vAimPos.z += GetArcHitboxZOffset(nHitbox, flHeight);

					ArcSolution_t tSolution;
					if (!CalculateArcAngle(vShootPos, vAimPos, weaponInfo.m_flVelocity, weaponInfo.m_flGravity, 
						tSolution, weaponInfo.m_flUpVelocity, nWeaponID))
						continue;

					int nArrivalTick = TIME_TO_TICKS(tSolution.m_flTime) + 1;
					
					// Sticky arm time check - matches Amalgam's exact logic:
					// bTime = bSplash || m_iPrimeTime < i || velocity.IsZero()
					// For direct hits: m_iPrimeTime < i means i > m_iPrimeTime (strictly greater)
					bool bUsePrimeTime = Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::UsePrimeTime;
					if (bUsePrimeTime && weaponInfo.m_nArmTicks > 0)
					{
						// Amalgam's bTime for direct hits: m_iPrimeTime < i || velocity.IsZero()
						bool bTime = weaponInfo.m_nArmTicks < nTick || tStorage.m_MoveData.m_vecVelocity.IsZero();
						if (!bTime)
							continue;
					}
					
					float flTickDelta = fabsf(static_cast<float>(nArrivalTick - nTick));
					
					// Allow larger tick delta for high velocity (charged) shots
					// At 2400 velocity, projectile travels ~36 units per tick, so timing is less critical
					float flMaxTickDelta = weaponInfo.m_flVelocity > 1500.f ? 5.f : 3.f;
					
					if (flTickDelta <= flMaxTickDelta && flTickDelta < flBestDelta)
					{
						vPredictedPos = vAimPos;
						vAngle.x = tSolution.m_flPitch;
						vAngle.y = tSolution.m_flYaw;
						vAngle.z = 0.f;
						flTime = tSolution.m_flTime;
						nBestTick = nTick;
						flBestDelta = flTickDelta;
						bFound = true;
						
						if (nArrivalTick == nTick) break;
					}
					
					// Early break optimization - if we're way past the target, stop searching
					// Use larger threshold for high velocity to avoid breaking too early
					bool bNoArmTime = !bUsePrimeTime || weaponInfo.m_nArmTicks == 0;
					int nEarlyBreakThreshold = weaponInfo.m_flVelocity > 1500.f ? 8 : 5;
					if (bNoArmTime && !bTargetAirborne && nArrivalTick < nTick - nEarlyBreakThreshold)
						break;
				}
				
				// Validate path
				if (bFound)
				{
					int nSimTicks = TIME_TO_TICKS(flTime) + 1;
					if (!ValidateArcPath(pLocal, pWeapon, vAngle, vPredictedPos, pPlayer, nSimTicks, nullptr))
					{
						bFound = false;
						flBestDelta = std::numeric_limits<float>::max();
					}
				}
			}

			// If direct hit not found, try splash prediction for splash-capable weapons
			float flSplashRadius = GetSplashRadius(pWeapon, pLocal);
			bool bUsePrimeTime = Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::UsePrimeTime;
			bool bTrySplash = !bFound && flSplashRadius > 0.f && 
				Vars::Aimbot::Projectile::SplashPrediction.Value != Vars::Aimbot::Projectile::SplashPredictionEnum::Off;
			
			if (bTrySplash)
			{
				// Setup Amalgam's projectile info for splash calculations
				F::AimbotProjectile->SetupInfo(pLocal, pWeapon);
				
				// Create a Target_t for the Amalgam splash functions
				Target_t tTarget;
				tTarget.m_pEntity = pPlayer;
				tTarget.m_iTargetType = TargetEnum::Player;
				tTarget.m_vPos = pPlayer->m_vecOrigin();
				F::AimbotProjectile->m_tInfo.m_vTargetEye = pPlayer->GetViewOffset();
				
				// Generate splash sphere points
				float flSize = pPlayer->GetSize().Length();
				int iPoints = Vars::Aimbot::Projectile::SplashPointsArc.Value;
				auto vSpherePoints = CAimbotProjectile::ComputeSphere(flSplashRadius + flSize, iPoints);
				
				// For stickies with prime time, start search from arm time tick
				// Stickies can't explode before they're armed, so no point checking earlier ticks
				int nStartTick = 1;
				if (bUsePrimeTime && weaponInfo.m_nArmTicks > 0)
					nStartTick = weaponInfo.m_nArmTicks;
				
				// Try splash points at each simulation tick (starting from arm time for stickies)
				for (int nTick = nStartTick; nTick <= nMaxTicks && !bFound; nTick++)
				{
					tTarget.m_vPos = vTargetPositions[nTick];
					
					// Get splash points for this tick
					auto vSplashPoints = F::AimbotProjectile->GetSplashPoints(tTarget, vSpherePoints, nTick);
					
					for (auto& tPoint : vSplashPoints)
					{
						if (tPoint.m_tSolution.m_iCalculated != CalculatedEnum::Good)
							continue;
						
						// Calculate arc angle to splash point
						ArcSolution_t tSolution;
						if (!CalculateArcAngle(vShootPos, tPoint.m_vPoint, weaponInfo.m_flVelocity, weaponInfo.m_flGravity,
							tSolution, weaponInfo.m_flUpVelocity, nWeaponID))
							continue;
						
						int nArrivalTick = TIME_TO_TICKS(tSolution.m_flTime) + 1;
						
						float flTickDelta = fabsf(static_cast<float>(nArrivalTick - nTick));
						float flMaxTickDelta = weaponInfo.m_flVelocity > 1500.f ? 6.f : 4.f;
						if (flTickDelta > flMaxTickDelta)
							continue;
						
						// Validate path to splash point
						Vec3 vSplashAngle = { tSolution.m_flPitch, tSolution.m_flYaw, 0.f };
						int nSimTicks = TIME_TO_TICKS(tSolution.m_flTime) + 1;
						if (!ValidateArcPath(pLocal, pWeapon, vSplashAngle, tPoint.m_vPoint, pPlayer, nSimTicks, nullptr))
							continue;
						
						// Found valid splash point!
						vPredictedPos = tPoint.m_vPoint;
						vAngle = vSplashAngle;
						flTime = tSolution.m_flTime;
						nBestTick = nTick;
						flBestDelta = flTickDelta;
						bFound = true;
						break;
					}
				}
			}
			
			if (!bFound) continue;
			if (flTime > weaponInfo.m_flMaxTime) continue;

			float flFOV = Math::CalcFov(vViewAngles, vAngle);
			if (flFOV > CFG::Aimbot_Projectile_FOV) continue;

			// Validate path with visualization
			int nSimTicks = TIME_TO_TICKS(flTime) + 1;
			std::vector<Vec3> vProjectilePath;
			if (!ValidateArcPath(pLocal, pWeapon, vAngle, vPredictedPos, pPlayer, nSimTicks, &vProjectilePath))
				continue;

			bool bBetter = false;
			if (CFG::Aimbot_Projectile_Sort == 0)
				bBetter = flFOV < flBestFOV;
			else
				bBetter = flTime < flBestTime;

			if (bBetter)
			{
				pBestTarget = pPlayer;
				vBestAngle = vAngle;
				flBestFOV = flFOV;
				flBestTime = flTime;
				bHasValidPrediction = true;
				
				m_TargetPath = vTempPath;
				m_ProjectilePath = vProjectilePath;
			}
		}
	}

	// Building targeting - buildings don't move so no movement simulation needed
	if (CFG::Aimbot_Target_Buildings)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
		{
			if (!pEntity)
				continue;

			auto pBuilding = pEntity->As<C_BaseObject>();
			if (!pBuilding)
				continue;

			// Get building center as aim position
			Vec3 vBuildingPos = pBuilding->GetCenter();
			
			Vec3 vInitialAngle = Math::CalcAngle(vShootPos, vBuildingPos);
			float flInitialFOV = Math::CalcFov(vViewAngles, vInitialAngle);
			
			// Quick FOV check
			if (flInitialFOV > CFG::Aimbot_Projectile_FOV * 2.0f)
				continue;

			// Calculate arc angle to building
			ArcSolution_t tSolution;
			if (!CalculateArcAngle(vShootPos, vBuildingPos, weaponInfo.m_flVelocity, weaponInfo.m_flGravity,
				tSolution, weaponInfo.m_flUpVelocity, nWeaponID))
				continue;

			Vec3 vAngle = { tSolution.m_flPitch, tSolution.m_flYaw, 0.f };
			float flTime = tSolution.m_flTime;

			if (flTime > weaponInfo.m_flMaxTime)
				continue;

			float flFOV = Math::CalcFov(vViewAngles, vAngle);
			if (flFOV > CFG::Aimbot_Projectile_FOV)
				continue;

			// Validate path to building
			int nSimTicks = TIME_TO_TICKS(flTime) + 1;
			std::vector<Vec3> vProjectilePath;
			if (!ValidateArcPath(pLocal, pWeapon, vAngle, vBuildingPos, pEntity, nSimTicks, &vProjectilePath))
				continue;

			bool bBetter = false;
			if (CFG::Aimbot_Projectile_Sort == 0)
				bBetter = flFOV < flBestFOV;
			else
				bBetter = flTime < flBestTime;

			if (bBetter)
			{
				pBestTarget = pEntity;
				vBestAngle = vAngle;
				flBestFOV = flFOV;
				flBestTime = flTime;
				bHasValidPrediction = true;
				
				m_TargetPath.clear(); // Buildings don't move
				m_ProjectilePath = vProjectilePath;
			}
		}
	}

	if (!pBestTarget)
	{
		if (bIsChargeWeapon && !bIsCharging)
			m_LockedTarget.Reset();
		
		// Handle Loose Cannon: keep holding attack while we wait for target to reappear
		// The cancel charge at fuse < 0.026s handles the critical case
		if (bIsLooseCannon && bLooseCannonCharging && m_LooseCannonState.m_bAimbotInitiatedCharge)
		{
			pCmd->buttons |= IN_ATTACK;
			return;
		}
		
		// If we're charging but simulation failed to find target,
		// keep holding attack to prevent firing without valid aim.
		if (bIsChargeWeapon && bIsCharging && CFG::Aimbot_AutoShoot)
		{
			pCmd->buttons |= IN_ATTACK;
		}
		return;
	}

	G::nTargetIndex = pBestTarget->entindex();
	G::nTargetIndexEarly = pBestTarget->entindex();

	Math::ClampAngles(vBestAngle);

	// Auto-charge fire logic
	// Flow: simulation runs every tick -> if valid target found:
	//   - If not charging: start charging
	//   - If charging but not enough ticks: keep charging  
	//   - If charge ready: fire!
	if (CFG::Aimbot_AutoShoot)
	{
		switch (nWeaponID)
		{
		case TF_WEAPON_COMPOUND_BOW:
		case TF_WEAPON_PIPEBOMBLAUNCHER:
		case TF_WEAPON_STICKY_BALL_LAUNCHER:
		case TF_WEAPON_GRENADE_STICKY_BALL:
		{
			// We found a valid target via simulation!
			if (bHasValidPrediction)
			{
				// If not charging yet, START charging
				if (!bIsCharging)
				{
					pCmd->buttons |= IN_ATTACK; // Start charging
					return; // Don't fire yet - simulation will run again next tick
				}
				
				// If charging but not enough ticks, keep charging
				if (nTicksCharged < nMinChargeTicks)
				{
					pCmd->buttons |= IN_ATTACK; // Keep charging
					return; // Don't fire yet
				}
				
				// Charge is ready and we have valid prediction - FIRE!
				pCmd->buttons &= ~IN_ATTACK; // Release to fire
			}
			else
			{
				// No valid prediction but we might be charging - keep holding
				if (bIsCharging)
					pCmd->buttons |= IN_ATTACK;
			}
			break;
		}
		case TF_WEAPON_CANNON:
		{
			// Loose Cannon: hold to control fuse time, release to fire
			// The cannonball has a 1 second fuse that starts when you begin charging
			// For Double-Donk: impact + explosion within 0.5s = mini-crit explosion
			
			// Check if user was manually charging before aimbot took over
			// If so, skip timed double donk and just do normal projectile aimbot
			if (bUserManualCharge)
			{
				// User was already charging - just fire when we have a valid prediction
				if (bHasValidPrediction)
				{
					// Fire immediately since user is controlling the charge
					pCmd->buttons &= ~IN_ATTACK; // Release to fire
				}
				break;
			}
			
			if (bHasValidPrediction)
			{
				// If not charging yet, START charging and mark that aimbot initiated it
				if (!bIsCharging)
				{
					m_LooseCannonState.m_bAimbotInitiatedCharge = true;
					pCmd->buttons |= IN_ATTACK; // Start charging
					return; // Don't fire yet
				}
				
				// Calculate optimal charge time for Double Donk
				// Fuse time is 1 second, starts when charging begins
				// Double Donk requires explosion within 0.5s of impact
				
				if (CFG::Aimbot_Projectile_Timed_Double_Donk && m_LooseCannonState.m_bAimbotInitiatedCharge)
				{
					float flMortar = SDKUtils::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
					if (flMortar > 0.f)
					{
						// flBestTime is the projectile travel time to target
						float flTravelTime = flBestTime;
						
						// Calculate optimal charge time for double donk
						// Fuse starts when charging begins, so:
						// fuse_remaining_when_fired = mortar_time - charge_time
						// We want: fuse_remaining_when_fired = travel_time + delay
						// So: mortar_time - charge_time = travel_time + delay
						// charge_time = mortar_time - travel_time - delay
						
						// The slider value represents how long after impact the explosion should happen
						// Positive = explode later, Negative = explode sooner (more charge time)
						float flDesiredFuseAtImpact = CFG::Aimbot_Projectile_Double_Donk_Delay;
						
						// Add compensation for network latency and processing delay
						// This makes us fire earlier to account for delays
						float flLatencyCompensation = 0.1f; // 100ms compensation
						
						float flOptimalChargeTime = flMortar - flTravelTime - flDesiredFuseAtImpact + flLatencyCompensation;
						
						// Clamp to valid range
						// Min: 0 (can't have negative charge time)
						// Max: mortar_time - small buffer (don't overcharge and explode in face)
						if (flOptimalChargeTime < 0.f)
							flOptimalChargeTime = 0.f;
						if (flOptimalChargeTime > flMortar - 0.05f)
							flOptimalChargeTime = flMortar - 0.05f;
						
						// Convert to ticks
						int nOptimalChargeTicks = TIME_TO_TICKS(flOptimalChargeTime);
						
						// Ensure at least 1 tick of charge for consistency
						if (nOptimalChargeTicks < 1)
							nOptimalChargeTicks = 1;
						
						// If we haven't charged enough, keep charging
						if (nTicksCharged < nOptimalChargeTicks)
						{
							pCmd->buttons |= IN_ATTACK; // Keep charging
							return; // Don't fire yet
						}
						
						// Optimal charge reached - FIRE for double donk!
						pCmd->buttons &= ~IN_ATTACK; // Release to fire
						m_LooseCannonState.m_bAimbotInitiatedCharge = false;
						break;
					}
				}
				
				// Non-double-donk mode or fallback: use minimum charge ticks
				if (nTicksCharged < nMinChargeTicks)
				{
					pCmd->buttons |= IN_ATTACK; // Keep charging
					return; // Don't fire yet
				}
				
				// Charge is ready - FIRE!
				pCmd->buttons &= ~IN_ATTACK; // Release to fire
				m_LooseCannonState.m_bAimbotInitiatedCharge = false;
			}
			else
			{
				// No valid prediction - keep holding if charging
				// The cancel charge at fuse < 0.026s handles the critical case
				if (bIsCharging)
				{
					pCmd->buttons |= IN_ATTACK;
				}
			}
			break;
		}
		default:
			// Non-charge weapons: fire immediately when we have a valid prediction
			if (bHasValidPrediction)
				pCmd->buttons |= IN_ATTACK;
			break;
		}
	}

	// Determine if firing - ONLY set G::bFiring if we have a valid prediction
	// This prevents firing without proper aim
	G::bFiring = false;
	if (bHasValidPrediction)
	{
		switch (nWeaponID)
		{
		case TF_WEAPON_COMPOUND_BOW:
			G::bFiring = !(pCmd->buttons & IN_ATTACK) && pWeapon->As<C_TFPipebombLauncher>()->m_flChargeBeginTime() > 0.f;
			break;
		case TF_WEAPON_PIPEBOMBLAUNCHER:
		case TF_WEAPON_STICKY_BALL_LAUNCHER:
		case TF_WEAPON_GRENADE_STICKY_BALL:
		{
			auto pPipeLauncher = pWeapon->As<C_TFPipebombLauncher>();
			float flCharge = pPipeLauncher->m_flChargeBeginTime() > 0.f 
				? I::GlobalVars->curtime - pPipeLauncher->m_flChargeBeginTime() 
				: 0.f;
			float flChargeRate = SDKUtils::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon);
			float flAmount = Math::RemapValClamped(flCharge, 0.f, flChargeRate, 0.f, 1.f);
			G::bFiring = (!(pCmd->buttons & IN_ATTACK) && flAmount > 0.f) || flAmount >= 1.f;
			break;
		}
		case TF_WEAPON_CANNON:
		{
			// Loose Cannon: fires when attack is released while charging
			auto pCannon = pWeapon->As<CTFGrenadeLauncher>();
			float flDetonateTime = pCannon->m_flDetonateTime();
			float flMortar = SDKUtils::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
			if (flMortar > 0.f && flDetonateTime > 0.f)
			{
				float flCharge = flMortar - (flDetonateTime - I::GlobalVars->curtime);
				float flAmount = Math::RemapValClamped(flCharge, 0.f, flMortar, 0.f, 1.f);
				G::bFiring = (!(pCmd->buttons & IN_ATTACK) && flAmount > 0.f) || flAmount >= 1.f;
			}
			else
			{
				G::bFiring = G::bCanPrimaryAttack && (pCmd->buttons & IN_ATTACK);
			}
			break;
		}
		default:
			G::bFiring = G::bCanPrimaryAttack && (pCmd->buttons & IN_ATTACK);
			break;
		}
	}
	
	if (G::bFiring && bIsChargeWeapon)
		m_LockedTarget.Reset();

	// Apply aim - for arc weapons, always apply when we have a target (not just when firing)
	// This helps with visual feedback and smooth aiming
	if (CFG::Aimbot_Projectile_Aim_Type == 1) // Silent
	{
		// Silent aim: only apply when actually firing
		if (G::bFiring)
		{
			H::AimUtils->FixMovement(pCmd, vBestAngle);
			pCmd->viewangles = vBestAngle;
			G::bPSilentAngles = true;
			G::bSilentAngles = true;
		}
	}
	else // Normal aim
	{
		// Normal aim: always show where we're aiming
		pCmd->viewangles = vBestAngle;
		I::EngineClient->SetViewAngles(vBestAngle);
	}

	// Set G::Attacking using SDK::IsAttacking - same as proj aimbot
	// This determines if we're actually firing this frame
	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);

	// For charge weapons, also detect tap-fire (quick click without charging)
	// SDK::IsAttacking requires m_flChargeBeginTime > 0, but tap-fire may not register charge
	// Track button release to detect tap-fire
	static bool bWasHoldingAttack = false;
	bool bTapFired = false;
	if (bIsChargeWeapon)
	{
		bool bHoldingAttack = (pCmd->buttons & IN_ATTACK) != 0;
		// Tap-fire: was holding attack, now released, and can attack
		if (bWasHoldingAttack && !bHoldingAttack && G::CanPrimaryAttack)
		{
			bTapFired = true;
		}
		bWasHoldingAttack = bHoldingAttack;
	}

	// Combine SDK::IsAttacking with tap-fire detection
	bool bActuallyFiring = (G::Attacking == 1) || bTapFired;

	// Draw paths for visualization - matches proj aimbot DrawVisuals exactly
	// Only draw when actually attacking OR when autoshoot is off (manual mode)
	// This ensures we only draw once per shot, not every frame
	if (bActuallyFiring || !Vars::Aimbot::General::AutoShoot.Value || !bHasValidPrediction)
	{
		bool bPlayerPath = Vars::Visuals::Simulation::PlayerPath.Value && !m_TargetPath.empty();
		// Projectile path: only when actually firing AND valid prediction
		bool bProjectilePath = Vars::Visuals::Simulation::ProjectilePath.Value && !m_ProjectilePath.empty() && 
			(bActuallyFiring || Vars::Debug::Info.Value) && bHasValidPrediction;
		
		if (bPlayerPath || bProjectilePath)
		{
			G::PathStorage.clear();
			G::BoxStorage.clear();
			G::LineStorage.clear();

			if (bPlayerPath)
			{
				if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
					G::PathStorage.emplace_back(m_TargetPath, Vars::Visuals::Simulation::Timed.Value ? -int(m_TargetPath.size()) : I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Simulation::PlayerPath.Value);
				if (Vars::Colors::PlayerPath.Value.a)
					G::PathStorage.emplace_back(m_TargetPath, Vars::Visuals::Simulation::Timed.Value ? -int(m_TargetPath.size()) : I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPath.Value, Vars::Visuals::Simulation::PlayerPath.Value, true);
			}
			if (bProjectilePath)
			{
				if (Vars::Colors::ProjectilePathIgnoreZ.Value.a)
					G::PathStorage.emplace_back(m_ProjectilePath, Vars::Visuals::Simulation::Timed.Value ? -int(m_ProjectilePath.size()) - TIME_TO_TICKS(F::Backtrack.GetReal()) : I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::ProjectilePathIgnoreZ.Value, Vars::Visuals::Simulation::ProjectilePath.Value);
				if (Vars::Colors::ProjectilePath.Value.a)
					G::PathStorage.emplace_back(m_ProjectilePath, Vars::Visuals::Simulation::Timed.Value ? -int(m_ProjectilePath.size()) - TIME_TO_TICKS(F::Backtrack.GetReal()) : I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::ProjectilePath.Value, Vars::Visuals::Simulation::ProjectilePath.Value, true);
			}
		}
	}

	// Clear paths after firing (so next shot gets fresh paths)
	if (bActuallyFiring)
	{
		m_TargetPath.clear();
		m_ProjectilePath.clear();
	}
}

bool CAimbotProjectileArc::IsFiring(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	const int nWeaponID = pWeapon->GetWeaponID();

	switch (nWeaponID)
	{
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	{
		auto pPipeLauncher = static_cast<C_TFPipebombLauncher*>(pWeapon);
		float flCharge = pPipeLauncher->m_flChargeBeginTime() > 0.f 
			? I::GlobalVars->curtime - pPipeLauncher->m_flChargeBeginTime() 
			: 0.f;
		float flChargeRate = SDKUtils::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon);
		float flAmount = Math::RemapValClamped(flCharge, 0.f, flChargeRate, 0.f, 1.f);
		return (!(pCmd->buttons & IN_ATTACK) && flAmount > 0.f) || flAmount >= 1.f;
	}

	case TF_WEAPON_COMPOUND_BOW:
	{
		auto pBow = static_cast<C_TFPipebombLauncher*>(pWeapon);
		return !(pCmd->buttons & IN_ATTACK) && pBow->m_flChargeBeginTime() > 0.f;
	}

	case TF_WEAPON_CANNON:
	{
		// Loose Cannon: fires when attack is released while charging
		auto pCannon = pWeapon->As<CTFGrenadeLauncher>();
		float flDetonateTime = pCannon->m_flDetonateTime();
		float flMortar = SDKUtils::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
		if (flMortar > 0.f && flDetonateTime > 0.f)
		{
			float flCharge = flMortar - (flDetonateTime - I::GlobalVars->curtime);
			float flAmount = Math::RemapValClamped(flCharge, 0.f, flMortar, 0.f, 1.f);
			return (!(pCmd->buttons & IN_ATTACK) && flAmount > 0.f) || flAmount >= 1.f;
		}
		return G::bCanPrimaryAttack && (pCmd->buttons & IN_ATTACK);
	}

	default:
		return G::bCanPrimaryAttack && (pCmd->buttons & IN_ATTACK);
	}
}
