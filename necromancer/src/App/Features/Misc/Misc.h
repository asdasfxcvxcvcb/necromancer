#pragma once

#include "../../../Utils/Vector/Vector.h"
#include "../../../SDK/SDK.h"

#include <deque>
#include <unordered_set>
#include <unordered_map>

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
	void ApplyFakeInterp();
	void AntiAFK(CUserCmd* pCmd);
	void PDAExploit(CUserCmd* pCmd);
	void AutoCallMedic();
	void VoiceCommandSpam();
	void AutoFaN(CUserCmd* pCmd);
	void FastClassSwitch();
	void AutoBalanceRun();
	void AutoBalanceWarnDraw();
	void AutoBalanceWarningSeen();
	void ReportSpamRun();
	void TradeSpamRun();
	void TradeSpamResponseCodeSeen(uint32_t nCode);

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
	int m_iFCSOriginalClass = 0;	// The class to switch back to
	bool m_bFCSWasAlive = false;	// Was the player alive last tick?
	float m_flFCSSendTime = 0.0f;		// Last time we sent joinclass (throttle to avoid server kick)

	float m_flABDodgeAt = 0.f;
	int m_iABState = 0;
	float m_flABStateChange = 0.f;
	float m_flABLastTick = 0.f;
	float m_flABLastCurTime = 0.f;
	int m_iABMine = 0;
	int m_iABOther = 0;
	float m_flABSafeTime = 0.f;
	float m_flABSeen[33] = {};

	float m_flRSLastTick = 0.f;
	float m_flRSAccum = 0.f;
	int m_iRSNextTarget = 1;
	int* m_pRSReportedCount = nullptr;

	float m_flTSLastTick = 0.f;
	float m_flTSAccum = 0.f;
	int m_iTSNextTarget = 1;
	float m_flTSRateScale = 1.0f;
	float m_flTSBackoffUntil = 0.f;
	std::deque<std::pair<uint32_t, float>> m_qTSRecent;
	std::unordered_set<uint32_t> m_setTSBlacklist;
	std::unordered_map<uint32_t, float> m_mapTSRetryAfter;

	// Fake interp delivery (raw NET_SetConVar, applied only while the server
	// gate is open: dead / no team / <2s post spawn)
	bool m_bInterpWasAlive = false;
	float m_flInterpSpawnTime = 0.0f;
};

MAKE_SINGLETON_SCOPED(CMisc, Misc, F);
