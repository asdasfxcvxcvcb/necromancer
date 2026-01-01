#pragma once

#include "../../../../SDK/SDK.h"
#include <deque>

// Command history structure - matching Amalgam's CmdHistory_t exactly
struct CmdHistory_t
{
	Vec3 m_vAngle;
	bool m_bAttack1;
	bool m_bAttack2;
	bool m_bSendingPacket;

	CmdHistory_t(const Vec3& vAngle, bool bAttack1, bool bAttack2, bool bSendingPacket)
		: m_vAngle(vAngle), m_bAttack1(bAttack1), m_bAttack2(bAttack2), m_bSendingPacket(bSendingPacket) {}
};

class CAntiCheatCompat
{
public:
	// Main processing function called from CreateMove
	void ProcessCommand(CUserCmd* pCmd, bool* pSendPacket);

	// Network cvar validation
	void ValidateNetworkCvars(void* pMsg);
	void SpoofCvarResponse(void* pMsg);

	// Feature limiting
	int GetMaxTickShift(int iServerMax);
	bool ShouldLimitBhop(int& iJumpCount, bool bGrounded, bool bLastGrounded, bool bJumping);
	float ClampBacktrackInterp(float flInterp);

	// Tracked sent values
	float m_flSentInterp = -1.f;
	int m_iSentCmdrate = -1;
	int m_iSentUpdaterate = -1;

private:
	std::deque<CmdHistory_t> m_vHistory;
	int m_iBhopCount = 0;

	// Detection thresholds - exact values from Amalgam
	static constexpr float MATH_EPSILON = 1.f / 16.f;
	static constexpr float PSILENT_EPSILON = 1.f - MATH_EPSILON;
	static constexpr float REAL_EPSILON = 0.1f + MATH_EPSILON;
	static constexpr float SNAP_SIZE_EPSILON = 10.f - MATH_EPSILON;
	static constexpr float SNAP_NOISE_EPSILON = 0.5f + MATH_EPSILON;
};

MAKE_SINGLETON_SCOPED(CAntiCheatCompat, AntiCheatCompat, F);
