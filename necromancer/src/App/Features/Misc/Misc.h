#pragma once

#include "../../../Utils/Vector/Vector.h"
#include "../../../SDK/SDK.h"

class CMisc
{
public:
	void Bunnyhop(CUserCmd* pCmd);
	void AutoJumpbug(C_TFPlayer* pLocal, CUserCmd* pCmd);
	void AutoStrafer(CUserCmd* pCmd);
	void CrouchWhileAirborne(CUserCmd* pCmd);
	void NoiseMakerSpam();
	void FastStop(CUserCmd* pCmd);
	void FastAccelerate(CUserCmd* pCmd);

	void AutoRocketJump(CUserCmd* cmd);
	void AutoDisguise(CUserCmd* cmd);
	void AutoMedigun(CUserCmd* cmd);
	void MovementLock(CUserCmd* cmd);
	void MvmInstaRespawn();
	void AntiAFK(CUserCmd* pCmd);
	void PDAExploit(CUserCmd* pCmd);
	void AutoCallMedic();
	void VoiceCommandSpam();
	void AutoFaN(CUserCmd* pCmd);
	void FastClassSwitch();

	int GetHPThresholdForClass(int nClass);

	// Auto Rocket Jump state (public so FakeLag and AntiCheat can access)
	bool m_bRJDisableFakeLag = false;
	
	// Auto FaN state (public so FakeLag and AntiCheat can access)
	bool m_bFaNRunning = false;
	
	// Check if auto rocket jump is currently running a sequence
	bool IsAutoRocketJumpRunning() const { return m_iRJFrame != -1 || m_bRJDisableFakeLag; }
	
	// Check if auto FaN is currently running
	bool IsAutoFaNRunning() const { return m_bFaNRunning; }

	float m_flLastJumpbug = 0.f;
	float m_flLastEdgeJump = 0.f;
	float m_flLastFaN = 0.f;

private:
	bool m_bJumpbugEngaged = false;
	int m_iRJFrame = -1;
	int m_iRJDelay = 0;
	bool m_bRJFull = false;
	Vec3 m_vRJAngles = {};
	bool m_bRJCancelingReload = false;
	
	bool SetRocketJumpAngles(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);

	// Fast Class Switch state machine
	enum FCSState {
		FCS_IDLE,			// Monitoring for death
		FCS_SWITCHING_BACK,	// Sent joinclass commands, keep sending joinclass original until we respawn as it
	};
	int m_iFCSState = FCS_IDLE;
	int m_iFCSOriginalClass = 0;		// The class to switch back to
	bool m_bFCSWasAlive = false;		// Was the player alive last tick?
	float m_flFCSSendTime = 0.0f;		// Last time we sent joinclass (throttle to avoid server kick)
};

MAKE_SINGLETON_SCOPED(CMisc, Misc, F);
