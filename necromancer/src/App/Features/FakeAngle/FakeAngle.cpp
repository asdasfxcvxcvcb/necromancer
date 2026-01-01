#include "FakeAngle.h"
#include "../CFG.h"
#include "../amalgam_port/AmalgamCompat.h"

// Jitter system (hash-based like Amalgam)
static inline int GetJitter(uint32_t uHash)
{
	static std::unordered_map<uint32_t, bool> mJitter = {};

	if (!I::ClientState->chokedcommands)
		mJitter[uHash] = !mJitter[uHash];
	return mJitter[uHash] ? 1 : -1;
}

// Simple hash for jitter keys
static constexpr uint32_t HashJitter(const char* str)
{
	uint32_t hash = 2166136261u;
	while (*str)
	{
		hash ^= static_cast<uint32_t>(*str++);
		hash *= 16777619u;
	}
	return hash;
}

bool CFakeAngle::AntiAimOn()
{
	if (!CFG::Exploits_AntiAim_Enabled)
		return false;
	
	// Legit AA counts as anti-aim being on
	if (CFG::Exploits_LegitAA_Enabled)
		return true;
	
	return (CFG::Exploits_AntiAim_PitchReal
		|| CFG::Exploits_AntiAim_PitchFake
		|| CFG::Exploits_AntiAim_YawReal
		|| CFG::Exploits_AntiAim_YawFake
		|| CFG::Exploits_AntiAim_RealYawBase
		|| CFG::Exploits_AntiAim_FakeYawBase
		|| CFG::Exploits_AntiAim_RealYawOffset
		|| CFG::Exploits_AntiAim_FakeYawOffset);
}

bool CFakeAngle::YawOn()
{
	if (!CFG::Exploits_AntiAim_Enabled)
		return false;
	
	// Legit AA counts as yaw being on
	if (CFG::Exploits_LegitAA_Enabled)
		return true;
	
	return (CFG::Exploits_AntiAim_YawReal
		|| CFG::Exploits_AntiAim_YawFake
		|| CFG::Exploits_AntiAim_RealYawBase
		|| CFG::Exploits_AntiAim_FakeYawBase
		|| CFG::Exploits_AntiAim_RealYawOffset
		|| CFG::Exploits_AntiAim_FakeYawOffset);
}

bool CFakeAngle::ShouldRun(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive() || pLocal->InCond(TF_COND_TAUNTING))
		return false;
	
	// Ghost check
	if (pLocal->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
		return false;
	
	// Use GetMoveType() - the netvar-based one
	if (pLocal->GetMoveType() != MOVETYPE_WALK)
		return false;
	
	if (pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return false;
	
	// Don't anti-aim while charging (demoman shield)
	if (pLocal->InCond(TF_COND_SHIELD_CHARGE))
		return false;
	
	// Don't anti-aim when attacking
	if (G::Attacking == 1 || G::bFiring || G::bSilentAngles || G::bPSilentAngles)
		return false;
	
	// Don't anti-aim during rapid fire shifting
	if (Shifting::bShiftingRapidFire || Shifting::bRapidFireWantShift)
		return false;
	
	// Don't anti-aim during recharging
	if (Shifting::bRecharging)
		return false;
	
	// Beggar's Bazooka check (like Amalgam)
	if (pWeapon && pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka 
		&& pCmd->buttons & IN_ATTACK && !(G::LastUserCmd ? G::LastUserCmd->buttons & IN_ATTACK : false))
		return false;
	
	return true;
}

// Legit AA: Get yaw offset based on class, weapon slot, and movement direction
static float GetLegitAAOffset(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal || !pWeapon || !pCmd)
		return 0.0f;
	
	int iClass = pLocal->m_iClass();
	
	// Get weapon slot
	int iSlot = pWeapon->GetSlot();
	bool bPrimary = (iSlot == WEAPON_SLOT_PRIMARY);
	bool bSecondary = (iSlot == WEAPON_SLOT_SECONDARY);
	bool bMelee = (iSlot >= WEAPON_SLOT_MELEE);
	
	// Get movement direction
	bool bForward = pCmd->forwardmove > 10.0f;
	bool bBackward = pCmd->forwardmove < -10.0f;
	bool bLeft = pCmd->sidemove < -10.0f;
	bool bRight = pCmd->sidemove > 10.0f;
	bool bStill = !bForward && !bBackward && !bLeft && !bRight;
	
	switch (iClass)
	{
		case TF_CLASS_SCOUT:
			if (bStill) return -105.0f; // All weapons standing still
			if (bMelee) return bLeft ? 100.0f : 100.0f;
			if (bLeft) return bSecondary ? 100.0f : 120.0f;
			return -110.0f;
		
		case TF_CLASS_SOLDIER:
			if (bMelee) return bStill ? -95.0f : 100.0f;
			if (bSecondary) return bStill ? 110.0f : 100.0f;
			if (bStill) return -95.0f;
			if (bRight) return -100.0f;
			return 110.0f;
		
		case TF_CLASS_PYRO:
			return bStill ? 110.0f : 115.0f;
		
		case TF_CLASS_DEMOMAN:
			return 120.0f;
		
		case TF_CLASS_HEAVYWEAPONS:
			return bStill ? 120.0f : 115.0f;
		
		case TF_CLASS_ENGINEER:
			return 105.0f;
		
		case TF_CLASS_MEDIC:
			if (bStill) return -180.0f;
			if (bLeft && (bForward || bBackward)) return 125.0f;
			if (bRight && (bForward || bBackward)) return -100.0f;
			if (bForward || bBackward) return 105.0f;
			if (bLeft) return 125.0f;
			if (bRight) return -100.0f;
			return 105.0f;
		
		case TF_CLASS_SNIPER:
			if (bPrimary)
			{
				bool bScoped = pLocal->InCond(TF_COND_ZOOMED);
				return bScoped ? -90.0f : -180.0f;
			}
			if (bSecondary)
			{
				if (bStill) return -180.0f;
				if (bLeft && (bForward || bBackward)) return 90.0f;
				if (bRight && (bForward || bBackward)) return -90.0f;
				return -180.0f;
			}
			// Melee
			if (bStill) return 120.0f;
			if (bForward && !bLeft && !bRight) return -90.0f;
			if (bBackward && !bLeft && !bRight) return -180.0f;
			if (bRight) return -120.0f;
			if (bLeft) return 110.0f;
			return 120.0f;
		
		case TF_CLASS_SPY:
			if (bSecondary && pWeapon->GetWeaponID() == TF_WEAPON_BUILDER)
				return -180.0f; // Sapper
			if (bMelee)
				return bRight ? -120.0f : 100.0f;
			// Primary/Revolver
			if (bStill) return 180.0f;
			if (bLeft && bBackward) return 90.0f;
			if (bLeft && bForward) return 120.0f;
			if (bLeft) return 110.0f;
			if (bForward && !bRight) return 105.0f;
			if (bBackward && !bRight) return 95.0f;
			if (bRight) return -130.0f;
			return 105.0f;
	}
	return 0.0f;
}

float CFakeAngle::GetYawOffset(C_TFPlayer* pLocal, bool bFake)
{
	// If Legit AA is enabled:
	// - Fake yaw (what enemies see) = Forward (0)
	// - Real yaw (your hitbox) = class/weapon/movement based offset
	if (CFG::Exploits_LegitAA_Enabled)
	{
		if (bFake)
			return 0.0f; // Fake yaw is forward
		
		// Real yaw uses the legit AA offset
		auto pWeapon = H::Entities->GetWeapon();
		return GetLegitAAOffset(pLocal, pWeapon, G::CurrentUserCmd);
	}
	
	const int iMode = bFake ? CFG::Exploits_AntiAim_YawFake : CFG::Exploits_AntiAim_YawReal;
	int iJitter = GetJitter(HashJitter("Yaw"));
	
	// Yaw modes: 0=Forward, 1=Left, 2=Right, 3=Backwards, 4=Edge, 5=Jitter, 6=Spin
	switch (iMode)
	{
		case 0: return 0.0f; // Forward
		case 1: return 90.0f; // Left
		case 2: return -90.0f; // Right
		case 3: return 180.0f; // Backwards
		case 4: // Edge - just use yaw value (no trace-based edge detection)
			return (bFake ? CFG::Exploits_AntiAim_FakeYawValue : CFG::Exploits_AntiAim_RealYawValue);
		case 5: // Jitter
			return (bFake ? CFG::Exploits_AntiAim_FakeYawValue : CFG::Exploits_AntiAim_RealYawValue) * iJitter;
		case 6: // Spin
			return fmod(I::GlobalVars->tickcount * CFG::Exploits_AntiAim_SpinSpeed + 180.0f, 360.0f) - 180.0f;
	}
	return 0.0f;
}

float CFakeAngle::GetBaseYaw(C_TFPlayer* pLocal, CUserCmd* pCmd, bool bFake)
{
	// Legit AA: always use view-based yaw with no offset
	if (CFG::Exploits_LegitAA_Enabled)
		return pCmd->viewangles.y;
	
	const int iMode = bFake ? CFG::Exploits_AntiAim_FakeYawBase : CFG::Exploits_AntiAim_RealYawBase;
	const float flOffset = bFake ? CFG::Exploits_AntiAim_FakeYawOffset : CFG::Exploits_AntiAim_RealYawOffset;
	
	// YawBase modes: 0=View, 1=Target
	switch (iMode)
	{
		case 0: // View
			return pCmd->viewangles.y + flOffset;
		case 1: // Target - find closest enemy
		{
			float flSmallestAngleTo = 0.0f;
			float flSmallestFovTo = 360.0f;
			
			for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
			{
				auto pEntity = I::ClientEntityList->GetClientEntity(i);
				if (!pEntity || pEntity == pLocal)
					continue;
				
				auto pPlayer = pEntity->As<C_TFPlayer>();
				if (!pPlayer || !pPlayer->IsAlive())
					continue;
				
				// Skip ghosts
				if (pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
					continue;
				
				if (pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
					continue;
				
				Vec3 vAngleTo = Math::CalcAngle(pLocal->m_vecOrigin(), pPlayer->m_vecOrigin());
				float flFOVTo = Math::CalcFov(I::EngineClient->GetViewAngles(), vAngleTo);
				
				if (flFOVTo < flSmallestFovTo)
				{
					flSmallestAngleTo = vAngleTo.y;
					flSmallestFovTo = flFOVTo;
				}
			}
			
			return (flSmallestFovTo == 360.0f ? pCmd->viewangles.y + flOffset : flSmallestAngleTo + flOffset);
		}
	}
	return pCmd->viewangles.y;
}

void CFakeAngle::RunOverlapping(C_TFPlayer* pLocal, CUserCmd* pCmd, float& flYaw, bool bFake, float flEpsilon)
{
	if (!CFG::Exploits_AntiAim_AntiOverlap || bFake)
		return;
	
	float flFakeYaw = GetBaseYaw(pLocal, pCmd, true) + GetYawOffset(pLocal, true);
	const float flYawDiff = Math::NormalizeAngle(flYaw - flFakeYaw);
	if (fabsf(flYawDiff) < flEpsilon)
		flYaw += flYawDiff > 0 ? flEpsilon : -flEpsilon;
}

float CFakeAngle::GetYaw(C_TFPlayer* pLocal, CUserCmd* pCmd, bool bFake)
{
	float flYaw = GetBaseYaw(pLocal, pCmd, bFake) + GetYawOffset(pLocal, bFake);
	RunOverlapping(pLocal, pCmd, flYaw, bFake);
	return flYaw;
}

float CFakeAngle::GetPitch(float flCurPitch)
{
	// Legit AA: no pitch manipulation, use current pitch
	if (CFG::Exploits_LegitAA_Enabled)
		return flCurPitch;
	
	int iJitter = GetJitter(HashJitter("Pitch"));
	
	float flRealPitch = 0.0f, flFakePitch = 0.0f;
	
	// PitchReal: 0=None, 1=Up, 2=Down, 3=Zero, 4=Jitter, 5=ReverseJitter
	switch (CFG::Exploits_AntiAim_PitchReal)
	{
		case 1: flRealPitch = -89.0f; break; // Up
		case 2: flRealPitch = 89.0f; break; // Down
		case 3: flRealPitch = 0.0f; break; // Zero
		case 4: flRealPitch = -89.0f * iJitter; break; // Jitter
		case 5: flRealPitch = 89.0f * iJitter; break; // ReverseJitter
	}
	
	// PitchFake: 0=None, 1=Up, 2=Down, 3=Jitter, 4=ReverseJitter
	switch (CFG::Exploits_AntiAim_PitchFake)
	{
		case 1: flFakePitch = -89.0f; break; // Up
		case 2: flFakePitch = 89.0f; break; // Down
		case 3: flFakePitch = -89.0f * iJitter; break; // Jitter
		case 4: flFakePitch = 89.0f * iJitter; break; // ReverseJitter
	}
	
	// Amalgam logic: if both are set, create exploit pitch
	// The +/-360 creates an "invalid" angle that the server clamps differently
	// This makes your visual model show fake pitch while real hitbox uses real pitch
	// NOTE: This only works for extreme pitches (up/down), not arbitrary values
	if (CFG::Exploits_AntiAim_PitchReal && CFG::Exploits_AntiAim_PitchFake)
		return flRealPitch + (flFakePitch > 0.0f ? 360.0f : -360.0f);
	else if (CFG::Exploits_AntiAim_PitchReal)
		return flRealPitch;
	else if (CFG::Exploits_AntiAim_PitchFake)
		return flFakePitch;
	else
		return flCurPitch;
}

void CFakeAngle::FakeShotAngles(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!CFG::Exploits_AntiAim_InvalidShootPitch || G::Attacking != 1 || pLocal->GetMoveType() != MOVETYPE_WALK)
		return;
	
	if (!pWeapon)
		return;
	
	// Don't apply to medigun or laser pointer
	int iWeaponID = pWeapon->GetWeaponID();
	if (iWeaponID == TF_WEAPON_MEDIGUN || iWeaponID == TF_WEAPON_LASER_POINTER)
		return;
	
	EWeaponType eWeaponType = H::AimUtils->GetWeaponType(pWeapon);
	
	G::bSilentAngles = true;
	
	switch (eWeaponType)
	{
		case EWeaponType::HITSCAN:
		{
			// Hitscan: flip pitch and yaw (180 - pitch, yaw + 180)
			// This creates an "invalid" angle that still hits the same spot
			pCmd->viewangles.x = 180.0f - pCmd->viewangles.x;
			pCmd->viewangles.y += 180.0f;
			break;
		}
		
		case EWeaponType::PROJECTILE:
		{
			// Projectile: use +/-360 pitch exploit (compatible with neckbreaker roll)
			// This hides the pitch visually while preserving the actual aim direction
			// The server clamps pitch but the visual model shows the "fake" direction
			// Preserve roll for neckbreaker compatibility
			pCmd->viewangles.x += 360.0f * (pCmd->viewangles.x < 0.0f ? -1.0f : 1.0f);
			break;
		}
		
		default:
			// Melee/Other: don't apply fake shot angles
			G::bSilentAngles = false;
			break;
	}
}

void CFakeAngle::MinWalk(CUserCmd* pCmd, C_TFPlayer* pLocal)
{
	if (!CFG::Exploits_AntiAim_MinWalk || !YawOn() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;
	
	if (!pLocal->m_hGroundEntity())
		return;
	
	if (!pCmd->forwardmove && !pCmd->sidemove && pLocal->m_vecVelocity().Length2D() < 2.0f)
	{
		// Amalgam's MinWalk with proper rotation
		static bool bVar = true;
		float flMove = (IsDucking(pLocal) ? 3.0f : 1.0f) * ((bVar = !bVar) ? 1.0f : -1.0f);
		Vec3 vDir = { flMove, flMove, 0.0f };
		
		// Rotate movement to account for view angles
		Vec3 vMove = Math::RotatePoint(vDir, {}, { 0.0f, -pCmd->viewangles.y, 0.0f });
		pCmd->forwardmove = vMove.x * (fmodf(fabsf(pCmd->viewangles.x), 180.0f) > 90.0f ? -1.0f : 1.0f);
		pCmd->sidemove = -vMove.y;
		
		// Prevent standing still detection (Amalgam trick)
		pLocal->m_vecVelocity() = { 1.0f, 1.0f, pLocal->m_vecVelocity().z };
	}
}

void CFakeAngle::Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, bool bSendPacket)
{
	// Set global anti-aim flag (like Amalgam's G::AntiAim)
	// ShouldRun checks G::Attacking == 1, which is now updated AFTER aimbot runs
	bool bAntiAimActive = AntiAimOn() && ShouldRun(pLocal, pWeapon, pCmd);
	
	// FakeShotAngles runs regardless of anti-aim state
	FakeShotAngles(pLocal, pWeapon, pCmd);
	
	if (!bAntiAimActive)
	{
		// Store current angles (clamped for visual purposes)
		m_vRealAngles = { std::clamp(pCmd->viewangles.x, -89.0f, 89.0f), pCmd->viewangles.y };
		m_vFakeAngles = { std::clamp(pCmd->viewangles.x, -89.0f, 89.0f), pCmd->viewangles.y };
		return;
	}
	
	// Amalgam approach:
	// bSendPacket = true  -> This is the FAKE tick (sent to server, what enemies see)
	// bSendPacket = false -> This is the REAL tick (choked, your actual hitbox)
	
	// Get the appropriate angles for this tick
	Vec2& vAngles = bSendPacket ? m_vFakeAngles : m_vRealAngles;
	
	// Pitch: Amalgam uses a single GetPitch that combines real+fake into exploit pitch
	// The pitch is the SAME for both ticks - the exploit is in the value itself
	vAngles.x = GetPitch(pCmd->viewangles.x);
	
	// Yaw: Different for real vs fake
	// bSendPacket = true means fake yaw (what enemies see)
	// bSendPacket = false means real yaw (your actual hitbox direction)
	vAngles.y = GetYaw(pLocal, pCmd, bSendPacket);
	
	// Store both angles for visualization (fake model needs fake angles)
	// We need to calculate the OTHER set of angles too for the fake model
	if (bSendPacket)
	{
		// This is the fake tick, also calculate real angles for storage
		m_vRealAngles.x = GetPitch(pCmd->viewangles.x);
		m_vRealAngles.y = GetYaw(pLocal, pCmd, false); // false = real yaw
	}
	else
	{
		// This is the real tick, also calculate fake angles for storage
		m_vFakeAngles.x = GetPitch(pCmd->viewangles.x);
		m_vFakeAngles.y = GetYaw(pLocal, pCmd, true); // true = fake yaw
	}
	
	// Apply angles to cmd
	Vec3 vCmdAngles = { vAngles.x, vAngles.y, 0.0f };
	
	// Only clamp cmd angles when anti-cheat compatibility is enabled (like Amalgam)
	if (CFG::Misc_AntiCheat_Enabled)
		Math::ClampAngles(vCmdAngles);
	
	// Fix movement using the angles we're applying
	// Use clamped pitch for movement fix to avoid issues
	Vec3 vClampedForMovement = { std::clamp(vAngles.x, -89.0f, 89.0f), vAngles.y, 0.0f };
	H::AimUtils->FixMovement(pCmd, vClampedForMovement);
	
	// Apply angles
	pCmd->viewangles.x = vCmdAngles.x;
	pCmd->viewangles.y = vCmdAngles.y;
	
	G::bSilentAngles = true;
	
	MinWalk(pCmd, pLocal);
}

void CFakeAngle::StoreSentBones(C_TFPlayer* pLocal)
{
	if (!pLocal || !pLocal->IsAlive())
	{
		m_bLastSentBonesValid = false;
		return;
	}
	
	auto pCachedBoneData = pLocal->GetCachedBoneData();
	if (!pCachedBoneData || pCachedBoneData->Count() <= 0)
	{
		m_bLastSentBonesValid = false;
		return;
	}
	
	// Store current bones as "last sent" bones
	memcpy(m_aLastSentBones, pCachedBoneData->Base(), sizeof(matrix3x4_t) * std::min(pCachedBoneData->Count(), 128));
	m_bLastSentBonesValid = true;
}

void CFakeAngle::SetupFakeModel(C_TFPlayer* pLocal)
{
	if (!pLocal || !pLocal->IsAlive())
	{
		m_bBonesSetup = false;
		return;
	}
	
	// Check if we should draw fake model:
	// 1. Anti-aim is on, OR
	// 2. Fakelag is enabled AND we're actually choking (chokedcommands > 0)
	bool bShouldDraw = AntiAimOn() || (CFG::Exploits_FakeLag_Enabled && I::ClientState->chokedcommands > 0);
	
	if (!bShouldDraw)
	{
		m_bBonesSetup = false;
		return;
	}
	
	// For fakelag-only mode (no anti-aim), use the last sent bones
	if (!AntiAimOn())
	{
		// Use last sent bones for fakelag visualization
		if (m_bLastSentBonesValid)
		{
			memcpy(m_aBones, m_aLastSentBones, sizeof(matrix3x4_t) * 128);
			m_bBonesSetup = true;
		}
		else
		{
			m_bBonesSetup = false;
		}
		return;
	}
	
	// Anti-aim mode: setup bones based on fake angles
	auto pAnimState = pLocal->GetAnimState();
	if (!pAnimState)
	{
		m_bBonesSetup = false;
		return;
	}
	
	// Skip if angles are too similar (no point showing fake model)
	// BUT always show for Legit AA since it's important to see the fake angle
	if (!CFG::Exploits_LegitAA_Enabled)
	{
		// Compare the VISUAL angles (clamped pitch) since that's what the model shows
		float flVisualRealPitch = std::clamp(m_vRealAngles.x, -89.0f, 89.0f);
		float flVisualFakePitch = std::clamp(m_vFakeAngles.x, -89.0f, 89.0f);
		float flPitchDiff = fabsf(flVisualRealPitch - flVisualFakePitch);
		float flYawDiff = fabsf(Math::NormalizeAngle(m_vRealAngles.y - m_vFakeAngles.y));
		if (flPitchDiff < 5.0f && flYawDiff < 5.0f)
		{
			m_bBonesSetup = false;
			return;
		}
	}
	
	// Save original state
	float flOldFrameTime = I::GlobalVars->frametime;
	int nOldSequence = pLocal->m_nSequence();
	float flOldCycle = pLocal->m_flCycle();
	auto pOldPoseParams = pLocal->m_flPoseParameter();
	
	// Save anim state
	char pOldAnimState[sizeof(CMultiPlayerAnimState)];
	memcpy(pOldAnimState, pAnimState, sizeof(CMultiPlayerAnimState));
	
	// Setup fake angles - clamp pitch for visual (like Amalgam)
	I::GlobalVars->frametime = 0.0f;
	Vec2 vAngle = { std::clamp(m_vFakeAngles.x, -89.0f, 89.0f), m_vFakeAngles.y };
	
	if (pLocal->InCond(TF_COND_TAUNTING) && pLocal->m_bAllowMoveDuringTaunt())
		pLocal->m_flTauntYaw() = vAngle.y;
	
	// Amalgam style: Update with feet yaw set to the fake yaw
	// This is what makes the fake model face the fake direction
	pAnimState->m_flCurrentFeetYaw = vAngle.y;
	pAnimState->Update(vAngle.y, vAngle.x);
	
	// Invalidate and setup bones with the new pose parameters
	pLocal->InvalidateBoneCache();
	m_bBonesSetup = pLocal->SetupBones(m_aBones, 128, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
	
	// Restore original state
	I::GlobalVars->frametime = flOldFrameTime;
	pLocal->m_nSequence() = nOldSequence;
	pLocal->m_flCycle() = flOldCycle;
	pLocal->m_flPoseParameter() = pOldPoseParams;
	memcpy(pAnimState, pOldAnimState, sizeof(CMultiPlayerAnimState));
}
