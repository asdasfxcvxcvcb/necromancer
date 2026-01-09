#pragma once
#include "../../../SDK/SDK.h"

class CFakeAngle
{
	// Angles storage (Vec2 like Amalgam)
	Vec2 m_vRealAngles = {};
	Vec2 m_vFakeAngles = {};

	float GetYawOffset(C_TFPlayer* pLocal, bool bFake);
	float GetBaseYaw(C_TFPlayer* pLocal, CUserCmd* pCmd, bool bFake);
	void RunOverlapping(C_TFPlayer* pLocal, CUserCmd* pCmd, float& flYaw, bool bFake, float flEpsilon = 15.0f);
	float GetYaw(C_TFPlayer* pLocal, CUserCmd* pCmd, bool bFake);
	float GetPitch(float flCurPitch);
	void FakeShotAngles(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);

public:
	bool AntiAimOn();
	bool YawOn();
	bool ShouldRun(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, bool bSendPacket, const Vec3& vOriginalAngles);
	void MinWalk(CUserCmd* pCmd, C_TFPlayer* pLocal);
	void SetupFakeModel(C_TFPlayer* pLocal);
	void StoreSentBones(C_TFPlayer* pLocal); // Store bones when packet is sent
	
	Vec2 GetRealAngles() const { return m_vRealAngles; }
	Vec2 GetFakeAngles() const { return m_vFakeAngles; }
	
	// Returns number of ticks needed for anti-aim (2)
	int AntiAimTicks() const { return 2; }
	
	// Fake model bones for chams
	matrix3x4_t m_aBones[128];
	bool m_bBonesSetup = false;
	
	// Last sent bones (for fakelag-only mode)
	matrix3x4_t m_aLastSentBones[128];
	bool m_bLastSentBonesValid = false;
	
	// Whether to draw fake angle chams (set by PacketManip)
	bool m_bDrawChams = false;
};

MAKE_SINGLETON_SCOPED(CFakeAngle, FakeAngle, F);
