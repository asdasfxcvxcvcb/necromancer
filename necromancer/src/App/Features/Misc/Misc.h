#pragma once

#include "../../../SDK/SDK.h"

class CMisc
{
public:
	void Bunnyhop(CUserCmd* pCmd);
	void AutoStrafer(CUserCmd* pCmd);
	void CrouchWhileAirborne(CUserCmd* pCmd);
	void NoiseMakerSpam();
	void FastStop(CUserCmd* pCmd);
	void FastAccelerate(CUserCmd* pCmd);

	void AutoRocketJump(CUserCmd* cmd);
	void AutoDisguise(CUserCmd* cmd);
	void AutoUber(CUserCmd* cmd);
	void AutoMedigun(CUserCmd* cmd);
	void MovementLock(CUserCmd* cmd);
	void MvmInstaRespawn();
	void AntiAFK(CUserCmd* pCmd);
	void AutoCallMedic();
	void AutoFaN(CUserCmd* pCmd);
	
	int GetHPThresholdForClass(int nClass);

	// Auto Rocket Jump state (public so FakeLag and AntiCheat can access)
	bool m_bRJDisableFakeLag = false;
	
	// Auto FaN state (public so FakeLag and AntiCheat can access)
	bool m_bFaNRunning = false;
	
	// Check if auto rocket jump is currently running a sequence
	bool IsAutoRocketJumpRunning() const { return m_iRJFrame != -1 || m_bRJDisableFakeLag; }
	
	// Check if auto FaN is currently running
	bool IsAutoFaNRunning() const { return m_bFaNRunning; }

private:
	int m_iRJFrame = -1;
	int m_iRJDelay = 0;
	bool m_bRJFull = false;
	Vec3 m_vRJAngles = {};
	bool m_bRJCancelingReload = false;
	
	bool SetRocketJumpAngles(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
};

MAKE_SINGLETON_SCOPED(CMisc, Misc, F);
