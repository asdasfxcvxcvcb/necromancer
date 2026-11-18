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
	return CFG::Exploits_Shifting_Recharge_Limit;
}

int CTickbaseManip::GetOptimalDTTicks()
{
	return CFG::Exploits_RapidFire_Ticks;
}
