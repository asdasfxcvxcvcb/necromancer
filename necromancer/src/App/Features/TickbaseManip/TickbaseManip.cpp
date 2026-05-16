#include "TickbaseManip.h"
#include "../CFG.h"
#include "../FakeAngle/FakeAngle.h"

float CTickbaseManip::GetPingMs()
{
	float flLatency = 0.0f;
	if (const auto pNetChannel = I::EngineClient->GetNetChannelInfo())
	{
		flLatency = pNetChannel->GetLatency(FLOW_OUTGOING) + pNetChannel->GetLatency(FLOW_INCOMING);
	}
	return flLatency * 1000.0f;
}

int CTickbaseManip::GetProcessableTicks() const
{
	const int nAntiAimTicks = F::FakeAngle->AntiAimOn() ? F::FakeAngle->AntiAimTicks() : 0;
	const int nAntiWarpReserve = CFG::Exploits_RapidFire_Antiwarp ? 1 : 0;
	const int nCurrentCommandReserve = 1;
	return Shifting::GetProcessableTicks(nAntiAimTicks + nAntiWarpReserve + nCurrentCommandReserve);
}

int CTickbaseManip::GetOptimalRechargeLimit()
{
	// Cap the user's recharge limit by what the server can actually handle.
	// sv_maxusrcmdprocessticks minus anti-aim ticks = max useful recharge.
	const int nAntiAimTicks = F::FakeAngle->AntiAimOn() ? F::FakeAngle->AntiAimTicks() : 0;
	const int nServerCap = (std::max)(Shifting::GetServerProcessBudget(nAntiAimTicks), 1);
	return std::min(CFG::Exploits_Shifting_Recharge_Limit, nServerCap);
}

int CTickbaseManip::GetOptimalDTTicks()
{
	// User controls DT ticks directly via the slider.
	// CL_Move handles choked-commands awareness (Amalgam-style).
	return CFG::Exploits_RapidFire_Ticks;
}

int CTickbaseManip::GetOptimalDelayTicks()
{
	// User controls delay ticks directly via the slider.
	return CFG::Exploits_RapidFire_Min_Ticks_Target_Same;
}
