#include "AntiCheatCompat.h"
#include "../../CFG.h"
#include "../Misc.h"

// Store original values to restore when anti-cheat is disabled
static bool g_bOriginalNeckbreaker = false;
static bool g_bSavedOriginals = false;

void CAntiCheatCompat::OnFakeLagFixBlocked()
{
	// When FakeLagFix blocks a shot, mark that we're in blocking state
	// This prevents false psilent detections from angle changes while waiting
	m_bFakeLagFixBlocking = true;
}

void CAntiCheatCompat::OnFakeLagFixAllowed()
{
	// When FakeLagFix allows shooting, clear history for a clean start
	// This prevents the angle history from containing pre-block angles
	// that would cause false psilent detection
	if (m_bFakeLagFixBlocking)
	{
		m_vHistory.clear();
		m_bFakeLagFixBlocking = false;
	}
}

void CAntiCheatCompat::ProcessCommand(CUserCmd* pCmd, bool* pSendPacket)
{
	// Reset modified flag at start
	m_bModifiedAngles = false;
	
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

	// never clamp a deliberately out-of-range pitch - 269/271 is the
	// anti-backstab trick itself, and real input can never exceed ±89
	if (pCmd->viewangles.x > -180.0f && pCmd->viewangles.x < 180.0f)
		Math::ClampAngles(pCmd->viewangles);
	else
		pCmd->viewangles.y = Math::NormalizeAngle(pCmd->viewangles.y);

	// Misc::AutoRocketJump marks its ticks for us ("AntiCheatCompat skips this
	// tick", Misc.cpp) but nothing honored the flag. Honor it: the RJ sequence
	// presses its own attack at its own angles, and the isolated-attack
	// injection below would add IN_ATTACK to the in-between ticks (reload
	// cancel gaps, C-tap frames) that carry view angles - rockets that then
	// fire raw the moment the server's cooldown or reload state lets them
	// through, instead of the aimed cmd the next RJ frame was about to send
	if (F::Misc->IsAutoRocketJumpRunning())
		return;

	// Store current command in history - matching Amalgam's exact approach
	// Only track the SENT angles (pCmd->viewangles after aimbot modification)
	m_vHistory.emplace_front(pCmd->viewangles, (pCmd->buttons & IN_ATTACK) != 0, (pCmd->buttons & IN_ATTACK2) != 0, *pSendPacket);
	if (m_vHistory.size() > 5)
		m_vHistory.pop_back();

	if (m_vHistory.size() < 3)
		return;

	auto preserveCurrentPacketGate = [&]()
	{
		// Anti-cheat correction may force a choke to mirror history, but it
		// must not turn a CL_Move non-final command into a sent packet.
		*pSendPacket = *pSendPacket && m_vHistory[1].m_bSendingPacket;
		m_vHistory[0].m_bSendingPacket = *pSendPacket;
	};

	// Prevent trigger checks, though this shouldn't happen ordinarily
	if (!m_vHistory[0].m_bAttack1 && m_vHistory[1].m_bAttack1 && !m_vHistory[2].m_bAttack1)
		pCmd->buttons |= IN_ATTACK;
	if (!m_vHistory[0].m_bAttack2 && m_vHistory[1].m_bAttack2 && !m_vHistory[2].m_bAttack2)
		pCmd->buttons |= IN_ATTACK2;

	// Don't care if we are actually attacking or not, a miss is less important than a detection
	if (m_vHistory[0].m_bAttack1 || m_vHistory[1].m_bAttack1 || m_vHistory[2].m_bAttack1)
	{
		// Prevent silent aim checks
		// Pattern: angles snap away (0->1 big) then snap back (0->2 small)
		if (Math::CalcFov(m_vHistory[0].m_vAngle, m_vHistory[1].m_vAngle) > PSILENT_EPSILON
			&& Math::CalcFov(m_vHistory[0].m_vAngle, m_vHistory[2].m_vAngle) < REAL_EPSILON)
		{
			pCmd->viewangles = m_vHistory[1].m_vAngle.LerpAngle(m_vHistory[0].m_vAngle, 0.5f);
			if (Math::CalcFov(pCmd->viewangles, m_vHistory[2].m_vAngle) < REAL_EPSILON)
				pCmd->viewangles = m_vHistory[0].m_vAngle + Vec3(0.f, REAL_EPSILON * 2, 0.f);
			m_vHistory[0].m_vAngle = pCmd->viewangles;
			preserveCurrentPacketGate();
			m_bModifiedAngles = true;
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
				pCmd->viewangles.y += SNAP_NOISE_EPSILON * 2;
				m_vHistory[0].m_vAngle = pCmd->viewangles;
				preserveCurrentPacketGate();
				m_bModifiedAngles = true;  
			}
		}
	}
}

void CAntiCheatCompat::ValidateNetworkCvars(void* pMsg)
{
	if (!pMsg)
		return;

	auto pSetConVar = reinterpret_cast<NET_SetConVar*>(pMsg);
	char* end = nullptr;

	for (int i = 0; i < pSetConVar->m_ConVars.Count(); i++)
	{
		NET_SetConVar::CVar_t* pCvar = &pSetConVar->m_ConVars[i];

		// cl_interp: clamp to max 0.1 and track sent value
		if (strcmp(pCvar->Name, "cl_interp") == 0)
		{
			float flValue = std::strtof(pCvar->Value, &end);
			if (end != pCvar->Value)
			{
				flValue = std::min(flValue, 0.1f);
				strncpy_s(pCvar->Value, std::to_string(flValue).c_str(), MAX_OSPATH);
				m_flSentInterp = flValue;
			}
		}
		// cl_cmdrate: clamp to min 10 and track sent value
		else if (strcmp(pCvar->Name, "cl_cmdrate") == 0)
		{
			float flValue = std::strtof(pCvar->Value, &end);
			if (end != pCvar->Value)
			{
				int iValue = static_cast<int>(flValue);
				iValue = std::max(iValue, 10);
				strncpy_s(pCvar->Value, std::to_string(iValue).c_str(), MAX_OSPATH);
				m_iSentCmdrate = iValue;
			}
		}
		// cl_updaterate: track sent value
		else if (strcmp(pCvar->Name, "cl_updaterate") == 0)
		{
			float flValue = std::strtof(pCvar->Value, &end);
			if (end != pCvar->Value)
			{
				int iValue = static_cast<int>(flValue);
				m_iSentUpdaterate = iValue;
			}
		}
		// cl_interp_ratio and cl_interpolate: force to 1
		else if (strcmp(pCvar->Name, "cl_interp_ratio") == 0 || strcmp(pCvar->Name, "cl_interpolate") == 0)
		{
			strncpy_s(pCvar->Value, "1", MAX_OSPATH);
		}
	}
}

void CAntiCheatCompat::SpoofCvarResponse(void* pMsg)
{
	if (!pMsg)
		return;

	auto pMsgPtr = reinterpret_cast<uintptr_t*>(pMsg);
	
	auto cvarName = reinterpret_cast<const char*>(pMsgPtr[6]);
	if (!cvarName)
		return;

	auto pConVar = I::CVar->FindVar(cvarName);
	if (!pConVar)
		return;

	static std::string sSpoofedValue = "";

	if (strcmp(cvarName, "cl_interp") == 0)
	{
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
		if (m_iSentCmdrate != -1)
			sSpoofedValue = std::to_string(m_iSentCmdrate);
		else
			sSpoofedValue = pConVar->GetString();
	}
	else if (strcmp(cvarName, "cl_updaterate") == 0)
	{
		if (m_iSentUpdaterate != -1)
			sSpoofedValue = std::to_string(m_iSentUpdaterate);
		else
			sSpoofedValue = pConVar->GetString();
	}
	else if (strcmp(cvarName, "mat_dxlevel") == 0)
	{
		sSpoofedValue = pConVar->GetString();
	}
	else
	{
		if (pConVar->m_pParent && pConVar->m_pParent->m_pszDefaultValue)
			sSpoofedValue = pConVar->m_pParent->m_pszDefaultValue;
		else
			sSpoofedValue = pConVar->GetString();
	}

	pMsgPtr[7] = reinterpret_cast<uintptr_t>(sSpoofedValue.c_str());
}

int CAntiCheatCompat::GetMaxTickShift(int iServerMax)
{
	return std::min(iServerMax, 8);
}

bool CAntiCheatCompat::ShouldLimitBhop(int& iJumpCount, bool bGrounded, bool bLastGrounded, bool bJumping)
{
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
	return std::min(flInterp, 0.1f);
}
