#include "AntiCheatCompat.h"
#include "../../CFG.h"
#include "../Misc.h"

// Debug flag - set to true to enable console output
static bool g_bDebugAntiCheat = false;

// Store original values to restore when anti-cheat is disabled
static bool g_bOriginalNeckbreaker = false;
static bool g_bSavedOriginals = false;

void CAntiCheatCompat::ProcessCommand(CUserCmd* pCmd, bool* pSendPacket)
{
	// Handle feature disabling when anti-cheat is enabled
	if (CFG::Misc_AntiCheat_Enabled)
	{
		// Save original values once
		if (!g_bSavedOriginals)
		{
			g_bOriginalNeckbreaker = CFG::Aimbot_Projectile_Neckbreaker;
			g_bSavedOriginals = true;
		}
		
		// Force disable neckbreaker - doesn't work well with anti-cheat
		CFG::Aimbot_Projectile_Neckbreaker = false;
	}
	else
	{
		// Restore original values when anti-cheat is disabled
		if (g_bSavedOriginals)
		{
			CFG::Aimbot_Projectile_Neckbreaker = g_bOriginalNeckbreaker;
			g_bSavedOriginals = false;
		}
		return;
	}

	// Skip anti-cheat processing during rocket jump - we need exact angles for rocket jumping
	// Matching Amalgam's behavior where AutoRocketJump bypasses anti-cheat checks
	if (F::Misc->IsAutoRocketJumpRunning())
		return;
	
	// Skip anti-cheat processing during FaN jump - same reason as rocket jump
	// The AutoFaN feature modifies viewangles for the jump boost, anticheat would break it
	if (F::Misc->IsAutoFaNRunning())
		return;

	// Save angles BEFORE any modification for debug
	Vec3 vAnglesBeforeAC = pCmd->viewangles;

	Math::ClampAngles(pCmd->viewangles); // shouldn't happen, but failsafe

	// Store current command in history (matching Amalgam's emplace_front pattern)
	CmdHistory_t cmd = {};
	cmd.m_vAngle = pCmd->viewangles;
	cmd.m_bAttack1 = (pCmd->buttons & IN_ATTACK) != 0;
	cmd.m_bAttack2 = (pCmd->buttons & IN_ATTACK2) != 0;
	cmd.m_bSendingPacket = pSendPacket ? *pSendPacket : true;

	m_vHistory.push_front(cmd);
	if (m_vHistory.size() > 5)
		m_vHistory.pop_back();

	if (m_vHistory.size() < 3)
		return;

	// Prevent trigger checks, though this shouldn't happen ordinarily
	if (!m_vHistory[0].m_bAttack1 && m_vHistory[1].m_bAttack1 && !m_vHistory[2].m_bAttack1)
		pCmd->buttons |= IN_ATTACK;
	if (!m_vHistory[0].m_bAttack2 && m_vHistory[1].m_bAttack2 && !m_vHistory[2].m_bAttack2)
		pCmd->buttons |= IN_ATTACK2;

	// Don't care if we are actually attacking or not, a miss is less important than a detection
	// NOTE: We do NOT skip this for silent aim - the LERP is needed to prevent server-side detection
	// The local view restoration is handled separately in CreateMove using vOldAngles
	if (m_vHistory[0].m_bAttack1 || m_vHistory[1].m_bAttack1 || m_vHistory[2].m_bAttack1)
	{
		float flFov01 = Math::CalcFov(m_vHistory[0].m_vAngle, m_vHistory[1].m_vAngle);
		float flFov02 = Math::CalcFov(m_vHistory[0].m_vAngle, m_vHistory[2].m_vAngle);

		// Prevent silent aim checks (only for pSilent, not regular silent)
		if (flFov01 > PSILENT_EPSILON && flFov02 < REAL_EPSILON)
		{
			Vec3 vOldAngles = pCmd->viewangles;
			pCmd->viewangles = m_vHistory[1].m_vAngle.LerpAngle(m_vHistory[0].m_vAngle, 0.5f);
			if (Math::CalcFov(pCmd->viewangles, m_vHistory[2].m_vAngle) < REAL_EPSILON)
				pCmd->viewangles = m_vHistory[0].m_vAngle + Vec3(0.f, REAL_EPSILON * 2, 0.f);
			m_vHistory[0].m_vAngle = pCmd->viewangles;
			m_vHistory[0].m_bSendingPacket = *pSendPacket = m_vHistory[1].m_bSendingPacket;

			// DEBUG OUTPUT - PSILENT DETECTION
			if (g_bDebugAntiCheat)
			{
				I::CVar->ConsoleColorPrintf(Color_t(255, 100, 100, 255), "[AC-DEBUG] PSILENT DETECTED!\n");
				I::CVar->ConsoleColorPrintf(Color_t(255, 255, 100, 255), "  History[0] (current): (%.2f, %.2f) ATK1=%d\n", 
					m_vHistory[0].m_vAngle.x, m_vHistory[0].m_vAngle.y, m_vHistory[0].m_bAttack1);
				I::CVar->ConsoleColorPrintf(Color_t(255, 255, 100, 255), "  History[1] (prev):    (%.2f, %.2f) ATK1=%d\n", 
					m_vHistory[1].m_vAngle.x, m_vHistory[1].m_vAngle.y, m_vHistory[1].m_bAttack1);
				I::CVar->ConsoleColorPrintf(Color_t(255, 255, 100, 255), "  History[2] (prev-1):  (%.2f, %.2f) ATK1=%d\n", 
					m_vHistory[2].m_vAngle.x, m_vHistory[2].m_vAngle.y, m_vHistory[2].m_bAttack1);
				I::CVar->ConsoleColorPrintf(Color_t(100, 255, 100, 255), "  FOV[0->1]=%.4f (threshold=%.4f)\n", flFov01, PSILENT_EPSILON);
				I::CVar->ConsoleColorPrintf(Color_t(100, 255, 100, 255), "  FOV[0->2]=%.4f (threshold=%.4f)\n", flFov02, REAL_EPSILON);
				I::CVar->ConsoleColorPrintf(Color_t(100, 200, 255, 255), "  LERP: (%.2f, %.2f) -> (%.2f, %.2f)\n", 
					vOldAngles.x, vOldAngles.y, pCmd->viewangles.x, pCmd->viewangles.y);
				I::CVar->ConsoleColorPrintf(Color_t(255, 100, 255, 255), "  G::bSilentAngles=%d G::bPSilentAngles=%d G::bFiring=%d\n",
					G::bSilentAngles, G::bPSilentAngles, G::bFiring);
			}
		}

		// Prevent aim snap checks
		if (m_vHistory.size() == 5)
		{
			float flDelta01 = Math::CalcFov(m_vHistory[0].m_vAngle, m_vHistory[1].m_vAngle);
			float flDelta12 = Math::CalcFov(m_vHistory[1].m_vAngle, m_vHistory[2].m_vAngle);
			float flDelta23 = Math::CalcFov(m_vHistory[2].m_vAngle, m_vHistory[3].m_vAngle);
			float flDelta34 = Math::CalcFov(m_vHistory[3].m_vAngle, m_vHistory[4].m_vAngle);

			if ((
				flDelta12 > SNAP_SIZE_EPSILON && flDelta23 < SNAP_NOISE_EPSILON && m_vHistory[2].m_vAngle != m_vHistory[3].m_vAngle
				|| flDelta23 > SNAP_SIZE_EPSILON && flDelta12 < SNAP_NOISE_EPSILON && m_vHistory[1].m_vAngle != m_vHistory[2].m_vAngle
				)
				&& flDelta01 < SNAP_NOISE_EPSILON && m_vHistory[0].m_vAngle != m_vHistory[1].m_vAngle
				&& flDelta34 < SNAP_NOISE_EPSILON && m_vHistory[3].m_vAngle != m_vHistory[4].m_vAngle)
			{
				Vec3 vOldAngles = pCmd->viewangles;
				pCmd->viewangles.y += SNAP_NOISE_EPSILON * 2;
				m_vHistory[0].m_vAngle = pCmd->viewangles;
				m_vHistory[0].m_bSendingPacket = *pSendPacket = m_vHistory[1].m_bSendingPacket;

				// DEBUG OUTPUT - SNAP DETECTION
				if (g_bDebugAntiCheat)
				{
					I::CVar->ConsoleColorPrintf(Color_t(255, 100, 100, 255), "[AC-DEBUG] SNAP DETECTED!\n");
					I::CVar->ConsoleColorPrintf(Color_t(100, 200, 255, 255), "  Added noise: (%.2f, %.2f) -> (%.2f, %.2f)\n", 
						vOldAngles.x, vOldAngles.y, pCmd->viewangles.x, pCmd->viewangles.y);
				}
			}
		}
	}
}

void CAntiCheatCompat::ValidateNetworkCvars(void* pMsg)
{
	if (!pMsg)
		return;

	auto pSetConVar = reinterpret_cast<NET_SetConVar*>(pMsg);

	for (int i = 0; i < pSetConVar->m_ConVars.Count(); i++)
	{
		NET_SetConVar::CVar_t* pCvar = &pSetConVar->m_ConVars[i];

		// cl_interp: clamp to max 0.1 and track sent value (matching Amalgam)
		if (strcmp(pCvar->Name, "cl_interp") == 0)
		{
			try {
				float flValue = std::stof(pCvar->Value);
				// Clamp to max 0.1f when anti-cheat is enabled
				flValue = std::min(flValue, 0.1f);
				strncpy_s(pCvar->Value, std::to_string(flValue).c_str(), MAX_OSPATH);
				// Track what we actually sent (matching Amalgam's m_flSentInterp)
				m_flSentInterp = flValue;
			} catch (...) {}
		}
		// cl_cmdrate: clamp to min 10 and track sent value (matching Amalgam)
		else if (strcmp(pCvar->Name, "cl_cmdrate") == 0)
		{
			try {
				int iValue = static_cast<int>(std::stof(pCvar->Value));
				// Clamp to min 10 when anti-cheat is enabled
				iValue = std::max(iValue, 10);
				strncpy_s(pCvar->Value, std::to_string(iValue).c_str(), MAX_OSPATH);
				// Track what we actually sent
				m_iSentCmdrate = iValue;
			} catch (...) {}
		}
		// cl_updaterate: track sent value
		else if (strcmp(pCvar->Name, "cl_updaterate") == 0)
		{
			try {
				int iValue = static_cast<int>(std::stof(pCvar->Value));
				// Track what we actually sent
				m_iSentUpdaterate = iValue;
			} catch (...) {}
		}
		// cl_interp_ratio and cl_interpolate: force to 1 (matching Amalgam)
		else if (strcmp(pCvar->Name, "cl_interp_ratio") == 0 || strcmp(pCvar->Name, "cl_interpolate") == 0)
		{
			strncpy_s(pCvar->Value, "1", MAX_OSPATH);
		}
	}
}

void CAntiCheatCompat::SpoofCvarResponse(void* pMsg)
{
	// This function handles clc_RespondCvarValue messages
	// The message structure is accessed as a raw pointer array
	if (!pMsg)
		return;

	auto pMsgPtr = reinterpret_cast<uintptr_t*>(pMsg);
	
	auto cvarName = reinterpret_cast<const char*>(pMsgPtr[6]);
	if (!cvarName)
		return;

	auto pConVar = I::CVar->FindVar(cvarName);
	if (!pConVar)
		return;

	// Static string to hold the spoofed value (must persist after function returns)
	static std::string sSpoofedValue = "";

	// Spoof specific cvars (matching Amalgam's exact behavior with sent value tracking)
	if (strcmp(cvarName, "cl_interp") == 0)
	{
		// CRITICAL: Return the value we SENT to the server, not the current cvar value
		// This ensures consistency between what we sent and what we report when queried
		if (m_flSentInterp != -1.f)
			sSpoofedValue = std::to_string(std::min(m_flSentInterp, 0.1f));
		else
			sSpoofedValue = pConVar->GetString();
	}
	else if (strcmp(cvarName, "cl_interp_ratio") == 0)
	{
		sSpoofedValue = "1";
	}
	else if (strcmp(cvarName, "cl_interpolate") == 0)
	{
		sSpoofedValue = "1";
	}
	else if (strcmp(cvarName, "cl_cmdrate") == 0)
	{
		// Return the value we SENT to the server
		if (m_iSentCmdrate != -1)
			sSpoofedValue = std::to_string(m_iSentCmdrate);
		else
			sSpoofedValue = pConVar->GetString();
	}
	else if (strcmp(cvarName, "cl_updaterate") == 0)
	{
		// Return the value we SENT to the server
		if (m_iSentUpdaterate != -1)
			sSpoofedValue = std::to_string(m_iSentUpdaterate);
		else
			sSpoofedValue = pConVar->GetString();
	}
	else if (strcmp(cvarName, "mat_dxlevel") == 0)
	{
		// Return current value for mat_dxlevel (matching Amalgam)
		sSpoofedValue = pConVar->GetString();
	}
	else
	{
		// CRITICAL: For unknown cvars, return the DEFAULT value, not current value
		// This prevents detection of modified cvars (matching Amalgam's behavior)
		if (pConVar->m_pParent && pConVar->m_pParent->m_pszDefaultValue)
			sSpoofedValue = pConVar->m_pParent->m_pszDefaultValue;
		else
			sSpoofedValue = pConVar->GetString();
	}

	// Set the spoofed value in the message
	pMsgPtr[7] = reinterpret_cast<uintptr_t>(sSpoofedValue.c_str());
}

int CAntiCheatCompat::GetMaxTickShift(int iServerMax)
{
	// Limit to maximum 8 ticks regardless of sv_maxusrcmdprocessticks (matching Amalgam)
	return std::min(iServerMax, 8);
}

bool CAntiCheatCompat::ShouldLimitBhop(int& iJumpCount, bool bGrounded, bool bLastGrounded, bool bJumping)
{
	// Prevent more than 9 bhops occurring (matching Amalgam)
	if (bGrounded)
	{
		if (!bLastGrounded && bJumping)
			m_iBhopCount++;
		else
			m_iBhopCount = 0;
	}

	return m_iBhopCount > 9;
}

float CAntiCheatCompat::ClampBacktrackInterp(float flInterp)
{
	// Clamp to maximum 0.1 seconds (matching Amalgam's GetFakeInterp)
	return std::min(flInterp, 0.1f);
}
