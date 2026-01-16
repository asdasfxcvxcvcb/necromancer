#include "TickbaseManip.h"
#include "../CFG.h"

// ============================================
// Auto Settings Implementation
// ============================================

float CTickbaseManip::GetPingMs()
{
	float flLatency = 0.0f;
	if (const auto pNetChannel = I::EngineClient->GetNetChannelInfo())
	{
		flLatency = pNetChannel->GetLatency(FLOW_OUTGOING) + pNetChannel->GetLatency(FLOW_INCOMING);
	}
	return flLatency * 1000.0f;
}

int CTickbaseManip::GetOptimalRechargeLimit()
{
	if (!CFG::Exploits_RapidFire_Auto_Settings)
		return CFG::Exploits_Shifting_Recharge_Limit;
	
	float flPing = GetPingMs();
	if (flPing <= 70.0f)
		return 23;
	else if (flPing <= 120.0f)
		return 21;
	else
		return 20;
}

int CTickbaseManip::GetOptimalDTTicks()
{
	if (!CFG::Exploits_RapidFire_Auto_Settings)
		return CFG::Exploits_RapidFire_Ticks;
	
	float flPing = GetPingMs();
	if (flPing <= 40.0f)
		return 22;
	else if (flPing <= 120.0f)
		return 21;
	else
		return 20;
}

int CTickbaseManip::GetOptimalDelayTicks()
{
	if (!CFG::Exploits_RapidFire_Auto_Settings)
		return CFG::Exploits_RapidFire_Min_Ticks_Target_Same;
	
	float flPing = GetPingMs();
	if (flPing <= 70.0f)
		return 1;
	else if (flPing <= 120.0f)
		return 2;
	else
		return 4;
}

int CTickbaseManip::GetOptimalMaxCommands()
{
	if (!CFG::Exploits_RapidFire_Auto_Settings)
		return CFG::Exploits_RapidFire_Max_Commands;
	
	float flPing = GetPingMs();
	if (flPing <= 70.0f)
		return 15;
	else if (flPing <= 132.0f)
		return 14;
	else
		return 13;
}

int CTickbaseManip::GetOptimalDTCommands()
{
	if (!CFG::Exploits_RapidFire_Auto_Settings)
		return CFG::Exploits_RapidFire_DT_Commands;
	
	float flPing = GetPingMs();
	if (flPing <= 70.0f)
		return 23;
	else if (flPing <= 120.0f)
		return 22;
	else
		return 20;
}
