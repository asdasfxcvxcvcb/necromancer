#include "IgnoreVAC.h"

#include "../../CFG.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>

namespace
{
	constexpr float BLACKOUT_DURATION = 5.0f;
	constexpr float CL_RESEND_OVERRIDDEN = 2.0f;

	float Now()
	{
		return std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void LogLine(const char* pszFmt, ...)
	{
		if (!I::CVar)
			return;

		char szBuf[512];
		va_list args;
		va_start(args, pszFmt);
		vsnprintf(szBuf, sizeof(szBuf), pszFmt, args);
		va_end(args);

		I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[IgnoreVAC] %s\n", szBuf);
	}
}

void CIgnoreVAC::SetResendOverride(bool bOn)
{
	static ConVar* pResend = nullptr;
	static float flOriginal = 0.0f;
	static bool bCaptured = false;

	if (!pResend)
		pResend = I::CVar->FindVar("cl_resend");

	if (!pResend)
		return;

	if (bOn)
	{
		if (!bCaptured)
		{
			flOriginal = pResend->GetFloat();
			bCaptured = true;
		}

		if (pResend->GetFloat() != CL_RESEND_OVERRIDDEN)
			pResend->SetValue(CL_RESEND_OVERRIDDEN);
	}
	else if (bCaptured)
	{
		pResend->SetValue(flOriginal);
		bCaptured = false;
	}
}

void CIgnoreVAC::OnTicketSent()
{
	if (!CFG::Misc_IgnoreVAC_Enabled)
		return;

	if (m_flBlackoutUntil != 0.0f)
		return;

	m_flBlackoutUntil = Now() + BLACKOUT_DURATION;
	LogLine("ticket out, inbound blackout for 5s");
}

bool CIgnoreVAC::ShouldDropOOBPackets()
{
	return CFG::Misc_IgnoreVAC_Enabled && m_flBlackoutUntil != 0.0f && Now() < m_flBlackoutUntil;
}

bool CIgnoreVAC::ShouldDropInboundPackets()
{
	return CFG::Misc_IgnoreVAC_Enabled && m_flBlackoutUntil != 0.0f && Now() < m_flBlackoutUntil;
}

void CIgnoreVAC::Run()
{
	if (!I::EngineClient || !I::ClientState || !I::CVar)
		return;

	if (!CFG::Misc_IgnoreVAC_Enabled)
	{
		if (m_bWasEnabled)
		{
			SetResendOverride(false);
			m_bWasEnabled = false;
			m_flBlackoutUntil = 0.0f;
			m_flInGameSince = 0.0f;
			LogLine("off, cl_resend restored");
		}
		return;
	}

	if (!m_bWasEnabled)
	{
		m_bWasEnabled = true;
		SetResendOverride(true);
		LogLine("on, cl_resend forced to 2.0");
	}

	const float flNow = Now();
	const int nSignon = I::ClientState->m_nSignonState;

	if (nSignon == SIGNONSTATE_NONE)
	{
		m_flBlackoutUntil = 0.0f;
		m_flInGameSince = 0.0f;
		return;
	}

	if (nSignon != SIGNONSTATE_FULL)
	{
		m_flInGameSince = 0.0f;
		return;
	}

	if (m_flInGameSince == 0.0f)
	{
		m_flInGameSince = flNow;
		return;
	}
}
