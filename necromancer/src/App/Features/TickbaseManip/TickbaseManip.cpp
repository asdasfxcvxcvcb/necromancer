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
	return Shifting::GetProcessableTicks(1);
}

int CTickbaseManip::GetOptimalRechargeLimit()
{
	const int nServerCap = (std::max)(Shifting::GetServerProcessBudget(0), 1);
	return std::min(CFG::Exploits_Shifting_Recharge_Limit, nServerCap);
}

int CTickbaseManip::GetOptimalDTTicks()
{
	return CFG::Exploits_RapidFire_Ticks;
}

int CTickbaseManip::GetOptimalDelayTicks()
{
	// User controls delay ticks directly via the slider.
	return CFG::Exploits_RapidFire_Min_Ticks_Target_Same;
}
